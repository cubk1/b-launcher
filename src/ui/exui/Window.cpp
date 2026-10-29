#include "ui/exui/Window.h"
#include "ui/exui/widgets/CaptionButton.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <bit>
#include <cmath>

namespace exui {
    using namespace Gdiplus;

    namespace {
        constexpr wchar_t ClassName[] = L"Ex_DirectUI";

        // Effective DPI scale of the monitor nearest to a point (1 when the process is not DPI aware).
        float MonitorScale(POINT point) {
            UINT dpiX = 96, dpiY = 96;
            const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
            if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) return 1;
            return dpiX / 96.f;
        }

        void InvalidateTree(const Widget& widget) {
            widget.Invalidate();
            for (const auto& child : widget.GetChildren())
                InvalidateTree(*child);
        }
    } // namespace

    Window::Window() : m_root(std::make_shared<Panel>()), m_chrome(std::make_shared<Panel>()) {
        m_root->Attach(this);
        m_chrome->Attach(this);
    }

    Window::~Window() {
        CloseWindow(false);
        m_root->Attach(nullptr);
        m_chrome->Attach(nullptr);
    }

    bool Window::RegisterWindowClass() {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = AppIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = AppIcon(GetSystemMetrics(SM_CXSMICON));
        wc.lpszClassName = ClassName;
        if (RegisterClassExW(&wc)) return true;
        WNDCLASSEXW existing{sizeof(existing)};
        return GetClassInfoExW(wc.hInstance, ClassName, &existing) && existing.lpfnWndProc == StaticWndProc;
    }

    Window* Window::FromHandle(HWND hwnd) {
        if (!hwnd || reinterpret_cast<WNDPROC>(GetClassLongPtrW(hwnd, GCLP_WNDPROC)) != StaticWndProc) return nullptr;
        return reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    bool Window::CreateHostWindow(HWND parent, const std::wstring& title, int w, int h, std::span<const uint8_t> skin,
                                  COLORREF base, int corner, uint32_t mask, bool show) {
        if (m_hwnd || w <= 0 || h <= 0 || !Initialize()) return false;
        m_destroying = false;
        m_title = title;
        m_width = w;
        m_height = h;
        m_baseColor = base;
        m_cornerRadius = corner;
        m_captionMask = mask;
        if (mask & CaptionButtonMask::FromWindowStyle)
            m_captionMask |= CaptionButtonMask::Icon | CaptionButtonMask::Title | CaptionButtonMask::Close |
                    CaptionButtonMask::Maximize | CaptionButtonMask::Minimize;
        // The system menu stays even without caption buttons.
        DWORD style = WS_POPUP | WS_CLIPSIBLINGS | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        if (!(m_captionMask & CaptionButtonMask::Minimize)) style &= ~WS_MINIMIZEBOX;
        if (!(m_captionMask & CaptionButtonMask::Maximize)) style &= ~WS_MAXIMIZEBOX;
        m_skinBytes.assign(skin.begin(), skin.end());
        m_skin = DecodeImage(m_skinBytes);
        LayoutCaption();

        // Centered on the work area of the owner's monitor, or the primary one.
        MONITORINFO monitor{sizeof(monitor)};
        const HMONITOR target = parent ? MonitorFromWindow(parent, MONITOR_DEFAULTTOPRIMARY)
                                       : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        RECT work{};
        if (GetMonitorInfoW(target, &monitor))
            work = monitor.rcWork;
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        m_scale = m_requestedScale > 0 ? m_requestedScale
                                       : MonitorScale({(work.left + work.right) / 2, (work.top + work.bottom) / 2});
        m_icon = AppIcon(ToPhysical(16));
        const int x = work.left + (work.right - work.left - PhysicalWidth()) / 2;
        const int y = work.top + (work.bottom - work.top - PhysicalHeight()) / 2;
        const HWND hwnd = CreateWindowExW(WS_EX_LAYERED, ClassName, title.c_str(), style, x, y, PhysicalWidth(),
                                          PhysicalHeight(), parent, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) return false;
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_icon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_icon));
        RenderFrame();
        if (show) ShowWindowWithAnimation(SW_SHOWNORMAL);
        return true;
    }

    bool Window::CreateSurfaceWindow(HWND owner, RECT bounds, float scale) {
        if (m_hwnd || bounds.right <= bounds.left || bounds.bottom <= bounds.top || !Initialize()) return false;
        m_destroying = false;
        m_requestedScale = m_scale = scale > 0 ? scale : 1;
        m_width = int(std::lround((bounds.right - bounds.left) / m_scale));
        m_height = int(std::lround((bounds.bottom - bounds.top) / m_scale));
        m_cornerRadius = 1;
        m_captionMask = 0;
        LayoutCaption();
        const HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, ClassName, L"", WS_POPUP,
                                          bounds.left, bounds.top, bounds.right - bounds.left,
                                          bounds.bottom - bounds.top, owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) return false;
        RenderFrame();
        return true;
    }

    // Caption buttons right to left, starting 5px from the right edge (17px with a close button).
    void Window::LayoutCaption() {
        if (m_captionButtons.empty())
            for (const uint32_t type :
                 {CaptionButtonMask::Close, CaptionButtonMask::Maximize, CaptionButtonMask::Minimize,
                  CaptionButtonMask::Menu, CaptionButtonMask::Skin, CaptionButtonMask::Help, CaptionButtonMask::Pin})
                if (m_captionMask & type) m_captionButtons.push_back(&m_chrome->Add<CaptionButton>(type));
        m_root->SetBounds(0, 0, m_width, m_height);
        m_chrome->SetBounds(0, 0, m_width, m_height);
        int x = m_width - 5 - ((m_captionMask & CaptionButtonMask::Close) ? 12 : 0);
        for (auto* button : m_captionButtons) {
            x -= 28;
            button->SetBounds(x, 7, CaptionButton::Width(button->GetType()), 19);
            button->Invalidate(); // the maximize image follows the zoom state
        }
    }

    POINT Window::ToLogical(LPARAM lp) const noexcept {
        return {int(std::floor(GET_X_LPARAM(lp) / m_scale)), int(std::floor(GET_Y_LPARAM(lp) / m_scale))};
    }

    void Window::SetScale(float scale) {
        m_requestedScale = std::max(scale, 0.f);
        if (scale > 0)
            ApplyScale(scale);
        else if (m_hwnd)
            ApplyScale(GetDpiForWindow(m_hwnd) / 96.f);
    }

    // Re-renders everything at the new scale. The window keeps its center, or
    // moves where Windows suggests for a monitor with another DPI.
    void Window::ApplyScale(float scale, const RECT* suggested) {
        scale = std::clamp(scale, 0.5f, 4.f);
        if (scale == m_scale) return;
        m_scale = scale;
        m_icon = AppIcon(ToPhysical(16));
        StopAnimations();
        InvalidateBackground();
        if (!m_hwnd) return;
        RECT r{};
        GetWindowRect(m_hwnd, &r);
        const int w = PhysicalWidth(), h = PhysicalHeight();
        const POINT origin = suggested ? POINT{suggested->left, suggested->top}
                                       : POINT{(r.left + r.right - w) / 2, (r.top + r.bottom - h) / 2};
        SetWindowPos(m_hwnd, nullptr, origin.x, origin.y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        RenderFrame();
    }

    // ---------------------------------------------------------------------------
    // Appearance

    void Window::SetSkinBytes(std::span<const uint8_t> bytes) {
        m_skinBytes.assign(bytes.begin(), bytes.end());
        m_skin = DecodeImage(m_skinBytes);
        InvalidateBackground();
    }

    void Window::SetBackgroundMode(SkinBackgroundMode mode, RECT margins) {
        m_skinMode = mode;
        m_skinMargins = margins;
        InvalidateBackground();
    }

    void Window::SetTitle(std::wstring title) {
        m_title = std::move(title);
        if (m_hwnd) SetWindowTextW(m_hwnd, m_title.c_str());
        InvalidateBackground();
    }

    void Window::SetAeroEnabled(bool enabled) {
        m_aeroEnabled = enabled;
        ApplyAero(m_animator.IsActive() ? false : enabled);
    }

    void Window::ApplyAero(bool enabled) {
        if (!m_hwnd || m_cornerRadius == 1) return; // bare popup surfaces never blur
        ApplyBlurBehind(enabled,
                        {ToPhysical(6), ToPhysical(6), ToPhysical(float(m_width - 5)), ToPhysical(float(m_height - 5))},
                        m_cornerRadius ? ToPhysical(6) : 0);
    }

    void Window::ApplyBlurBehind(bool enabled, const RECT& bounds, int diameter) {
        BOOL composition = FALSE;
        if (!m_hwnd || FAILED(DwmIsCompositionEnabled(&composition)) || !composition) return;
        DWM_BLURBEHIND blur{};
        blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        blur.fEnable = enabled;
        blur.hRgnBlur = CreateRoundRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom, diameter, diameter);
        DwmEnableBlurBehindWindow(m_hwnd, &blur);
        if (blur.hRgnBlur) DeleteObject(blur.hRgnBlur);
    }

    void Window::SetWindowAlpha(int alpha) {
        m_windowAlpha = alpha == -1 ? 255 : std::clamp(alpha, 0, 255);
        SetAeroEnabled(m_windowAlpha < 255);
        InvalidateBackground();
    }

    WindowBackground Window::GetBackground() const {
        return {m_skinBytes, m_baseColor,   m_windowAlpha, m_cornerRadius,
                m_skinMode,  m_skinMargins, m_aeroEnabled, m_controlAnimationsEnabled};
    }

    void Window::SetBackground(const WindowBackground& background) {
        m_baseColor = background.baseColor;
        m_cornerRadius = background.cornerRadius;
        SetSkinBytes(background.skinBytes);
        SetBackgroundMode(background.mode, background.margins);
        SetWindowAlpha(background.windowAlpha);
        if (background.aeroEnabled) SetAeroEnabled(*background.aeroEnabled);
        if (background.controlAnimationsEnabled) SetControlAnimationsEnabled(*background.controlAnimationsEnabled);
    }

    // Widgets may take their colors from the background, so they repaint as well.
    void Window::InvalidateBackground() {
        m_sceneDirty = true;
        InvalidateTree(*m_root);
        InvalidateTree(*m_chrome);
        RequestFrame();
    }

    // ---------------------------------------------------------------------------
    // Modal loops

    void Window::SetWindowEnabled(bool enabled) {
        if (m_hwnd) EnableWindow(m_hwnd, enabled);
        if (!enabled) {
            CancelCapture();
            UpdateHover(nullptr);
        }
    }

    void Window::RunModalLoop(bool filterExternalInput) {
        const HWND target = m_hwnd;
        MSG message{};
        while (target && IsWindow(target)) {
            if (GetMessageW(&message, nullptr, 0, 0) <= 0) return; // WM_QUIT is consumed, not reposted
            const bool input = (message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) ||
                    (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST);
            if (filterExternalInput && input && message.hwnd != target) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    // ---------------------------------------------------------------------------
    // Message dispatch

    LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            window = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            if (!window) return FALSE;
            window->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        if (!window) return DefWindowProcW(hwnd, msg, wp, lp);
        if (msg == WM_NCDESTROY) {
            window->m_destroying = true;
            window->m_widgetTimers.clear(); // the timers die with the HWND
            window->StopAnimations();
            window->CancelCapture();
            window->UpdateHover(nullptr);
            window->SetFocusedWidget(nullptr);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            window->m_hwnd = nullptr;
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        return window->HandleMessage(msg, wp, lp);
    }

    LRESULT Window::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        // Copy the filter: a callback may replace itself.
        LRESULT handled = 0;
        const auto filter = OnWindowMessageFilter;
        if (filter && filter(msg, wp, lp, handled)) return handled;
        if (!m_hwnd) return 0; // the filter may have destroyed the window

        if (msg == WM_TIMER) {
            if (wp == reinterpret_cast<UINT_PTR>(this))
                AdvanceAnimations();
            else if (!m_tooltip.OnTimer(m_hwnd, wp))
                for (auto* widget : m_widgetTimers)
                    if (wp == reinterpret_cast<UINT_PTR>(widget)) {
                        if (const auto keep = widget->weak_from_this().lock()) keep->OnTimer();
                        break;
                    }
            return 0;
        }
        // A closing window ignores all input and further close requests.
        if (IsClosing() &&
            (msg == WM_CLOSE || msg == WM_SYSCOMMAND || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
             (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)))
            return 0;
        if (const auto focused = m_focused ? m_focused->weak_from_this().lock() : nullptr; focused &&
            focused->IsEffectivelyEnabled() && focused->IsEffectivelyVisible() &&
            ((msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_COMPOSITION) ||
             (msg >= WM_IME_SETCONTEXT && msg <= WM_IME_KEYUP)) &&
            focused->OnImeMessage(m_hwnd, msg, wp, lp, handled))
            return handled;

        switch (msg) {
            case WM_GETMINMAXINFO: {
                MONITORINFO monitor{sizeof(monitor)};
                if (!lp || !GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return 0;
                // Maximized windows cover the work area plus seven pixels on every side.
                RECT work = monitor.rcWork;
                OffsetRect(&work, -monitor.rcMonitor.left, -monitor.rcMonitor.top);
                auto& info = *reinterpret_cast<MINMAXINFO*>(lp);
                info.ptMaxSize = {work.right - work.left + ToPhysical(14), work.bottom - work.top + ToPhysical(14)};
                info.ptMaxPosition = {work.left - ToPhysical(7), work.top - ToPhysical(7)};
                const int buttons = int(m_captionButtons.size());
                const bool caption = m_captionMask != 0;
                info.ptMinTrackSize = {
                        ToPhysical(float(caption ? buttons * 28 +
                                                         ((m_captionMask & CaptionButtonMask::Close) ? 11 : 0) + 36
                                                 : 21)),
                        ToPhysical(caption ? 40.f : 20.f)};
                return 0;
            }
            case WM_NCCALCSIZE: return 0;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                BeginPaint(m_hwnd, &ps);
                if (!m_animator.IsActive() && !m_animator.IsOpeningPrepared() && m_frameDirty) RenderFrame();
                EndPaint(m_hwnd, &ps);
                return 0;
            }
            case WM_SHOWWINDOW:
                if (!wp)
                    StopAnimations();
                else if (!m_animator.IsActive() && !m_animator.IsOpeningPrepared()) {
                    SetAeroEnabled(m_aeroEnabled);
                    RenderFrame();
                }
                break;
            case WM_SIZE:
                if (wp == SIZE_MINIMIZED) {
                    StopAnimations();
                    return 0;
                }
                {
                    const int width = int(std::lround(LOWORD(lp) / m_scale)),
                              height = int(std::lround(HIWORD(lp) / m_scale));
                    if (m_width != width || m_height != height) StopAnimations();
                    m_width = width;
                    m_height = height;
                }
                LayoutCaption();
                InvalidateBackground();
                return 0;
            case WM_SETTEXT: {
                const LRESULT result = DefWindowProcW(m_hwnd, msg, wp, lp);
                m_title = lp ? reinterpret_cast<const wchar_t*>(lp) : L"";
                InvalidateBackground();
                return result;
            }
            case WM_CLOSE: CloseWindow(); return 0;
            case WM_DPICHANGED:
                if (m_requestedScale == 0) ApplyScale(LOWORD(wp) / 96.f, reinterpret_cast<const RECT*>(lp));
                return 0;
            case WM_ENABLE:
                if (!wp) {
                    CancelCapture();
                    UpdateHover(nullptr);
                }
                return 0;
            case WM_CANCELMODE: CancelCapture(); return 0;
            case WM_CAPTURECHANGED:
                // Another window took the mouse: the press can no longer complete.
                if (reinterpret_cast<HWND>(lp) != m_hwnd && m_pressed) CancelCapture();
                return 0;
            // Widget focus survives HWND focus changes; only SetFocusedWidget moves it.
            case WM_SETFOCUS: return 0;
            case WM_KILLFOCUS:
                HideTooltip();
                CancelCapture();
                return 0;
            case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
            case WM_NCLBUTTONDOWN:
                if (wp == HTCAPTION && !IsWindowVisible(m_hwnd)) return 0; // no move loop for hidden windows
                break;
            case WM_MOUSELEAVE:
                // During a press the capture keeps the pointer; hover resolves on release.
                if (!m_pressed) UpdateHover(nullptr);
                return 0;
            case WM_MOUSEMOVE: return HandleMouseMove(wp, lp);
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN: return HandleButtonDown(msg, lp);
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP: return HandleButtonUp(msg, lp);
            case WM_MOUSEWHEEL:
                if (HandleWheel(wp, lp)) return 0;
                break;
            case WM_SYSKEYDOWN:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_CHAR:
                if (HandleKey(msg, wp, lp)) return 0;
                break;
        }
        return m_hwnd ? DefWindowProcW(m_hwnd, msg, wp, lp) : 0;
    }

} // namespace exui
