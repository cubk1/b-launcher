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
}
