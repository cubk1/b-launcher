#pragma once

#include <string>

// The launcher's own files on the author's site: what the latest version is, the Java to download and the home page's
// announcement.
namespace Server {

    inline constexpr const char* BASE_URL = "https://cubk.cn/bl/";

    // The address of one of the files, such as config.json.
    inline std::string file(const char* name) {
        return std::string(BASE_URL) + name;
    }
}
