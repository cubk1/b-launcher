#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <memory>
#include <string>

class ResizeSurface;

class BorderlessWindow {
public:
    BorderlessWindow();
    ~BorderlessWindow();
    BorderlessWindow(const BorderlessWindow&) = delete;
    BorderlessWindow& operator=(const BorderlessWindow&) = delete;

    HWND hwnd = nullptr;
    HDC deviceContext = nullptr;
    HGLRC glContext = nullptr;
    int width = 0;
    int height = 0;
    int textAreaX = -1;
    int textAreaY = -1;
    int textAreaH = -1;
    bool imeOn = true;
    void allowIme(bool on);
    bool composition(LPARAM lParam);
    std::function<bool(int x, int y)> isDragArea;
    bool resizable = false;
    bool framed = false;
    bool quitOnDestroy = true;
    std::function<void()> onClose;
    // During preparingResize(), width/height are the proposed client size and
    // swap() captures the frame without presenting or changing the native window.
    std::function<void()> onModalFrame;
    std::function<bool(unsigned long long kind, const std::wstring& text)> onCopyData;
    std::function<bool(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)> onMessage;
    void* imguiContext = nullptr;

    bool create(const std::wstring& title, int clientWidth, int clientHeight, HGLRC share = nullptr);
    void resize(int clientWidth, int clientHeight);
    void place(int x, int y, int clientWidth, int clientHeight);
    void setFramed(bool on);
    void cloak(bool on);
    void setMinimumSize(int clientWidth, int clientHeight);
    void destroy();
    void show(bool maximized = false);
    void minimize();
    void setTextArea(float x, float y, float h, bool ime = true);
    void clearTextArea();
    void toggleMaximize();
    bool maximized() const;
    bool sizing() const;
    bool preparingResize() const;
    void close();
    void setDarkMode(bool dark);
    void setIcons(HICON large, HICON compact);
    void setTitle(const std::wstring& title);
    bool pumpMessages();
    bool minimized() const;
    SIZE frameSize() const;
    void swap();
    void setVsync(bool on);

private:
    static LRESULT WINAPI windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    DWORD style() const;
    void applyFrame();
    void applyRegion();
    bool createGlContext(HGLRC share);

    int minWidth = 0;
    int minHeight = 0;
    HICON bigIcon = nullptr;
    HICON smallIcon = nullptr;
    bool inSizeMove = false;
    bool inModalFrame = false;
    ULONGLONG lastResizeFrame = 0;

    void modalFrame();
    void prepareResize(int clientWidth, int clientHeight, bool refresh = false);
    void showResizeLayer(const RECT& position);
    void releaseResize();

    std::unique_ptr<ResizeSurface> resizeSurface;

    bool regionCorners = false;
    bool regionSuspended = false;
};
