#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace JavaDownloads {

    struct Package {
        std::string folder;
        std::string vendor;
        std::string product;
        std::string flavour;
        std::string version;
        int major = 0;
        bool preview = false;
        std::string url;
        std::string sha256;
        std::string prefix;
        std::string homePrefix;
        uint64_t archiveSize = 0;
        uint64_t unpackedSize = 0;
    };

    enum class Status { Idle, Loading, Ready, Failed };

    struct List {
        Status status = Status::Idle;
        std::vector<Package> packages;
        std::string error;
    };

    enum class State { None, Running, Done, Failed };

    const List& list();
    void refresh();
    std::string name(const Package& package);
    bool installed(const Package& package);
    State state(const std::string& folder);
    float progress(const std::string& folder);
    void install(const Package& package);
    std::filesystem::path root();
    void poll();
    void shutdown();
}
