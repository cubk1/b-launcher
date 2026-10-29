#pragma once
#include "ui/exui/Widget.h"

namespace exui {

    // A frame with its title at (8,2), cut into the top edge at half the text height.
    class GroupBox : public Widget {
    public:
        GroupBox() = default;
        GroupBox(int x, int y, int width, int height, std::wstring caption);
        void SetTitleAlignment(Gdiplus::StringAlignment align) {
            m_align = align;
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
        void SetRounded(bool rounded) {
            m_rounded = rounded;
            m_explicitRounded = true;
            Invalidate();
        }
        Gdiplus::RectF GetTitleCutout(Gdiplus::Graphics& g) const;
        Gdiplus::RectF GetFrameBounds(Gdiplus::Graphics& g) const;
        void OnPaint(Gdiplus::Graphics& g) override;

    private:
        Gdiplus::StringAlignment m_align = Gdiplus::StringAlignmentNear;
        Gdiplus::ARGB m_background = 0, m_border = 0x50000000;
        bool m_rounded = true;
        bool m_explicitRounded = false;
        bool IsRounded() const noexcept {
            return m_explicitRounded ? m_rounded : WindowIsRounded();
        }
    };
} // namespace exui
