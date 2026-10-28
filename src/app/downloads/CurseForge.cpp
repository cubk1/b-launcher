#include "app/downloads/CurseForge.h"

#include <core/base/cancellation.hpp>
#include <core/base/format.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace CurseForge {

    namespace blb = b_launcher_base;
    using json = nlohmann::json;
    using Addons::Category;
    using Addons::Dependency;
    using Addons::Fetched;
    using Addons::File;
    using Addons::Hit;
    using Addons::Kind;
    using Addons::Query;
    using Addons::Results;
    using Addons::Sort;
    using Addons::Status;
    using Addons::Version;
    using Addons::Versions;

    namespace {
        using Continuation = std::function<void()>;

        const char* const API = "https://api.curseforge.com/v1";
        const char* const API_MIRROR = "https://mod.mcimirror.top/curseforge/v1";
        const char* const KEY = "JDJhJDEwJDdXV1YxV0VMY3dYOFhwN2Q2OE1Icy41Z3JUeVpWYTZTeW55ZVN5TWgxcnNFODluSWEwOXpP";
        const int GAME = 432;
        const int FILE_PAGE = 50;
        const int FILE_PAGES = 20;

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

        std::string id(const json& node, const char* key) {
            auto it = node.find(key);
            return it != node.end() && it->is_number_integer() ? std::to_string(it->get<long long>()) : std::string();
        }

        blb::result<json> api(const std::string& path, const std::string& body, blb::cancellation_token token) {
            blb::error last = blb::make_error(blb::errc::network, "无法连接 CurseForge");
            for (const char* root : {API, API_MIRROR}) {
                blb::net::http_request request;
                request.url = std::string(root) + path;
                request.headers.push_back({"Accept", "application/json"});
                if (root == API) {
                    request.headers.push_back({"x-api-key", apiKey()});
                }
                if (!body.empty()) {
                    request.method = "POST";
                    request.body = body;
                    request.headers.push_back({"Content-Type", "application/json"});
                }
                auto response = blb::net::http_send(request, token);
                if (!response) {
                    last = response.err();
                    if (last.code == blb::errc::cancelled) {
                        return last;
                    }
                    continue;
                }
                if (!response->ok()) {
                    last = blb::make_error(blb::errc::http_status, blb::sformat("HTTP {}", response->status));
                    continue;
                }
                json doc = json::parse(response->body, nullptr, false);
                if (doc.is_discarded() || !doc.is_object()) {
                    last = blb::make_error(blb::errc::parse, "CurseForge 返回的内容无效");
                    continue;
                }
                return doc;
            }
            return last;
        }

        int classOf(Kind kind) {
            switch (kind) {
                case Kind::Modpack: return 4471;
                case Kind::ResourcePack: return 12;
                case Kind::Shader: return 6552;
                case Kind::Datapack: return 6945;
                default: return 6;
            }
        }

        int sortField(Sort sort) {
            switch (sort) {
                case Sort::Downloads: return 6;
                case Sort::Updated: return 3;
                case Sort::Newest: return 11;
                case Sort::Name: return 4;
                case Sort::Relevance: return 1;
                default: return 2;
            }
        }

        int loaderType(const std::string& tag) {
            if (tag == "forge") {
                return 1;
            }
            if (tag == "liteloader") {
                return 3;
            }
            if (tag == "fabric") {
                return 4;
            }
            if (tag == "quilt") {
                return 5;
            }
            if (tag == "neoforge") {
                return 6;
            }
            return 0;
        }

        std::string loaderTag(long long type) {
            switch (type) {
                case 1: return "forge";
                case 3: return "liteloader";
                case 4: return "fabric";
                case 5: return "quilt";
                case 6: return "neoforge";
                default: return std::string();
            }
        }

        std::string loaderTag(const std::string& name) {
            static const std::map<std::string, std::string> names = {
                    {"forge", "forge"}, {"neoforge", "neoforge"}, {"fabric", "fabric"},         {"quilt", "quilt"},
                    {"iris", "iris"},   {"optifine", "optifine"}, {"liteloader", "liteloader"},
            };
            auto found = names.find(blb::to_lower(name));
            return found != names.end() ? found->second : std::string();
        }

        bool numeric(const std::string& value) {
            return !value.empty() && value.size() < 18 &&
                    std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        std::string queryKey(const Query& query) {
            std::string out = std::to_string((int) query.kind) + "\n" + query.text + "\n" + query.gameVersion + "\n";
            for (const std::string& loader : query.loaders) {
                out += loader + ",";
            }
            out += "\n";
            for (const std::string& category : query.categories) {
                out += category + ",";
            }
            return out + "\n" + std::to_string((int) query.sort) + "\n" + std::to_string(query.page);
        }

        Hit parseHit(const json& item) {
            Hit hit;
            hit.id = id(item, "id");
            hit.slug = text(item, "slug");
            hit.title = text(item, "name");
            hit.description = text(item, "summary");
            hit.downloads = number(item, "downloadCount");
            hit.follows = number(item, "thumbsUpCount");
            if (auto authors = item.find("authors");
                authors != item.end() && authors->is_array() && !authors->empty()) {
                hit.author = text(authors->front(), "name");
            }
            if (auto logo = item.find("logo"); logo != item.end() && logo->is_object()) {
                hit.iconUrl = text(*logo, "thumbnailUrl");
                if (hit.iconUrl.empty()) {
                    hit.iconUrl = text(*logo, "url");
                }
            }
            if (auto links = item.find("links"); links != item.end() && links->is_object()) {
                hit.pageUrl = text(*links, "websiteUrl");
            }
            int primary = (int) number(item, "classId");
            if (auto categories = item.find("categories"); categories != item.end() && categories->is_array()) {
                for (const json& category : *categories) {
                    if (category.is_object() && number(category, "id") != primary) {
                        hit.categories.push_back(text(category, "name"));
                    }
                }
            }
            std::set<std::string> seen;
            if (auto files = item.find("latestFilesIndexes"); files != item.end() && files->is_array()) {
                for (const json& file : *files) {
                    std::string tag = loaderTag(number(file, "modLoader"));
                    if (!tag.empty() && seen.insert(tag).second) {
                        hit.loaders.push_back(tag);
                    }
                }
            }
            return hit;
        }

        void fetchSearch(const Query& query) {
            std::string key = queryKey(query);
            Results& entry = searches[key];
            entry.status = Status::Loading;
            entry.error.clear();
            std::string path = "/mods/search?gameId=" + std::to_string(GAME) +
                    "&classId=" + std::to_string(classOf(query.kind)) +
                    "&sortField=" + std::to_string(sortField(query.sort)) +
                    "&sortOrder=" + (query.sort == Sort::Name ? "asc" : "desc") +
                    "&index=" + std::to_string(query.page * Addons::PAGE_SIZE) +
                    "&pageSize=" + std::to_string(Addons::PAGE_SIZE);
            if (!query.text.empty()) {
                path += "&searchFilter=" + blb::url_encode(query.text);
            }
            if (!query.gameVersion.empty()) {
                path += "&gameVersion=" + blb::url_encode(query.gameVersion);
            }
            if (!query.loaders.empty()) {
                if (int type = loaderType(query.loaders.front())) {
                    path += "&modLoaderType=" + std::to_string(type);
                }
            }
            json ids = json::array();
            for (const std::string& category : query.categories) {
                if (numeric(category)) {
                    ids.push_back(std::stoll(category));
                }
            }
            if (!ids.empty()) {
                path += "&categoryIds=" + blb::url_encode(ids.dump());
            }
            run(
                    [key, path](blb::cancellation_token token) -> Continuation {
                        auto doc = api(path, std::string(), token);
                        if (!doc) {
                            std::string error = doc.err().message;
                            return [key, error] {
                                searches[key].status = Status::Failed;
                                searches[key].error = error;
                            };
                        }
                        Results results;
                        results.status = Status::Ready;
                        if (auto page = doc->find("pagination"); page != doc->end() && page->is_object()) {
                            results.total = (int) std::min<long long>(number(*page, "totalCount"), 10000);
                        }
                        if (auto data = doc->find("data"); data != doc->end() && data->is_array()) {
                            for (const json& item : *data) {
                                if (item.is_object()) {
                                    results.hits.push_back(parseHit(item));
                                }
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
                        auto doc = api("/categories?gameId=" + std::to_string(GAME), std::string(), token);
                        auto data = doc ? doc->find("data") : json::iterator();
                        if (!doc || data == doc->end() || !data->is_array()) {
                            return [] { categoriesStarted = false; };
                        }
                        std::map<int, std::vector<Category>> byClass;
                        for (const json& item : *data) {
                            if (!item.is_object() || item.value("isClass", false)) {
                                continue;
                            }
                            int owner = (int) number(item, "classId");
                            if (number(item, "parentCategoryId") != owner) {
                                continue;
                            }
                            byClass[owner].push_back(
                                    Category{id(item, "id"), text(item, "name"), std::string(), text(item, "iconUrl")});
                        }
                        for (auto& [owner, list] : byClass) {
                            std::sort(list.begin(), list.end(),
                                      [](const Category& a, const Category& b) { return a.name < b.name; });
                        }
                        return [byClass] {
                            for (Kind kind :
                                 {Kind::Mod, Kind::Modpack, Kind::ResourcePack, Kind::Shader, Kind::Datapack}) {
                                auto found = byClass.find(classOf(kind));
                                if (found != byClass.end()) {
                                    categoryLists[(int) kind] = found->second;
                                }
                            }
                        };
                    },
                    nullptr);
        }

        Version parseFile(const json& item) {
            Version version;
            version.source = Addons::Source::CurseForge;
            version.id = id(item, "id");
            version.projectId = id(item, "modId");
            version.name = text(item, "displayName");
            version.number = text(item, "fileName");
            switch (number(item, "releaseType")) {
                case 2: version.type = "beta"; break;
                case 3: version.type = "alpha"; break;
                default: version.type = "release"; break;
            }
            version.published = text(item, "fileDate");
            version.downloads = number(item, "downloadCount");
            if (auto games = item.find("gameVersions"); games != item.end() && games->is_array()) {
                for (const json& value : *games) {
                    if (!value.is_string()) {
                        continue;
                    }
                    std::string name = value.get<std::string>();
                    if (!name.empty() && name[0] >= '0' && name[0] <= '9') {
                        version.gameVersions.push_back(name);
                    } else if (std::string tag = loaderTag(name); !tag.empty()) {
                        version.loaders.push_back(tag);
                    }
                }
            }
            File file;
            file.url = text(item, "downloadUrl");
            file.filename = text(item, "fileName");
            file.size = number(item, "fileLength");
            file.primary = true;
            if (file.url.empty() && numeric(version.id) && !file.filename.empty()) {
                long long fileId = std::stoll(version.id);
                file.url = blb::sformat("https://edge.forgecdn.net/files/{}/{}/{}", fileId / 1000, fileId % 1000,
                                        blb::url_encode(file.filename));
            }
            if (auto hashes = item.find("hashes"); hashes != item.end() && hashes->is_array()) {
                for (const json& hash : *hashes) {
                    if (hash.is_object() && number(hash, "algo") == 1) {
                        file.sha1 = text(hash, "value");
                    }
                }
            }
            version.files.push_back(std::move(file));
            if (auto dependencies = item.find("dependencies"); dependencies != item.end() && dependencies->is_array()) {
                for (const json& dependency : *dependencies) {
                    long long relation = number(dependency, "relationType");
                    if (relation == 2 || relation == 3) {
                        version.dependencies.push_back(
                                Dependency{id(dependency, "modId"), std::string(), relation == 3});
                    }
                }
            }
            return version;
        }

        std::vector<Version> parseFiles(const json& doc) {
            std::vector<Version> out;
            if (auto data = doc.find("data"); data != doc.end() && data->is_array()) {
                for (const json& item : *data) {
                    if (item.is_object()) {
                        out.push_back(parseFile(item));
                    }
                }
            }
            return out;
        }

        void newestFirst(std::vector<Version>& list) {
            std::stable_sort(list.begin(), list.end(),
                             [](const Version& a, const Version& b) { return a.published > b.published; });
        }

        blb::result<std::vector<Version>> allFiles(const std::string& projectId, const std::string& filter,
                                                   blb::cancellation_token token) {
            std::vector<Version> out;
            for (int page = 0; page < FILE_PAGES; page++) {
                std::string path = "/mods/" + blb::url_encode(projectId) +
                        "/files?index=" + std::to_string(page * FILE_PAGE) + "&pageSize=" + std::to_string(FILE_PAGE) +
                        filter;
                auto doc = api(path, std::string(), token);
                if (!doc) {
                    if (out.empty()) {
                        return doc.err();
                    }
                    break;
                }
                std::vector<Version> chunk = parseFiles(*doc);
                out.insert(out.end(), chunk.begin(), chunk.end());
                long long total = 0;
                if (auto pagination = doc->find("pagination"); pagination != doc->end() && pagination->is_object()) {
                    total = number(*pagination, "totalCount");
                }
                if (chunk.empty() || (long long) out.size() >= total) {
                    break;
                }
            }
            newestFirst(out);
            return out;
        }

        void fetchVersions(const std::string& projectId) {
            Versions& entry = versionLists[projectId];
            entry.status = Status::Loading;
            entry.error.clear();
            run(
                    [projectId](blb::cancellation_token token) -> Continuation {
                        auto list = allFiles(projectId, std::string(), token);
                        if (!list) {
                            std::string error = list.err().message;
                            return [projectId, error] {
                                versionLists[projectId].status = Status::Failed;
                                versionLists[projectId].error = error;
                            };
                        }
                        std::vector<Version> files = std::move(*list);
                        return [projectId, files]() mutable {
                            versionLists[projectId].status = Status::Ready;
                            versionLists[projectId].list = std::move(files);
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

    const std::string& apiKey() {
        static const std::string key = blb::base64_decode(KEY).value_or(std::string());
        return key;
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

    Fetched fetchVersion(const std::string& fileId) {
        Fetched out;
        if (!numeric(fileId)) {
            out.error = "CurseForge 上找不到这个文件";
            return out;
        }
        json body = {{"fileIds", json::array({std::stoll(fileId)})}};
        auto doc = api("/mods/files", body.dump(), stopping.token());
        if (!doc) {
            out.error = doc.err().message;
            return out;
        }
        out.versions = parseFiles(*doc);
        out.ok = !out.versions.empty();
        if (!out.ok) {
            out.error = "CurseForge 上找不到这个文件";
        }
        return out;
    }

    Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                 const std::string& loader) {
        Fetched out;
        std::string filter;
        if (!gameVersion.empty()) {
            filter += "&gameVersion=" + blb::url_encode(gameVersion);
        }
        if (int type = loaderType(loader)) {
            filter += "&modLoaderType=" + std::to_string(type);
        }
        auto list = allFiles(projectId, filter, stopping.token());
        if (!list) {
            out.error = list.err().message;
            return out;
        }
        out.ok = true;
        out.versions = std::move(*list);
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
}
