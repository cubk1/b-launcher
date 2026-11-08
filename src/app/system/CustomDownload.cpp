#include "app/system/CustomDownload.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Transfers.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/network/download_client.hpp>
#include <core/network/http_client.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <span>

namespace CustomDownload {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        struct Live {
            std::atomic<int> state{(int) State::Probing};
            std::atomic<uint64_t> done{0};
            std::atomic<uint64_t> total{0};
            std::atomic<double> speed{0.0};
            uint64_t transfer = 0;
        };

        class Sink final : public blb::progress_sink {
        public:
            explicit Sink(std::shared_ptr<Live> live) : live(std::move(live)) {}

            void on_progress(const blb::progress_event& event) override {
                live->done = event.done_bytes;
                if (event.total_bytes > 0) {
                    live->total = event.total_bytes;
                }
                live->speed = event.bytes_per_second;
                Transfers::report(live->transfer, event);
            }

        private:
            std::shared_ptr<Live> live;
        };

        AsyncTasks tasks;
        Status shown;
        std::shared_ptr<Live> live;
        std::shared_ptr<blb::cancellation_source> cancelling;

        std::int64_t sizeOf(const blb::net::http_response& response) {
            for (auto it = response.headers.rbegin(); it != response.headers.rend(); ++it) {
                if (blb::iequals(it->name, "Content-Length")) {
                    std::optional<std::int64_t> value = blb::parse_int(blb::trim(it->value));
                    return value && *value > 0 ? *value : -1;
                }
            }
            return -1;
        }

        int hexDigit(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        std::string percentDecoded(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); i++) {
                if (text[i] == '%' && i + 2 < text.size() && hexDigit(text[i + 1]) >= 0 && hexDigit(text[i + 2]) >= 0) {
                    out += (char) (hexDigit(text[i + 1]) * 16 + hexDigit(text[i + 2]));
                    i += 2;
                } else {
                    out += text[i] == '+' ? ' ' : text[i];
                }
            }
            return out;
        }
    }

    const Status& status() {
        return shown;
    }

    bool busy() {
        return shown.state == State::Probing || shown.state == State::Running;
    }

    std::string nameOf(const std::string& url) {
        std::string path = url.substr(0, url.find_first_of("?#"));
        size_t scheme = path.find("://");
        size_t start = scheme == std::string::npos ? 0 : scheme + 3;
        size_t slash = path.find('/', start);
        std::string name = slash == std::string::npos ? std::string() : path.substr(path.rfind('/') + 1);
        name = std::string(blb::trim(blb::fs::filter_filename(percentDecoded(name))));
        while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
            name.pop_back();
        }
        return blb::fs::safe_filename(name) ? name : std::string("download");
    }

    void start(const std::string& url, const std::filesystem::path& target) {
        if (busy()) {
            return;
        }
        shown = Status();
        shown.state = State::Probing;
        shown.url = url;
        shown.target = target;
        live = std::make_shared<Live>();
        cancelling = std::make_shared<blb::cancellation_source>();
        Transfers::Spec spec;
        spec.kind = Transfers::Kind::File;
        spec.title = blb::path_to_utf8(target.filename());
        spec.folder = target.parent_path();
        spec.cancel = [] { cancel(); };
        live->transfer = Transfers::begin(std::move(spec));
        Transfers::stage(live->transfer, "正在连接");
        std::shared_ptr<Live> progress = live;
        blb::cancellation_token token = cancelling->token();
        tasks.run(
                [url, target, progress, token](blb::cancellation_token) -> Continuation {
                    blb::net::http_request head;
                    head.method = "HEAD";
                    head.url = url;
                    head.timeout = std::chrono::milliseconds(15000);
                    head.headers.push_back({"Accept-Encoding", "identity"});
                    std::int64_t size = -1;
                    if (auto response = blb::net::http_send(head, token); response && response->ok()) {
                        size = sizeOf(*response);
                    }
                    std::string error;
                    if (!token.is_cancelled()) {
                        Transfers::stage(progress->transfer, std::string());
                        progress->state = (int) State::Running;
                        if (size > 0) {
                            progress->total = (uint64_t) size;
                        }
                        if (auto made = blb::fs::create_directories(target.parent_path()); !made) {
                            error = made.err().message;
                        } else {
                            blb::net::download_task task;
                            task.urls = {url};
                            task.target = target;
                            task.size = size;
                            Sink sink(progress);
                            blb::progress_tracker tracker(&sink, "download");
                            blb::net::download_client client;
                            blb::net::download_report report =
                                    client.run(std::span<const blb::net::download_task>(&task, 1), &tracker, token);
                            if (!report.ok()) {
                                error = report.failures.front().reason.message;
                            }
                        }
                    }
                    bool cancelled = token.is_cancelled();
                    return [error, cancelled, target, progress] {
                        if (cancelled) {
                            Transfers::finish(progress->transfer, Transfers::State::Cancelled);
                            shown.state = State::Idle;
                            Notices::add(Notices::Kind::Info, "已取消下载", 2500);
                            return;
                        }
                        if (!error.empty()) {
                            Transfers::finish(progress->transfer, Transfers::State::Failed, error);
                            shown.state = State::Failed;
                            shown.error = error;
                            Notices::add(Notices::Kind::Failed, "下载失败：" + error, 5000);
                            return;
                        }
                        Transfers::finish(progress->transfer, Transfers::State::Done);
                        shown.state = State::Done;
                        shown.done = progress->done.load();
                        shown.total = std::max(shown.total, shown.done);
                        Notices::add(Notices::Kind::Done, "已下载 " + blb::path_to_utf8(target.filename()), 3000);
                    };
                },
                [progress](const std::string& error) -> Continuation {
                    return [error, progress] {
                        Transfers::finish(progress->transfer, Transfers::State::Failed, error);
                        shown.state = State::Failed;
                        shown.error = error;
                        Notices::add(Notices::Kind::Failed, "下载失败：" + error, 5000);
                    };
                });
    }

    void cancel() {
        if (cancelling != nullptr) {
            cancelling->cancel();
        }
    }

    void poll() {
        tasks.poll();
        if (busy() && live != nullptr) {
            shown.state = (State) live->state.load();
            shown.done = live->done.load();
            shown.total = live->total.load();
            shown.speed = live->speed.load();
        }
    }

    void shutdown() {
        cancel();
        tasks.stop();
    }
}
