#pragma once

#include <filesystem>
#include <string>

namespace KeyBindEditor {

    void draw(const std::string& id, const std::filesystem::path& file, bool legacy, const std::string& note, float top,
              bool blocked);
    bool page(const std::string& id, const std::filesystem::path& file, bool legacy, const std::string& note,
              bool blocked);
    int count(const std::filesystem::path& file);
}
