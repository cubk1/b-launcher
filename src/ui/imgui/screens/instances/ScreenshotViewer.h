#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ScreenshotViewer {

    struct Picture {
        std::string thumb;
        std::string full;
        std::string title;
        std::string description;
    };

    void open(const std::string& key, const std::filesystem::path& path);
    void openWeb(std::vector<Picture> pictures, int index);
    void close();
    bool isOpen();
}
