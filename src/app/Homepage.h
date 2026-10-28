#pragma once

#include <string>
#include <vector>

namespace Homepage {

    constexpr int SOURCE_NONE = 0;
    constexpr int SOURCE_PRESET = 1;
    constexpr int SOURCE_FILE = 2;
    constexpr int SOURCE_URL = 3;

    int source();
    std::vector<std::string> choices();
    std::string choice();
    void choose(const std::string& name);
    std::string url();
    void setUrl(const std::string& value);
    std::string file();
    void setFile(const std::string& value);
    void reload();

    void draw(float left, float top, float right, float bottom, float reserve, bool blocked);
    void poll();
    void shutdown();
}
