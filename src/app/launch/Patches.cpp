#include "app/launch/Patches.h"
#include "app/system/Config.h"
#include "app/launch/EmbeddedAgent.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <map>
#include <mutex>
#include <string_view>

namespace Patches {

    namespace blb = b_launcher_base;

    namespace {
        const char* const SECTION = "patches";
        const char* const LITTLESKIN = "yggdrasil:https://littleskin.cn/api/yggdrasil";

        struct Default {
            const char* key;
            const char* value;
            bool passed;
        };

        const Default DEFAULTS[] = {
                {"skin.authlib", "true", true},    {"skin.legacy", "true", true},     {"skin.mode", "fallback", true},
                {"skin.trusted", "true", true},    {"log4j.enabled", "true", true},   {"input.enabled", "true", true},
                {"lwjgl.unsafe", "true", true},    {"window.title", "", true},        {"window.icon", "", true},
                {"debug", "false", true},          {"skin.source", "mojang", false},  {"skin.player", "", false},
                {"skin.template.skin", "", false}, {"skin.template.cape", "", false},
        };

        struct Textures {
            std::string skin;
            std::string cape;
            bool slim = false;
        };

        std::mutex lookups;
        std::map<std::string, Textures> resolved;

        nlohmann::json& section() {
            nlohmann::json& doc = Config::data();
            if (!doc.contains(SECTION) || !doc[SECTION].is_object()) {
                doc[SECTION] = nlohmann::json::object();
            }
            return doc[SECTION];
        }

        const char* fallback(const std::string& key) {
            for (const Default& entry : DEFAULTS) {
                if (key == entry.key) {
                    return entry.value;
                }
            }
            return "";
        }

        bool inlineSafe(const std::string& text) {
            for (unsigned char c : text) {
                if (c < 0x20 || c >= 0x7F || c == ';') {
                    return false;
                }
            }
            return true;
        }

        std::string_view embedded() {
            return std::string_view((const char*) EmbeddedAgent::launcherAgent, EmbeddedAgent::launcherAgentSize);
        }

        void put(Plan& plan, const std::string& key, const std::string& text) {
            for (auto& entry : plan.values) {
                if (entry.first == key) {
                    entry.second = text;
                    return;
                }
            }
            plan.values.emplace_back(key, text);
        }

        std::string textOf(const nlohmann::json& doc, const char* key) {
            auto found = doc.find(key);
            return found != doc.end() && found->is_string() ? found->get<std::string>() : std::string();
        }

        std::optional<Textures> lookup(const std::string& name, blb::cancellation_token token, std::string& problem) {
            auto profile = blb::net::http_get(
                    "https://api.mojang.com/users/profiles/minecraft/" + blb::url_encode(name), token);
            if (!profile) {
                problem = profile.err().code == blb::errc::http_status ? "找不到这个正版玩家" : profile.err().message;
                return std::nullopt;
            }
            nlohmann::json who = nlohmann::json::parse(*profile, nullptr, false);
            std::string id = who.is_object() ? textOf(who, "id") : std::string();
            if (id.empty()) {
                problem = "找不到这个正版玩家";
                return std::nullopt;
            }
            auto session =
                    blb::net::http_get("https://sessionserver.mojang.com/session/minecraft/profile/" + id, token);
            if (!session) {
                problem = session.err().message;
                return std::nullopt;
            }
            nlohmann::json doc = nlohmann::json::parse(*session, nullptr, false);
            auto properties = doc.is_object() ? doc.find("properties") : doc.end();
            if (!doc.is_object() || properties == doc.end() || !properties->is_array()) {
                problem = "正版服务器返回的内容无效";
                return std::nullopt;
            }
            for (const nlohmann::json& property : *properties) {
                if (!property.is_object() || textOf(property, "name") != "textures") {
                    continue;
                }
                std::optional<std::string> decoded = blb::base64_decode(textOf(property, "value"));
                nlohmann::json textures = decoded ? nlohmann::json::parse(*decoded, nullptr, false) : nlohmann::json();
                auto all = textures.is_object() ? textures.find("textures") : textures.end();
                if (!textures.is_object() || all == textures.end() || !all->is_object()) {
                    break;
                }
                Textures out;
                if (auto skin = all->find("SKIN"); skin != all->end() && skin->is_object()) {
                    out.skin = textOf(*skin, "url");
                    auto metadata = skin->find("metadata");
                    out.slim = metadata != skin->end() && metadata->is_object() && textOf(*metadata, "model") == "slim";
                }
                if (auto cape = all->find("CAPE"); cape != all->end() && cape->is_object()) {
                    out.cape = textOf(*cape, "url");
                }
                if (out.skin.empty() && out.cape.empty()) {
                    problem = "这个正版玩家没有设置皮肤";
                    return std::nullopt;
                }
                return out;
            }
            problem = "正版服务器返回的内容无效";
            return std::nullopt;
        }
    }

    bool all() {
        const nlohmann::json& doc = Config::data();
        auto found = doc.find(SECTION);
        if (found == doc.end() || !found->is_object()) {
            return false;
        }
        auto on = found->find("all");
        return on != found->end() && on->is_boolean() && on->get<bool>();
    }

    void setAll(bool on) {
        section()["all"] = on;
        Config::save();
    }

    bool enabled(const std::optional<bool>& instance) {
        return instance ? *instance : all();
    }

    std::string value(const std::string& key) {
        const nlohmann::json& doc = Config::data();
        auto found = doc.find(SECTION);
        if (found != doc.end() && found->is_object()) {
            auto values = found->find("values");
            if (values != found->end() && values->is_object()) {
                auto entry = values->find(key);
                if (entry != values->end() && entry->is_string()) {
                    return entry->get<std::string>();
                }
            }
        }
        return fallback(key);
    }

    void setValue(const std::string& key, const std::string& text) {
        nlohmann::json& values = section()["values"];
        if (!values.is_object()) {
            values = nlohmann::json::object();
        }
        values[key] = text;
        Config::save();
    }

    bool flag(const std::string& key) {
        return value(key) == "true";
    }

    void setFlag(const std::string& key, bool on) {
        setValue(key, on ? "true" : "false");
    }

    Plan plan() {
        Plan out;
        for (const Default& entry : DEFAULTS) {
            if (entry.passed) {
                out.values.emplace_back(entry.key, value(entry.key));
            }
        }
        std::string source = value("skin.source");
        if (source == "littleskin") {
            put(out, "skin.sources", LITTLESKIN);
        } else if (source == "custom") {
            put(out, "skin.sources", "template");
            put(out, "skin.template.skin", value("skin.template.skin"));
            put(out, "skin.template.cape", value("skin.template.cape"));
            put(out, "skin.template.model", "auto");
        } else {
            put(out, "skin.sources", "mojang");
            out.player = std::string(blb::trim(value("skin.player")));
        }
        return out;
    }

    std::string resolve(Plan& plan, blb::cancellation_token token) {
        if (plan.player.empty()) {
            return {};
        }
        std::string key = blb::to_lower(plan.player);
        std::optional<Textures> found;
        {
            std::lock_guard<std::mutex> guard(lookups);
            auto cached = resolved.find(key);
            if (cached != resolved.end()) {
                found = cached->second;
            }
        }
        std::string problem;
        if (!found) {
            found = lookup(plan.player, token, problem);
            if (found) {
                std::lock_guard<std::mutex> guard(lookups);
                resolved[key] = *found;
            }
        }
        if (!found) {
            return "无法获取正版玩家 " + plan.player + " 的皮肤：" + problem + "，改为按玩家自己的名字查询";
        }
        put(plan, "skin.sources", "template");
        put(plan, "skin.template.skin", found->skin);
        put(plan, "skin.template.cape", found->cape);
        put(plan, "skin.template.model", found->slim ? "slim" : "default");
        return {};
    }

    std::filesystem::path jarIn(const std::filesystem::path& directory) {
        static const std::string tag = [] {
            std::string_view data = embedded();
            return blb::to_hex(blb::sha1::digest(data.data(), data.size())).substr(0, 12);
        }();
        return directory / ("launcher-agent-" + tag + ".jar");
    }

    std::vector<std::string> arguments(const std::filesystem::path& jar, const Plan& plan) {
        std::string options;
        std::vector<std::string> properties;
        for (const auto& [key, text] : plan.values) {
            if (inlineSafe(text)) {
                options += (options.empty() ? "" : ";") + key + "=" + text;
            } else {
                properties.push_back("-Dlauncheragent." + key + "=" + text);
            }
        }
        std::vector<std::string> out;
        out.push_back("-javaagent:" + blb::path_to_native(jar) + (options.empty() ? "" : "=" + options));
        out.insert(out.end(), properties.begin(), properties.end());
        return out;
    }

    std::string extract(const std::filesystem::path& jar) {
        std::string_view data = embedded();
        if (auto existing = blb::fs::read_file(jar); existing && *existing == data) {
            return {};
        }
        if (auto made = blb::fs::create_directories(jar.parent_path()); !made) {
            return made.err().message;
        }
        if (auto written = blb::fs::write_file_atomic(jar, data); !written) {
            return written.err().message;
        }
        return {};
    }
}
