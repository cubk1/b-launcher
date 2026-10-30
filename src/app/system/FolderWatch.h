#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Watches folders and everything in them for files and folders being added, removed, renamed or written to, on a
// thread of its own.
class FolderWatch {
public:
    // A change in folder: the path of what changed, relative to the folder, and whether it was added, removed or
    // renamed rather than written to. An empty path says that too much changed at once to tell what.
    using Changed = std::function<void(const std::filesystem::path& folder, const std::wstring& path, bool moved)>;

    FolderWatch() = default;
    FolderWatch(const FolderWatch&) = delete;
    FolderWatch& operator=(const FolderWatch&) = delete;
    ~FolderWatch();

    // Watches these folders instead, calling changed on the watcher's thread; folders that cannot be opened are left
    // out.
    void start(std::vector<std::filesystem::path> folders, Changed changed);
    void stop();

    const std::vector<std::filesystem::path>& folders() const {
        return watched;
    }

private:
    std::thread thread;
    void* stopping = nullptr;
    std::vector<std::filesystem::path> watched;
};
