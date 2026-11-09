#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ThemeAssets {

    struct Texture {
        uint32_t id = 0;
        int width = 0;
        int height = 0;
        bool ready = false;
        bool failed = false;
    };

    const Texture& get(const std::string& source);
    void poll();
    void shutdown();
    void reset();
    std::vector<std::string> problems();
}
