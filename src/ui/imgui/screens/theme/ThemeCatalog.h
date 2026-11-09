#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ThemeCatalog {

    struct Entry {
        std::string id;
        std::string name;
        std::string author;
        std::string version;
        std::string description;
        std::string updated;
        std::vector<std::string> dependencies;
        std::string package;
        uint64_t size = 0;
        std::string sha256;
        std::string icon;
        std::vector<std::string> previews;
    };

    enum class Status { Idle, Loading, Ready, Failed };
    enum class Local { Missing, Current, Outdated, Unknown };

    void refresh();
    void poll();
    void shutdown();
    Status status();
    const std::string& error();
    const std::vector<Entry>& entries();
    const Entry* find(const std::string& id);
    Local local(const Entry& entry);
    bool busy(const std::string& id);
    float progress(const std::string& id);
    void install(const std::string& id, bool apply);
}
