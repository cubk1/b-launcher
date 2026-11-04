#include "app/downloads/Aggregate.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Platform.h"

#include <core/base/json.hpp>
#include <core/network/http_client.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace Aggregate {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Status;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        const char* const ENDPOINT = "https://b-api.cubk.cn/api/v1/search";
        constexpr size_t KEPT_SEARCHES = 10;

        struct Live {
            std::mutex lock;
            std::string thought;
            std::vector<Step> steps;
            bool changed = false;
        };

        AsyncTasks tasks;
        std::map<std::string, Results> searches;
        std::map<std::string, std::shared_ptr<Live>> live;
        Recent searchUse;

        std::string keyOf(const Request& request) {
            return request.text + "\n" + request.gameVersion + "\n" + request.loader;
        }

        long long number(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<long long>() : 0;
        }

        Item itemFrom(const json& node) {
            Item item;
            item.title = blb::json_text(node, "title");
            item.zh = blb::json_text(node, "zh");
            item.summary = blb::json_text(node, "summary");
            item.type = blb::json_text(node, "type");
            item.iconUrl = blb::json_text(node, "iconUrl");
            item.downloads = number(node, "downloads");
            item.gameVersions = blb::json_strings(node, "gameVersions");
            item.loaders = blb::json_strings(node, "loaders");
            item.mcmodUrl = blb::json_text(node, "mcmodUrl");
            if (auto sources = node.find("sources"); sources != node.end() && sources->is_array()) {
                for (const json& entry : *sources) {
                    if (!entry.is_object()) {
                        continue;
                    }
                    std::string name = blb::json_text(entry, "source");
                    if (name != "modrinth" && name != "curseforge") {
                        continue;
                    }
                    Link link;
                    link.source = name == "curseforge" ? Addons::Source::CurseForge : Addons::Source::Modrinth;
                    link.id = blb::json_text(entry, "id");
                    link.slug = blb::json_text(entry, "slug");
                    link.url = blb::json_text(entry, "url");
                    link.downloads = number(entry, "downloads");
                    if (!link.id.empty()) {
                        item.links.push_back(std::move(link));
                    }
                }
            }
            return item;
        }

        class Stream {
        public:
            explicit Stream(std::shared_ptr<Live> progress) : progress(std::move(progress)) {
                out.status = Status::Failed;
                out.error = "搜索服务没有返回结果";
            }

            void feed(std::string_view chunk) {
                pending.append(chunk);
                size_t start = 0;
                for (size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', start)) {
                    line(std::string_view(pending).substr(start, end - start));
                    start = end + 1;
                }
                pending.erase(0, start);
            }

            Results finish() {
                if (!pending.empty()) {
                    line(pending);
                    pending.clear();
                }
                dispatch();
                return std::move(out);
            }

        private:
            std::shared_ptr<Live> progress;
            std::string pending;
            std::string event;
            std::string data;
            Results out;

            void line(std::string_view text) {
                if (!text.empty() && text.back() == '\r') {
                    text.remove_suffix(1);
                }
                if (text.empty()) {
                    dispatch();
                } else if (text.starts_with("event:")) {
                    std::string_view name = text.substr(6);
                    while (!name.empty() && name.front() == ' ') {
                        name.remove_prefix(1);
                    }
                    event = std::string(name);
                } else if (text.starts_with("data:")) {
                    std::string_view value = text.substr(5);
                    if (!value.empty() && value.front() == ' ') {
                        value.remove_prefix(1);
                    }
                    if (!data.empty()) {
                        data += '\n';
                    }
                    data += value;
                }
            }

            void dispatch() {
                std::string name = std::move(event);
                std::string payload = std::move(data);
                event.clear();
                data.clear();
                if (payload.empty()) {
                    return;
                }
                json doc = json::parse(payload, nullptr, false);
                if (!doc.is_object()) {
                    return;
                }
                if (name == "thought" || name == "search" || name == "found") {
                    report(name, doc);
                } else if (name == "result") {
                    settle(doc);
                } else if (name == "error" && out.status != Status::Ready) {
                    std::string message = blb::json_text(doc, "message");
                    out.status = Status::Failed;
                    out.error = message.empty() ? std::string("搜索失败") : message;
                }
            }

            void report(const std::string& name, const json& doc) {
                std::lock_guard<std::mutex> guard(progress->lock);
                progress->changed = true;
                if (name == "thought") {
                    progress->thought = blb::json_text(doc, "text");
                    return;
                }
                int step = (int) number(doc, "step");
                auto found = std::find_if(progress->steps.begin(), progress->steps.end(),
                                          [step](const Step& each) { return each.number == step; });
                if (found == progress->steps.end()) {
                    Step added;
                    added.number = step;
                    progress->steps.push_back(std::move(added));
                    found = progress->steps.end() - 1;
                }
                found->target = blb::json_text(doc, "target");
                found->keyword = blb::json_text(doc, "keyword");
                if (name == "found") {
                    found->done = true;
                    found->count = (int) number(doc, "count");
                    found->titles = blb::json_strings(doc, "titles");
                }
            }

            void settle(const json& doc) {
                out.status = Status::Ready;
                out.error.clear();
                out.summary = blb::json_text(doc, "summary");
                out.partial = blb::json_bool(doc, "partial");
                out.items.clear();
                if (auto items = doc.find("items"); items != doc.end() && items->is_array()) {
                    for (const json& node : *items) {
                        if (node.is_object()) {
                            out.items.push_back(itemFrom(node));
                        }
                    }
                }
            }
        };

        Results failed(const std::string& error) {
            Results out;
            out.status = Status::Failed;
            out.error = error;
            return out;
        }

        void fetch(const Request& request) {
            std::string key = keyOf(request);
            Results& entry = searches[key];
            entry = Results();
            auto progress = std::make_shared<Live>();
            live[key] = progress;
            json body = {{"q", request.text}};
            if (!request.gameVersion.empty()) {
                body["gameVersion"] = request.gameVersion;
            }
            if (!request.loader.empty()) {
                body["loader"] = request.loader;
            }
            std::string payload = body.dump(-1, ' ', false, json::error_handler_t::replace);
            tasks.run(
                    [key, payload, progress](blb::cancellation_token token) -> Continuation {
                        Stream stream(progress);
                        blb::net::http_request call;
                        call.method = "POST";
                        call.url = ENDPOINT;
                        call.body = payload;
                        call.headers.push_back({"User-Agent", Platform::USER_AGENT});
                        call.headers.push_back({"Content-Type", "application/json"});
                        call.headers.push_back({"Accept", "text/event-stream"});
                        call.timeout = std::chrono::seconds(90);
                        call.on_data = [&stream](std::string_view chunk) { stream.feed(chunk); };
                        auto response = blb::net::http_send(call, token);
                        Results results;
                        if (!response) {
                            if (response.err().code == blb::errc::cancelled) {
                                return [] {};
                            }
                            results = failed("无法连接聚合搜索服务");
                        } else if (!response->ok()) {
                            json doc = json::parse(response->body, nullptr, false);
                            std::string message = doc.is_object() ? blb::json_text(doc, "message") : std::string();
                            results = failed(message.empty() ? "聚合搜索服务返回 " + std::to_string(response->status)
                                                             : message);
                        } else {
                            results = stream.finish();
                        }
                        return [key, progress, results = std::move(results)]() mutable {
                            auto current = live.find(key);
                            if (current != live.end() && current->second == progress) {
                                live.erase(current);
                            }
                            searches[key] = std::move(results);
                        };
                    },
                    [key, progress](const std::string& error) -> Continuation {
                        return [key, progress, error] {
                            auto current = live.find(key);
                            if (current != live.end() && current->second == progress) {
                                live.erase(current);
                            }
                            searches[key] = failed(error);
                        };
                    });
        }
    }

    const Results& search(const Request& request) {
        std::string key = keyOf(request);
        searchUse.use(key);
        auto found = searches.find(key);
        if (found == searches.end()) {
            fetch(request);
            found = searches.find(key);
        }
        return found->second;
    }

    void refresh(const Request& request) {
        auto found = searches.find(keyOf(request));
        if (found == searches.end() || found->second.status != Status::Loading) {
            fetch(request);
        }
    }

    void poll() {
        tasks.poll();
        for (const auto& [key, progress] : live) {
            auto entry = searches.find(key);
            if (entry == searches.end() || entry->second.status != Status::Loading) {
                continue;
            }
            std::lock_guard<std::mutex> guard(progress->lock);
            if (progress->changed) {
                progress->changed = false;
                entry->second.thought = progress->thought;
                entry->second.steps = progress->steps;
            }
        }
        searchUse.trim(searches, KEPT_SEARCHES,
                       [](const Results& results) { return results.status != Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
    }
}
