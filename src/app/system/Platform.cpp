#include "app/system/Platform.h"

#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/meta/game_layout.hpp>

#include <algorithm>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace Platform {

    void* window = nullptr;

    std::filesystem::path userDirectory() {
        std::filesystem::path exe = exeDirectory();
#if !defined(_WIN32)
        if (access(exe.c_str(), W_OK) != 0) {
            std::filesystem::path home = b_launcher_base::home_directory();
            if (!home.empty()) {
                return home;
            }
        }
#endif
        return exe;
    }

    std::filesystem::path cacheDirectory() {
        std::error_code error;
        for (const std::filesystem::path& root : {exeDirectory() / L".minecraft", defaultMinecraft()}) {
            if (!root.empty() && std::filesystem::is_directory(root, error)) {
                return b_launcher_base::meta::game_layout(root).cache();
            }
        }
        return {};
    }

    bool webUrl(const std::string& url) {
        size_t scheme = url.find("://");
        if (scheme == std::string::npos ||
            (!b_launcher_base::iequals(url.substr(0, scheme), "http") &&
             !b_launcher_base::iequals(url.substr(0, scheme), "https")))
            return false;
        size_t host = scheme + 3;
        if (host == url.size() || url.find_first_of("/\\?#", host) == host ||
            std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            return false;
        return true;
    }

    uint32_t totalMemoryMb() {
        return memory().totalMb;
    }
}
