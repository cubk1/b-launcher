#include "app/system/Platform.h"
#include "app/system/PlatformPosix.h"

#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Platform {

    namespace {
        namespace blb = b_launcher_base;

        std::string unquote(std::string value) {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            if (value.starts_with("$HOME")) {
                value = blb::path_to_utf8(blb::home_directory()) + value.substr(5);
            }
            return value;
        }

        uint64_t meminfo(const std::string& text, const char* name) {
            size_t at = text.find(name);
            if (at == std::string::npos) {
                return 0;
            }
            return std::strtoull(text.c_str() + at + std::strlen(name), nullptr, 10);
        }
    }

    std::vector<std::string> commandLine() {
        std::ifstream file("/proc/self/cmdline", std::ios::binary);
        std::vector<std::string> args;
        std::string arg;
        while (std::getline(file, arg, '\0')) {
            args.push_back(arg);
        }
        return args;
    }

    std::filesystem::path executablePath() {
        std::error_code error;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::filesystem::path() : path;
    }

    std::filesystem::path exeDirectory() {
        if (auto image = blb::environment("APPIMAGE"); image && !image->empty()) {
            return blb::path_from_utf8(*image).parent_path();
        }
        std::filesystem::path exe = executablePath();
        return exe.empty() ? std::filesystem::current_path() : exe.parent_path();
    }

    std::filesystem::path configDirectory() {
        if (auto xdg = blb::environment("XDG_CONFIG_HOME"); xdg && xdg->starts_with('/')) {
            return blb::path_from_utf8(*xdg) / "b-launcher";
        }
        std::filesystem::path home = blb::home_directory();
        return home.empty() ? std::filesystem::path() : home / ".config" / "b-launcher";
    }

    std::filesystem::path desktopDirectory() {
        std::filesystem::path config = configDirectory().parent_path() / "user-dirs.dirs";
        std::ifstream file(config);
        std::string line;
        while (std::getline(file, line)) {
            if (line.starts_with("XDG_DESKTOP_DIR=")) {
                std::string value = unquote(line.substr(16));
                if (!value.empty()) {
                    return blb::path_from_utf8(value);
                }
            }
        }
        return blb::home_directory() / "Desktop";
    }

    bool open(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return false;
        }
        return spawnDetached({"xdg-open", path.string()});
    }

    bool openUrl(const std::string& url) {
        return webUrl(url) && spawnDetached({"xdg-open", url});
    }

    void reveal(const std::filesystem::path& path) {
        open(path.parent_path());
    }

    Memory memory() {
        std::ifstream file("/proc/meminfo");
        std::stringstream text;
        text << file.rdbuf();
        uint64_t total = meminfo(text.str(), "MemTotal:");
        uint64_t available = meminfo(text.str(), "MemAvailable:");
        Memory out;
        if (total == 0) {
            out.totalMb = 8192;
            out.availableMb = 4096;
            return out;
        }
        out.totalMb = (uint32_t) (total / 1024);
        out.availableMb = (uint32_t) (available / 1024);
        return out;
    }
}
