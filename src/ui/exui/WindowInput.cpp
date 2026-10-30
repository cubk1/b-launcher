#include "ui/exui/Window.h"
#include <windowsx.h>
#include <cwctype>

namespace exui {
    using namespace Gdiplus;

    namespace {
        bool Available(const Widget* w) {
            return w && w->IsEffectivelyVisible() && w->IsEffectivelyEnabled();
        }
        bool CanFocus(const Widget* w) {
            return Available(w) && w->IsTabStop();
        }
        bool Clickable(const Widget* w) {
            return w &&
                    (w->GetActivationKind() == ActivationKind::Button ||
                     w->GetActivationKind() == ActivationKind::Hyperlink);
        }
        std::shared_ptr<Widget> Hold(Widget* widget) {
            return widget ? widget->weak_from_this().lock() : nullptr;
        }
        POINT Local(const Widget& widget, int x, int y) {
            const POINT origin = widget.GetWindowOrigin();
            return {x - origin.x, y - origin.y};
        }

        // Depth first, parents before children: paint order and Tab order.
        void Collect(Widget& widget, std::vector<Widget*>& out) {
            for (const auto& child : widget.GetChildren()) {
                out.push_back(child.get());
                Collect(*child, out);
            }
        }

        // The topmost enabled descendant of parent at (x, y), in parent coordinates.
        Widget* HitTest(const Widget& parent, int x, int y) {
            const auto& children = parent.GetChildren();
            for (auto i = children.rbegin(); i != children.rend(); ++i) {
                Widget& child = **i;
                const int cx = x - child.GetX(), cy = y - child.GetY();
                if (!child.IsVisible() || !child.IsEnabled() || !child.Contains(cx, cy)) continue;
                if (auto* inner = HitTest(child, cx, cy)) return inner;
                if (!child.IsTransparentToPointer() && child.HitsPoint(cx, cy)) return &child;
            }
            return nullptr;
        }
    } // namespace

    // ---------------------------------------------------------------------------
    // Focus, hover and capture

    void Window::SetFocusedWidget(Widget* widget, bool fromTab) {
        const auto keep = Hold(widget);
        if (widget && (!keep || widget->GetWindow() != this || !CanFocus(widget))) return;
        if (m_focused == widget) return; // the Tab cue only clears when the focus moves
        if (auto* oldTab = m_tabFocused) {
            m_tabFocused = nullptr;
            oldTab->Invalidate();
        }
        const auto old = Hold(m_focused);
        m_focused = widget;
        if (old) old->OnFocusChanged(false);
        if (keep && m_focused == widget) {
            m_tabFocused = fromTab ? widget : nullptr;
            keep->OnFocusChanged(true);
        }
    }

    Widget* Window::HitTestWidget(int x, int y) const {
        if (m_animator.IsOpening() && !m_animator.IncludesOpeningWidgets()) return nullptr;
        if (auto* caption = HitTest(*m_chrome, x, y)) return caption;
        return HitTest(*m_root, x, y);
    }

    void Window::ReconcileInputState() {
        if (m_pressed && !Available(m_pressed)) CancelCapture();
        if (m_hovered && !Available(m_hovered)) UpdateHover(nullptr);
        if (m_focused && !CanFocus(m_focused)) SetFocusedWidget(nullptr);
    }

    void Window::StartWidgetTimer(Widget* widget, UINT intervalMs) {
        if (!m_hwnd || !SetTimer(m_hwnd, reinterpret_cast<UINT_PTR>(widget), intervalMs, nullptr)) return;
        if (std::find(m_widgetTimers.begin(), m_widgetTimers.end(), widget) == m_widgetTimers.end())
            m_widgetTimers.push_back(widget);
    }

    void Window::StopWidgetTimer(Widget* widget) {
        const auto at = std::find(m_widgetTimers.begin(), m_widgetTimers.end(), widget);
        if (at == m_widgetTimers.end()) return;
        m_widgetTimers.erase(at);
        if (m_hwnd) KillTimer(m_hwnd, reinterpret_cast<UINT_PTR>(widget));
    }

    void Window::ForgetWidget(Widget* widget) {
        StopWidgetTimer(widget);
        if (m_pressed == widget) {
            m_pressed = nullptr;
            if (m_hwnd && GetCapture() == m_hwnd) ReleaseCapture();
        }
        if (m_hovered == widget) {
            m_hovered = nullptr;
            HideTooltip();
        }
        if (m_tabFocused == widget) m_tabFocused = nullptr;
        if (m_focused == widget) m_focused = nullptr;
    }

    void Window::UpdateHover(Widget* widget) {
        if (m_hovered == widget) return;
        HideTooltip();
        const auto old = Hold(m_hovered), next = Hold(widget);
        m_hovered = widget;
        if (old) old->OnMouseLeave();
        if (next && m_hovered == widget) next->OnMouseEnter();
    }

    void Window::CancelCapture() {
        const auto pressed = Hold(m_pressed);
        m_pressed = nullptr;
        if (m_hwnd && GetCapture() == m_hwnd) ReleaseCapture();
        if (pressed) pressed->OnMouseCancel();
    }

    void Window::HideTooltip() {
        m_tooltip.Hide(m_hwnd);
    }

    void Window::RefreshTooltip(Widget* widget) {
        if (widget != m_hovered) return;
        if (!Available(widget) || widget->GetTooltipText().empty() || m_pressed || IsWindowAnimating()) {
            HideTooltip();
            return;
        }
        m_tooltip.Schedule(m_hwnd, widget, widget->GetTooltipText());
    }

    void Window::MoveTabFocus(bool backward) {
        std::vector<Widget*> all, order;
        Collect(*m_root, all);
        for (auto* widget : all)
            if (CanFocus(widget)) order.push_back(widget);
        if (order.empty()) return;
        const auto current = std::find(order.begin(), order.end(), m_focused);
        size_t index = backward ? order.size() - 1 : 0;
        if (current != order.end()) {
            const size_t at = size_t(current - order.begin());
            index = backward ? (at + order.size() - 1) % order.size() : (at + 1) % order.size();
        }
        SetFocusedWidget(order[index], true);
    }

    // Reports a completed activation of a button or link to the window's callbacks.
    void Window::ActivateWidget(Widget* widget) {
        const auto keep = Hold(widget);
        if (!keep || !Available(widget)) return;
        if (widget->GetActivationKind() == ActivationKind::Hyperlink) {
            if (OnHyperlinkClicked) OnHyperlinkClicked(widget);
        } else if (widget->GetActivationKind() == ActivationKind::Button) {
            if (OnButtonClicked) OnButtonClicked(widget);
        }
    }

    // ---------------------------------------------------------------------------
    // Pointer

    LRESULT Window::HandleMouseMove(WPARAM wp, LPARAM lp) {
        if (!IsWindowEnabled(m_hwnd)) return 0;
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_hwnd, 0};
        TrackMouseEvent(&track);
        const auto [x, y] = ToLogical(lp);
        // While a widget holds the press only it sees movement; hover waits for the release.
        if (const auto pressed = Hold(m_pressed)) {
            const auto local = Local(*pressed, x, y);
            pressed->OnMouseMove(local.x, local.y, UINT(wp));
            return 0;
        }
        auto* target = HitTestWidget(x, y);
        if (!Available(target)) target = nullptr;
        UpdateHover(target);
        if (const auto receiver = Hold(target)) {
            const auto local = Local(*receiver, x, y);
            receiver->OnMouseMove(local.x, local.y, UINT(wp));
        }
        RefreshTooltip(m_hovered);
        return 0;
    }

    LRESULT Window::HandleButtonDown(UINT msg, LPARAM lp) {
        HideTooltip();
        if (!IsWindowEnabled(m_hwnd)) return 0;
        const auto [x, y] = ToLogical(lp);
        const UINT button = msg == WM_RBUTTONDOWN ? VK_RBUTTON : msg == WM_MBUTTONDOWN ? VK_MBUTTON : VK_LBUTTON;
        const bool doubleClick = msg == WM_LBUTTONDBLCLK;
        auto deliver = [&](Widget& widget) {
            const auto local = Local(widget, x, y);
            if (doubleClick)
                widget.OnMouseDoubleClick(local.x, local.y, button);
            else
                widget.OnMouseDown(local.x, local.y, button);
        };
        if (const auto pressed = Hold(m_pressed)) {
            deliver(*pressed);
            return 0;
        }
        const auto target = Hold(HitTestWidget(x, y));
        if (button == VK_LBUTTON && (!target || !target->AcceptsPointerInput())) {
            // Empty areas and decorative widgets drag the window.
            SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        if (!Available(target.get())) return 0;
        if (button != VK_LBUTTON && Clickable(target.get())) return 0; // buttons only take the left button
        SetFocus(m_hwnd);
        if (button == VK_LBUTTON) {
            UpdateHover(target.get());
            if (CanFocus(target.get())) SetFocusedWidget(target.get());
            m_pressed = target.get();
            SetCapture(m_hwnd);
        }
        deliver(*target);
        return 0;
    }

    LRESULT Window::HandleButtonUp(UINT msg, LPARAM lp) {
        const auto [x, y] = ToLogical(lp);
        if (msg != WM_LBUTTONUP) {
            // Right and middle releases reach other widgets and never end a left-button press.
            const UINT button = msg == WM_RBUTTONUP ? VK_RBUTTON : VK_MBUTTON;
            const auto target = Hold(m_pressed ? m_pressed : HitTestWidget(x, y));
            if (Available(target.get()) && !Clickable(target.get())) {
                const auto local = Local(*target, x, y);
                target->OnMouseUp(local.x, local.y, button);
            }
            return 0;
        }
        const HWND hwnd = m_hwnd;
        const auto pressed = Hold(m_pressed);
        if (!pressed) return 0; // a release without a press does nothing
        m_pressed = nullptr;
        if (GetCapture() == hwnd) ReleaseCapture();
        const auto local = Local(*pressed, x, y);
        pressed->OnMouseUp(local.x, local.y, VK_LBUTTON);
        if (m_hwnd != hwnd) return 0;
        // A click needs the press and the release on the same widget.
        const auto target = Hold(HitTestWidget(x, y));
        if (target == pressed && Available(pressed.get())) {
            pressed->Activate();
            ActivateWidget(pressed.get());
        }
        if (m_hwnd == hwnd) UpdateHover(Available(target.get()) ? target.get() : nullptr);
        return 0;
    }

    // The wheel scrolls the widget under the pointer, or the nearest parent that scrolls.
    bool Window::HandleWheel(WPARAM wp, LPARAM lp) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(m_hwnd, &p);
        p = {int(std::floor(p.x / m_scale)), int(std::floor(p.y / m_scale))};
        for (auto target = Hold(HitTestWidget(p.x, p.y)); target; target = Hold(target->GetParent())) {
            const auto local = Local(*target, p.x, p.y);
            if (target->IsEffectivelyEnabled() && target->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp), local.x, local.y))
                return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // Keyboard

    bool Window::HandleKey(UINT msg, WPARAM wp, LPARAM lp) {
        const auto focused = Hold(m_focused);
        const bool focusedAvailable = Available(focused.get());
        switch (msg) {
            case WM_SYSKEYDOWN: {
                // Alt+mnemonic activates the first available button or link with that key.
                const auto key = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(wp)));
                if (!key) return false;
                std::vector<Widget*> all;
                Collect(*m_root, all);
                for (auto* widget : all) {
                    if (!Clickable(widget) || !Available(widget) ||
                        Widget::CaptionMnemonic(widget->GetCaption()) != key)
                        continue;
                    const auto target = Hold(widget);
                    target->Activate();
                    ActivateWidget(target.get());
                    return true;
                }
                return false;
            }
            case WM_KEYDOWN: {
                // The focused widget, then its parents, see every key before the window does.
                if (focusedAvailable)
                    for (auto target = focused; target; target = Hold(target->GetParent()))
                        if (target->OnKeyDown(UINT(wp), UINT(lp))) {
                            if (!m_hwnd || m_focused) return true;
                            break; // Tab still moves focus when the handler cleared it
                        }
                if (!m_hwnd) return true;
                if (wp == VK_TAB) {
                    MoveTabFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0);
                    return true;
                }
                const auto current = Hold(m_focused); // the handler may have moved the focus
                if ((wp == VK_RETURN || wp == VK_SPACE) && Available(current.get()) && Clickable(current.get())) {
                    current->Activate();
                    ActivateWidget(current.get());
                    return true;
                }
                return false;
            }
            case WM_KEYUP: return focusedAvailable && focused->OnKeyUp(UINT(wp), UINT(lp));
            case WM_CHAR:
                if (!focusedAvailable) return false;
                if ((wp == '\r' || wp == ' ') && Clickable(focused.get())) return true;
                return focused->OnChar(static_cast<wchar_t>(wp));
        }
        return false;
    }

} // namespace exui
