#pragma once

#include <filesystem>
#include <string>

namespace ScreenshotViewer {

    void open(const std::string& key, const std::filesystem::path& path);
    void close();
    bool isOpen();
}
