#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Modrinth {

    enum class Kind { Mod, Modpack, ResourcePack, Shader, Datapack };
    const char* projectType(Kind kind);

    enum class Sort { Relevance, Downloads, Follows, Newest, Updated };

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

    const Results& search(const Query& query);
    void refresh(const Query& query);

    struct Category {
        std::string name;
        std::string header;
    };
    const std::vector<Category>& categories(Kind kind);
    void preload();

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

    const Versions& versions(const std::string& projectId);
    void refreshVersions(const std::string& projectId);

    uint32_t icon(const std::string& url);

    std::string label(const std::string& tag);
    std::string count(long long value);

    void poll();
    void shutdown();

    struct Fetched {
        bool ok = false;
        std::string error;
        std::vector<Version> versions;
    };
    Fetched fetchVersion(const std::string& versionId);
    Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                 const std::string& loader);
    std::vector<std::string> fileUrls(const std::string& url);
}
