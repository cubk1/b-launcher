#include "app/shell/BorderlessWindow.h"

#include "imgui.h"

#include <dwmapi.h>

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
}

bool BorderlessWindow::create(const std::wstring& title, int clientWidth, int clientHeight, HGLRC share) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = (HICON) LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                  GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON) LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    RECT area = monitor.rcWork;
    if (resizable) {
        clientWidth = std::min(clientWidth, (int) (area.right - area.left));
        clientHeight = std::min(clientHeight, (int) (area.bottom - area.top));
    }
    int x = area.left + (area.right - area.left - clientWidth) / 2;
    int y = area.top + (area.bottom - area.top - clientHeight) / 2;

    DWORD style =
            WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | (resizable ? WS_MAXIMIZEBOX : 0);
    width = clientWidth;
    height = clientHeight;
    minWidth = clientWidth;
    minHeight = clientHeight;
    hwnd = CreateWindowExW(WS_EX_APPWINDOW, CLASS_NAME, title.c_str(), style, x, y, clientWidth, clientHeight, nullptr,
                           nullptr, wc.hInstance, this);
    if (hwnd == nullptr) {
        return false;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    applyFrame();
    if (bigIcon != nullptr || smallIcon != nullptr) {
        setIcons(bigIcon, smallIcon);
    }
    return createGlContext(share);
}

void BorderlessWindow::resize(int clientWidth, int clientHeight) {
    if (hwnd == nullptr || (clientWidth == width && clientHeight == height)) {
        return;
    }
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int x = (rect.left + rect.right) / 2 - clientWidth / 2;
    int y = (rect.top + rect.bottom) / 2 - clientHeight / 2;
    MONITORINFO monitor = {sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        RECT area = monitor.rcWork;
        x = std::max((int) area.left, std::min(x, (int) area.right - clientWidth));
        y = std::max((int) area.top, std::min(y, (int) area.bottom - clientHeight));
    }
    width = clientWidth;
    height = clientHeight;
    SetWindowPos(hwnd, nullptr, x, y, clientWidth, clientHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (regionCorners) {
        SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, FALLBACK_RADIUS * 2, FALLBACK_RADIUS * 2),
                     TRUE);
    }
}

void BorderlessWindow::setMinimumSize(int clientWidth, int clientHeight) {
    minWidth = clientWidth;
    minHeight = clientHeight;
    if (hwnd != nullptr && !IsZoomed(hwnd) && (width < minWidth || height < minHeight)) {
        resize(std::max(width, minWidth), std::max(height, minHeight));
    }
}

void BorderlessWindow::applyFrame() {
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    COLORREF none = DWMWA_COLOR_NONE_;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR_, &none, sizeof(none));
    int corner = DWMWCP_ROUND_;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &corner, sizeof(corner)))) {
        regionCorners = true;
        SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, width + 1, height + 1, FALLBACK_RADIUS * 2, FALLBACK_RADIUS * 2),
                     TRUE);
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
    COLORREF border = DWMWA_COLOR_NONE_;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR_, &border, sizeof(border));
}

bool BorderlessWindow::createGlContext(HGLRC share) {
    deviceContext = GetDC(hwnd);
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
    UnregisterClassW(CLASS_NAME, GetModuleHandleW(nullptr));
}

void BorderlessWindow::show(bool maximized) {
    ShowWindow(hwnd, maximized && resizable ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);
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

void BorderlessWindow::modalFrame() {
    if (!onModalFrame || inModalFrame || IsIconic(hwnd)) {
        return;
    }
    inModalFrame = true;
    onModalFrame();
    inModalFrame = false;
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
    SwapBuffers(deviceContext);
}

void BorderlessWindow::setVsync(bool on) {
    using SwapInterval = BOOL(WINAPI*)(int);
    if (auto swapInterval = (SwapInterval) wglGetProcAddress("wglSwapIntervalEXT")) {
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
        case WM_NCCALCSIZE:
            if (wParam == TRUE) {
                if (IsZoomed(hwnd)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    int frameX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    int frameY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left += frameX;
                    params->rgrc[0].right -= frameX;
                    params->rgrc[0].top += frameY;
                    params->rgrc[0].bottom -= frameY;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT point = {(short) LOWORD(lParam), (short) HIWORD(lParam)};
            ScreenToClient(hwnd, &point);
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
                if (inSizeMove) {
                    modalFrame();
                }
            }
            break;
        case WM_ENTERSIZEMOVE:
            inSizeMove = true;
            SetTimer(hwnd, 1, USER_TIMER_MINIMUM, nullptr);
            break;
        case WM_EXITSIZEMOVE:
            inSizeMove = false;
            KillTimer(hwnd, 1);
            break;
        case WM_TIMER:
            if (wParam == 1) {
                modalFrame();
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
        case WM_NCACTIVATE: return DefWindowProcW(hwnd, msg, wParam, -1);
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (resizable) {
                info->ptMinTrackSize.x = minWidth;
                info->ptMinTrackSize.y = minHeight;
                break;
            }
            info->ptMinTrackSize.x = width;
            info->ptMinTrackSize.y = height;
            info->ptMaxTrackSize.x = width;
            info->ptMaxTrackSize.y = height;
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
