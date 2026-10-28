#pragma once

#include <cstdint>
#include <string>

namespace WebImages {

    struct Image {
        uint32_t texture = 0;
        int width = 0;
        int height = 0;
        bool done = false;
        bool failed = false;
    };

    const Image& get(const std::string& source, const std::string& fallback = std::string());
    long long generation();
    void poll();
    void shutdown();
}
