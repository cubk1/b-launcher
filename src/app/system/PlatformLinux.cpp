#include "app/system/Platform.h"
#include "app/system/PlatformPosix.h"

#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <ctime>
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

        std::string trashPath(const std::string& path) {
            static const char* hex = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : path) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
                    c == '-' || c == '_' || c == '.' || c == '~') {
                    out += (char) c;
                } else {
                    out += '%';
                    out += hex[c >> 4];
                    out += hex[c & 15];
                }
            }
            return out;
        }

        std::filesystem::path mountRoot(const std::filesystem::path& path, dev_t device) {
            std::filesystem::path at = path;
            while (at.has_relative_path()) {
                struct stat st {};
                std::filesystem::path parent = at.parent_path();
                if (lstat(parent.c_str(), &st) != 0 || st.st_dev != device) {
                    return at;
                }
                at = parent;
            }
            return at;
        }

        bool ownTrash(const std::filesystem::path& trash) {
            struct stat st {};
            if (lstat(trash.c_str(), &st) != 0) {
                return mkdir(trash.c_str(), 0700) == 0;
            }
            return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_uid == getuid();
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

    bool recycle(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::path target = std::filesystem::absolute(path, error).lexically_normal();
        struct stat source {};
        if (error || !target.has_relative_path() || lstat(target.c_str(), &source) != 0) {
            return false;
        }
        std::filesystem::path home = blb::app_data_directory();
        struct stat homeStat {};
        std::filesystem::path trash;
        std::string recorded;
        if (!home.empty() && stat(home.c_str(), &homeStat) == 0 && homeStat.st_dev == source.st_dev) {
            trash = home / "Trash";
            recorded = target.string();
        } else {
            std::filesystem::path top = mountRoot(target.parent_path(), source.st_dev);
            trash = top / (".Trash-" + std::to_string(getuid()));
            recorded = target.lexically_relative(top).string();
            if (!ownTrash(trash)) {
                return false;
            }
        }
        std::filesystem::create_directories(trash / "files", error);
        std::filesystem::create_directories(trash / "info", error);
        std::string base = target.filename().string();
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char date[32];
        std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local);
        std::string info = "[Trash Info]\nPath=" + trashPath(recorded) + "\nDeletionDate=" + date + "\n";
        for (int attempt = 1; attempt < 1000; attempt++) {
            std::string name = attempt == 1 ? base : base + "." + std::to_string(attempt);
            std::filesystem::path record = trash / "info" / (name + ".trashinfo");
            int fd = ::open(record.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (fd < 0) {
                if (errno == EEXIST) {
                    continue;
                }
                return false;
            }
            bool written = write(fd, info.data(), info.size()) == (ssize_t) info.size();
            written = close(fd) == 0 && written;
            std::filesystem::path stored = trash / "files" / name;
            if (written && lstat(stored.c_str(), &homeStat) == 0) {
                unlink(record.c_str());
                continue;
            }
            if (written && rename(target.c_str(), stored.c_str()) == 0) {
                return true;
            }
            unlink(record.c_str());
            return false;
        }
        return false;
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
