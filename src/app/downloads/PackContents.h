#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace PackContents {

    struct Item {
        std::string name;
        std::string title;
        std::string version;
        std::string iconUrl;
        Addons::Source source = Addons::Source::Modrinth;
        std::string projectId;
        std::string slug;
        std::string client;
        std::string server;
        long long size = -1;
        bool resolved = false;
        bool manual = false;
        bool optional = false;
        bool bundled = false;
        bool file = false;
        std::vector<Addons::Dependency> dependencies;
    };

    struct Folder {
        std::string name;
        std::vector<Folder> folders;
        std::vector<Item> items;
        size_t total = 0;
    };

    struct Contents {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        std::string minecraft;
        std::vector<std::string> loaders;
        Folder root;
        size_t mods = 0;
        size_t bundled = 0;
        unsigned long long downloaded = 0;
        unsigned long long size = 0;
    };

    const Contents& contents(const Addons::Version& version);
    void refresh(const Addons::Version& version);
    void poll();
    void shutdown();
}
