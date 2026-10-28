#include "app/downloads/Modrinth.h"

#include <core/base/cancellation.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace Modrinth {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Category;
    using Addons::Dependency;
    using Addons::Fetched;
    using Addons::File;
    using Addons::Hit;
    using Addons::Kind;
    using Addons::PAGE_SIZE;
    using Addons::Query;
    using Addons::Results;
    using Addons::Sort;
    using Addons::Status;
    using Addons::Version;
    using Addons::Versions;

    namespace {
        using Continuation = std::function<void()>;

        const char* const API = "https://api.modrinth.com/v2";
        const char* const API_MIRROR = "https://mod.mcimirror.top/modrinth/v2";

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;

        std::map<std::string, Results> searches;
        std::map<int, std::vector<Category>> categoryLists;
        bool categoriesStarted = false;
        std::map<std::string, Versions> versionLists;

        void run(std::function<Continuation(blb::cancellation_token)> work,
                 std::function<Continuation(const std::string&)> fail) {
            blb::cancellation_token token = stopping.token();
            workers.emplace_back([work, fail, token] {
                Continuation next;
                try {
                    next = work(token);
                } catch (const std::exception& error) {
                    next = fail ? fail(error.what()) : nullptr;
                }
                if (next) {
                    std::lock_guard<std::mutex> guard(lock);
                    finished.push_back(std::move(next));
                }
            });
        }

        std::string text(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        long long number(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<long long>() : 0;
        }

        std::vector<std::string> strings(const json& node, const char* key) {
            std::vector<std::string> out;
            auto it = node.find(key);
            if (it != node.end() && it->is_array()) {
                for (const json& item : *it) {
                    if (item.is_string()) {
                        out.push_back(item.get<std::string>());
                    }
                }
            }
            return out;
        }

        blb::result<json> api(const std::string& path, blb::cancellation_token token) {
            std::vector<std::string> urls = {std::string(API) + path, std::string(API_MIRROR) + path};
            auto body = blb::net::http_get(urls, token);
            if (!body) {
                return body.err();
            }
            json doc = json::parse(*body, nullptr, false);
            if (doc.is_discarded()) {
                return blb::make_error(blb::errc::parse, "Modrinth 返回的内容无效");
            }
            return doc;
        }

        bool isLoader(const std::string& tag) {
            static const std::set<std::string> loaders = {
                    "fabric",    "forge",      "neoforge",   "quilt",      "liteloader",    "rift",
                    "modloader", "risugami",   "babric",     "bta-babric", "legacy-fabric", "nilloader",
                    "ornithe",   "java-agent", "minecraft",  "datapack",   "iris",          "optifine",
                    "canvas",    "vanilla",    "bukkit",     "spigot",     "paper",         "purpur",
                    "folia",     "sponge",     "bungeecord", "velocity",   "waterfall",
            };
            return loaders.count(tag) != 0;
        }

        std::string joined(const std::vector<std::string>& values) {
            std::string out;
            for (const std::string& value : values) {
                out += value + ",";
            }
            return out;
        }

        std::string queryKey(const Query& query) {
            return std::string(projectType(query.kind)) + "\n" + query.text + "\n" + query.gameVersion + "\n" +
                    joined(query.loaders) + "\n" + joined(query.categories) + "\n" + (query.client ? "c" : "") +
                    (query.server ? "s" : "") + (query.openSource ? "o" : "") + "\n" +
                    std::to_string((int) query.sort) + "\n" + std::to_string(query.page);
        }

        const char* sortIndex(Sort sort) {
            switch (sort) {
                case Sort::Downloads: return "downloads";
                case Sort::Follows: return "follows";
                case Sort::Newest: return "newest";
                case Sort::Updated: return "updated";
                default: return "relevance";
            }
        }

        void fetchSearch(const Query& query) {
            std::string key = queryKey(query);
            Results& entry = searches[key];
            entry.status = Status::Loading;
            entry.error.clear();
            json facets = json::array();
            facets.push_back(json::array({std::string("project_type:") + projectType(query.kind)}));
            if (!query.gameVersion.empty()) {
                facets.push_back(json::array({"versions:" + query.gameVersion}));
            }
            if (!query.loaders.empty()) {
                json any = json::array();
                for (const std::string& loader : query.loaders) {
                    any.push_back("categories:" + loader);
                }
                facets.push_back(any);
            }
            for (const std::string& category : query.categories) {
                facets.push_back(json::array({"categories:" + category}));
            }
            if (query.client) {
                facets.push_back(json::array({"client_side:required", "client_side:optional"}));
            }
            if (query.server) {
                facets.push_back(json::array({"server_side:required", "server_side:optional"}));
            }
            if (query.openSource) {
                facets.push_back(json::array({"open_source:true"}));
            }
            std::string path = "/search?query=" + blb::url_encode(query.text) +
                    "&facets=" + blb::url_encode(facets.dump()) + "&index=" + sortIndex(query.sort) +
                    "&offset=" + std::to_string(query.page * PAGE_SIZE) + "&limit=" + std::to_string(PAGE_SIZE);
            std::string kind = projectType(query.kind);
            run(
                    [key, path, kind](blb::cancellation_token token) -> Continuation {
                        auto doc = api(path, token);
                        if (!doc) {
                            std::string error = doc.err().message;
                            return [key, error] {
                                searches[key].status = Status::Failed;
                                searches[key].error = error;
                            };
                        }
                        Results results;
                        results.status = Status::Ready;
                        results.total = (int) number(*doc, "total_hits");
                        auto hits = doc->find("hits");
                        if (hits != doc->end() && hits->is_array()) {
                            for (const json& item : *hits) {
                                Hit hit;
                                hit.id = text(item, "project_id");
                                hit.slug = text(item, "slug");
                                hit.title = text(item, "title");
                                hit.description = text(item, "description");
                                hit.author = text(item, "author");
                                hit.iconUrl = text(item, "icon_url");
                                hit.pageUrl =
                                        "https://modrinth.com/" + kind + "/" + (hit.slug.empty() ? hit.id : hit.slug);
                                hit.downloads = number(item, "downloads");
                                hit.follows = number(item, "follows");
                                std::vector<std::string> tags = strings(item, "display_categories");
                                if (tags.empty()) {
                                    tags = strings(item, "categories");
                                }
                                for (const std::string& tag : tags) {
                                    (isLoader(tag) ? hit.loaders : hit.categories).push_back(tag);
                                }
                                results.hits.push_back(std::move(hit));
                            }
                        }
                        return [key, results]() mutable { searches[key] = std::move(results); };
                    },
                    [key](const std::string& error) -> Continuation {
                        return [key, error] {
                            searches[key].status = Status::Failed;
                            searches[key].error = error;
                        };
                    });
        }

        void fetchCategories() {
            categoriesStarted = true;
            run(
                    [](blb::cancellation_token token) -> Continuation {
                        auto doc = api("/tag/category", token);
                        if (!doc || !doc->is_array()) {
                            return [] { categoriesStarted = false; };
                        }
                        std::map<std::string, std::vector<Category>> byType;
                        for (const json& item : *doc) {
                            std::string name = text(item, "name");
                            byType[text(item, "project_type")].push_back(
                                    Category{name, name, text(item, "header"), std::string()});
                        }
                        return [byType] {
                            for (Kind kind :
                                 {Kind::Mod, Kind::Modpack, Kind::ResourcePack, Kind::Shader, Kind::Datapack}) {
                                auto found = byType.find(kind == Kind::Datapack ? "mod" : projectType(kind));
                                if (found != byType.end()) {
                                    categoryLists[(int) kind] = found->second;
                                }
                            }
                        };
                    },
                    nullptr);
        }

        Version parseVersion(const json& item) {
            Version version;
            version.source = Addons::Source::Modrinth;
            version.id = text(item, "id");
            version.projectId = text(item, "project_id");
            version.name = text(item, "name");
            version.number = text(item, "version_number");
            version.type = text(item, "version_type");
            version.gameVersions = strings(item, "game_versions");
            version.loaders = strings(item, "loaders");
            version.published = text(item, "date_published");
            version.downloads = number(item, "downloads");
            auto files = item.find("files");
            if (files != item.end() && files->is_array()) {
                for (const json& file : *files) {
                    File f;
                    f.url = text(file, "url");
                    f.filename = text(file, "filename");
                    f.size = number(file, "size");
                    f.primary = file.value("primary", false);
                    auto hashes = file.find("hashes");
                    if (hashes != file.end() && hashes->is_object()) {
                        f.sha1 = text(*hashes, "sha1");
                    }
                    version.files.push_back(std::move(f));
                }
            }
            auto dependencies = item.find("dependencies");
            if (dependencies != item.end() && dependencies->is_array()) {
                for (const json& dependency : *dependencies) {
                    std::string type = text(dependency, "dependency_type");
                    if (type != "required" && type != "optional") {
                        continue;
                    }
                    version.dependencies.push_back(Dependency{text(dependency, "project_id"),
                                                              text(dependency, "version_id"), type == "required"});
                }
            }
            return version;
        }

        std::vector<Version> parseVersions(const json& doc) {
            std::vector<Version> out;
            if (doc.is_array()) {
                for (const json& item : doc) {
                    out.push_back(parseVersion(item));
                }
            }
            std::stable_sort(out.begin(), out.end(),
                             [](const Version& a, const Version& b) { return a.published > b.published; });
            return out;
        }

        void fetchVersions(const std::string& projectId) {
            Versions& entry = versionLists[projectId];
            entry.status = Status::Loading;
            entry.error.clear();
            run(
                    [projectId](blb::cancellation_token token) -> Continuation {
                        auto doc = api("/project/" + blb::url_encode(projectId) + "/version", token);
                        if (!doc) {
                            std::string error = doc.err().message;
                            return [projectId, error] {
                                versionLists[projectId].status = Status::Failed;
                                versionLists[projectId].error = error;
                            };
                        }
                        std::vector<Version> list = parseVersions(*doc);
                        return [projectId, list]() mutable {
                            versionLists[projectId].status = Status::Ready;
                            versionLists[projectId].list = std::move(list);
                        };
                    },
                    [projectId](const std::string& error) -> Continuation {
                        return [projectId, error] {
                            versionLists[projectId].status = Status::Failed;
                            versionLists[projectId].error = error;
                        };
                    });
        }

    }

    const char* projectType(Kind kind) {
        switch (kind) {
            case Kind::Modpack: return "modpack";
            case Kind::ResourcePack: return "resourcepack";
            case Kind::Shader: return "shader";
            case Kind::Datapack: return "datapack";
            default: return "mod";
        }
    }

    const Results& search(const Query& query) {
        std::string key = queryKey(query);
        auto found = searches.find(key);
        if (found == searches.end()) {
            fetchSearch(query);
            found = searches.find(key);
        }
        return found->second;
    }

    void refresh(const Query& query) {
        auto found = searches.find(queryKey(query));
        if (found == searches.end() || found->second.status != Status::Loading) {
            fetchSearch(query);
        }
    }

    void preload() {
        if (!categoriesStarted) {
            fetchCategories();
        }
    }

    const std::vector<Category>& categories(Kind kind) {
        static const std::vector<Category> none;
        if (!categoriesStarted) {
            fetchCategories();
        }
        auto found = categoryLists.find((int) kind);
        return found != categoryLists.end() ? found->second : none;
    }

    const Versions& versions(const std::string& projectId) {
        auto found = versionLists.find(projectId);
        if (found == versionLists.end()) {
            fetchVersions(projectId);
            found = versionLists.find(projectId);
        }
        return found->second;
    }

    void refreshVersions(const std::string& projectId) {
        auto found = versionLists.find(projectId);
        if (found == versionLists.end() || found->second.status != Status::Loading) {
            fetchVersions(projectId);
        }
    }

    std::string label(const std::string& tag) {
        static const std::unordered_map<std::string, std::string> names = {
                {"adventure", "冒险"},
                {"cursed", "诅咒"},
                {"decoration", "装饰"},
                {"economy", "经济"},
                {"equipment", "装备"},
                {"food", "食物"},
                {"game-mechanics", "游戏机制"},
                {"library", "库"},
                {"magic", "魔法"},
                {"management", "管理"},
                {"minigame", "小游戏"},
                {"mobs", "生物"},
                {"optimization", "优化"},
                {"social", "社交"},
                {"storage", "存储"},
                {"technology", "科技"},
                {"transportation", "交通"},
                {"utility", "实用"},
                {"worldgen", "世界生成"},
                {"challenging", "挑战"},
                {"combat", "战斗"},
                {"kitchen-sink", "大杂烩"},
                {"lightweight", "轻量"},
                {"multiplayer", "多人"},
                {"quests", "任务"},
                {"audio", "音频"},
                {"blocks", "方块"},
                {"core-shaders", "核心着色器"},
                {"entities", "实体"},
                {"environment", "环境"},
                {"fonts", "字体"},
                {"gui", "界面"},
                {"items", "物品"},
                {"locale", "语言"},
                {"modded", "模组"},
                {"models", "模型"},
                {"realistic", "写实"},
                {"simplistic", "简约"},
                {"themed", "主题"},
                {"tweaks", "调整"},
                {"vanilla-like", "原版风格"},
                {"atmosphere", "大气"},
                {"bloom", "泛光"},
                {"cartoon", "卡通"},
                {"colored-lighting", "彩色光照"},
                {"fantasy", "奇幻"},
                {"foliage", "植被"},
                {"high", "高配"},
                {"low", "低配"},
                {"medium", "中配"},
                {"path-tracing", "路径追踪"},
                {"pbr", "PBR"},
                {"potato", "土豆"},
                {"reflections", "反射"},
                {"screenshot", "截图"},
                {"semi-realistic", "半写实"},
                {"shadows", "阴影"},
                {"fabric", "Fabric"},
                {"forge", "Forge"},
                {"neoforge", "NeoForge"},
                {"quilt", "Quilt"},
                {"liteloader", "LiteLoader"},
                {"rift", "Rift"},
                {"modloader", "ModLoader"},
                {"babric", "Babric"},
                {"legacy-fabric", "Legacy Fabric"},
                {"ornithe", "Ornithe"},
                {"iris", "Iris"},
                {"optifine", "OptiFine"},
                {"canvas", "Canvas"},
                {"vanilla", "原版"},
                {"datapack", "数据包"},
                {"minecraft", "Minecraft"},
                {"bukkit", "Bukkit"},
                {"spigot", "Spigot"},
                {"paper", "Paper"},
                {"purpur", "Purpur"},
                {"folia", "Folia"},
                {"sponge", "Sponge"},
                {"velocity", "Velocity"},
                {"bungeecord", "BungeeCord"},
                {"waterfall", "Waterfall"},
        };
        auto found = names.find(tag);
        if (found != names.end()) {
            return found->second;
        }
        std::string out = tag;
        if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
            out[0] = (char) (out[0] - 'a' + 'A');
        }
        std::replace(out.begin(), out.end(), '-', ' ');
        return out;
    }

    void poll() {
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            if (next) {
                next();
            }
        }
    }

    void shutdown() {
        stopping.cancel();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }

    Fetched fetchVersion(const std::string& versionId) {
        Fetched out;
        auto doc = api("/version/" + blb::url_encode(versionId), stopping.token());
        if (!doc) {
            out.error = doc.err().message;
            return out;
        }
        out.ok = true;
        out.versions.push_back(parseVersion(*doc));
        return out;
    }

    Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                 const std::string& loader) {
        Fetched out;
        std::string path = "/project/" + blb::url_encode(projectId) + "/version";
        std::string separator = "?";
        if (!loader.empty()) {
            path += separator + "loaders=" + blb::url_encode(json::array({loader}).dump());
            separator = "&";
        }
        if (!gameVersion.empty()) {
            path += separator + "game_versions=" + blb::url_encode(json::array({gameVersion}).dump());
        }
        auto doc = api(path, stopping.token());
        if (!doc) {
            out.error = doc.err().message;
            return out;
        }
        out.ok = true;
        out.versions = parseVersions(*doc);
        return out;
    }
}
