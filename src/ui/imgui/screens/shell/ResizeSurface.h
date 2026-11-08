#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

// Owns the temporary GPU/GDI resources used during a resize. The host's DC and
// WGL context are borrowed and must outlive this object.
class ResizeSurface {
public:
    ResizeSurface(HDC deviceContext, HGLRC glContext);
    ~ResizeSurface();
    ResizeSurface(const ResizeSurface&) = delete;
    ResizeSurface& operator=(const ResizeSurface&) = delete;

    void prepare(int clientWidth, int clientHeight, const std::function<void()>& drawFrame, bool refresh);
    bool preparing() const;
    bool captureFrame();
    bool hasLayer() const;
    // Returns true only when a new image/position was submitted. With alpha the frame is
    // composed as premultiplied, for transparent corners.
    bool updateLayer(HWND owner, const RECT& position, bool alpha);
    bool present(int clientWidth, int clientHeight, bool keepLayer);

private:
    HDC deviceContext;
    HGLRC glContext;

    unsigned int framebuffer = 0;
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    bool drawing = false;
    bool ready = false;

    HWND layer = nullptr;
    HDC layerDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ stockBitmap = nullptr;
    void* pixels = nullptr;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    RECT layerRect = {};
    bool layerCurrent = false;
};
