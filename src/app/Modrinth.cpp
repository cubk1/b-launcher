#include "Modrinth.h"

#include "ui/panel/UiImages.h"

#include <core/base/cancellation.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

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

    namespace {
        using Continuation = std::function<void()>;

        const char* const API = "https://api.modrinth.com/v2";
        const char* const API_MIRROR = "https://mod.mcimirror.top/modrinth/v2";
        const char* const CDN = "https://cdn.modrinth.com";
        const char* const CDN_MIRROR = "https://mod.mcimirror.top";
        constexpr int ICON_SIZE = 64;
        constexpr int ICON_WORKERS = 4;

        std::mutex lock;
        std::deque<Continuation> finished;
        std::vector<std::thread> workers;
        blb::cancellation_source stopping;

        std::map<std::string, Results> searches;
        std::map<int, std::vector<Category>> categoryLists;
        bool categoriesStarted = false;
        std::map<std::string, Versions> versionLists;

        struct Icon {
            uint32_t texture = 0;
            bool requested = false;
        };
        std::unordered_map<std::string, Icon> icons;
        std::mutex iconLock;
        std::condition_variable iconWake;
        std::deque<std::string> iconQueue;
        std::vector<std::thread> iconWorkers;
        bool iconsStopping = false;

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

        void hand(Continuation next) {
            std::lock_guard<std::mutex> guard(lock);
            finished.push_back(std::move(next));
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
            run(
                    [key, path](blb::cancellation_token token) -> Continuation {
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
                            byType[text(item, "project_type")].push_back(
                                    Category{text(item, "name"), text(item, "header")});
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

        void roundCorners(std::vector<unsigned char>& rgba) {
            const float radius = ICON_SIZE * 0.22f;
            for (int y = 0; y < ICON_SIZE; y++) {
                for (int x = 0; x < ICON_SIZE; x++) {
                    float px = x + 0.5f;
                    float py = y + 0.5f;
                    float cx = std::clamp(px, radius, ICON_SIZE - radius);
                    float cy = std::clamp(py, radius, ICON_SIZE - radius);
                    float distance = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
                    float cover = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
                    if (cover < 1.0f) {
                        unsigned char& alpha = rgba[((size_t) y * ICON_SIZE + x) * 4 + 3];
                        alpha = (unsigned char) std::lround(alpha * cover);
                    }
                }
            }
        }

        bool decodeIcon(const std::string& data, std::vector<unsigned char>& rgba) {
            IWICImagingFactory* factory = nullptr;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory)))) {
                return false;
            }
            bool ok = false;
            IWICStream* stream = nullptr;
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICBitmapScaler* scaler = nullptr;
            IWICFormatConverter* converter = nullptr;
            UINT w = 0;
            UINT h = 0;
            if (SUCCEEDED(factory->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory((BYTE*) data.data(), (DWORD) data.size())) &&
                SUCCEEDED(
                        factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0) {
                float fit = (float) ICON_SIZE / (float) std::max(w, h);
                UINT sw = std::max(1u, (UINT) (w * fit + 0.5f));
                UINT sh = std::max(1u, (UINT) (h * fit + 0.5f));
                WICBitmapInterpolationMode mode = std::max(w, h) <= 32 ? WICBitmapInterpolationModeNearestNeighbor
                                                                       : WICBitmapInterpolationModeHighQualityCubic;
                if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame, sw, sh, mode)) &&
                    SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                                    nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    std::vector<unsigned char> scaled((size_t) sw * sh * 4);
                    if (SUCCEEDED(converter->CopyPixels(nullptr, sw * 4, (UINT) scaled.size(), scaled.data()))) {
                        rgba.assign((size_t) ICON_SIZE * ICON_SIZE * 4, 0);
                        UINT ox = (ICON_SIZE - sw) / 2;
                        UINT oy = (ICON_SIZE - sh) / 2;
                        for (UINT row = 0; row < sh; row++) {
                            std::copy(scaled.begin() + (size_t) row * sw * 4,
                                      scaled.begin() + (size_t) (row + 1) * sw * 4,
                                      rgba.begin() + ((size_t) (oy + row) * ICON_SIZE + ox) * 4);
                        }
                        roundCorners(rgba);
                        ok = true;
                    }
                }
            }
            if (converter) converter->Release();
            if (scaler) scaler->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (stream) stream->Release();
            factory->Release();
            return ok;
        }

        void iconWorker() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            blb::cancellation_token token = stopping.token();
            while (true) {
                std::string url;
                {
                    std::unique_lock<std::mutex> guard(iconLock);
                    iconWake.wait(guard, [] { return iconsStopping || !iconQueue.empty(); });
                    if (iconsStopping) {
                        break;
                    }
                    url = iconQueue.front();
                    iconQueue.pop_front();
                }
                auto data = blb::net::http_get(fileUrls(url), token);
                auto rgba = std::make_shared<std::vector<unsigned char>>();
                if (data && decodeIcon(*data, *rgba)) {
                    hand([url, rgba] { icons[url].texture = UiImages::upload(rgba->data(), ICON_SIZE, ICON_SIZE); });
                }
            }
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
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

    const File* Version::file() const {
        for (const File& f : files) {
            if (f.primary) {
                return &f;
            }
        }
        return files.empty() ? nullptr : &files.front();
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

    uint32_t icon(const std::string& url) {
        if (url.empty()) {
            return 0;
        }
        Icon& entry = icons[url];
        if (!entry.requested) {
            entry.requested = true;
            std::lock_guard<std::mutex> guard(iconLock);
            if (iconWorkers.empty() && !iconsStopping) {
                for (int i = 0; i < ICON_WORKERS; i++) {
                    iconWorkers.emplace_back(iconWorker);
                }
            }
            iconQueue.push_back(url);
            iconWake.notify_one();
        }
        return entry.texture;
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

    std::string count(long long value) {
        char text[32];
        if (value >= 100000000LL) {
            std::snprintf(text, sizeof(text), "%.1f亿", value / 100000000.0);
        } else if (value >= 10000LL) {
            std::snprintf(text, sizeof(text), "%.1f万", value / 10000.0);
        } else {
            std::snprintf(text, sizeof(text), "%lld", value);
        }
        return text;
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
        {
            std::lock_guard<std::mutex> guard(iconLock);
            iconsStopping = true;
        }
        iconWake.notify_all();
        for (std::thread& worker : iconWorkers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        iconWorkers.clear();
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

    std::vector<std::string> fileUrls(const std::string& url) {
        std::vector<std::string> out = {url};
        if (url.rfind(CDN, 0) == 0) {
            out.push_back(std::string(CDN_MIRROR) + url.substr(std::string(CDN).size()));
        }
        return out;
    }
}
