#pragma once

#include <cstdint>
#include <string>

namespace Background {

    uint32_t sidebar();
    uint32_t panel();
    uint32_t wholeSidebar();
    uint32_t wholePanel();
    bool themeImage();

    std::string path();
    void setPath(const std::string& path);
    bool preparing();

    void poll();
    void shutdown();
}
