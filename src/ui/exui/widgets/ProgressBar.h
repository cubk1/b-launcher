#pragma once
#include "ui/exui/Widget.h"

namespace exui {

    // A rounded (or square) bar, filled from the left or the bottom; no text.
    class ProgressBar : public Widget {
    public:
        ProgressBar() = default;
        ProgressBar(int x, int y, int width, int height);
        void SetRange(int minimum, int maximum);
        void SetPosition(int position);
        int GetMin() const noexcept {
            return m_min;
        }
        int GetMax() const noexcept {
            return m_max;
        }
        int GetPosition() const noexcept {
            return m_pos;
        }
        int GetFillExtent() const noexcept;
        void SetVertical(bool vertical) {
            m_vertical = vertical;
            Invalidate();
        }
        void SetRounded(bool rounded) {
            m_rounded = rounded;
            m_explicitRounded = true;
            Invalidate();
        }
        void SetFillColors(Gdiplus::ARGB start, Gdiplus::ARGB end) {
            m_start = start;
            m_end = end;
            Invalidate();
        }
        void SetBackgroundColor(Gdiplus::ARGB color) {
            m_background = color;
            Invalidate();
        }
        void SetBorderColor(Gdiplus::ARGB color) {
            m_border = color;
            Invalidate();
        }
        void OnPaint(Gdiplus::Graphics& g) override;

    private:
        int m_min = 0, m_max = 100, m_pos = 0;
        bool m_vertical = false, m_rounded = true;
        bool m_explicitRounded = false;
        bool IsRounded() const noexcept {
            return m_explicitRounded ? m_rounded : WindowIsRounded();
        }
        Gdiplus::ARGB m_start = 0xE60088FF, m_end = 0xE600E080;
        Gdiplus::ARGB m_background = 0xD2FFFFFF, m_border = 0x50000000;
    };
} // namespace exui
