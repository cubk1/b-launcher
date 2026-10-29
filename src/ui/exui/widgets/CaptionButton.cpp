#include "ui/exui/widgets/CaptionButton.h"
#include "ui/exui/Window.h"

namespace exui {
    using namespace Gdiplus;

    CaptionButton::CaptionButton(uint32_t type) : m_type(type) {}

    Rect CaptionButton::Slice(uint32_t type, int state, bool zoomed) {
        const int s = state == 1 ? 1 : state == 2 ? 2 : 0;
        if (type == CaptionButtonMask::Close) return {40 * s, 2, 39, 19};
        int x = 0;
        switch (type) {
            case CaptionButtonMask::Maximize: x = zoomed ? 207 : 120; break;
            case CaptionButtonMask::Minimize: x = 294; break;
            case CaptionButtonMask::Menu: x = 381; break;
            case CaptionButtonMask::Skin: x = 468; break;
            case CaptionButtonMask::Help: x = 555; break;
            case CaptionButtonMask::Pin: x = 642; break;
            default: return {};
        }
        return {x + s * 29, 2, 28, 19};
    }

    int CaptionButton::Width(uint32_t type) noexcept {
        return type == CaptionButtonMask::Close ? 39 : 28;
    }

    void CaptionButton::OnPaint(Graphics& g) {
        auto* atlas = SkinBitmap(Skin::FrameAtlas);
        const HWND hwnd = GetWindow() ? GetWindow()->GetHwnd() : nullptr;
        const Rect slice = Slice(m_type, int(m_state), hwnd && IsZoomed(hwnd));
        if (atlas)
            g.DrawImage(atlas, Rect(0, 0, GetWidth(), 19), slice.X, slice.Y, slice.Width, slice.Height, UnitPixel);
    }

    // Fades in the new image: from nothing back to normal, otherwise from 102,
    // or 153 to and from the pressed image.
    void CaptionButton::Show(WidgetState state) {
        if (state == m_state) return;
        const bool pressed = state == WidgetState::Pressed || m_state == WidgetState::Pressed;
        BeginTransition(state == WidgetState::Normal ? 0 : pressed ? 153 : 102);
        m_state = state;
        Invalidate();
    }

    void CaptionButton::OnMouseEnter() {
        Show(m_armed ? WidgetState::Pressed : WidgetState::Hover);
    }
    void CaptionButton::OnMouseLeave() {
        Show(WidgetState::Normal);
    }
    void CaptionButton::OnMouseCancel() {
        m_armed = false;
        Show(WidgetState::Normal);
    }

    bool CaptionButton::OnMouseDown(int, int, UINT button) {
        if (button != VK_LBUTTON) return false;
        m_armed = true;
        Show(WidgetState::Pressed);
        return true;
    }

    bool CaptionButton::OnMouseMove(int x, int y, UINT) {
        if (m_armed) Show(Contains(x, y) ? WidgetState::Pressed : WidgetState::Normal);
        return m_armed;
    }

    bool CaptionButton::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON || !m_armed) return false;
        m_armed = false;
        Show(Contains(x, y) ? WidgetState::Hover : WidgetState::Normal);
        return true;
    }

    void CaptionButton::Activate() {
        auto* window = GetWindow();
        if (!window) return;
        const HWND hwnd = window->GetHwnd();
        if (window->OnCaptionButtonClicked) window->OnCaptionButtonClicked(m_type);
        if (!IsWindow(hwnd)) return;
        if (m_type == CaptionButtonMask::Close)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (m_type == CaptionButtonMask::Maximize)
            PostMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
        else if (m_type == CaptionButtonMask::Minimize && !IsIconic(hwnd))
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }

} // namespace exui
