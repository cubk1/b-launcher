#include "app/downloads/ModNames.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <utility>

namespace ModNames {

    namespace blb = b_launcher_base;

    namespace {
        using json = nlohmann::json;
        using Continuation = AsyncTasks::Continuation;
        using Clock = std::chrono::steady_clock;

        const char* const API = "https://mcmod-api.cubk.cn";
        const char* const PREFERENCE_KEY = "modNames";
        constexpr size_t BATCH = 200;
        constexpr size_t KEPT_TRANSLATIONS = 20;
        constexpr int SEARCH_LIMIT = 10;
        constexpr size_t SPELLED_MIN = 2;
        constexpr size_t SPELLED_MAX = 40;
        constexpr std::int64_t FOUND_SECONDS = 30LL * 24 * 3600;
        constexpr std::int64_t MISSING_SECONDS = 7LL * 24 * 3600;
        constexpr auto SAVE_EVERY = std::chrono::seconds(5);

        struct Match {
            std::string en;
            std::string by;
            bool exact = false;
        };

        struct Translation {
            Addons::Status status = Addons::Status::Loading;
            std::vector<Match> matches;
        };

        struct Name {
            bool pending = true;
            bool failed = false;
            bool retry = false;
            std::string zh;
            long long id = 0;
            std::int64_t checked = 0;
        };

        AsyncTasks tasks;
        std::unordered_map<std::string, Name> names;
        std::vector<std::string> wanted;
        bool asking = false;
        Clock::time_point pausedUntil;
        std::map<std::string, Translation> translations;
        std::unordered_map<std::string, Clock::time_point> failedAt;
        Recent translationUse;
        bool loaded = false;
        bool dirty = false;
        bool saving = false;
        Clock::time_point savedAt;
        int changes = 0;

        std::int64_t nowSeconds() {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
        }

        std::filesystem::path cacheFile() {
            std::filesystem::path cache = Platform::cacheDirectory();
            return cache.empty() ? cache : cache / "mod-names.json";
        }

        void loadCache() {
            if (loaded) {
                return;
            }
            loaded = true;
            std::filesystem::path file = cacheFile();
            std::error_code error;
            if (file.empty() || !std::filesystem::is_regular_file(file, error)) {
                return;
            }
            auto data = blb::fs::read_file(file);
            json doc = data ? json::parse(*data, nullptr, false) : json();
            auto list = doc.is_object() ? doc.find("names") : doc.end();
            if (!data || list == doc.end() || !list->is_object()) {
                return;
            }
            for (const auto& [english, value] : list->items()) {
                if (!value.is_array() || value.size() != 3 || !value[0].is_string() || !value[1].is_number_integer() ||
                    !value[2].is_number_integer()) {
                    continue;
                }
                Name name;
                name.pending = false;
                name.zh = value[0].get<std::string>();
                name.id = value[1].get<long long>();
                name.checked = value[2].get<std::int64_t>();
                names.emplace(english, std::move(name));
            }
        }

        std::string serialize() {
            json list = json::object();
            for (const auto& [english, name] : names) {
                if (name.checked > 0) {
                    list[english] = json::array({name.zh, name.id, name.checked});
                }
            }
            return json{{"names", list}}.dump();
        }

        void write(const std::filesystem::path& file, const std::string& text) {
            if (file.empty()) {
                return;
            }
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            (void) blb::fs::write_file_atomic(file, text);
        }

        void saveLater() {
            if (!dirty || saving || Clock::now() - savedAt < SAVE_EVERY) {
                return;
            }
            dirty = false;
            saving = true;
            savedAt = Clock::now();
            std::filesystem::path file = cacheFile();
            std::string text = serialize();
            tasks.run(
                    [file, text](blb::cancellation_token) -> Continuation {
                        write(file, text);
                        return [] { saving = false; };
                    },
                    [](const std::string&) -> Continuation { return [] { saving = false; }; });
        }

        bool stale(const Name& name) {
            std::int64_t age = nowSeconds() - name.checked;
            return age >= (name.zh.empty() ? MISSING_SECONDS : FOUND_SECONDS);
        }

        blb::result<json> call(const std::string& path, const std::string& body, blb::cancellation_token token) {
            blb::net::http_request request;
            request.method = body.empty() ? "GET" : "POST";
            request.url = std::string(API) + path;
            request.headers.push_back({"User-Agent", Platform::USER_AGENT});
            request.headers.push_back({"Accept", "application/json"});
            if (!body.empty()) {
                request.headers.push_back({"Content-Type", "application/json"});
                request.body = body;
            }
            request.timeout = std::chrono::seconds(6);
            auto response = blb::net::http_send(request, token);
            if (!response) {
                return response.err();
            }
            if (!response->ok()) {
                return blb::make_error(blb::errc::http_status, "中文名服务返回 " + std::to_string(response->status));
            }
            json doc = json::parse(response->body, nullptr, false);
            if (doc.is_discarded() || !doc.is_object()) {
                return blb::make_error(blb::errc::parse, "中文名服务返回的内容无效");
            }
            return doc;
        }

        Name& entry(const std::string& english) {
            loadCache();
            auto found = names.find(english);
            if (found == names.end()) {
                found = names.emplace(english, Name{}).first;
                wanted.push_back(english);
                return found->second;
            }
            Name& name = found->second;
            if (!name.pending && (name.failed ? Clock::now() >= pausedUntil : stale(name))) {
                name.pending = true;
                name.retry = true;
                name.failed = false;
                wanted.push_back(english);
            }
            return name;
        }

        void ask() {
            if (asking || wanted.empty() || Clock::now() < pausedUntil) {
                return;
            }
            asking = true;
            size_t count = std::min(wanted.size(), BATCH);
            std::vector<std::string> batch(wanted.begin(), wanted.begin() + (long) count);
            wanted.erase(wanted.begin(), wanted.begin() + (long) count);
            auto forget = [batch] {
                asking = false;
                pausedUntil = Clock::now() + std::chrono::seconds(30);
                for (const std::string& name : batch) {
                    Name& entry = names[name];
                    entry.pending = false;
                    entry.retry = false;
                    entry.failed = true;
                }
            };
            tasks.run(
                    [batch, forget](blb::cancellation_token token) -> Continuation {
                        json body = {{"names", batch}};
                        auto doc = call("/v1/names", body.dump(), token);
                        auto items = doc ? doc->find("items") : json::iterator();
                        if (!doc || items == doc->end() || !items->is_array() || items->size() != batch.size()) {
                            return forget;
                        }
                        std::vector<Name> found(batch.size());
                        for (size_t i = 0; i < batch.size(); i++) {
                            const json& item = (*items)[i];
                            found[i].pending = false;
                            if (item.is_object()) {
                                auto id = item.find("id");
                                found[i].zh = blb::json_text(item, "zh");
                                found[i].id = id != item.end() && id->is_number_integer() ? id->get<long long>() : 0;
                            }
                        }
                        return [batch, found] {
                            asking = false;
                            std::int64_t now = nowSeconds();
                            for (size_t i = 0; i < batch.size(); i++) {
                                Name& name = names[batch[i]];
                                name = found[i];
                                name.checked = now;
                            }
                            dirty = true;
                            changes++;
                        };
                    },
                    [forget](const std::string&) -> Continuation { return forget; });
        }

        void fetch(const std::string& text) {
            translations[text] = Translation{};
            tasks.run(
                    [text](blb::cancellation_token token) -> Continuation {
                        auto doc =
                                call("/v1/search?q=" + blb::url_encode(text) + "&limit=" + std::to_string(SEARCH_LIMIT),
                                     std::string(), token);
                        Translation result;
                        if (!doc) {
                            result.status = Addons::Status::Failed;
                        } else {
                            result.status = Addons::Status::Ready;
                            if (auto items = doc->find("items"); items != doc->end() && items->is_array()) {
                                for (const json& item : *items) {
                                    if (!item.is_object()) {
                                        continue;
                                    }
                                    auto exact = item.find("exact");
                                    Match match;
                                    match.en = blb::json_text(item, "en");
                                    match.by = blb::json_text(item, "by");
                                    match.exact = exact != item.end() && exact->is_boolean() && exact->get<bool>();
                                    result.matches.push_back(std::move(match));
                                }
                            }
                        }
                        return [text, result = std::move(result)]() mutable {
                            if (result.status == Addons::Status::Failed) {
                                failedAt[text] = Clock::now();
                            }
                            translations[text] = std::move(result);
                        };
                    },
                    [text](const std::string&) -> Continuation {
                        return [text] {
                            failedAt[text] = Clock::now();
                            translations[text].status = Addons::Status::Failed;
                        };
                    });
        }

        bool chinese(const std::string& text) {
            for (size_t i = 0; i + 2 < text.size(); i++) {
                unsigned char lead = (unsigned char) text[i];
                if ((lead & 0xF0) != 0xE0) {
                    continue;
                }
                unsigned int point = ((lead & 0x0Fu) << 12) | (((unsigned char) text[i + 1] & 0x3Fu) << 6) |
                        ((unsigned char) text[i + 2] & 0x3Fu);
                if ((point >= 0x3400 && point <= 0x9FFF) || (point >= 0xF900 && point <= 0xFAFF)) {
                    return true;
                }
            }
            return false;
        }

        bool spelled(const std::string& text) {
            return text.size() >= SPELLED_MIN && text.size() <= SPELLED_MAX &&
                    std::all_of(text.begin(), text.end(), [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                    });
        }

        const Translation& translate(const std::string& text) {
            translationUse.use(text);
            auto found = translations.find(text);
            auto failed = failedAt.find(text);
            bool stale = found != translations.end() && found->second.status == Addons::Status::Failed &&
                    failed != failedAt.end() && Clock::now() - failed->second >= std::chrono::seconds(30);
            if (found == translations.end() || stale) {
                failedAt.erase(text);
                fetch(text);
                found = translations.find(text);
            }
            return found->second;
        }
    }

    bool applies(Addons::Kind kind) {
        return kind == Addons::Kind::Mod || kind == Addons::Kind::Datapack;
    }

    Rewrite rewrite(const std::string& query) {
        Rewrite out{false, query};
        bool han = chinese(query);
        if (!han && !spelled(query)) {
            return out;
        }
        const Translation& translation = translate(query);
        out.waiting = translation.status == Addons::Status::Loading;
        for (const Match& match : translation.matches) {
            if (han) {
                if (!match.en.empty()) {
                    out.text = match.en;
                    break;
                }
                continue;
            }
            if (match.exact && match.by != "en" && !match.en.empty()) {
                out.text = match.en;
            }
            break;
        }
        return out;
    }

    std::string zh(const std::string& english) {
        return english.empty() ? std::string() : entry(english).zh;
    }

    long long wikiId(const std::string& english) {
        return english.empty() ? 0 : entry(english).id;
    }

    bool resolved(const std::vector<std::string>& englishNames) {
        bool paused = Clock::now() < pausedUntil;
        bool done = true;
        for (const std::string& english : englishNames) {
            if (english.empty()) {
                continue;
            }
            const Name& name = entry(english);
            if (name.pending && !name.retry && !paused) {
                done = false;
            }
        }
        return done;
    }

    bool resolved(const std::vector<Addons::Hit>& hits) {
        std::vector<std::string> titles;
        titles.reserve(hits.size());
        for (const Addons::Hit& hit : hits) {
            titles.push_back(hit.title);
        }
        return resolved(titles);
    }

    int revision() {
        return changes;
    }

    bool original() {
        return blb::json_text(Config::data(), PREFERENCE_KEY) == "original";
    }

    void setOriginal(bool on) {
        if (original() == on) {
            return;
        }
        Config::data()[PREFERENCE_KEY] = on ? "original" : "zh";
        Config::save();
        changes++;
    }

    void poll() {
        tasks.poll();
        ask();
        saveLater();
        translationUse.trim(translations, KEPT_TRANSLATIONS,
                            [](const Translation& entry) { return entry.status != Addons::Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
        if (dirty) {
            write(cacheFile(), serialize());
            dirty = false;
        }
    }
}
