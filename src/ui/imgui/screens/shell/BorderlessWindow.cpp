#include "ui/imgui/screens/shell/BorderlessWindow.h"
#include "ui/imgui/screens/shell/ResizeSurface.h"
#include "ui/imgui/gl/UiGL.h"

#include "imgui.h"

#include <dwmapi.h>
#include <imm.h>

#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ = 20;
    constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
    constexpr DWORD DWMWA_BORDER_COLOR_ = 34;
    constexpr DWORD DWMWA_CAPTION_COLOR_ = 35;
    constexpr int DWMWCP_ROUND_ = 2;
    constexpr COLORREF DWMWA_COLOR_NONE_ = 0xFFFFFFFE;
    constexpr int FALLBACK_RADIUS = 8;
    const wchar_t* const CLASS_NAME = L"b-launcher.window";

    struct WindowClass {
        HICON icon = nullptr;
        HICON smallIcon = nullptr;
        bool registered = false;

        void releaseIcons() {
            if (icon) DestroyIcon(icon);
            if (smallIcon) DestroyIcon(smallIcon);
            icon = smallIcon = nullptr;
        }

        bool ensure(WNDPROC procedure) {
            if (registered) {
                return true;
            }
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = procedure;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            icon = (HICON) LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
            smallIcon = (HICON) LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
            wc.hIcon = icon;
            wc.hIconSm = smallIcon;
            wc.lpszClassName = CLASS_NAME;
            registered = RegisterClassExW(&wc) != 0;
            if (!registered) {
                releaseIcons();
            }
            return registered;
        }

        ~WindowClass() {
            // Keep the class and its correctly sized icons for later windows.
            // Icons must outlive every window still using this class.
            if (!registered || UnregisterClassW(CLASS_NAME, GetModuleHandleW(nullptr))) {
                releaseIcons();
            }
        }
    };

    // Surface operations can dispatch window messages too. Keep nested sizing
    // notifications from drawing again or deleting a surface still in use.
    struct ModalFrameGuard {
        bool& active;
        explicit ModalFrameGuard(bool& active) : active(active) { active = true; }
        ~ModalFrameGuard() { active = false; }
    };
}

BorderlessWindow::BorderlessWindow() = default;
BorderlessWindow::~BorderlessWindow() = default;

bool BorderlessWindow::create(const std::wstring& title, int clientWidth, int clientHeight, HGLRC share) {
    if (hwnd != nullptr) {
        return false;
    }
    static WindowClass windowClass;
    if (!windowClass.ensure(windowProc)) {
        return false;
    }

    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    RECT area = monitor.rcWork;
    SIZE extra = frameSize();
    if (resizable) {
        clientWidth = std::min(clientWidth, (int) (area.right - area.left - extra.cx));
        clientHeight = std::min(clientHeight, (int) (area.bottom - area.top - extra.cy));
    }
    int outerWidth = clientWidth + (int) extra.cx;
    int outerHeight = clientHeight + (int) extra.cy;
    int x = area.left + (area.right - area.left - outerWidth) / 2;
    int y = area.top + (area.bottom - area.top - outerHeight) / 2;

    width = clientWidth;
    height = clientHeight;
    minWidth = clientWidth;
    minHeight = clientHeight;
    hwnd = CreateWindowExW(WS_EX_APPWINDOW, CLASS_NAME, title.c_str(), style(), x, y, outerWidth, outerHeight, nullptr,
                           nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd == nullptr) {
        return false;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    applyFrame();
    if (bigIcon != nullptr || smallIcon != nullptr) {
        setIcons(bigIcon, smallIcon);
    }
    HDC previousDc = wglGetCurrentDC();
    HGLRC previousContext = wglGetCurrentContext();
    if (!createGlContext(share)) {
        // createGlContext() can fail after acquiring a DC and a GL context.
        // Leave the object reusable and restore the caller's current context.
        destroy();
        if (wglGetCurrentDC() != previousDc || wglGetCurrentContext() != previousContext) {
            wglMakeCurrent(previousDc, previousContext);
        }
        return false;
    }
    return true;
}

void BorderlessWindow::resize(int clientWidth, int clientHeight) {
    if (hwnd == nullptr || (clientWidth == width && clientHeight == height)) {
        return;
    }
    RECT rect;
    GetWindowRect(hwnd, &rect);
    SIZE extra = frameSize();
    int outerWidth = clientWidth + (int) extra.cx;
    int outerHeight = clientHeight + (int) extra.cy;
    int x = (rect.left + rect.right) / 2 - outerWidth / 2;
    int y = (rect.top + rect.bottom) / 2 - outerHeight / 2;
    MONITORINFO monitor = {sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        RECT area = monitor.rcWork;
        x = std::max((int) area.left, std::min(x, (int) area.right - outerWidth));
        y = std::max((int) area.top, std::min(y, (int) area.bottom - outerHeight));
    }
    width = clientWidth;
    height = clientHeight;
    SetWindowPos(hwnd, nullptr, x, y, outerWidth, outerHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    applyRegion();
}

void BorderlessWindow::applyRegion() {
    if (!regionCorners) {
        return;
    }
    if (inSizeMove) {
        if (!regionSuspended) {
            // Keep an explicit region: removing it re-enables DWM's native frame
            // on Win10. Cover the tracking limits so this needs no per-size updates.
            int regionW = std::max(width, GetSystemMetrics(SM_CXMAXTRACK));
            int regionH = std::max(height, GetSystemMetrics(SM_CYMAXTRACK));
            HRGN region = CreateRectRgn(0, 0, regionW + 1, regionH + 1);
            if (region != nullptr) {
                regionSuspended = true;
                if (!SetWindowRgn(hwnd, region, FALSE)) {
                    regionSuspended = false;
                    DeleteObject(region);
                }
            }
        }
        return;
    }
    // A maximized window is square to the screen; a region of another size would cut the window off.
    if (IsZoomed(hwnd)) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, FALLBACK_RADIUS * 2, FALLBACK_RADIUS * 2), TRUE);
}

void BorderlessWindow::setMinimumSize(int clientWidth, int clientHeight) {
    minWidth = clientWidth;
    minHeight = clientHeight;
    if (hwnd != nullptr && !IsZoomed(hwnd) && (width < minWidth || height < minHeight)) {
        resize(std::max(width, minWidth), std::max(height, minHeight));
    }
}

DWORD BorderlessWindow::style() const {
    if (framed) {
        return WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                (resizable ? WS_THICKFRAME | WS_MAXIMIZEBOX : 0);
    }
    return WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | (resizable ? WS_MAXIMIZEBOX : 0);
}

SIZE BorderlessWindow::frameSize() const {
    if (!framed) {
        return {0, 0};
    }
    RECT rect = {0, 0, 0, 0};
    AdjustWindowRectEx(&rect, style(), FALSE, WS_EX_APPWINDOW);
    return {rect.right - rect.left, rect.bottom - rect.top};
}

void BorderlessWindow::applyFrame() {
    if (framed) {
        setDarkMode(true);
        return;
    }
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    COLORREF none = DWMWA_COLOR_NONE_;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR_, &none, sizeof(none));
    int corner = DWMWCP_ROUND_;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &corner, sizeof(corner)))) {
        // Before Windows 11 the corners are rounded by a region, which follows the size of the window.
        regionCorners = true;
        applyRegion();
    }
    setDarkMode(true);
}

void BorderlessWindow::setTitle(const std::wstring& title) {
    if (hwnd != nullptr) {
        SetWindowTextW(hwnd, title.c_str());
    }
}

void BorderlessWindow::setIcons(HICON large, HICON compact) {
    bigIcon = large;
    smallIcon = compact;
    if (hwnd == nullptr) {
        return;
    }
    if (large != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM) large);
    }
    if (compact != nullptr) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) compact);
    }
}

void BorderlessWindow::setDarkMode(bool dark) {
    if (hwnd == nullptr) {
        return;
    }
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_, &value, sizeof(value));
    if (framed) {
        return;
    }
    COLORREF border = DWMWA_COLOR_NONE_;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR_, &border, sizeof(border));
}

bool BorderlessWindow::createGlContext(HGLRC share) {
    deviceContext = GetDC(hwnd);
    if (deviceContext == nullptr) {
        return false;
    }
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    int format = ChoosePixelFormat(deviceContext, &pfd);
    if (format == 0 || !SetPixelFormat(deviceContext, format, &pfd)) {
        return false;
    }
    glContext = wglCreateContext(deviceContext);
    if (glContext == nullptr) {
        return false;
    }
    if (share != nullptr && !wglShareLists(share, glContext)) {
        return false;
    }
    return wglMakeCurrent(deviceContext, glContext);
}

void BorderlessWindow::destroy() {
    releaseResize();
    if (wglGetCurrentContext() == glContext) {
        wglMakeCurrent(nullptr, nullptr);
    }
    if (glContext != nullptr) {
        wglDeleteContext(glContext);
        glContext = nullptr;
    }
    if (hwnd != nullptr) {
        HWND gone = hwnd;
        ReleaseDC(hwnd, deviceContext);
        hwnd = nullptr;
        DestroyWindow(gone);
    }
    deviceContext = nullptr;
}

void BorderlessWindow::show(bool maximized) {
    ShowWindow(hwnd, maximized && resizable ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);
}

void BorderlessWindow::setTextArea(float x, float y, float h) {
    if (hwnd == nullptr) {
        return;
    }
    int areaX = (int) x;
    int areaY = (int) y;
    int areaH = std::max(1, (int) (h + 0.5f));
    if (areaX == textAreaX && areaY == textAreaY && areaH == textAreaH) {
        return;
    }
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return;
    }
    textAreaX = areaX;
    textAreaY = areaY;
    textAreaH = areaH;
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = {areaX, areaY};
    ImmSetCompositionWindow(context, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = {areaX, areaY + areaH};
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(hwnd, context);
}

void BorderlessWindow::clearTextArea() {
    textAreaX = -1;
    textAreaY = -1;
    textAreaH = -1;
}

void BorderlessWindow::minimize() {
    ShowWindow(hwnd, SW_MINIMIZE);
}

void BorderlessWindow::toggleMaximize() {
    if (resizable) {
        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    }
}

bool BorderlessWindow::maximized() const {
    return IsZoomed(hwnd) != FALSE;
}

bool BorderlessWindow::sizing() const {
    return inSizeMove;
}

bool BorderlessWindow::preparingResize() const {
    return resizeSurface && resizeSurface->preparing();
}

void BorderlessWindow::prepareResize(int clientWidth, int clientHeight, bool refresh) {
    if (!onModalFrame || inModalFrame || clientWidth <= 0 || clientHeight <= 0 ||
        (!refresh && clientWidth == width && clientHeight == height &&
            (!resizeSurface || !resizeSurface->hasLayer()))) {
        return;
    }
    if (!resizeSurface) {
        resizeSurface = std::make_unique<ResizeSurface>(deviceContext, glContext);
    }
    int actualWidth = width, actualHeight = height;
    auto restore = [&] {
        width = actualWidth;
        height = actualHeight;
    };
    {
        ModalFrameGuard guard(inModalFrame);
        try {
            resizeSurface->prepare(clientWidth, clientHeight, [this, clientWidth, clientHeight] {
                width = clientWidth;
                height = clientHeight;
                onModalFrame();
            }, refresh);
        } catch (...) {
            restore();
            throw;
        }
        restore();
    }
    if (!inSizeMove) {
        releaseResize();
    }
}

void BorderlessWindow::showResizeLayer(const RECT& position) {
    if (!inSizeMove || inModalFrame || !resizeSurface) {
        return;
    }
    {
        ModalFrameGuard guard(inModalFrame);
        if (resizeSurface->updateLayer(hwnd, position)) {
            lastResizeFrame = GetTickCount64();
        }
    }
    if (!inSizeMove) {
        releaseResize();
    }
}

void BorderlessWindow::releaseResize() {
    // An exit message may arrive from inside the draw callback. Defer deletion
    // until prepare() has restored the caller's GL bindings.
    if (!inModalFrame) {
        ModalFrameGuard guard(inModalFrame);
        resizeSurface.reset();
    }
}

void BorderlessWindow::modalFrame() {
    if (!onModalFrame || inModalFrame || IsIconic(hwnd)) {
        return;
    }
    {
        ModalFrameGuard guard(inModalFrame);
        if (!resizeSurface || !resizeSurface->present(width, height, inSizeMove)) {
            onModalFrame();
        }
    }
    if (!inSizeMove) {
        releaseResize();
    }
}

void BorderlessWindow::close() {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

bool BorderlessWindow::pumpMessages() {
    bool open = true;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            open = false;
        }
    }
    return open;
}

bool BorderlessWindow::minimized() const {
    return IsIconic(hwnd) != FALSE;
}

void BorderlessWindow::swap() {
    if (resizeSurface && resizeSurface->captureFrame()) {
        return;
    }
    SwapBuffers(deviceContext);
}

void BorderlessWindow::setVsync(bool on) {
    using SwapInterval = BOOL(WINAPI*)(int);
    if (auto swapInterval = reinterpret_cast<SwapInterval>(UiGL::extensionProc("wglSwapIntervalEXT"))) {
        swapInterval(on ? 1 : 0);
    }
}

LRESULT WINAPI BorderlessWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<BorderlessWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* target =
            self != nullptr && self->imguiContext != nullptr ? (ImGuiContext*) self->imguiContext : previous;
    if (target != nullptr) {
        ImGui::SetCurrentContext(target);
        bool handled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
        ImGui::SetCurrentContext(previous);
        if (handled) {
            return 1;
        }
    }
    if (self == nullptr || self->hwnd == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->handle(msg, wParam, lParam);
}

LRESULT BorderlessWindow::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZING: {
            const auto* proposed = reinterpret_cast<const RECT*>(lParam);
            if (inSizeMove && resizable && !framed && !IsZoomed(hwnd)) {
                // This precedes SetWindowPos, before the compositor sees the new
                // position. WM_WINDOWPOSCHANGING is also kept for programmatic sizes.
                prepareResize(proposed->right - proposed->left, proposed->bottom - proposed->top);
                showResizeLayer(*proposed);
            }
            return TRUE;
        }
        case WM_WINDOWPOSCHANGING: {
            LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
            if (inSizeMove && resizable && !framed && !IsZoomed(hwnd) && (position->flags & SWP_NOSIZE) == 0) {
                // Render the next size while the previous position is still on screen.
                prepareResize(position->cx, position->cy);
                RECT proposed;
                GetWindowRect(hwnd, &proposed);
                if ((position->flags & SWP_NOMOVE) == 0) {
                    proposed.left = position->x;
                    proposed.top = position->y;
                }
                proposed.right = proposed.left + position->cx;
                proposed.bottom = proposed.top + position->cy;
                showResizeLayer(proposed);
            }
            return result;
        }
        case WM_WINDOWPOSCHANGED: {
            LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
            if (inSizeMove && !inModalFrame && (position->flags & SWP_NOSIZE) == 0) {
                // WM_SIZE invalidated the client. Present after both its size and
                // position notifications have completed, before the next resize.
                UpdateWindow(hwnd);
            }
            return result;
        }
        case WM_NCCALCSIZE:
            if (wParam == TRUE && !framed) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                if (IsZoomed(hwnd)) {
                    int frameX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    int frameY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left += frameX;
                    params->rgrc[0].right -= frameX;
                    params->rgrc[0].top += frameY;
                    params->rgrc[0].bottom -= frameY;
                } else if (inSizeMove) {
                    // Keep the last frame anchored to the edge that did not move
                    // until OpenGL presents the resized frame. Left/top alignment
                    // would briefly shift right/bottom-anchored controls instead.
                    const RECT& next = params->rgrc[0];
                    const RECT& previous = params->rgrc[2];
                    bool fixedRight = next.left != previous.left && next.right == previous.right;
                    bool fixedBottom = next.top != previous.top && next.bottom == previous.bottom;
                    return (fixedRight ? WVR_ALIGNRIGHT : WVR_ALIGNLEFT) |
                            (fixedBottom ? WVR_ALIGNBOTTOM : WVR_ALIGNTOP);
                }
                return WVR_REDRAW;
            }
            break;
        case WM_NCHITTEST: {
            POINT point = {(short) LOWORD(lParam), (short) HIWORD(lParam)};
            ScreenToClient(hwnd, &point);
            if (framed) {
                LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
                return hit == HTCLIENT && isDragArea && isDragArea(point.x, point.y) ? HTCAPTION : hit;
            }
            if (resizable && !IsZoomed(hwnd)) {
                RECT client;
                GetClientRect(hwnd, &client);
                const int edge = 6;
                bool left = point.x < edge;
                bool right = point.x >= client.right - edge;
                bool top = point.y < edge;
                bool bottom = point.y >= client.bottom - edge;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            return isDragArea && isDragArea(point.x, point.y) ? HTCAPTION : HTCLIENT;
        }
        case WM_SIZE:
            if (resizable && wParam != SIZE_MINIMIZED) {
                width = LOWORD(lParam);
                height = HIWORD(lParam);
                applyRegion();
                if (inSizeMove) {
                    // Wait until the native position/size operation has returned
                    // before submitting the prepared image to the window drawable.
                    InvalidateRect(hwnd, nullptr, FALSE);
                    lastResizeFrame = GetTickCount64();
                }
            }
            break;
        case WM_PAINT:
            if (inSizeMove && onModalFrame) {
                PAINTSTRUCT paint;
                BeginPaint(hwnd, &paint);
                modalFrame();
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;
        case WM_ENTERSIZEMOVE:
            inSizeMove = true;
            SetTimer(hwnd, 1, USER_TIMER_MINIMUM, nullptr);
            break;
        case WM_EXITSIZEMOVE:
            inSizeMove = false;
            KillTimer(hwnd, 1);
            if (regionSuspended) {
                regionSuspended = false;
                applyRegion();
            }
            modalFrame();
            releaseResize();
            break;
        case WM_TIMER:
            if (wParam == 1) {
                // Size events already supplied fresh frames. Render timer frames
                // only when the drag pauses, rather than queueing the same size twice.
                if (inSizeMove && GetTickCount64() - lastResizeFrame >= 50) {
                    if (resizeSurface && resizeSurface->hasLayer() && !inModalFrame) {
                        prepareResize(width, height, true);
                        RECT position;
                        if (GetWindowRect(hwnd, &position)) {
                            showResizeLayer(position);
                        }
                    }
                    modalFrame();
                    lastResizeFrame = GetTickCount64();
                }
                return 0;
            }
            break;
        case WM_COPYDATA: {
            const COPYDATASTRUCT* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (onCopyData && data != nullptr && data->lpData != nullptr && data->cbData >= sizeof(wchar_t)) {
                std::wstring text((const wchar_t*) data->lpData, data->cbData / sizeof(wchar_t));
                while (!text.empty() && text.back() == L'\0') {
                    text.pop_back();
                }
                return onCopyData((unsigned long long) data->dwData, text) ? 1 : 0;
            }
            break;
        }
        case WM_CLOSE:
            if (onClose) {
                onClose();
                return 0;
            }
            break;
        case WM_NCACTIVATE:
            if (framed) {
                break;
            }
            return DefWindowProcW(hwnd, msg, wParam, -1);
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            SIZE extra = frameSize();
            if (resizable) {
                info->ptMinTrackSize.x = minWidth + extra.cx;
                info->ptMinTrackSize.y = minHeight + extra.cy;
                break;
            }
            info->ptMinTrackSize.x = width + extra.cx;
            info->ptMinTrackSize.y = height + extra.cy;
            info->ptMaxTrackSize.x = width + extra.cx;
            info->ptMaxTrackSize.y = height + extra.cy;
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_DESTROY:
            if (quitOnDestroy) {
                PostQuitMessage(0);
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
