#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ThemeStore {

    struct Entry {
        std::string id;
        std::string name;
        bool builtIn = false;
        bool packed = false;
        std::filesystem::path zip;
        std::string prefix;
    };

    struct Source {
        const unsigned char* data = nullptr;
        size_t size = 0;
        std::filesystem::path file;
        std::shared_ptr<const std::string> owned;
        std::filesystem::path zip;
        std::string entry;
    };

    void rescan();
    void rescanIfStale();
    const std::vector<Entry>& entries();
    const Entry* find(const std::string& id);
    std::optional<std::string> read(const std::string& id, const std::string& path);
    std::optional<Source> locate(const std::string& id, const std::string& path);
    std::optional<std::string> contents(const Source& source);
    std::filesystem::path folder();
    std::filesystem::path location(const std::string& id);
    bool validId(std::string_view id);
    bool builtIn(const std::string& id);
    std::optional<std::string> duplicate(const std::string& id);
    std::vector<std::pair<std::string, std::string>> files(const std::string& id);
    bool remove(const std::string& id);
}
