#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace ModNames {

    struct Rewrite {
        bool waiting = false;
        std::string text;
    };

    bool applies(Addons::Kind kind);
    Rewrite rewrite(const std::string& query);
    std::string zh(const std::string& english);
    long long wikiId(const std::string& english);
    bool resolved(const std::vector<std::string>& englishNames);
    bool resolved(const std::vector<Addons::Hit>& hits);
    int revision();
    bool original();
    void setOriginal(bool on);
    void poll();
    void shutdown();
}
