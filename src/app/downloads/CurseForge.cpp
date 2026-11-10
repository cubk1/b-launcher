#include "app/downloads/CurseForge.h"
#include "app/downloads/DownloadSources.h"
#include "app/system/AsyncTasks.h"
#include "app/downloads/Recent.h"

#include <core/base/cancellation.hpp>
#include <core/base/format.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <core/base/json.hpp>

#include <algorithm>
#include <utility>
#include <map>
#include <set>

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
        using Continuation = AsyncTasks::Continuation;

        const char* const API = "https://api.curseforge.com/v1";
        const char* const KEY = "JDJhJDEwJDdXV1YxV0VMY3dYOFhwN2Q2OE1Icy41Z3JUeVpWYTZTeW55ZVN5TWgxcnNFODluSWEwOXpP";
        const int GAME = 432;
        const int FILE_PAGE = 50;
        const int FILE_PAGES = 20;

        AsyncTasks tasks;

        std::map<std::string, Results> searches;
        std::map<int, std::vector<Category>> categoryLists;
        bool categoriesStarted = false;
        std::map<std::string, Versions> versionLists;
        Recent searchUse, versionUse;

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
            for (const std::string& url : DownloadSources::provider()->resolve(std::string(API) + path)) {
                blb::net::http_request request;
                request.url = url;
                request.headers.push_back({"Accept", "application/json"});
                if (url.starts_with(API)) {
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
                case Kind::World: return 17;
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
            out += "\n";
            for (const std::string& category : query.curseCategories) {
                out += category + ",";
            }
            return out + "\n" + std::to_string((int) query.sort) + "\n" + std::to_string(query.page);
        }

        Hit parseHit(const json& item) {
            Hit hit;
            hit.id = id(item, "id");
            hit.slug = blb::json_text(item, "slug");
            hit.title = blb::json_text(item, "name");
            hit.description = blb::json_text(item, "summary");
            hit.downloads = number(item, "downloadCount");
            hit.follows = number(item, "thumbsUpCount");
            if (auto authors = item.find("authors");
                authors != item.end() && authors->is_array() && !authors->empty()) {
                hit.author = blb::json_text(authors->front(), "name");
                hit.authorId = id(authors->front(), "id");
            }
            if (auto logo = item.find("logo"); logo != item.end() && logo->is_object()) {
                hit.iconUrl = blb::json_text(*logo, "thumbnailUrl");
                if (hit.iconUrl.empty()) {
                    hit.iconUrl = blb::json_text(*logo, "url");
                }
            }
            if (auto links = item.find("links"); links != item.end() && links->is_object()) {
                hit.pageUrl = blb::json_text(*links, "websiteUrl");
            }
            int primary = (int) number(item, "classId");
            if (auto categories = item.find("categories"); categories != item.end() && categories->is_array()) {
                for (const json& category : *categories) {
                    if (category.is_object() && number(category, "id") != primary) {
                        hit.categories.push_back(blb::json_text(category, "name"));
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
            for (const std::string& category : query.curseCategories) {
                if (numeric(category)) {
                    ids.push_back(std::stoll(category));
                }
            }
            if (!ids.empty()) {
                path += "&categoryIds=" + blb::url_encode(ids.dump());
            }
            tasks.run(
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
                        return [key, results = std::move(results)]() mutable { searches[key] = std::move(results); };
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
            tasks.run(
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
                                    Category{id(item, "id"), blb::json_text(item, "name"), std::string(), blb::json_text(item, "iconUrl")});
                        }
                        for (auto& [owner, list] : byClass) {
                            std::sort(list.begin(), list.end(),
                                      [](const Category& a, const Category& b) { return a.name < b.name; });
                        }
                        return [byClass = std::move(byClass)] {
                            for (Kind kind : {Kind::Mod, Kind::Modpack, Kind::ResourcePack, Kind::Shader,
                                              Kind::Datapack, Kind::World}) {
                                auto found = byClass.find(classOf(kind));
                                if (found != byClass.end()) {
                                    categoryLists[(int) kind] = found->second;
                                }
                            }
                        };
                    },
                    [](const std::string&) -> Continuation {
                        return [] { categoriesStarted = false; };
                    });
        }

        Version parseFile(const json& item) {
            Version version;
            version.source = Addons::Source::CurseForge;
            version.id = id(item, "id");
            version.projectId = id(item, "modId");
            version.name = blb::json_text(item, "displayName");
            version.number = blb::json_text(item, "fileName");
            switch (number(item, "releaseType")) {
                case 2: version.type = "beta"; break;
                case 3: version.type = "alpha"; break;
                default: version.type = "release"; break;
            }
            version.published = blb::json_text(item, "fileDate");
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
            file.url = blb::json_text(item, "downloadUrl");
            file.filename = blb::json_text(item, "fileName");
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
                        file.sha1 = blb::json_text(hash, "value");
                    }
                }
            }
            version.files.push_back(std::move(file));
            if (auto dependencies = item.find("dependencies"); dependencies != item.end() && dependencies->is_array()) {
                for (const json& dependency : *dependencies) {
                    long long relation = number(dependency, "relationType");
                    if (relation == 1 || relation == 2 || relation == 3 || relation == 5) {
                        version.dependencies.push_back(Dependency{id(dependency, "modId"), std::string(), relation == 3,
                                                                  relation == 5, relation == 1});
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
            tasks.run(
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
                        return [projectId, files = std::move(files)]() mutable {
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

    blb::result<json> call(const std::string& path, const std::string& body, blb::cancellation_token token) {
        return api(path, body, token);
    }

    Kind kindOf(long long classId) {
        for (Kind kind : {Kind::Mod, Kind::Modpack, Kind::ResourcePack, Kind::Shader, Kind::Datapack, Kind::World}) {
            if (classOf(kind) == classId) {
                return kind;
            }
        }
        return Kind::Mod;
    }

    Hit hitFrom(const json& item) {
        return parseHit(item);
    }

    Version versionFrom(const json& item) {
        return parseFile(item);
    }

    const std::string& apiKey() {
        static const std::string key = blb::base64_decode(KEY).value_or(std::string());
        return key;
    }

    const Results& search(const Query& query) {
        std::string key = queryKey(query);
        searchUse.use(key);
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
        versionUse.use(projectId);
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

    Fetched fetchVersion(const std::string& fileId, blb::cancellation_token token) {
        Fetched out;
        if (!numeric(fileId)) {
            out.error = "CurseForge 上找不到这个文件";
            return out;
        }
        json body = {{"fileIds", json::array({std::stoll(fileId)})}};
        auto doc = api("/mods/files", body.dump(), token);
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
                                 const std::string& loader, blb::cancellation_token token) {
        Fetched out;
        std::string filter;
        if (!gameVersion.empty()) {
            filter += "&gameVersion=" + blb::url_encode(gameVersion);
        }
        if (int type = loaderType(loader)) {
            filter += "&modLoaderType=" + std::to_string(type);
        }
        auto list = allFiles(projectId, filter, token);
        if (!list) {
            out.error = list.err().message;
            return out;
        }
        out.ok = true;
        out.versions = std::move(*list);
        return out;
    }

    blb::result<std::map<std::uint32_t, Version>> matchFingerprints(const std::vector<std::uint32_t>& fingerprints,
                                                                    blb::cancellation_token token) {
        std::map<std::uint32_t, Version> out;
        if (fingerprints.empty()) {
            return out;
        }
        json body = {{"fingerprints", fingerprints}};
        auto doc = api("/fingerprints/" + std::to_string(GAME), body.dump(), token);
        if (!doc) {
            return doc.err();
        }
        auto data = doc->find("data");
        if (data == doc->end() || !data->is_object()) {
            return out;
        }
        auto matches = data->find("exactMatches");
        if (matches == data->end() || !matches->is_array()) {
            return out;
        }
        for (const json& match : *matches) {
            auto file = match.find("file");
            if (file == match.end() || !file->is_object()) {
                continue;
            }
            Version version = parseFile(*file);
            if (!version.id.empty() && !version.projectId.empty()) {
                out.emplace((std::uint32_t) number(*file, "fileFingerprint"), std::move(version));
            }
        }
        return out;
    }

    blb::result<std::vector<Hit>> fetchProjects(const std::vector<std::string>& ids, blb::cancellation_token token) {
        std::vector<Hit> out;
        json list = json::array();
        for (const std::string& value : ids) {
            if (numeric(value)) {
                list.push_back(std::stoll(value));
            }
        }
        if (list.empty()) {
            return out;
        }
        json body = {{"modIds", list}, {"filterPcOnly", true}};
        auto doc = api("/mods", body.dump(), token);
        if (!doc) {
            return doc.err();
        }
        if (auto data = doc->find("data"); data != doc->end() && data->is_array()) {
            for (const json& item : *data) {
                if (item.is_object()) {
                    out.push_back(parseHit(item));
                }
            }
        }
        return out;
    }

    void poll() {
        tasks.poll();
        // What is still loading is written when it arrives.
        searchUse.trim(searches, Addons::KEPT_SEARCHES,
                       [](const Results& results) { return results.status != Status::Loading; });
        versionUse.trim(versionLists, Addons::KEPT_VERSION_LISTS,
                        [](const Versions& versions) { return versions.status != Status::Loading; });
    }

    void shutdown() {
        tasks.stop();
    }
}
