#include "ui/exui/widgets/Slider.h"
#include "ui/exui/Window.h"
#include <algorithm>
#include <cmath>

namespace exui {
    Slider::Slider() { SetTabStop(true); }
    void Slider::SetStep(int value) noexcept { step = std::max(1, value); }

    void Slider::SetRange(int low, int high) {
        high = std::max(low, high);
        if (minimum == low && maximum == high) return;
        CancelInteraction();
        minimum = low; maximum = high;
        position = std::clamp(position, minimum, maximum);
        Invalidate();
    }
    void Slider::SetPosition(int value) {
        value = std::clamp(value, minimum, maximum);
        if (value == position) return;
        position = value;
        Invalidate();
    }
    bool Slider::Preview(int value) {
        value = std::clamp(value, minimum, maximum);
        if (value == position) return false;
        SetPosition(value);
        if (const auto changed = OnChanged) changed(value);
        return true;
    }
    void Slider::CancelInteraction() {
        wheel = 0;
        if (!dragging) return;
        dragging = false;
        m_state = WidgetState::Normal;
        Invalidate();
        Preview(beforeDrag);
    }

    Gdiplus::RectF Slider::GetThumbBounds() const {
        float width = (float) std::clamp(GetWidth(), 0, 14);
        float height = (float) std::clamp(GetHeight() - 2, 0, 22);
        double fraction = maximum == minimum ? 0 : ((double) position - minimum) / ((double) maximum - minimum);
        return {(float) (fraction * (GetWidth() - width)), (GetHeight() - height) / 2, width, height};
    }

    void Slider::MoveTo(int x) {
        auto thumb = GetThumbBounds();
        double travel = GetWidth() - thumb.Width;
        if (travel <= 0 || maximum == minimum) return;
        double fraction = std::clamp((x - grabOffset - thumb.Width / 2) / travel, 0.0, 1.0);
        double steps = std::round(fraction * ((double) maximum - minimum) / step);
        long long next = (long long) minimum + (long long) (steps * step);
        if (fraction >= 1) next = maximum;
        Preview((int) std::clamp(next, (long long) minimum, (long long) maximum));
    }

    void Slider::OnPaint(Gdiplus::Graphics& g) {
        using namespace Gdiplus;
        auto saved = g.Save();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        auto thumb = GetThumbBounds();
        RectF track(thumb.Width / 2, GetHeight() / 2.0f - 3, std::max(0.0f, GetWidth() - thumb.Width), 6);
        FillRoundedRect(g, track, 4, 0xC8FFFFFF);
        DrawRoundedRect(g, track, 4, 0x66003048);
        RectF fill = track; fill.Width = thumb.X;
        FillRoundedRect(g, fill, 4, 0xB04A9DC6);
        if (thumb.Width > 0 && thumb.Height > 0) {
            g.TranslateTransform(thumb.X, thumb.Y);
            DrawButtonFrame(g, thumb.Width, thumb.Height, (int) m_state,
                            GetWindow() ? GetWindow()->GetBaseColor() : RGB(112, 184, 216), IsTabFocused());
            Pen grip(Color(0x70004466));
            g.DrawLine(&grip, thumb.Width / 2, 6.0f, thumb.Width / 2, thumb.Height - 6);
        }
        g.Restore(saved);
    }
    void Slider::OnMouseEnter() { if (!dragging) { m_state = WidgetState::Hover; Invalidate(); } }
    void Slider::OnMouseLeave() { if (!dragging) { m_state = WidgetState::Normal; Invalidate(); } }
    void Slider::OnMouseCancel() { CancelInteraction(); }
    void Slider::OnFocusChanged(bool focused) {
        if (!focused) CancelInteraction();
        Widget::OnFocusChanged(focused);
    }
    bool Slider::OnMouseDown(int x, int y, UINT button) {
        if (button != VK_LBUTTON || !IsEffectivelyEnabled() || !IsEffectivelyVisible() || maximum == minimum) return false;
        beforeDrag = position; dragging = true;
        auto thumb = GetThumbBounds();
        grabOffset = thumb.Contains((float) x, (float) y) ? x - thumb.X - thumb.Width / 2 : 0;
        m_state = WidgetState::Pressed; Invalidate();
        MoveTo(x);
        return true;
    }
    bool Slider::OnMouseMove(int x, int, UINT) {
        if (!dragging) return false;
        MoveTo(x); return true;
    }
    bool Slider::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON || !dragging) return false;
        const auto keep = weak_from_this().lock();
        MoveTo(x);
        if (!dragging) return true;
        dragging = false;
        m_state = Contains(x, y) ? WidgetState::Hover : WidgetState::Normal;
        Invalidate();
        if (position != beforeDrag) if (const auto commit = OnCommit) commit(position);
        return true;
    }
    void Slider::StepBy(long long amount) {
        const auto keep = weak_from_this().lock();
        if (dragging) CancelInteraction();
        int next = (int) std::clamp((long long) position + amount, (long long) minimum, (long long) maximum);
        if (Preview(next)) if (const auto commit = OnCommit) commit(position);
    }
    bool Slider::OnKeyDown(UINT key, UINT) {
        if (!IsEffectivelyEnabled()) return false;
        if (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN && key != VK_PRIOR &&
            key != VK_NEXT && key != VK_HOME && key != VK_END) return false;
        const auto keep = weak_from_this().lock();
        CancelInteraction();
        long long page = std::max(1LL, ((long long) maximum - minimum) / 10);
        switch (key) {
            case VK_LEFT: case VK_DOWN: StepBy(-step); break;
            case VK_RIGHT: case VK_UP: StepBy(step); break;
            case VK_PRIOR: StepBy(page); break;
            case VK_NEXT: StepBy(-page); break;
            case VK_HOME: StepBy((long long) minimum - position); break;
            case VK_END: StepBy((long long) maximum - position); break;
            default: return false;
        }
        return true;
    }
    bool Slider::OnMouseWheel(int delta, int, int) {
        if (!IsFocused() || !IsEffectivelyEnabled() || dragging) return false;
        wheel += delta;
        int count = wheel / WHEEL_DELTA;
        wheel %= WHEEL_DELTA;
        if (count) StepBy((long long) count * step);
        return true;
    }
}
