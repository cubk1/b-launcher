#include "app/system/Platform.h"
#include "app/system/PlatformPosix.h"

#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>

#include <cstdint>
#include <cstring>

namespace Platform {

    namespace {
        namespace blb = b_launcher_base;
    }

    std::vector<std::string> commandLine() {
        std::vector<std::string> args;
        int count = *_NSGetArgc();
        char** values = *_NSGetArgv();
        for (int i = 0; i < count; i++) {
            args.push_back(values[i]);
        }
        return args;
    }

    std::filesystem::path executablePath() {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
        buffer.resize(std::strlen(buffer.c_str()));
        std::error_code error;
        std::filesystem::path path = std::filesystem::canonical(buffer, error);
        return error ? std::filesystem::path(buffer) : path;
    }

    std::filesystem::path exeDirectory() {
        std::filesystem::path exe = executablePath();
        if (exe.empty()) {
            return std::filesystem::current_path();
        }
        std::filesystem::path folder = exe.parent_path();
        std::filesystem::path bundle = folder.parent_path().parent_path();
        if (folder.filename() == "MacOS" && folder.parent_path().filename() == "Contents" &&
            bundle.extension() == ".app") {
            return bundle.parent_path();
        }
        return folder;
    }

    std::filesystem::path configDirectory() {
        std::filesystem::path home = blb::home_directory();
        return home.empty() ? std::filesystem::path() : home / "Library" / "Application Support" / "b-launcher";
    }

    std::filesystem::path desktopDirectory() {
        return blb::home_directory() / "Desktop";
    }

    bool open(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return false;
        }
        return spawnDetached({"/usr/bin/open", path.string()});
    }

    bool openUrl(const std::string& url) {
        return webUrl(url) && spawnDetached({"/usr/bin/open", url});
    }

    void reveal(const std::filesystem::path& path) {
        spawnDetached({"/usr/bin/open", "-R", path.string()});
    }

    Memory memory() {
        Memory out;
        uint64_t total = blb::current_platform().total_memory;
        if (total == 0) {
            out.totalMb = 8192;
            out.availableMb = 4096;
            return out;
        }
        out.totalMb = (uint32_t) (total / (1024ull * 1024ull));
        vm_statistics64_data_t stats = {};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        vm_size_t page = 0;
        if (host_page_size(mach_host_self(), &page) == KERN_SUCCESS &&
            host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t) &stats, &count) == KERN_SUCCESS) {
            uint64_t pages = (uint64_t) stats.free_count + stats.inactive_count + stats.purgeable_count;
            out.availableMb = (uint32_t) (pages * page / (1024ull * 1024ull));
        } else {
            out.availableMb = out.totalMb / 2;
        }
        return out;
    }
}
