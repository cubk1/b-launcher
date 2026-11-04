#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Platform {

    std::vector<std::string> commandLine();
    std::filesystem::path executablePath();
    std::filesystem::path configDirectory();
    bool spawnDetached(const std::vector<std::string>& arguments);
}
