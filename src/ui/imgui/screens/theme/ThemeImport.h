#pragma once

#include <filesystem>

namespace ThemeImport {

    bool looksLikeTheme(const std::filesystem::path& zip);
    void start(const std::filesystem::path& zip);
    bool busy();
    void poll();
    void shutdown();
}
