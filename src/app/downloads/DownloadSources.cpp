#include "app/downloads/DownloadSources.h"
#include "app/system/Config.h"

#include <core/base/json.hpp>

#include <mutex>
#include <string>
#include <utility>

namespace DownloadSources {

    namespace blb = b_launcher_base;
    using blb::net::source_order;

    namespace {
        const char* const KEY = "downloadSources";
        const source_order ORDERS[] = {source_order::mirror_first, source_order::official_first,
                                       source_order::official_only};

        std::mutex lock;
        std::shared_ptr<blb::net::mirror_provider> current;

        const char* keyOf(Kind kind) {
            switch (kind) {
                case Kind::Assets: return "assets";
                case Kind::Modrinth: return "modrinth";
                case Kind::CurseForge: return "curseforge";
                default: return "minecraft";
            }
        }

        const char* nameOf(source_order value) {
            switch (value) {
                case source_order::official_first: return "official";
                case source_order::official_only: return "officialOnly";
                default: return "mirror";
            }
        }

        source_order fallback(Kind kind) {
            blb::net::source_orders defaults;
            switch (kind) {
                case Kind::Assets: return defaults.assets;
                case Kind::Modrinth: return defaults.modrinth;
                case Kind::CurseForge: return defaults.curseforge;
                default: return defaults.game;
            }
        }
    }

    source_order order(Kind kind) {
        const nlohmann::json& settings = Config::data();
        if (auto group = settings.find(KEY); group != settings.end() && group->is_object()) {
            std::string stored = blb::json_text(*group, keyOf(kind));
            for (source_order candidate : ORDERS) {
                if (stored == nameOf(candidate)) {
                    return candidate;
                }
            }
        }
        return fallback(kind);
    }

    void setOrder(Kind kind, source_order value) {
        nlohmann::json& settings = Config::data();
        if (!settings[KEY].is_object()) {
            settings[KEY] = nlohmann::json::object();
        }
        settings[KEY][keyOf(kind)] = nameOf(value);
        Config::save();
        apply();
    }

    std::shared_ptr<blb::net::mirror_provider> provider() {
        std::lock_guard<std::mutex> guard(lock);
        if (!current) {
            current = blb::net::make_source_provider({});
        }
        return current;
    }

    void apply() {
        blb::net::source_orders orders;
        orders.game = order(Kind::Minecraft);
        orders.assets = order(Kind::Assets);
        orders.modrinth = order(Kind::Modrinth);
        orders.curseforge = order(Kind::CurseForge);
        std::shared_ptr<blb::net::mirror_provider> next = blb::net::make_source_provider(orders);
        std::lock_guard<std::mutex> guard(lock);
        current = std::move(next);
    }
}
