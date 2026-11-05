#include "app/system/Platform.h"
#include "app/system/EmbeddedIcons.h"
#include "app/system/PlatformPosix.h"

#include <core/base/fs.hpp>

#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include <SDL3/SDL_misc.h>

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

        std::string desktopValue(std::string_view value) {
            std::string out;
            for (char c : value) {
                switch (c) {
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += c; break;
                }
            }
            return out;
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

        // A /nix/store path breaks after an update and garbage collection, so a shortcut uses
        // the profile link on PATH (e.g. /run/current-system/sw/bin) that points to the same file.
        std::filesystem::path stableExecutable(const std::filesystem::path& exe) {
            if (!blb::path_to_utf8(exe).starts_with("/nix/store/")) {
                return exe;
            }
            std::string path = blb::environment("PATH").value_or("");
            for (std::string_view entry : blb::split(path, ':')) {
                if (!entry.starts_with('/') || entry.starts_with("/nix/store/")) {
                    continue;
                }
                std::filesystem::path link = blb::path_from_utf8(entry) / exe.filename();
                std::error_code error;
                if (std::filesystem::equivalent(link, exe, error)) {
                    return link;
                }
            }
            return exe;
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
        if (auto image = blb::environment("APPIMAGE"); image && !image->empty()) {
            return blb::path_from_utf8(*image);
        }
        std::error_code error;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::filesystem::path() : path;
    }

    std::filesystem::path exeDirectory() {
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

    // SDL asks the desktop portal first and runs xdg-open only when there is no portal.
    bool open(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return false;
        }
        std::filesystem::path target = std::filesystem::absolute(path, error);
        return !error && SDL_OpenURL(target.c_str());
    }

    bool openUrl(const std::string& url) {
        return webUrl(url) && SDL_OpenURL(url.c_str());
    }

    void reveal(const std::filesystem::path& path) {
        open(path.parent_path());
    }

    std::string createShortcut(const Shortcut& shortcut) {
        std::filesystem::path file = shortcut.file;
        if (file.extension() != ".desktop") {
            file += ".desktop";
        }
        std::filesystem::path exe = stableExecutable(executablePath());
        std::filesystem::path icon = shortcut.icon;
        if (icon.empty()) {
            icon = configDirectory() / "b-launcher.svg";
            if (!blb::fs::exists(icon)) {
                auto made = blb::fs::create_directories(icon.parent_path());
                auto written = made ? blb::fs::write_file_atomic(icon,
                                                                 std::string_view((const char*) EmbeddedIcons::logoSvg,
                                                                                  EmbeddedIcons::logoSvgSize))
                                    : made;
                if (!written) {
                    return "无法保存快捷方式图标";
                }
            }
        }
        auto quoted = [](const std::string& text) {
            std::string out = "\"";
            for (char c : text) {
                if (c == '%') {
                    out += "%%";
                    continue;
                }
                if (c == '"' || c == '`' || c == '$' || c == '\\') {
                    out += '\\';
                }
                out += c;
            }
            return out + "\"";
        };
        std::string executable = blb::path_to_utf8(exe);
        std::string command = quoted(executable);
        // GIO checks the first word before expanding %% in executable paths.
        if (executable.find('%') != std::string::npos) {
            command = "/usr/bin/env -- " + command;
        }
        for (const std::string& argument : blb::split_arguments(shortcut.arguments)) {
            command += " " + quoted(argument);
        }
        std::string name = file.stem().string();
        std::string text = "[Desktop Entry]\nType=Application\nName=" + desktopValue(name) +
                "\nComment=" + desktopValue(shortcut.description) + "\nExec=" + desktopValue(command) +
                "\nIcon=" + desktopValue(blb::path_to_utf8(icon)) + "\nTerminal=false\nCategories=Game;\n";
        auto written = blb::fs::write_file_atomic(file, text);
        if (!written) {
            return "无法创建快捷方式：" + written.err().message;
        }
        std::error_code error;
        std::filesystem::permissions(file,
                                     std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add, error);
        return {};
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
