#include "app/system/Updates.h"
#include "app/system/About.h"
#include "app/system/Platform.h"
#include "app/system/Notices.h"

#include <core/base/cancellation.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace Updates {

    namespace blb = b_launcher_base;

    namespace {
        const char* const CONFIG_URL = "https://cubk.cn/bl/config.json";

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

        std::vector<int> parts(const std::string& version) {
            std::vector<int> out;
            size_t i = 0;
            while (i < version.size() && (version[i] < '0' || version[i] > '9')) {
                i++;
            }
            while (i < version.size()) {
                int value = 0;
                bool digits = false;
                while (i < version.size() && version[i] >= '0' && version[i] <= '9') {
                    value = value * 10 + (version[i] - '0');
                    digits = true;
                    i++;
                }
                if (!digits) {
                    break;
                }
                out.push_back(value);
                if (i >= version.size() || version[i] != '.') {
                    break;
                }
                i++;
            }
            return out;
        }

        bool newer(const std::string& candidate, const std::string& current) {
            std::vector<int> a = parts(candidate);
            std::vector<int> b = parts(current);
            size_t n = std::max(a.size(), b.size());
            a.resize(n, 0);
            b.resize(n, 0);
            return a > b;
        }

        std::string text(const nlohmann::json& doc, const char* key) {
            auto it = doc.find(key);
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
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
                fetched.version = doc.is_object() ? text(doc, "latest_version") : std::string();
                if (fetched.version.empty()) {
                    fetched.error = "更新信息无法识别";
                } else {
                    fetched.ok = true;
                    fetched.url = text(doc, "download_url");
                    if (fetched.url.empty()) {
                        fetched.url = text(doc, "url");
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
