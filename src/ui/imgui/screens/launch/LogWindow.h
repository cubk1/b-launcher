#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

class BorderlessWindow;

namespace LogWindow {

    void init(BorderlessWindow* main);
    void setIcons(void* large, void* compact);
    void setTitle(const std::wstring& title);
    void open();
    void close();
    bool isOpen();
    void frame(float scale);
    void shutdown();
}
