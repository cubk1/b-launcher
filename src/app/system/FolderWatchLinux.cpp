#include "app/system/FolderWatch.h"
#include "app/system/Platform.h"

#include <core/base/strings.hpp>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <unordered_map>

namespace {
    constexpr uint32_t MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE |
            IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

    struct Watched {
        size_t root = 0;
        std::filesystem::path relative;
    };

    struct Tree {
        int fd = -1;
        const std::vector<std::filesystem::path>& folders;
        std::unordered_map<int, Watched> watches;
        std::atomic<bool>& restartRequired;

        void add(size_t root, const std::filesystem::path& relative) {
            std::filesystem::path full = relative.empty() ? folders[root] : folders[root] / relative;
            int wd = inotify_add_watch(fd, full.c_str(), MASK);
            if (wd < 0) {
                if (relative.empty() || errno == ENOSPC) {
                    restartRequired = true;
                }
                return;
            }
            watches[wd] = Watched{root, relative};
            std::error_code error;
            for (std::filesystem::directory_iterator it(full, error), end; !error && it != end; it.increment(error)) {
                std::error_code kind;
                if (it->is_directory(kind) && !it->is_symlink(kind)) {
                    add(root, relative / it->path().filename());
                }
            }
        }
    };

    std::wstring wide(const std::filesystem::path& path) {
        return Platform::widen(b_launcher_base::path_to_utf8(path));
    }

    void run(const std::vector<std::filesystem::path>& folders, const FolderWatch::Changed& changed, int wake,
             std::atomic<bool>& restartRequired) {
        Tree tree{inotify_init1(IN_CLOEXEC | IN_NONBLOCK), folders, {}, restartRequired};
        if (tree.fd < 0) {
            restartRequired = true;
            return;
        }
        for (size_t i = 0; i < folders.size(); i++) {
            tree.add(i, {});
        }
        alignas(inotify_event) char buffer[64 * 1024];
        for (;;) {
            pollfd waits[2] = {{tree.fd, POLLIN, 0}, {wake, POLLIN, 0}};
            if (poll(waits, 2, -1) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                restartRequired = true;
                break;
            }
            if (waits[1].revents != 0) {
                break;
            }
            ssize_t got = read(tree.fd, buffer, sizeof(buffer));
            if (got <= 0) {
                continue;
            }
            for (char* at = buffer; at < buffer + got;) {
                const auto* event = (const inotify_event*) at;
                at += sizeof(inotify_event) + event->len;
                if (event->mask & IN_Q_OVERFLOW) {
                    for (const std::filesystem::path& folder : folders) {
                        changed(folder, std::wstring(), true);
                    }
                    continue;
                }
                auto found = tree.watches.find(event->wd);
                if (found == tree.watches.end()) {
                    continue;
                }
                Watched watch = found->second;
                if (event->mask & IN_IGNORED) {
                    tree.watches.erase(found);
                    if (watch.relative.empty()) {
                        restartRequired = true;
                    }
                    continue;
                }
                if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                    if (watch.relative.empty()) {
                        changed(folders[watch.root], std::wstring(), true);
                    }
                    continue;
                }
                std::filesystem::path relative =
                        event->len > 0 ? watch.relative / std::filesystem::path(event->name) : watch.relative;
                bool moved = (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)) != 0;
                changed(folders[watch.root], wide(relative), moved);
                if ((event->mask & IN_ISDIR) && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
                    tree.add(watch.root, relative);
                    std::error_code error;
                    for (std::filesystem::recursive_directory_iterator it(folders[watch.root] / relative, error), end;
                         !error && it != end; it.increment(error)) {
                        changed(folders[watch.root],
                                wide(relative / it->path().lexically_relative(folders[watch.root] / relative)), true);
                    }
                }
            }
        }
        close(tree.fd);
    }
}

FolderWatch::~FolderWatch() {
    stop();
}

void FolderWatch::start(std::vector<std::filesystem::path> folders, Changed changed) {
    stop();
    restartRequired = false;
    if (folders.empty()) {
        return;
    }
    int wake = eventfd(0, EFD_CLOEXEC);
    if (wake < 0) {
        return;
    }
    completionPort = (void*) (intptr_t) (wake + 1);
    try {
        watched = folders;
        thread = std::thread([this, folders = std::move(folders), changed = std::move(changed), wake] {
            run(folders, changed, wake, restartRequired);
        });
    } catch (...) {
        close(wake);
        completionPort = nullptr;
        watched.clear();
        throw;
    }
}

void FolderWatch::stop() {
    int wake = completionPort != nullptr ? (int) (intptr_t) completionPort - 1 : -1;
    if (thread.joinable()) {
        uint64_t one = 1;
        ssize_t written = write(wake, &one, sizeof(one));
        (void) written;
        thread.join();
    }
    if (wake >= 0) {
        close(wake);
        completionPort = nullptr;
    }
    watched.clear();
}
