#include "ui/exui/Tooltip.h"
#include "ui/exui/Window.h"
#include <cmath>
#include <uxtheme.h>
#include <vssym32.h>

namespace exui {

    struct Tooltip::Popup {
        const void* subject = nullptr;
        std::wstring text;
        HWND hwnd = nullptr;
        HFONT font = nullptr;
        HTHEME theme = nullptr;
        Surface surface;
        bool displayed = false;

        ~Popup() {
            if (IsWindow(hwnd)) DestroyWindow(hwnd);
            if (theme) CloseThemeData(theme);
            if (font) DeleteObject(font);
        }

        // The standard tooltip theme and a 1024px text limit, rendered into our own DIB.
        void Paint(UINT dpi) {
            NONCLIENTMETRICSW metrics{sizeof(metrics)};
            if (!font && SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
                font = CreateFontIndirectW(&metrics.lfStatusFont);
            if (!theme) theme = OpenThemeDataForDpi(nullptr, L"TOOLTIP", dpi);
            HDC screen = GetDC(nullptr);
            const auto old = SelectObject(screen, font ? font : GetStockObject(DEFAULT_GUI_FONT));
            RECT extent{0, 0, 1024, 0};
            constexpr DWORD flags = DT_WORDBREAK | DT_NOPREFIX;
            if (!theme ||
                FAILED(GetThemeTextExtent(theme, screen, TTP_STANDARD, TTSS_NORMAL, text.c_str(), -1, flags, &extent,
                                          &extent))) {
                extent = {0, 0, 1024, 0};
                DrawTextW(screen, text.c_str(), -1, &extent, flags | DT_CALCRECT);
            }
            MARGINS margin{3, 3, 3, 3};
            if (theme) GetThemeMargins(theme, screen, TTP_STANDARD, TTSS_NORMAL, TMT_CONTENTMARGINS, nullptr, &margin);
            const int width = std::max(1L, extent.right - extent.left) + margin.cxLeftWidth + margin.cxRightWidth;
            const int height = std::max(1L, extent.bottom - extent.top) + margin.cyTopHeight + margin.cyBottomHeight;
            SelectObject(screen, old);
            ReleaseDC(nullptr, screen);
            if (!surface.Resize(width, height)) return;

            HDC dc = surface.GetDC();
            RECT whole{0, 0, width, height};
            FillRect(dc, &whole, GetSysColorBrush(COLOR_INFOBK));
            if (theme)
                DrawThemeBackground(theme, dc, TTP_STANDARD, TTSS_NORMAL, &whole, nullptr);
            else
                FrameRect(dc, &whole, GetSysColorBrush(COLOR_WINDOWFRAME));
            RECT content{margin.cxLeftWidth, margin.cyTopHeight, width - margin.cxRightWidth,
                         height - margin.cyBottomHeight};
            const auto previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
            if (theme)
                DrawThemeText(theme, dc, TTP_STANDARD, TTSS_NORMAL, text.c_str(), -1, flags, 0, &content);
            else
                DrawTextW(dc, text.c_str(), -1, &content, flags);
            SelectObject(dc, previous);
            GdiFlush();
            for (auto& pixel : surface.Pixels())
                pixel |= 0xFF000000;
        }

        static LRESULT CALLBACK Procedure(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
            auto* popup = reinterpret_cast<Popup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                popup = static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
            }
            if (!popup) return DefWindowProcW(window, msg, wp, lp);
            switch (msg) {
                case WM_NCHITTEST: return HTTRANSPARENT;
                case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
                case WM_ERASEBKGND: return 1;
                case WM_PAINT: {
                    PAINTSTRUCT ps{};
                    BeginPaint(window, &ps);
                    BitBlt(ps.hdc, 0, 0, popup->surface.Width(), popup->surface.Height(), popup->surface.GetDC(), 0, 0,
                           SRCCOPY);
                    EndPaint(window, &ps);
                    return 0;
                }
                case WM_NCDESTROY: popup->hwnd = nullptr; break;
            }
            return DefWindowProcW(window, msg, wp, lp);
        }

        void Show(HWND owner) {
            // The owner's scale, which may be set explicitly rather than follow the monitor.
            const auto* window = Window::FromHandle(owner);
            Paint(UINT(std::lround(96 * (window ? window->GetScale() : 1.f))));
            if (!surface.GetBitmap()) return;
            WNDCLASSEXW cls{sizeof(cls)};
            cls.lpfnWndProc = Procedure;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"Ex_DirectUI_Tooltip";
            cls.style = CS_DROPSHADOW;
            if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
            POINT p{};
            GetCursorPos(&p);
            p.y += GetSystemMetrics(SM_CYCURSOR) / 2;
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &monitor)) {
                p.x = std::max(monitor.rcWork.left, std::min(p.x, monitor.rcWork.right - surface.Width()));
                p.y = std::max(monitor.rcWork.top, std::min(p.y, monitor.rcWork.bottom - surface.Height()));
            }
            hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls.lpszClassName, L"",
                                   WS_POPUP, p.x, p.y, surface.Width(), surface.Height(), owner, nullptr, cls.hInstance,
                                   this);
            if (hwnd && theme) {
                RECT bounds{0, 0, surface.Width(), surface.Height()};
                HRGN region = nullptr;
                if (SUCCEEDED(GetThemeBackgroundRegion(theme, surface.GetDC(), TTP_STANDARD, TTSS_NORMAL, &bounds,
                                                       &region)))
                    if (!SetWindowRgn(hwnd, region, FALSE)) DeleteObject(region);
            }
            // A hidden owner shows no tooltip window.
            if (hwnd && IsWindowVisible(owner)) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            displayed = true;
        }
    };

    Tooltip::Tooltip() = default;
    Tooltip::~Tooltip() = default;

    void Tooltip::Schedule(HWND owner, const void* subject, std::wstring text) {
        if (m_popup && m_popup->subject == subject && m_popup->text == text) return;
        Hide(owner);
        m_popup = std::make_unique<Popup>();
        m_popup->subject = subject;
        m_popup->text = std::move(text);
        SetTimer(owner, TimerId(), std::max(1U, GetDoubleClickTime()), nullptr);
    }

    void Tooltip::Hide(HWND owner) {
        if (owner) KillTimer(owner, TimerId());
        m_popup.reset();
    }

    bool Tooltip::OnTimer(HWND owner, UINT_PTR id) {
        if (id != TimerId()) return false;
        KillTimer(owner, id);
        if (!m_popup) return true;
        if (m_popup->displayed) {
            // Hide after the display period, and do not reopen while the pointer stays.
            if (IsWindow(m_popup->hwnd)) DestroyWindow(m_popup->hwnd);
            return true;
        }
        m_popup->Show(owner);
        SetTimer(owner, id, GetDoubleClickTime() * 10, nullptr);
        return true;
    }

    HWND Tooltip::Hwnd() const noexcept {
        return m_popup ? m_popup->hwnd : nullptr;
    }

    std::unique_ptr<Gdiplus::Bitmap> Tooltip::Snapshot() const {
        return m_popup && m_popup->surface.GetBitmap() ? CopyBitmap(*m_popup->surface.GetBitmap()) : nullptr;
    }

} // namespace exui
