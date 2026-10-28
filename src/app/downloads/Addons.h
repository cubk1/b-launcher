#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Addons {

    enum class Source { Modrinth, CurseForge };
    enum class Kind { Mod, Modpack, ResourcePack, Shader, Datapack };
    enum class Sort { Relevance, Popularity, Downloads, Follows, Newest, Updated, Name };
    enum class Status { Loading, Ready, Failed };

    struct Query {
        Kind kind = Kind::Mod;
        std::string text;
        std::string gameVersion;
        std::vector<std::string> loaders;
        std::vector<std::string> categories;
        bool client = false;
        bool server = false;
        bool openSource = false;
        Sort sort = Sort::Relevance;
        int page = 0;
    };

    constexpr int PAGE_SIZE = 20;

    struct Hit {
        std::string id;
        std::string slug;
        std::string title;
        std::string description;
        std::string author;
        std::string iconUrl;
        std::string pageUrl;
        std::vector<std::string> categories;
        std::vector<std::string> loaders;
        long long downloads = 0;
        long long follows = 0;
    };

    struct Results {
        Status status = Status::Loading;
        std::vector<Hit> hits;
        int total = 0;
        std::string error;
    };

    struct Category {
        std::string id;
        std::string name;
        std::string header;
        std::string iconUrl;
    };

    struct File {
        std::string url;
        std::string filename;
        std::string sha1;
        long long size = -1;
        bool primary = false;
    };

    struct Dependency {
        std::string projectId;
        std::string versionId;
        bool required = false;
    };

    struct Version {
        Source source = Source::Modrinth;
        std::string id;
        std::string projectId;
        std::string name;
        std::string number;
        std::string type;
        std::vector<std::string> gameVersions;
        std::vector<std::string> loaders;
        std::string published;
        long long downloads = 0;
        std::vector<File> files;
        std::vector<Dependency> dependencies;

        const File* file() const;
    };

    struct Versions {
        Status status = Status::Loading;
        std::vector<Version> list;
        std::string error;
    };

    struct Fetched {
        bool ok = false;
        std::string error;
        std::vector<Version> versions;
    };

    const char* sourceName(Source source);
    const Results& search(Source source, const Query& query);
    void refresh(Source source, const Query& query);
    const std::vector<Category>& categories(Source source, Kind kind);
    const Versions& versions(Source source, const std::string& projectId);
    void refreshVersions(Source source, const std::string& projectId);
    Fetched fetchVersion(Source source, const std::string& versionId);
    Fetched fetchProjectVersions(Source source, const std::string& projectId, const std::string& gameVersion,
                                 const std::string& loader);

    uint32_t icon(const std::string& url);
    std::string count(long long value);
    std::vector<std::string> fileUrls(const std::string& url);

    void preload();
    void poll();
    void shutdown();
}
