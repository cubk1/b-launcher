#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

class BorderlessWindow {
public:
    HWND hwnd = nullptr;
    HDC deviceContext = nullptr;
    HGLRC glContext = nullptr;
    int width = 0;
    int height = 0;
    std::function<bool(int x, int y)> isDragArea;
    bool resizable = false;
    bool quitOnDestroy = true;
    std::function<void()> onClose;
    std::function<void()> onModalFrame;
    void* imguiContext = nullptr;

    bool create(const std::wstring& title, int clientWidth, int clientHeight, HGLRC share = nullptr);
    void resize(int clientWidth, int clientHeight);
    void setMinimumSize(int clientWidth, int clientHeight);
    void destroy();
    void show(bool maximized = false);
    void minimize();
    void toggleMaximize();
    bool maximized() const;
    bool sizing() const;
    void close();
    void setDarkMode(bool dark);
    bool pumpMessages();
    bool minimized() const;
    void swap();

private:
    static LRESULT WINAPI windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    void applyFrame();
    bool createGlContext(HGLRC share);

    int minWidth = 0;
    int minHeight = 0;
    bool inSizeMove = false;
    bool inModalFrame = false;

    void modalFrame();

    bool regionCorners = false;
};
