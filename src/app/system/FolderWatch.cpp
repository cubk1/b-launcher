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

    void run(const std::vector<std::filesystem::path>& folders, const FolderWatch::Changed& changed, HANDLE stopping) {
        // The reads in flight point into the watches, so they never move.
        std::vector<std::unique_ptr<Watch>> watches;
        for (const std::filesystem::path& folder : folders) {
            auto watch = std::make_unique<Watch>();
            watch->folder = folder;
            watch->handle = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            watch->overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (watch->handle == INVALID_HANDLE_VALUE || watch->overlapped.hEvent == nullptr || !watch->read()) {
                watch->close();
                continue;
            }
            watches.push_back(std::move(watch));
        }
        for (;;) {
            std::vector<HANDLE> events = {stopping};
            for (const auto& watch : watches) {
                events.push_back(watch->overlapped.hEvent);
            }
            DWORD woke = WaitForMultipleObjects((DWORD) events.size(), events.data(), FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0 || woke == WAIT_FAILED || woke >= WAIT_OBJECT_0 + events.size()) {
                break;
            }
            size_t index = woke - WAIT_OBJECT_0 - 1;
            Watch& watch = *watches[index];
            DWORD bytes = 0;
            bool ok = GetOverlappedResult(watch.handle, &watch.overlapped, &bytes, FALSE) != FALSE;
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
                watch.close();
                watches.erase(watches.begin() + (ptrdiff_t) index);
            }
        }
        for (const auto& watch : watches) {
            watch->close();
        }
    }
}

FolderWatch::~FolderWatch() {
    stop();
}

void FolderWatch::start(std::vector<std::filesystem::path> folders, Changed changed) {
    stop();
    watched = folders;
    if (folders.empty()) {
        return;
    }
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return;
    }
    stopping = event;
    thread = std::thread(
            [folders = std::move(folders), changed = std::move(changed), event] { run(folders, changed, event); });
}

void FolderWatch::stop() {
    if (thread.joinable()) {
        SetEvent((HANDLE) stopping);
        thread.join();
    }
    if (stopping != nullptr) {
        CloseHandle((HANDLE) stopping);
        stopping = nullptr;
    }
    watched.clear();
}
