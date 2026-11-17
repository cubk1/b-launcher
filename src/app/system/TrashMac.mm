#include "app/system/Platform.h"

#include <Foundation/Foundation.h>

namespace Platform {

    bool recycle(const std::filesystem::path& path) {
        if (path.empty()) {
            return false;
        }
        std::error_code error;
        std::filesystem::path target = std::filesystem::absolute(path, error);
        if (error) {
            return false;
        }
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithFileSystemRepresentation:target.c_str()
                                                       isDirectory:NO
                                                     relativeToURL:nil];
            return url != nil && [[NSFileManager defaultManager] trashItemAtURL:url
                                                               resultingItemURL:nullptr
                                                                          error:nullptr];
        }
    }

    RecycleResult recycleDetailed(const std::filesystem::path& path) {
        RecycleResult result;
        std::error_code error;
        if (path.empty() || !std::filesystem::exists(std::filesystem::symlink_status(path, error))) {
            result.reason = "文件已不存在";
            return result;
        }
        if (recycle(path)) {
            result.ok = true;
            return result;
        }
        result.refused = true;
        result.reason = "无法移到系统回收站，可能是没有操作权限，或文件所在的磁盘不支持回收站";
        return result;
    }

    RecycleSpace recycleSpace(const std::filesystem::path& path) {
        (void) path;
        return RecycleSpace{};
    }
}
