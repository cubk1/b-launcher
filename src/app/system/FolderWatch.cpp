#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "app/system/FolderWatch.h"

#include <windows.h>

#include <memory>

namespace {
    constexpr DWORD FILTER = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
    // 64 KB, the most a folder on a network share takes.
    constexpr size_t BUFFER_WORDS = 16 * 1024;

    struct Watch {
        std::filesystem::path folder;
        HANDLE handle = INVALID_HANDLE_VALUE;
        OVERLAPPED overlapped = {};
        // DWORDs, as the notifications are aligned to them.
        std::vector<DWORD> buffer = std::vector<DWORD>(BUFFER_WORDS);

        ~Watch() { close(); }

        bool read() {
            return ReadDirectoryChangesW(handle, buffer.data(), (DWORD) (buffer.size() * sizeof(DWORD)), TRUE, FILTER,
                                         nullptr, &overlapped, nullptr) != FALSE;
        }

        void close() {
            if (handle != INVALID_HANDLE_VALUE) {
                CancelIoEx(handle, &overlapped);
                DWORD bytes = 0;
                GetOverlappedResult(handle, &overlapped, &bytes, TRUE);
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
            }
            if (overlapped.hEvent != nullptr) {
                CloseHandle(overlapped.hEvent);
                overlapped.hEvent = nullptr;
            }
        }
    };

    void run(const std::vector<std::filesystem::path>& folders, const FolderWatch::Changed& changed, HANDLE port,
             std::atomic<bool>& restartRequired) {
        // Both the reads and queued completion keys point into these records.
        // Keep them alive until the receive loop ends, including failed watches.
        std::vector<std::unique_ptr<Watch>> watches;
        for (const std::filesystem::path& folder : folders) {
            auto watch = std::make_unique<Watch>();
            watch->folder = folder;
            watch->handle = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            watch->overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (watch->handle == INVALID_HANDLE_VALUE || watch->overlapped.hEvent == nullptr ||
                CreateIoCompletionPort(watch->handle, port, reinterpret_cast<ULONG_PTR>(watch.get()), 0) == nullptr) {
                restartRequired = true;
                continue;
            }
            watches.push_back(std::move(watch));
            if (!watches.back()->read()) {
                restartRequired = true;
                watches.back()->close();
            }
        }
        for (;;) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* completed = nullptr;
            bool ok = GetQueuedCompletionStatus(port, &bytes, &key, &completed, INFINITE) != FALSE;
            // stop() posts a packet without an OVERLAPPED. A port failure also
            // returns no request, whereas a failed directory read has one.
            if (completed == nullptr) {
                break;
            }
            Watch& watch = *reinterpret_cast<Watch*>(key);
            if (watch.handle == INVALID_HANDLE_VALUE) {
                continue;
            }
            if (!ok || bytes == 0) {
                // Gone, or more changes than the buffer holds.
                changed(watch.folder, std::wstring(), true);
            } else {
                const BYTE* at = (const BYTE*) watch.buffer.data();
                for (;;) {
                    const auto* info = (const FILE_NOTIFY_INFORMATION*) at;
                    changed(watch.folder, std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR)),
                            info->Action != FILE_ACTION_MODIFIED);
                    if (info->NextEntryOffset == 0) {
                        break;
                    }
                    at += info->NextEntryOffset;
                }
            }
            if (!ok || !watch.read()) {
                restartRequired = true;
                watch.close();
            }
        }
        // Watch destructors cancel and wait for outstanding I/O before freeing
        // buffers. Remaining completion packets are discarded when the owner
        // closes the port, after joining this thread.
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
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (port == nullptr) {
        return;
    }
    completionPort = port;
    try {
        watched = folders;
        thread = std::thread(
                [this, folders = std::move(folders), changed = std::move(changed), port] {
                    run(folders, changed, port, restartRequired);
                });
    } catch (...) {
        CloseHandle(port);
        completionPort = nullptr;
        watched.clear();
        throw;
    }
}

void FolderWatch::stop() {
    if (thread.joinable()) {
        if (!PostQueuedCompletionStatus((HANDLE) completionPort, 0, 0, nullptr)) {
            // Closing the port wakes GetQueuedCompletionStatus on Windows 10+.
            CloseHandle((HANDLE) completionPort);
            completionPort = nullptr;
        }
        thread.join();
    }
    if (completionPort != nullptr) {
        CloseHandle((HANDLE) completionPort);
        completionPort = nullptr;
    }
    watched.clear();
}
