#pragma once

#include "app/system/Pixels.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Screenshots {

    struct Entry {
        std::filesystem::path path;
        std::string filename;
        std::uint64_t size = 0;
        std::int64_t modified = 0;
    };

    struct List {
        bool loading = false;
        std::vector<Entry> entries;
        int serial = 0;
    };

    const List& of(const std::string& instanceKey);
    void scan(const std::string& instanceKey);

    std::shared_ptr<const Pixels> thumbnail(const std::filesystem::path& path, std::int64_t modified);
    std::shared_ptr<const Pixels> fullImage(const std::filesystem::path& path, std::int64_t modified);
    bool fullImageFailed(const std::filesystem::path& path, std::int64_t modified);
    void forgetFullImage();
    void copy(const std::filesystem::path& path, std::function<void()> onCopied = nullptr);

    void remove(const std::string& instanceKey, const std::filesystem::path& path);
    bool rename(const std::string& instanceKey, const std::filesystem::path& path, const std::string& newName);

    void poll();
    void shutdown();
}
