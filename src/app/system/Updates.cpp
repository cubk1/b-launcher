#include "app/system/Updates.h"
#include "app/system/About.h"
#include "app/system/Platform.h"
#include "app/system/Server.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/network/http_client.hpp>

#include <core/base/json.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace Updates {

    namespace blb = b_launcher_base;

    namespace {
        const std::string CONFIG_URL = Server::file("config.json");

        struct Result {
            bool ok = false;
            std::string version;
            std::string url;
            std::string error;
        };

        std::mutex lock;
        std::thread worker;
        blb::cancellation_source stopping;
        State current = State::Idle;
        bool notifyFound = false;
        bool notifyFailure = false;
        bool finished = false;
        Result result;
        std::string latestVersion;
        std::string downloadUrl = About::RELEASES_URL;

        std::vector<std::string> parts(const std::string& version) {
            std::vector<std::string> out;
            size_t i = 0;
            while (i < version.size() && (version[i] < '0' || version[i] > '9')) {
                i++;
            }
            while (i < version.size()) {
                size_t start = i;
                while (i < version.size() && version[i] >= '0' && version[i] <= '9') {
                    i++;
                }
                if (start == i) {
                    break;
                }
                while (start + 1 < i && version[start] == '0') {
                    start++;
                }
                out.push_back(version.substr(start, i - start));
                if (i >= version.size() || version[i] != '.') {
                    break;
                }
                i++;
            }
            return out;
        }

        bool newer(const std::string& candidate, const std::string& current) {
            std::vector<std::string> a = parts(candidate);
            std::vector<std::string> b = parts(current);
            size_t n = std::max(a.size(), b.size());
            a.resize(n, "0");
            b.resize(n, "0");
            for (size_t i = 0; i < n; i++) {
                if (a[i].size() != b[i].size()) {
                    return a[i].size() > b[i].size();
                }
                if (a[i] != b[i]) {
                    return a[i] > b[i];
                }
            }
            return false;
        }

   }

    void check(bool found, bool failure) {
        if (current == State::Checking) {
            return;
        }
        if (worker.joinable()) {
            worker.join();
        }
        current = State::Checking;
        notifyFound = found;
        notifyFailure = failure;
        blb::cancellation_token token = stopping.token();
        worker = std::thread([token] {
            Result fetched;
            blb::net::http_request request;
            request.url = CONFIG_URL;
            request.headers.push_back({"User-Agent", Platform::USER_AGENT});
            request.timeout = std::chrono::seconds(15);
            auto response = blb::net::http_send(request, token);
            if (!response) {
                fetched.error = response.err().message;
            } else if (!response->ok()) {
                fetched.error = "HTTP " + std::to_string(response->status);
            } else {
                nlohmann::json doc = nlohmann::json::parse(response->body, nullptr, false);
                fetched.version = doc.is_object() ? blb::json_text(doc, "latest_version") : std::string();
                if (fetched.version.empty()) {
                    fetched.error = "更新信息无法识别";
                } else {
                    fetched.ok = true;
                    fetched.url = blb::json_text(doc, "download_url");
                    if (fetched.url.empty()) {
                        fetched.url = blb::json_text(doc, "url");
                    }
                }
            }
            std::lock_guard<std::mutex> guard(lock);
            result = std::move(fetched);
            finished = true;
        });
    }

    State state() {
        return current;
    }

    const std::string& latest() {
        return latestVersion;
    }

    void openDownload() {
        Platform::openUrl(downloadUrl);
    }

    void poll() {
        Result done;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!finished) {
                return;
            }
            finished = false;
            done = result;
        }
        if (worker.joinable()) {
            worker.join();
        }
        if (!done.ok) {
            current = State::Failed;
            if (notifyFailure) {
                Notices::add(Notices::Kind::Failed, "检查更新失败：" + done.error, 5000);
            }
            return;
        }
        latestVersion = done.version;
        downloadUrl = done.url.empty() ? std::string(About::RELEASES_URL) : done.url;
        current = newer(done.version, About::VERSION) ? State::Available : State::Latest;
        if (current == State::Available && notifyFound) {
            Notices::add(Notices::Kind::Info, "发现新版本 v" + latestVersion + "，可在 设置 → 关于 中下载", 6000);
        }
    }

    void shutdown() {
        stopping.cancel();
        if (worker.joinable()) {
            worker.join();
        }
    }
}
