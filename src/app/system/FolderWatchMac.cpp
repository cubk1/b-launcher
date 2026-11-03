#include "app/system/FolderWatch.h"
#include "app/system/Platform.h"

#include <core/base/strings.hpp>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <memory>

namespace {
    struct Stream {
        std::vector<std::filesystem::path> folders;
        FolderWatch::Changed changed;
        std::atomic<bool>* restartRequired = nullptr;
        FSEventStreamRef stream = nullptr;
        dispatch_queue_t queue = nullptr;
    };

    void report(Stream& self, const std::string& path, FSEventStreamEventFlags flags) {
        std::filesystem::path changed = b_launcher_base::path_from_utf8(path).lexically_normal();
        for (const std::filesystem::path& folder : self.folders) {
            std::filesystem::path relative = changed.lexically_relative(folder);
            std::string text = relative.string();
            if (relative.empty() || text.starts_with("..")) {
                continue;
            }
            if (flags & (kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagRootChanged)) {
                self.changed(folder, std::wstring(), true);
            } else if (text != ".") {
                bool moved = (flags &
                              (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved |
                               kFSEventStreamEventFlagItemRenamed)) != 0;
                self.changed(folder, Platform::widen(text), moved);
            }
            if (flags & kFSEventStreamEventFlagRootChanged) {
                self.restartRequired->store(true);
            }
        }
    }

    void callback(ConstFSEventStreamRef, void* info, size_t count, void* paths, const FSEventStreamEventFlags flags[],
                  const FSEventStreamEventId[]) {
        Stream& self = *(Stream*) info;
        char** names = (char**) paths;
        for (size_t i = 0; i < count; i++) {
            report(self, names[i], flags[i]);
        }
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
    auto self = std::make_unique<Stream>();
    self->changed = std::move(changed);
    self->restartRequired = &restartRequired;
    CFMutableArrayRef paths = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    for (const std::filesystem::path& folder : folders) {
        std::error_code error;
        std::filesystem::path real = std::filesystem::canonical(folder, error);
        if (error) {
            restartRequired = true;
            continue;
        }
        self->folders.push_back(real);
        CFStringRef text = CFStringCreateWithCString(nullptr, real.c_str(), kCFStringEncodingUTF8);
        if (text != nullptr) {
            CFArrayAppendValue(paths, text);
            CFRelease(text);
        }
    }
    if (CFArrayGetCount(paths) == 0) {
        CFRelease(paths);
        return;
    }
    FSEventStreamContext context = {0, self.get(), nullptr, nullptr, nullptr};
    self->stream = FSEventStreamCreate(nullptr, callback, &context, paths, kFSEventStreamEventIdSinceNow, 0.05,
                                       kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer |
                                               kFSEventStreamCreateFlagWatchRoot);
    CFRelease(paths);
    if (self->stream == nullptr) {
        restartRequired = true;
        return;
    }
    self->queue = dispatch_queue_create("b-launcher.folders", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(self->stream, self->queue);
    if (!FSEventStreamStart(self->stream)) {
        FSEventStreamInvalidate(self->stream);
        FSEventStreamRelease(self->stream);
        dispatch_release(self->queue);
        restartRequired = true;
        return;
    }
    watched = std::move(folders);
    completionPort = self.release();
}

void FolderWatch::stop() {
    if (completionPort != nullptr) {
        std::unique_ptr<Stream> self((Stream*) completionPort);
        completionPort = nullptr;
        FSEventStreamStop(self->stream);
        FSEventStreamInvalidate(self->stream);
        FSEventStreamRelease(self->stream);
        dispatch_sync_f(self->queue, nullptr, [](void*) {});
        dispatch_release(self->queue);
    }
    watched.clear();
}
