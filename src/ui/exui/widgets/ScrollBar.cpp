#include "ui/exui/widgets/ScrollBar.h"
#include <climits>

namespace exui {
    using namespace Gdiplus;

    namespace {
        int BoundedAdd(int value, int amount) {
            return int(std::clamp(static_cast<long long>(value) + amount, static_cast<long long>(INT_MIN),
                                  static_cast<long long>(INT_MAX)));
        }
    } // namespace

    ScrollBar::ScrollBar(int x, int y, int w, int h, bool vertical) : m_vertical(vertical) {
        SetBounds(x, y, w, h);
    }

    void ScrollBar::SetRange(int min, int max, int page) {
        if (min == m_minPos && std::max(min, max) == m_maxPos && std::max(1, page) == m_pageSize) return;
        m_minPos = min;
        m_maxPos = std::max(min, max);
        m_pageSize = std::max(1, page);
        m_pos = std::clamp(m_pos, m_minPos, m_maxPos);
        Invalidate();
    }

    void ScrollBar::SetPosition(int pos) {
        pos = std::clamp(pos, m_minPos, m_maxPos);
        if (m_pos == pos) return;
        m_pos = pos;
        Invalidate();
    }

    void ScrollBar::SetAlwaysShowTrack(bool value) {
        m_alwaysShow = value;
        Invalidate();
    }

    void ScrollBar::ScrollBy(int amount) {
        const int pos = std::clamp(BoundedAdd(m_pos, amount), m_minPos, m_maxPos);
        if (pos == m_pos) return;
        SetPosition(pos);
        if (const auto callback = OnScroll) callback(pos);
    }

    ScrollBar::Geometry ScrollBar::GetGeometry() const {
        const float length = float(m_vertical ? GetHeight() : GetWidth());
        const float track = std::max(0.f, length - 20);
        float thumb = 0;
        if (m_maxPos > m_minPos && track > 0) {
            const double range = double(m_maxPos) - m_minPos;
            thumb = std::max(30.f, float(int(track * track / (range + track))));
        }
        float position = 10;
        if (m_maxPos > m_minPos)
            position += float(int((double(m_pos) - m_minPos) * (track - thumb) / (double(m_maxPos) - m_minPos)));
        if (m_vertical)
            return {{0, 0, 10, 9}, {0, std::max(0.f, length - 9), 10, 9}, {1, 10, 8, track}, {1, position, 8, thumb}};
        return {{0, 0, 9, 10}, {std::max(0.f, length - 9), 0, 9, 10}, {10, 1, track, 8}, {position, 1, thumb, 8}};
    }

    ScrollBar::Part ScrollBar::HitPart(int x, int y) const {
        const auto geo = GetGeometry();
        const float fx = float(x), fy = float(y);
        if (geo.decrement.Contains(fx, fy)) return Decrement;
        if (geo.increment.Contains(fx, fy)) return Increment;
        if (geo.thumb.Contains(fx, fy)) return Thumb;
        if (geo.track.Contains(fx, fy)) return Track;
        return None;
    }

    void ScrollBar::OnPaint(Graphics& g) {
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        const auto geo = GetGeometry();
        const bool active = m_alwaysShow || m_state != WidgetState::Normal;
        if (active) {
            FillRoundedRect(g, geo.track, 6, 0x28000000);
            DrawRoundedRect(g, geo.track, 6, 0x1E000000);
            for (const Part part : {Decrement, Increment}) {
                const auto& r = part == Decrement ? geo.decrement : geo.increment;
                const bool lit = (m_pressedPart ? m_pressedPart : m_hoverPart) == part;
                if (lit) DrawRoundedRect(g, r, 4, 0xA0000000);
                // A triangle with a seven pixel base.
                const float apex = part == Decrement ? 2.f : 7.f, base = part == Decrement ? 5.f : 4.f;
                PointF p[3];
                if (m_vertical)
                    p[0] = {r.X + 4.5f, r.Y + apex}, p[1] = {r.X + 1, r.Y + base}, p[2] = {r.X + 8, r.Y + base};
                else
                    p[0] = {r.X + apex, r.Y + 4.5f}, p[1] = {r.X + base, r.Y + 1}, p[2] = {r.X + base, r.Y + 8};
                SolidBrush brush{Color(lit ? 0xA0000000 : 0x64000000)};
                g.FillPolygon(&brush, p, 3);
                Pen pen{Color(lit ? 0x8C000000 : 0x3C000000)};
                g.DrawPolygon(&pen, p, 3);
            }
        }
        if ((m_vertical ? geo.thumb.Height : geo.thumb.Width) > 0) {
            FillRoundedRect(g, geo.thumb, 6, active ? 0x6E000000 : 0x28000000);
            DrawRoundedRect(g, geo.thumb, 6, active ? 0x50000000 : 0x1E000000);
        }
    }

    // The track shows while the pointer is over the bar or holds a part of it.
    void ScrollBar::SetParts(Part hover, Part pressed, bool inside) {
        const auto state = pressed ? WidgetState::Pressed : inside ? WidgetState::Hover : WidgetState::Normal;
        if (hover == m_hoverPart && pressed == m_pressedPart && state == m_state) return;
        BeginTransition(51);
        m_hoverPart = hover;
        m_pressedPart = pressed;
        m_state = state;
        Invalidate();
    }

    void ScrollBar::OnMouseEnter() {
        SetParts(m_hoverPart, m_pressedPart, true);
    }

    void ScrollBar::OnMouseLeave() {
        if (!m_pressedPart) SetParts(None, None, false);
    }

    void ScrollBar::OnMouseCancel() {
        StopTimer();
        SetParts(None, None, false);
    }

    bool ScrollBar::OnMouseMove(int x, int y, UINT) {
        if (m_pressedPart == Decrement || m_pressedPart == Increment) return true;
        if (m_pressedPart == Thumb) {
            // Maps the grabbed pixel back to a position.
            const auto geo = GetGeometry();
            const double start = m_vertical ? geo.track.Y : geo.track.X;
            const double travel = m_vertical ? geo.track.Height - geo.thumb.Height : geo.track.Width - geo.thumb.Width;
            if (travel > 0 && m_maxPos > m_minPos) {
                const double pixel = double(m_vertical ? y : x) - m_thumbGrabOffset;
                const double range = double(m_maxPos) - m_minPos;
                const double position = pixel <= start ? m_minPos
                        : pixel >= start + travel      ? m_maxPos
                                                       : (pixel - start) * range / travel + m_minPos;
                ScrollBy(int(position) - m_pos);
            }
            return true;
        }
        SetParts(HitPart(x, y), None, true);
        return true;
    }

    // An arrow steps at once, then repeats every 60 ms while held. The track does nothing.
    bool ScrollBar::OnMouseDown(int x, int y, UINT button) {
        if (button != VK_LBUTTON) return false;
        const Part part = HitPart(x, y);
        SetParts(part, part, true);
        if (part == Thumb) {
            const auto geo = GetGeometry();
            m_thumbGrabOffset = (m_vertical ? y : x) - int(m_vertical ? geo.thumb.Y : geo.thumb.X);
        } else if (part == Decrement || part == Increment) {
            StepArrow();
            StartTimer(60);
        }
        return part != None;
    }

    bool ScrollBar::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON) return false;
        StopTimer();
        SetParts(Contains(x, y) ? HitPart(x, y) : None, None, Contains(x, y));
        return true;
    }

    void ScrollBar::OnTimer() {
        if (m_pressedPart != Decrement && m_pressedPart != Increment)
            StopTimer();
        else
            StepArrow();
    }

    void ScrollBar::StepArrow() {
        ScrollBy(m_pressedPart == Decrement ? -m_lineStep : m_lineStep);
        if (m_pos <= m_minPos || m_pos >= m_maxPos) StopTimer();
    }

} // namespace exui
