#pragma once

// CMake gives the version; a build without it says 0.0.0.
#ifndef B_LAUNCHER_VERSION
#define B_LAUNCHER_VERSION "0.0.0"
#endif

namespace About {

    // The launcher and where it comes from, the same in both interfaces.
    inline constexpr const char* NAME = "B Launcher";
    // The name with the trademark sign, as the launcher shows it as a title.
    inline constexpr const char* BRAND = "B Launcher\xE2\x84\xA2";
    inline constexpr const char* VERSION = B_LAUNCHER_VERSION;
    inline constexpr const char* AUTHOR = "cubk";
    inline constexpr const char* AUTHOR_URL = "https://cubk.cn/";
    inline constexpr const char* REPOSITORY = "cubk1/b-launcher";
    inline constexpr const char* REPOSITORY_URL = "https://github.com/cubk1/b-launcher";
    inline constexpr const char* RELEASES_URL = "https://github.com/cubk1/b-launcher/releases";

    struct Link {
        const char* name;
        const char* value;
        const char* url;
    };

    // What 关于 lists: the project, who it thanks and what it is built on.
    inline constexpr Link PROJECT[] = {
            {"作者", AUTHOR, AUTHOR_URL},
            {"GitHub", REPOSITORY, REPOSITORY_URL},
    };
    inline constexpr Link THANKS[] = {
            {"BMCLAPI", "提供下载源", "https://www.bangbang93.com/"},
            {"PCL", "提供主页系统", "https://github.com/Meloong-Git/PCL"},
    };
    inline constexpr Link CREDITS[] = {
            {"Dear ImGui", "MIT", "https://github.com/ocornut/imgui"},
            {"FreeType", "FreeType License", "https://freetype.org"},
            {"libcurl", "curl License", "https://curl.se"},
            {"zlib-ng", "zlib License", "https://github.com/zlib-ng/zlib-ng"},
            {"minizip-ng", "zlib License", "https://github.com/zlib-ng/minizip-ng"},
            {"simdjson", "Apache-2.0", "https://github.com/simdjson/simdjson"},
            {"JSON for Modern C++", "MIT", "https://github.com/nlohmann/json"},
            {"reproc", "MIT", "https://github.com/DaanDeMeyer/reproc"},
            {"stb", "MIT", "https://github.com/nothings/stb"},
            {"nanosvg", "zlib License", "https://github.com/memononen/nanosvg"},
            {"Font Awesome Free", "SIL OFL 1.1", "https://fontawesome.com"},
    };
}
