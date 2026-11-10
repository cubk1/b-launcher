#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace Aggregate {

    struct Link {
        Addons::Source source = Addons::Source::Modrinth;
        std::string id;
        std::string slug;
        std::string url;
        long long downloads = 0;
    };

    struct Item {
        std::string title;
        std::string zh;
        std::string summary;
        std::string type;
        std::string iconUrl;
        long long downloads = 0;
        std::vector<std::string> gameVersions;
        std::vector<std::string> loaders;
        std::string mcmodUrl;
        std::vector<Link> links;
    };

    struct Request {
        std::string text;
        std::string gameVersion;
        std::string loader;
    };

    struct Step {
        int number = 0;
        std::string target;
        std::string keyword;
        bool done = false;
        int count = 0;
        std::vector<std::string> titles;
    };

    struct Results {
        Addons::Status status = Addons::Status::Loading;
        std::string summary;
        std::vector<Item> items;
        bool partial = false;
        std::string error;
        std::string thought;
        std::vector<Step> steps;
    };

    const Results& search(const Request& request);
    void refresh(const Request& request);
    void poll();
    void shutdown();
}
