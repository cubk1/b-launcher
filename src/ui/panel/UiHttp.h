#pragma once

#include <string>
#include <utility>
#include <vector>

namespace UiHttp {

    bool get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers, int timeoutMs,
             size_t maxBytes, std::vector<unsigned char>& out, int& status);
    std::string urlEncode(const std::string& value);
}
