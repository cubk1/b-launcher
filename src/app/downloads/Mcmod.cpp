#include "app/downloads/Mcmod.h"
#include "app/downloads/Recent.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Platform.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <map>

namespace Mcmod {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Source;
    using Addons::Status;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        const char* const API = "https://mcmod-api.cubk.cn/v1";
        constexpr size_t KEPT = 16;

        AsyncTasks tasks;
        std::map<std::string, Lookup> lookups;
        std::map<std::string, Mod> mods;
        std::map<std::string, Text> intros;
        std::map<std::string, Relations> relationLists;
        Recent modUse, introUse, relationUse;

        long long number(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<long long>() : 0;
        }

        std::string base(const Ref& ref) {
            return std::string(ref.type == "modpack" ? "/modpacks/" : "/mods/") + std::to_string(ref.id);
        }

        std::string keyOf(const Ref& ref) {
            return ref.type + ":" + std::to_string(ref.id);
        }

        Mod parseMod(const json& doc) {
            Mod m;
            m.status = Status::Ready;
            m.ref = refOf(doc);
            if (auto state = doc.find("status"); state != doc.end() && state->is_object()) {
                m.state = blb::json_text(*state, "name");
            }
            m.tags = blb::json_strings(doc, "tags");
            m.views = number(doc, "views");
            m.modpacks = number(doc, "modpacks");
            if (auto authors = doc.find("authors"); authors != doc.end() && authors->is_array()) {
                for (const json& item : *authors) {
                    m.authors.push_back(Author{number(item, "id"), blb::json_text(item, "name"),
                                               blb::json_text(item, "avatar"), blb::json_text(item, "roles")});
                }
            }
            if (auto links = doc.find("links"); links != doc.end() && links->is_array()) {
                for (const json& link : *links) {
                    std::string key = blb::json_text(link, "key");
                    std::string note = blb::json_text(link, "note");
                    if (!note.empty()) {
                        continue;
                    }
                    if (key.rfind("modrinth:", 0) == 0 && m.modrinth.empty()) {
                        m.modrinth = key.substr(9);
                    } else if (key.rfind("curseforge:", 0) == 0 && m.curseforge.empty()) {
                        m.curseforge = key.substr(11);
                    }
                }
            }
            return m;
        }

        template <class Entry>
        void failed(std::map<std::string, Entry>& map, const std::string& key) {
            auto found = map.find(key);
            if (found != map.end()) {
                found->second.status = Status::Failed;
            }
        }
    }

    const Lookup& lookup(Source source, const std::string& slug) {
        std::string key = std::string(source == Source::Modrinth ? "modrinth:" : "curseforge:") + blb::to_lower(slug);
        auto found = lookups.find(key);
        if (found != lookups.end()) {
            return found->second;
        }
        Lookup& entry = lookups[key];
        if (slug.empty()) {
            entry.status = Status::Failed;
            return entry;
        }
        tasks.run(
                [key](blb::cancellation_token token) -> Continuation {
                    std::string param = key.substr(0, key.find(':'));
                    std::string value = key.substr(key.find(':') + 1);
                    auto doc = call("/lookup?" + param + "=" + blb::url_encode(value), token);
                    Lookup result;
                    result.status = Status::Failed;
                    if (doc) {
                        auto items = doc->find("items");
                        if (items != doc->end() && items->is_array() && !items->empty()) {
                            auto matches = items->front().find("matches");
                            if (matches != items->front().end() && matches->is_array() && !matches->empty()) {
                                result.ref = refOf(matches->front());
                                result.status = result.ref.id > 0 ? Status::Ready : Status::Failed;
                            }
                        }
                    }
                    return [key, result] { lookups[key] = result; };
                },
                [key](const std::string&) -> Continuation { return [key] { lookups[key].status = Status::Failed; }; });
        return entry;
    }

    const Mod& mod(const Ref& ref) {
        std::string key = keyOf(ref);
        modUse.use(key);
        auto found = mods.find(key);
        if (found != mods.end()) {
            return found->second;
        }
        Mod& entry = mods[key];
        entry.ref = ref;
        std::string path = base(ref);
        tasks.run(
                [key, path](blb::cancellation_token token) -> Continuation {
                    auto doc = call(path, token);
                    if (!doc) {
                        std::string error = doc.err().message;
                        return [key, error] {
                            failed(mods, key);
                            mods[key].error = error;
                        };
                    }
                    Mod m = parseMod(*doc);
                    return [key, m = std::move(m)]() mutable { mods[key] = std::move(m); };
                },
                [key](const std::string&) -> Continuation { return [key] { failed(mods, key); }; });
        return entry;
    }

    const Text& intro(const Ref& ref) {
        std::string key = keyOf(ref);
        introUse.use(key);
        auto found = intros.find(key);
        if (found != intros.end()) {
            return found->second;
        }
        Text& entry = intros[key];
        std::string path = base(ref) + "/intro";
        tasks.run(
                [key, path](blb::cancellation_token token) -> Continuation {
                    auto doc = call(path, token);
                    if (!doc) {
                        return [key] { failed(intros, key); };
                    }
                    Text text;
                    text.status = Status::Ready;
                    text.content = blb::json_text(*doc, "content");
                    text.html = blb::json_text(*doc, "format") == "html";
                    return [key, text = std::move(text)]() mutable { intros[key] = std::move(text); };
                },
                [key](const std::string&) -> Continuation { return [key] { failed(intros, key); }; });
        return entry;
    }

    const Relations& relations(const Ref& ref) {
        std::string key = keyOf(ref);
        relationUse.use(key);
        auto found = relationLists.find(key);
        if (found != relationLists.end()) {
            return found->second;
        }
        Relations& entry = relationLists[key];
        if (ref.type != "mod") {
            entry.status = Status::Ready;
            return entry;
        }
        std::string path = base(ref) + "/relations";
        tasks.run(
                [key, path](blb::cancellation_token token) -> Continuation {
                    auto doc = call(path, token);
                    if (!doc) {
                        return [key] { failed(relationLists, key); };
                    }
                    Relations list;
                    list.status = Status::Ready;
                    if (auto items = doc->find("items"); items != doc->end() && items->is_array()) {
                        for (const json& item : *items) {
                            auto modNode = item.find("mod");
                            if (modNode == item.end() || !modNode->is_object()) {
                                continue;
                            }
                            list.items.push_back(Relation{blb::json_text(item, "type"), blb::json_text(item, "scope"),
                                                          refOf(*modNode)});
                        }
                    }
                    return [key, list = std::move(list)]() mutable { relationLists[key] = std::move(list); };
                },
                [key](const std::string&) -> Continuation { return [key] { failed(relationLists, key); }; });
        return entry;
    }

    blb::result<json> call(const std::string& path, blb::cancellation_token token) {
        blb::net::http_request request;
        request.url = std::string(API) + path;
        request.headers.push_back({"User-Agent", Platform::USER_AGENT});
        request.headers.push_back({"Accept", "application/json"});
        request.timeout = std::chrono::seconds(10);
        auto response = blb::net::http_send(request, token);
        if (!response) {
            return response.err();
        }
        if (!response->ok()) {
            return blb::make_error(blb::errc::http_status, "MC百科返回 " + std::to_string(response->status));
        }
        json doc = json::parse(response->body, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            return blb::make_error(blb::errc::parse, "MC百科返回的内容无效");
        }
        return doc;
    }

    Ref refOf(const json& node) {
        Ref ref;
        ref.id = number(node, "id");
        ref.type = blb::json_text(node, "type");
        ref.zh = blb::json_text(node, "zh");
        ref.en = blb::json_text(node, "en");
        ref.url = blb::json_text(node, "url");
        ref.roles = blb::json_text(node, "roles");
        ref.keys = blb::json_strings(node, "keys");
        if (ref.type.empty()) {
            ref.type = "mod";
        }
        return ref;
    }

    std::string slugOn(const Ref& ref, const std::string& site) {
        std::string prefix = site + ":";
        for (const std::string& key : ref.keys) {
            if (key.size() > prefix.size() && key.rfind(prefix, 0) == 0) {
                return key.substr(prefix.size());
            }
        }
        return std::string();
    }

    std::string displayName(const Ref& ref) {
        if (ref.zh.empty()) {
            return ref.en;
        }
        if (ref.en.empty() || ref.en == ref.zh) {
            return ref.zh;
        }
        return ref.zh + " (" + ref.en + ")";
    }

    void poll() {
        tasks.poll();
        auto settled = [](const auto& entry) { return entry.status != Status::Loading; };
        modUse.trim(mods, KEPT, settled);
        introUse.trim(intros, KEPT, settled);
        relationUse.trim(relationLists, KEPT, settled);
    }

    void shutdown() {
        tasks.stop();
    }
}
