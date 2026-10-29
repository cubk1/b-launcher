#include "ui/exui/widgets/ProgressBar.h"
#include "ui/exui/Window.h"
#include <cmath>

namespace exui {
    using namespace Gdiplus;
    namespace {
        void ClipInterior(Graphics& g, const RectF& r, bool rounded) {
            if (!rounded) {
                g.SetClip(r, CombineModeIntersect);
                return;
            }
            const HRGN handle =
                    CreateRoundRectRgn(static_cast<int>(r.X), static_cast<int>(r.Y),
                                       static_cast<int>(r.X + r.Width + 1), static_cast<int>(r.Y + r.Height + 1), 3, 3);
            if (handle) {
                Region region(handle);
                g.SetClip(&region, CombineModeIntersect);
                DeleteObject(handle);
            } else
                g.SetClip(Rect(0, 0, 0, 0), CombineModeIntersect);
        }
    }
    ProgressBar::ProgressBar(int x, int y, int w, int h) {
        SetBounds(x, y, w, h);
    }
    void ProgressBar::SetRange(int minimum, int maximum) {
        m_min = minimum;
        m_max = maximum;
        Invalidate();
    }
    void ProgressBar::SetPosition(int position) {
        m_pos = position;
        Invalidate();
    }
    // The filled part of the inner area, which is 4px shorter than the bar.
    int ProgressBar::GetFillExtent() const noexcept {
        if (m_max == m_min) return 0;
        const double fraction = std::clamp((double(m_pos) - m_min) / (double(m_max) - m_min), 0.0, 1.0);
        return int(std::floor((m_vertical ? GetHeight() - 4 : GetWidth() - 4) * fraction + 0.5));
    }
    void ProgressBar::OnPaint(Graphics& g) {
        if (GetWidth() <= 0 || GetHeight() <= 0) return;
        const auto saved = g.Save();
        const RectF whole(0, 0, float(GetWidth()), float(GetHeight()));
        const RectF inner(2, 2, float(GetWidth() - 4), float(GetHeight() - 4));
        if (m_background) {
            if (IsRounded())
                FillRoundedRect(g, whole, 6, m_background);
            else {
                SolidBrush brush{Color(m_background)};
                g.FillRectangle(&brush, whole);
            }
        }
        {
            const auto clip = g.Save();
            ClipInterior(g, inner, IsRounded());
            FillGradient(g, whole, 0x00000000, 0x33000000,
                         m_vertical ? LinearGradientModeHorizontal : LinearGradientModeVertical);
            g.Restore(clip);
        }
        RectF fill = inner;
        const int extent = GetFillExtent();
        if (m_vertical) {
            fill.Height = float(extent);
            fill.Y = float(GetHeight() - extent - 2);
        } else
            fill.Width = float(extent);
        {
            const auto clip = g.Save();
            ClipInterior(g, fill, IsRounded());
            FillGradient(g, whole, m_vertical ? m_end : m_start, m_vertical ? m_start : m_end,
                         m_vertical ? LinearGradientModeVertical : LinearGradientModeHorizontal);
            FillGradient(g, whole, 0x00000000, 0x51000000,
                         m_vertical ? LinearGradientModeHorizontal : LinearGradientModeVertical);
            g.Restore(clip);
        }
        if (IsRounded())
            DrawRoundedRect(g, whole, 6, m_border);
        else
            DrawBorder(g, whole, m_border);
        g.Restore(saved);
    }
} // namespace exui
