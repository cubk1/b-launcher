#include "ui/imgui/screens/shell/TrayPanel.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Compact.h"
#include "ui/imgui/panel/UiTheme.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {
    constexpr UINT TRAY_MESSAGE = WM_APP + 0x51;
    constexpr UINT TRAY_ID = 1;
    constexpr UINT OPEN_MAIN = 1;
    constexpr UINT QUIT = 2;
    constexpr float GAP = 12.0f;
    constexpr ULONGLONG REOPEN_MS = 300;
}

TrayPanel::TrayPanel(BorderlessWindow& window)
    : window(window),
      taskbarCreated(RegisterWindowMessageW(L"TaskbarCreated")) {}

TrayPanel::~TrayPanel() {
    removeIcon();
}

bool TrayPanel::active() const {
    return on;
}

void TrayPanel::addIcon() {
    if (window.hwnd == nullptr) {
        return;
    }
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = window.hwnd;
    data.uID = TRAY_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TRAY_MESSAGE;
    data.hIcon = icon != nullptr ? icon : (HICON) GetClassLongPtrW(window.hwnd, GCLP_HICONSM);
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(iconShown ? NIM_MODIFY : NIM_ADD, &data)) {
        iconShown = true;
    }
}

void TrayPanel::removeIcon() {
    if (!iconShown) {
        return;
    }
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = window.hwnd;
    data.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &data);
    iconShown = false;
}

void TrayPanel::setIcon(HICON next) {
    if (next == nullptr || next == icon) {
        return;
    }
    icon = next;
    if (iconShown) {
        addIcon();
    }
}

void TrayPanel::setTip(const std::wstring& next) {
    if (next == tip) {
        return;
    }
    tip = next;
    if (iconShown) {
        addIcon();
    }
}

void TrayPanel::place() {
    APPBARDATA bar = {};
    bar.cbSize = sizeof(bar);
    bool hasBar = SHAppBarMessage(ABM_GETTASKBARPOS, &bar) != 0;
    RECT spot = {};
    NOTIFYICONIDENTIFIER id = {};
    id.cbSize = sizeof(id);
    id.hWnd = window.hwnd;
    id.uID = TRAY_ID;
    RECT overlap;
    bool hasSpot = iconShown && SUCCEEDED(Shell_NotifyIconGetRect(&id, &spot)) && spot.right > spot.left &&
            (!hasBar || IntersectRect(&overlap, &spot, &bar.rc));
    POINT probe = hasSpot ? POINT{(spot.left + spot.right) / 2, (spot.top + spot.bottom) / 2}
            : hasBar      ? POINT{bar.rc.right - 1, bar.rc.bottom - 1}
                          : POINT{0, 0};
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(probe, MONITOR_DEFAULTTOPRIMARY), &monitor);
    RECT work = monitor.rcWork;
    int gap = (int) std::lround(GAP * scale);
    int w = std::min((int) std::lround(Compact::WIDTH * scale), (int) (work.right - work.left) - gap * 2);
    int h = std::min((int) std::lround(Compact::HEIGHT * scale), (int) (work.bottom - work.top) - gap * 2);
    UINT edge = hasBar ? bar.uEdge : ABE_BOTTOM;
    int x = edge == ABE_LEFT ? work.left + gap : work.right - w - gap;
    int y = edge == ABE_TOP ? work.top + gap : work.bottom - h - gap;
    if (hasSpot && (edge == ABE_LEFT || edge == ABE_RIGHT)) {
        y = (spot.top + spot.bottom) / 2 - h / 2;
    } else if (hasSpot) {
        x = (spot.left + spot.right) / 2 - w / 2;
    }
    x = std::clamp(x, (int) work.left + gap, std::max((int) work.left + gap, (int) work.right - w - gap));
    y = std::clamp(y, (int) work.top + gap, std::max((int) work.top + gap, (int) work.bottom - h - gap));
    window.place(x, y, w, h);
}

void TrayPanel::enter(float layoutScale, bool reveal, bool maximized) {
    if (on || window.hwnd == nullptr) {
        return;
    }
    HWND hwnd = window.hwnd;
    scale = layoutScale;
    saved = {};
    saved.length = sizeof(saved);
    haveSaved = GetWindowPlacement(hwnd, &saved) != FALSE;
    savedMaximized = maximized || IsZoomed(hwnd);
    ShowWindow(hwnd, SW_HIDE);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~(LONG_PTR) (WS_MAXIMIZE | WS_MINIMIZE));
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, (ex & ~(LONG_PTR) WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);
    window.resizable = false;
    window.setFramed(false);
    window.setDarkMode(!UiTheme::isLight());
    on = true;
    Compact::setActive(true);
    addIcon();
    if (reveal) {
        show();
    } else {
        place();
    }
}

void TrayPanel::leave(bool reveal, int minWidth, int minHeight) {
    if (!on || window.hwnd == nullptr) {
        return;
    }
    HWND hwnd = window.hwnd;
    on = false;
    Compact::setActive(false);
    ShowWindow(hwnd, SW_HIDE);
    removeIcon();
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, (ex & ~(LONG_PTR) WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    window.resizable = true;
    window.setFramed(Appearance::systemFrameRunning());
    window.setDarkMode(!UiTheme::isLight());
    if (haveSaved) {
        WINDOWPLACEMENT restore = saved;
        restore.flags = 0;
        restore.showCmd = SW_HIDE;
        SetWindowPlacement(hwnd, &restore);
    }
    RECT client;
    if (GetClientRect(hwnd, &client)) {
        window.width = client.right - client.left;
        window.height = client.bottom - client.top;
    }
    window.setMinimumSize(minWidth, minHeight);
    if (reveal) {
        window.cloak(true);
        cloakedFrames = 1;
        ShowWindow(hwnd, savedMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);
    }
}

void TrayPanel::show() {
    if (!on || window.hwnd == nullptr) {
        return;
    }
    place();
    window.cloak(true);
    cloakedFrames = 1;
    SetWindowPos(window.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window.hwnd);
    shownAt = GetTickCount64();
}

void TrayPanel::hide() {
    if (!on || window.hwnd == nullptr) {
        return;
    }
    if (IsWindowVisible(window.hwnd)) {
        ShowWindow(window.hwnd, SW_HIDE);
        hiddenAt = GetTickCount64();
        Compact::hidden();
    }
    if (cloakedFrames > 0) {
        cloakedFrames = 0;
        window.cloak(false);
    }
}

void TrayPanel::fit(float layoutScale) {
    scale = layoutScale;
    if (on) {
        place();
    }
}

void TrayPanel::presented() {
    if (cloakedFrames > 0 && --cloakedFrames == 0) {
        window.cloak(false);
    }
}

void TrayPanel::menu() {
    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) {
        return;
    }
    AppendMenuW(popup, MF_STRING, OPEN_MAIN, L"打开主窗口");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, QUIT, L"退出");
    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(window.hwnd);
    UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    UINT picked = (UINT) TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align,
                                        cursor.x, cursor.y, 0, window.hwnd, nullptr);
    PostMessageW(window.hwnd, WM_NULL, 0, 0);
    DestroyMenu(popup);
    if (picked == OPEN_MAIN && onOpenMain) {
        onOpenMain();
    } else if (picked == QUIT && onQuit) {
        onQuit();
    }
}

bool TrayPanel::message(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (msg == TRAY_MESSAGE) {
        UINT event = LOWORD(lParam);
        if (event == WM_LBUTTONUP && on) {
            if (IsWindowVisible(window.hwnd)) {
                if (GetTickCount64() - shownAt > GetDoubleClickTime()) {
                    hide();
                }
            } else if (GetTickCount64() - hiddenAt > REOPEN_MS) {
                show();
            }
        } else if (event == WM_RBUTTONUP && on) {
            menu();
        }
        result = 0;
        return true;
    }
    if (taskbarCreated != 0 && msg == taskbarCreated) {
        if (on) {
            iconShown = false;
            addIcon();
        }
        return false;
    }
    if (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE && on) {
        hide();
    }
    return false;
}
