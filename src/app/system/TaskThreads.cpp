#include "app/system/TaskThreads.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

bool threadExited(std::thread& thread) {
    return WaitForSingleObject((HANDLE) thread.native_handle(), 0) == WAIT_OBJECT_0;
}
#else
bool threadExited(std::thread&) {
    return true;
}
#endif
