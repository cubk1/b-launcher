#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>

#if defined(_WIN32)
class BorderlessWindow;
using ShellWindow = BorderlessWindow;
#else
class SdlWindow;
using ShellWindow = SdlWindow;
#endif

namespace LogWindow {

    void init(ShellWindow* main);
    void setIcons(void* large, void* compact);
    void setTitle(const std::wstring& title);
    void open();
    void close();
    bool isOpen();
    void frame(float scale);
    void shutdown();
}
