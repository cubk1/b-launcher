#include "ui/exui/Popup.h"

namespace exui {

    Popup::~Popup() {
        Closed = {};
        Close();
    }

    bool Popup::Open(HWND owner, RECT bounds, Widget* focus) {
        if (IsOpen() || m_closing || !IsWindow(owner)) return false;
        const auto* host = Window::FromHandle(owner);
        if (!CreateSurfaceWindow(owner, bounds, host ? host->GetScale() : 1.f)) return false;
        // The owner's held press cannot complete once the popup is up.
        if (GetCapture() == owner) ReleaseCapture();
        ShowWindow(GetHwnd(), SW_SHOW);
        SetFocus(GetHwnd());
        if (focus) SetFocusedWidget(focus);
        return IsOpen();
    }

    void Popup::Close() {
        const HWND hwnd = GetHwnd();
        if (!hwnd || m_closing) return;
        m_closing = true;
        const HWND owner = GetWindow(hwnd, GW_OWNER);
        const bool hadFocus = GetFocus() == hwnd;
        CloseWindow(false);
        if (hadFocus && IsWindow(owner)) SetFocus(owner);
        m_closing = false;
        if (const auto closed = Closed) closed();
    }

    void Popup::RequestClose() {
        if (IsOpen()) PostMessageW(GetHwnd(), WM_CLOSE, 0, 0);
    }

    LRESULT Popup::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CLOSE: Close(); return 0;
            case WM_ACTIVATE:
                if (LOWORD(wp) == WA_INACTIVE) RequestClose();
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE) {
                    RequestClose();
                    return 0;
                }
                break;
            case WM_NCLBUTTONDOWN: return 0; // a popup is never dragged
        }
        return Window::HandleMessage(msg, wp, lp);
    }

} // namespace exui
