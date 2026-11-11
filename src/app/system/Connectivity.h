#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Connectivity {
    bool answers(const std::string& url, std::chrono::milliseconds timeout = std::chrono::seconds(3));
    bool answers(const std::vector<std::string>& urls, std::chrono::milliseconds timeout = std::chrono::seconds(3));
    bool internet(std::chrono::milliseconds timeout = std::chrono::seconds(3));
}
