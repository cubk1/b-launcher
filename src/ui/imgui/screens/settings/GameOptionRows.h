#pragma once

#include <filesystem>
#include <string>

namespace GameOptionRows {

    constexpr int COUNT = 5;

    void caption(const std::string& title, const std::filesystem::path& file, const std::string& note, float y);
    void draw(const std::string& id, const std::filesystem::path& file, float y, bool blocked);
}
