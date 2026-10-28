#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class BorderlessWindow;

namespace LogWindow {

    void init(BorderlessWindow* main);
    void open();
    void close();
    bool isOpen();
    void frame(float scale);
    void shutdown();
}
