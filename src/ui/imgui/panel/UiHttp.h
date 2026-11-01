#pragma once

#include <string>
#include <stop_token>
#include <utility>
#include <vector>

namespace UiHttp {

    // Cancellation is observed between WinHTTP calls; existing phase timeouts remain.
    bool get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers, int timeoutMs,
             size_t maxBytes, std::vector<unsigned char>& out, int& status, std::stop_token stop = {});
    std::string urlEncode(const std::string& value);
}
