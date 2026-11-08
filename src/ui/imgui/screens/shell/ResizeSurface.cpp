#include "ui/imgui/screens/shell/ResizeSurface.h"
#include "ui/imgui/gl/UiGL.h"

#include <dwmapi.h>
#include <GL/gl.h>

#include <algorithm>

namespace {
    struct CurrentGl {
        HDC previousDc = wglGetCurrentDC();
        HGLRC previous = wglGetCurrentContext();
        bool changed;
        bool valid;

        CurrentGl(HDC dc, HGLRC context)
            : changed(previousDc != dc || previous != context),
              valid(context != nullptr && (!changed || wglMakeCurrent(dc, context))) {}

        ~CurrentGl() {
            if (changed) {
                wglMakeCurrent(previousDc, previous);
            }
        }
    };
}

ResizeSurface::ResizeSurface(HDC deviceContext, HGLRC glContext)
    : deviceContext(deviceContext), glContext(glContext) {}

ResizeSurface::~ResizeSurface() {
    if (layer) {
        DestroyWindow(layer);
    }
    if (layerDc) {
        if (stockBitmap) {
            SelectObject(layerDc, stockBitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        DeleteDC(layerDc);
    }
    if (framebuffer || texture) {
        CurrentGl context(deviceContext, glContext);
        if (context.valid) {
            if (framebuffer) {
                UiGL::DeleteFramebuffers(1, &framebuffer);
            }
            if (texture) {
                glDeleteTextures(1, &texture);
            }
        }
    }
}

bool ResizeSurface::preparing() const {
    return drawing;
}

bool ResizeSurface::captureFrame() {
    if (!drawing) {
        return false;
    }
    ready = true;
    return true;
}

bool ResizeSurface::hasLayer() const {
    return layer != nullptr;
}

void ResizeSurface::prepare(int clientWidth, int clientHeight, const std::function<void()>& drawFrame, bool refresh) {
    if (drawing || !drawFrame || clientWidth <= 0 || clientHeight <= 0 || !UiGL::loaded() ||
        !UiGL::BlitFramebuffer || !UiGL::CheckFramebufferStatus) {
        return;
    }
    if (!refresh && ready && width == clientWidth && height == clientHeight) {
        return;
    }
    CurrentGl context(deviceContext, glContext);
    if (!context.valid) {
        return;
    }
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (clientWidth > limit || clientHeight > limit) {
        return;
    }
    GLint read = 0, draw = 0, previousTexture = 0, viewport[4];
    glGetIntegerv(UGL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(UGL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (framebuffer == 0) {
        UiGL::GenFramebuffers(1, &framebuffer);
    }
    if (texture == 0) {
        glGenTextures(1, &texture);
    }
    if (framebuffer == 0 || texture == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    if (clientWidth > textureWidth || clientHeight > textureHeight) {
        int allocatedWidth = std::min(limit, (std::max(clientWidth, textureWidth) + 63) / 64 * 64);
        int allocatedHeight = std::min(limit, (std::max(clientHeight, textureHeight) + 63) / 64 * 64);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocatedWidth, allocatedHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // A failed allocation may leave the old, smaller texture attached and
        // the FBO still complete. Check the storage size before rendering to it.
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &textureWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &textureHeight);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, previousTexture);
    UiGL::BindFramebuffer(UGL_FRAMEBUFFER, framebuffer);
    UiGL::FramebufferTexture2D(UGL_FRAMEBUFFER, UGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    bool complete = textureWidth >= clientWidth && textureHeight >= clientHeight &&
            UiGL::CheckFramebufferStatus(UGL_FRAMEBUFFER) == UGL_FRAMEBUFFER_COMPLETE;
    auto restore = [&] {
        drawing = false;
        UiGL::BindFramebuffer(UGL_READ_FRAMEBUFFER, read);
        UiGL::BindFramebuffer(UGL_DRAW_FRAMEBUFFER, draw);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    };
    ready = false;
    layerCurrent = false;
    if (complete) {
        width = clientWidth;
        height = clientHeight;
        drawing = true;
        try {
            drawFrame();
        } catch (...) {
            restore();
            throw;
        }
    }
    restore();
}

bool ResizeSurface::updateLayer(HWND owner, const RECT& position, bool alpha) {
    if (drawing || !ready || !IsWindowVisible(owner) ||
        width != position.right - position.left || height != position.bottom - position.top) {
        return false;
    }
    if (layerCurrent && EqualRect(&layerRect, &position)) {
        return false;
    }
    CurrentGl context(deviceContext, glContext);
    if (!context.valid) {
        return false;
    }
    if (!layerDc) {
        layerDc = CreateCompatibleDC(nullptr);
        if (!layerDc) {
            return false;
        }
    }
    if (bitmapWidth < width || bitmapHeight < height) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = textureWidth;
        info.bmiHeader.biHeight = textureHeight; // Bottom-up, like glReadPixels.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* nextPixels = nullptr;
        HBITMAP nextBitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &nextPixels, nullptr, 0);
        if (!nextBitmap) {
            return false;
        }
        HGDIOBJ previous = SelectObject(layerDc, nextBitmap);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(nextBitmap);
            return false;
        }
        if (bitmap) {
            DeleteObject(bitmap);
        } else {
            stockBitmap = previous;
        }
        bitmap = nextBitmap;
        pixels = nextPixels;
        bitmapWidth = textureWidth;
        bitmapHeight = textureHeight;
    }

    // WGL and DWM can briefly disagree about a resized drawable's origin. A
    // layered window commits its image, position and size together, so the
    // opposite edge stays fixed. Read back only during interactive resizing.
    constexpr GLenum pixelPackBuffer = 0x88EB;
    constexpr GLenum pixelPackBufferBinding = 0x88ED;
    constexpr GLenum bgra = 0x80E1;
    GLint read = 0, buffer = 0, alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
    glGetIntegerv(UGL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(pixelPackBufferBinding, &buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    UiGL::BindFramebuffer(UGL_READ_FRAMEBUFFER, framebuffer);
    UiGL::BindBuffer(pixelPackBuffer, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, bitmapWidth);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0, 0, width, height, bgra, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    UiGL::BindBuffer(pixelPackBuffer, buffer);
    UiGL::BindFramebuffer(UGL_READ_FRAMEBUFFER, read);

    if (!layer) {
        // This window must have no CS_OWNDC, unlike the OpenGL host. It is owned
        // by the host and passes mouse input through without taking focus.
        layer = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!layer) {
            return false;
        }
    }
    POINT destination = {position.left, position.top};
    POINT source = {0, bitmapHeight - height};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(layer, nullptr, &destination, &size, layerDc, &source,
            0, alpha ? &blend : nullptr, alpha ? ULW_ALPHA : ULW_OPAQUE)) {
        ShowWindow(layer, SW_HIDE);
        layerCurrent = false;
        return false;
    }
    ShowWindow(layer, SW_SHOWNOACTIVATE);
    layerRect = position;
    layerCurrent = true;
    return true;
}

bool ResizeSurface::present(int clientWidth, int clientHeight, bool keepLayer) {
    if (!ready || width != clientWidth || height != clientHeight) {
        if (layer) {
            ShowWindow(layer, SW_HIDE);
        }
        layerCurrent = false;
        return false;
    }
    if (keepLayer && layerCurrent) {
        return true;
    }
    CurrentGl context(deviceContext, glContext);
    if (!context.valid) {
        return false;
    }
    bool hadLayer = layer && IsWindowVisible(layer);
    if (hadLayer) {
        ShowWindow(layer, SW_HIDE);
    }
    GLint read = 0, draw = 0;
    glGetIntegerv(UGL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(UGL_DRAW_FRAMEBUFFER_BINDING, &draw);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    UiGL::BindFramebuffer(UGL_READ_FRAMEBUFFER, framebuffer);
    UiGL::BindFramebuffer(UGL_DRAW_FRAMEBUFFER, 0);
    UiGL::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    UiGL::BindFramebuffer(UGL_READ_FRAMEBUFFER, read);
    UiGL::BindFramebuffer(UGL_DRAW_FRAMEBUFFER, draw);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
    // Restore the OpenGL host in the same turn that removes the temporary
    // surface. Only this handoff needs a compositor wait, not every resize step.
    using SwapInterval = BOOL(WINAPI*)(int);
    using GetSwapInterval = int(WINAPI*)();
    auto setInterval = reinterpret_cast<SwapInterval>(UiGL::extensionProc("wglSwapIntervalEXT"));
    auto getInterval = reinterpret_cast<GetSwapInterval>(UiGL::extensionProc("wglGetSwapIntervalEXT"));
    int interval = hadLayer && getInterval ? getInterval() : 0;
    if (setInterval && interval != 0) {
        setInterval(0);
    }
    SwapBuffers(deviceContext);
    if (hadLayer) {
        glFinish();
        DwmFlush();
    }
    if (setInterval && interval != 0) {
        setInterval(interval);
    }
    ready = false;
    return true;
}
