#include "ui/exui/widgets/GroupBox.h"
#include "ui/exui/Window.h"
#include "ui/exui/widgets/Label.h"

namespace exui {
    using namespace Gdiplus;
    GroupBox::GroupBox(int x, int y, int w, int h, std::wstring caption) {
        SetBounds(x, y, w, h);
        SetCaption(std::move(caption));
    }
    RectF GroupBox::GetTitleCutout(Graphics& g) const {
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        if (!font) return {};
        StringFormat format(StringFormatFlagsNoWrap);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        const auto size = widget_detail::MeasureText(g, m_caption, *font,
                                                     RectF(8, 0, float(GetWidth() - 16), font->GetHeight(&g)), format);
        float x = 8;
        if (m_align == StringAlignmentCenter)
            x = (GetWidth() - size.Width) / 2;
        else if (m_align == StringAlignmentFar)
            x = GetWidth() - 10 - size.Width;
        return {x, 2, size.Width - 1, size.Height};
    }
    RectF GroupBox::GetFrameBounds(Graphics& g) const {
        const auto title = GetTitleCutout(g);
        const int half = static_cast<int>(title.Height) / 2;
        return {1, float(half + 1), float(GetWidth() - 2), float(GetHeight() - 2 - half)};
    }
    void GroupBox::OnPaint(Graphics& g) {
        const auto saved = g.Save();
        const auto frame = GetFrameBounds(g);
        auto title = GetTitleCutout(g);
        if (frame.Width > 0 && frame.Height > 0) {
            GraphicsPath path;
            RoundedRectPath(path, frame, 6);
            if (m_background) {
                SolidBrush brush{Color(m_background)};
                if (IsRounded())
                    g.FillPath(&brush, &path);
                else
                    g.FillRectangle(&brush, frame);
            }
            g.SetClip(frame, CombineModeIntersect);
            if (title.Width > 0 && title.Height > 0) g.SetClip(title, CombineModeExclude);
            if (IsRounded()) {
                Pen border{Color(m_border), 1};
                g.DrawPath(&border, &path);
            } else
                DrawBorder(g, frame, m_border);
        }
        g.Restore(saved);
        title.Width += 5;
        StringFormat format(StringFormatFlagsNoWrap);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        widget_detail::DrawText(g, *this, title, format, m_fontStyle, m_textColor);
    }
} // namespace exui
