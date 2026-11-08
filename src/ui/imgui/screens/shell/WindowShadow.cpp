#include "ui/imgui/screens/shell/WindowShadow.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
    const wchar_t* const CLASS_NAME = L"b-launcher.shadow";
    constexpr UINT_PTR FADE_TIMER = 1;
    // Fade in once the owner's show or restore animation is about to finish.
    constexpr ULONGLONG FADE_DELAY_MS = 150;
    constexpr float FADE_MS = 120.0f;
    constexpr float INACTIVE_ALPHA = 140.0f;
    constexpr int SAMPLES = 16;

    // At 96 DPI: a wide ambient layer and a tight one along the edge, both cast slightly downwards.
    struct Layer {
        float sigma;
        float offsetY;
        float opacity;
    };
    constexpr Layer LAYERS[] = {{10.0f, 4.0f, 0.30f}, {2.5f, 1.0f, 0.20f}};

    float spanX(float x, float y, float sigma, float corner, float halfW, float halfH) {
        float delta = std::min(halfH - corner - std::fabs(y), 0.0f);
        float curved = halfW - corner + std::sqrt(std::max(0.0f, corner * corner - delta * delta));
        float k = 0.70710678f / sigma;
        return 0.5f * (std::erf((x + curved) * k) - std::erf((x - curved) * k));
    }

    // Gaussian blur of a rounded box centered at the origin: integrated exactly across x and
    // sampled along y (Evan Wallace, "Fast Rounded Rectangle Shadows").
    float blurredBox(float x, float y, float halfW, float halfH, float sigma, float corner) {
        float start = std::clamp(-3.0f * sigma, y - halfH, y + halfH);
        float end = std::clamp(3.0f * sigma, y - halfH, y + halfH);
        float step = (end - start) / SAMPLES;
        float value = 0.0f;
        for (int i = 0; i < SAMPLES; i++) {
            float t = start + step * ((float) i + 0.5f);
            value += spanX(x, y - t, sigma, corner, halfW, halfH) * std::exp(-0.5f * t * t / (sigma * sigma));
        }
        return value * step / (2.5066283f * sigma);
    }

    // Coverage of the window's shape, matching ShaderUtil::cutCorners.
    float coverage(float x, float y, float halfW, float halfH, float radius) {
        float r = std::min(radius, std::min(halfW, halfH));
        float qx = std::fabs(x) - halfW + r;
        float qy = std::fabs(y) - halfH + r;
        float dist = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
        return std::clamp(0.5f - dist, 0.0f, 1.0f);
    }

    // Alpha at pixel (px, py) relative to the top left of a w x h window.
    uint8_t shade(int px, int py, int w, int h, float radius, float scale) {
        float halfW = (float) w * 0.5f;
        float halfH = (float) h * 0.5f;
        float x = (float) px + 0.5f - halfW;
        float y = (float) py + 0.5f - halfH;
        float open = 1.0f - coverage(x, y, halfW, halfH, radius);
        if (open <= 0.0f) {
            return 0;
        }
        float clear = 1.0f;
        for (const Layer& layer : LAYERS) {
            clear *= 1.0f - layer.opacity * blurredBox(x, y - layer.offsetY * scale, halfW, halfH,
                                                       layer.sigma * scale, radius);
        }
        return (uint8_t) std::lround(std::clamp((1.0f - clear) * open, 0.0f, 1.0f) * 255.0f);
    }

    // Away from the corners the shadow only varies across the edge. A window just large enough to
    // separate the corners therefore holds every distinct row and column, and any larger size
    // repeats its middle row and column.
    struct Reference {
        int radius = -1;
        int dpi = 0;
        int margin = 0;
        int sliceX = 0;
        int sliceY = 0;
        int width = 0;
        int height = 0;
        std::vector<uint8_t> alpha;
    };

    const Reference& reference(int radius, int dpi) {
        static Reference ref;
        if (ref.radius == radius && ref.dpi == dpi) {
            return ref;
        }
        float scale = (float) dpi / 96.0f;
        float sigma = 0.0f;
        float offset = 0.0f;
        for (const Layer& layer : LAYERS) {
            sigma = std::max(sigma, layer.sigma);
            offset = std::max(offset, layer.offsetY);
        }
        int edgeX = radius + (int) std::ceil(3.0f * sigma * scale) + 2;
        int edgeY = radius + (int) std::ceil((3.0f * sigma + offset) * scale) + 2;
        int w = edgeX * 2 + 1;
        int h = edgeY * 2 + 1;
        ref.radius = radius;
        ref.dpi = dpi;
        ref.margin = (int) std::ceil((3.0f * sigma + offset) * scale) + 1;
        ref.sliceX = ref.margin + edgeX;
        ref.sliceY = ref.margin + edgeY;
        ref.width = w + ref.margin * 2;
        ref.height = h + ref.margin * 2;
        ref.alpha.assign((size_t) ref.width * ref.height, 0);
        for (int y = 0; y < ref.height; y++) {
            uint8_t* row = ref.alpha.data() + (size_t) y * ref.width;
            // Symmetric across the vertical axis.
            for (int x = 0; x <= ref.width / 2; x++) {
                row[x] = row[ref.width - 1 - x] = shade(x - ref.margin, y - ref.margin, w, h, (float) radius, scale);
            }
        }
        return ref;
    }

    bool animated() {
        ANIMATIONINFO info = {sizeof(info)};
        return SystemParametersInfoW(SPI_GETANIMATION, sizeof(info), &info, 0) && info.iMinAnimate != 0;
    }
}

WindowShadow::WindowShadow(HWND owner) : owner(owner), active(GetForegroundWindow() == owner) {}

WindowShadow::~WindowShadow() {
    if (layer != nullptr) {
        SetWindowLongPtrW(layer, GWLP_USERDATA, 0);
        DestroyWindow(layer);
    }
    if (dc != nullptr) {
        if (stockBitmap != nullptr) {
            SelectObject(dc, stockBitmap);
        }
        DeleteDC(dc);
    }
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
    }
}

LRESULT CALLBACK WindowShadow::procedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<WindowShadow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_WINDOWPOSCHANGING:
            // Windows shows owned windows again as soon as their owner is restored, before its
            // animation has finished. Only place() shows the shadow.
            if (self != nullptr && !self->visible) {
                reinterpret_cast<WINDOWPOS*>(lParam)->flags &= ~SWP_SHOWWINDOW;
            }
            break;
        case WM_TIMER:
            if (self != nullptr && wParam == FADE_TIMER) {
                self->fadeStep();
                return 0;
            }
            break;
        case WM_NCDESTROY:
            // Destroying the owner destroys this window too.
            if (self != nullptr) {
                self->layer = nullptr;
                self->visible = false;
            }
            break;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool WindowShadow::ensureLayer() {
    if (layer != nullptr) {
        return true;
    }
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance;
        wc.lpszClassName = CLASS_NAME;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered) {
            return false;
        }
    }
    layer = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, CLASS_NAME, L"",
                            WS_POPUP, 0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (layer == nullptr) {
        return false;
    }
    SetWindowLongPtrW(layer, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

bool WindowShadow::reserve(int w, int h) {
    if (bitmap != nullptr && w <= capacityW && h <= capacityH) {
        return true;
    }
    int nextW = (std::max(w, capacityW) + 63) / 64 * 64;
    int nextH = (std::max(h, capacityH) + 63) / 64 * 64;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = nextW;
    info.bmiHeader.biHeight = -nextH;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP next = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (next == nullptr) {
        return false;
    }
    if (dc == nullptr) {
        dc = CreateCompatibleDC(nullptr);
        if (dc == nullptr) {
            DeleteObject(next);
            return false;
        }
    }
    HGDIOBJ previous = SelectObject(dc, next);
    if (previous == nullptr || previous == HGDI_ERROR) {
        DeleteObject(next);
        return false;
    }
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
    } else {
        stockBitmap = previous;
    }
    bitmap = next;
    pixels = static_cast<uint32_t*>(bits);
    capacityW = nextW;
    capacityH = nextH;
    return true;
}

bool WindowShadow::paint(int w, int h, int radius, int dpi) {
    const Reference& ref = reference(radius, dpi);
    int outW = w + ref.margin * 2;
    int outH = h + ref.margin * 2;
    if (!reserve(outW, outH)) {
        return false;
    }
    GdiFlush();
    // Premultiplied black: only the alpha byte is set.
    bool sliced = outW >= ref.width && outH >= ref.height;
    float scale = (float) dpi / 96.0f;
    int shiftX = outW - ref.width;
    int shiftY = outH - ref.height;
    for (int y = 0; y < outH; y++) {
        uint32_t* row = pixels + (size_t) y * capacityW;
        if (!sliced) {
            for (int x = 0; x < outW; x++) {
                row[x] = (uint32_t) shade(x - ref.margin, y - ref.margin, w, h, (float) radius, scale) << 24;
            }
            continue;
        }
        int source = y < ref.sliceY ? y : y >= outH - ref.sliceY ? y - shiftY : ref.sliceY;
        const uint8_t* alpha = ref.alpha.data() + (size_t) source * ref.width;
        for (int x = 0; x < ref.sliceX; x++) {
            row[x] = (uint32_t) alpha[x] << 24;
        }
        std::fill(row + ref.sliceX, row + outW - ref.sliceX, (uint32_t) alpha[ref.sliceX] << 24);
        for (int x = outW - ref.sliceX; x < outW; x++) {
            row[x] = (uint32_t) alpha[x - shiftX] << 24;
        }
    }
    margin = ref.margin;
    paintedW = w;
    paintedH = h;
    paintedRadius = radius;
    paintedDpi = dpi;
    return true;
}

BLENDFUNCTION WindowShadow::blend() const {
    float strength = active ? 255.0f : INACTIVE_ALPHA;
    return {AC_SRC_OVER, 0, (BYTE) std::lround(strength * fade), AC_SRC_ALPHA};
}

void WindowShadow::place(const RECT& window, int radius, int dpi) {
    int w = window.right - window.left;
    int h = window.bottom - window.top;
    if (w <= 0 || h <= 0 || !ensureLayer()) {
        hide();
        return;
    }
    bool repaint = w != paintedW || h != paintedH || radius != paintedRadius || dpi != paintedDpi;
    if (repaint && !paint(w, h, radius, dpi)) {
        hide();
        return;
    }
    POINT position = {window.left - margin, window.top - margin};
    if (!visible || repaint) {
        if (!visible) {
            fade = animated() ? 0.0f : 1.0f;
        }
        SIZE size = {w + margin * 2, h + margin * 2};
        POINT source = {0, 0};
        BLENDFUNCTION value = blend();
        if (!UpdateLayeredWindow(layer, nullptr, &position, &size, dc, &source, 0, &value, ULW_ALPHA)) {
            hide();
            return;
        }
    } else if (!EqualRect(&placed, &window)) {
        SetWindowPos(layer, nullptr, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    placed = window;
    if (!visible) {
        visible = true;
        shownAt = GetTickCount64();
        ShowWindow(layer, SW_SHOWNOACTIVATE);
        if (fade < 1.0f) {
            SetTimer(layer, FADE_TIMER, USER_TIMER_MINIMUM, nullptr);
        }
    }
}

void WindowShadow::hide() {
    if (!visible) {
        return;
    }
    visible = false;
    if (layer != nullptr) {
        KillTimer(layer, FADE_TIMER);
        ShowWindow(layer, SW_HIDE);
    }
}

void WindowShadow::setActive(bool on) {
    if (active == on) {
        return;
    }
    active = on;
    if (visible && layer != nullptr) {
        BLENDFUNCTION value = blend();
        UpdateLayeredWindow(layer, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &value, ULW_ALPHA);
    }
}

void WindowShadow::fadeStep() {
    ULONGLONG elapsed = GetTickCount64() - shownAt;
    if (elapsed <= FADE_DELAY_MS) {
        return;
    }
    fade = std::min(1.0f, (float) (elapsed - FADE_DELAY_MS) / FADE_MS);
    BLENDFUNCTION value = blend();
    UpdateLayeredWindow(layer, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &value, ULW_ALPHA);
    if (fade >= 1.0f) {
        KillTimer(layer, FADE_TIMER);
    }
}
