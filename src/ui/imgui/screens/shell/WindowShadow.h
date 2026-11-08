#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

// A soft shadow for a borderless window that DWM leaves without one (Windows 10 with rounded
// corners). It is a click-through layered window owned by that window. Owned windows stay above
// their owner, so the shadow has an anti-aliased hole in the shape of the window, through which
// its transparent corners still show the shadow behind them.
class WindowShadow {
public:
    explicit WindowShadow(HWND owner);
    ~WindowShadow();
    WindowShadow(const WindowShadow&) = delete;
    WindowShadow& operator=(const WindowShadow&) = delete;

    // window is the owner's rectangle in screen coordinates, radius its corner radius in pixels.
    void place(const RECT& window, int radius, int dpi);
    void hide();
    void setActive(bool on);

private:
    static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool ensureLayer();
    bool reserve(int w, int h);
    bool paint(int w, int h, int radius, int dpi);
    BLENDFUNCTION blend() const;
    void fadeStep();

    HWND owner;
    HWND layer = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ stockBitmap = nullptr;
    uint32_t* pixels = nullptr;
    int capacityW = 0;
    int capacityH = 0;
    int paintedW = 0;
    int paintedH = 0;
    int paintedRadius = -1;
    int paintedDpi = 0;
    int margin = 0;
    RECT placed = {};
    bool visible = false;
    bool active = true;
    float fade = 1.0f;
    ULONGLONG shownAt = 0;
};
