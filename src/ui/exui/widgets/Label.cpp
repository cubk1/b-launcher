#include "ui/exui/widgets/Label.h"
#include "ui/exui/Window.h"
#include <shellapi.h>
#include <cmath>

namespace exui {
    using namespace Gdiplus;
    namespace widget_detail {
        ARGB ContrastTextColor(const Widget& widget, ARGB color) {
            if (const auto* window = widget.GetWindow()) {
                const POINT origin = widget.GetWindowOrigin();
                return window->ContrastTextColor(
                        RectF(float(origin.x), float(origin.y), float(widget.GetWidth()), float(widget.GetHeight())),
                        color);
            }
            return ToArgb(0, static_cast<uint8_t>(color >> 24));
        }
        std::unique_ptr<Font> MakeFont(const Widget& widget, int style) {
            FontFamily requested(widget.GetFontFamily().c_str());
            FontFamily fallback(DefaultFontFamily().c_str());
            auto font = std::make_unique<Font>(requested.GetLastStatus() == Ok ? &requested : &fallback,
                                               widget.GetFontSize(), style, UnitPixel);
            if (font->GetLastStatus() == Ok) return font;
            return DefaultFont() ? std::unique_ptr<Font>(DefaultFont()->Clone()) : nullptr;
        }
        float UnboundedTextWidth(const Widget& widget) {
            if (widget.GetCaption().empty()) return 0;
            Bitmap scratch(1, 1, PixelFormat32bppPARGB);
            Graphics g(&scratch);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            const auto font = MakeFont(widget, widget.GetFontStyle());
            if (!font) return 0;
            StringFormat format(StringFormatFlagsNoWrap);
            format.SetTrimming(StringTrimmingNone);
            RectF measured;
            if (g.MeasureString(widget.GetCaption().c_str(), -1, font.get(), PointF(0, 0), &format, &measured) != Ok)
                return 0;
            return measured.Width;
        }
        SizeF MeasureText(Graphics& g, std::wstring_view text, const Font& font, const RectF& bounds,
                          const StringFormat& format) {
            if (text.empty()) return {};
            RectF measured;
            const auto saved = g.Save();
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            const auto status =
                    g.MeasureString(text.data(), static_cast<INT>(text.size()), &font, bounds, &format, &measured);
            g.Restore(saved);
            if (status != Ok) return {};
            return {std::floor(measured.Width + 0.5f), std::floor(measured.Height + 0.5f)};
        }
        void DrawText(Graphics& g, const Widget& widget, const RectF& bounds, const StringFormat& format, int style,
                      ARGB color) {
            const auto& text = widget.GetCaption();
            if (text.empty() || bounds.Width <= 0 || bounds.Height <= 0) return;
            if (const auto font = MakeFont(widget, style))
                DrawGlowText(g, text, *font, bounds, format, color, widget.ResolveGlowColor(color),
                             widget.GetGlowScale());
        }
    } // namespace widget_detail

    Label::Label(int x, int y, int w, int h, std::wstring caption, ARGB color) {
        SetBounds(x, y, w, h);
        SetCaption(std::move(caption));
        SetTextColor(color);
    }
    void Label::SetAlignment(StringAlignment horizontal, StringAlignment vertical) {
        m_hAlign = horizontal;
        m_vAlign = vertical;
        Invalidate();
    }
    void Label::SetMultiline(bool value) {
        m_multiline = value;
        Invalidate();
    }
    void Label::SetBackgroundColor(ARGB color) {
        m_backgroundStart = m_backgroundEnd = color;
        Invalidate();
    }
    void Label::SetBackgroundGradient(ARGB start, ARGB end, LinearGradientMode mode) {
        m_backgroundStart = start;
        m_backgroundEnd = end;
        m_gradientMode = mode;
        Invalidate();
    }
    void Label::SetBorderColor(ARGB color) {
        m_borderColor = color;
        Invalidate();
    }
    void Label::SetTrimming(StringTrimming trimming) {
        m_trimming = trimming;
        Invalidate();
    }
    RectF Label::GetTextBounds(Graphics& g) const {
        RectF r(2, 2, float(GetWidth() - 4), float(GetHeight() - 4));
        if (m_multiline || r.Width <= 0 || r.Height <= 0) return r;
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        if (!font) return {};
        StringFormat format(StringFormatFlagsNoWrap);
        format.SetTrimming(m_trimming);
        const auto measured = widget_detail::MeasureText(g, m_caption, *font,
                                                         RectF(0, 0, float(GetWidth()), font->GetHeight(&g)), format);
        // Single lines are placed by their measured size rather than by the format.
        if (m_hAlign == StringAlignmentCenter)
            r.X += (r.Width - measured.Width) / 2;
        else if (m_hAlign == StringAlignmentFar)
            r.X += r.Width - measured.Width;
        if (m_vAlign == StringAlignmentCenter)
            r.Y += (r.Height - measured.Height) / 2;
        else if (m_vAlign == StringAlignmentFar)
            r.Y += r.Height - measured.Height - 2;
        else
            r.Y += 2;
        r.Height = measured.Height;
        return r;
    }
    // Automatic contrast keeps the alpha of the current color, a link's hover color included.
    ARGB Label::ResolveTextColor() const {
        return m_autoTextColor ? widget_detail::ContrastTextColor(*this, m_textColor) : m_textColor;
    }
    void Label::OnPaint(Graphics& g) {
        const auto saved = g.Save();
        const RectF whole(0, 0, float(GetWidth()), float(GetHeight()));
        if (m_borderColor) DrawBorder(g, whole, m_borderColor);
        if (m_backgroundStart || m_backgroundEnd)
            FillGradient(g, whole, m_backgroundStart, m_backgroundEnd, m_gradientMode);
        if (!m_caption.empty() && GetWidth() > 4 && GetHeight() > 4) {
            g.SetClip(RectF(2, 2, float(GetWidth() - 4), float(GetHeight() - 4)), CombineModeIntersect);
            const auto r = GetTextBounds(g);
            StringFormat format(m_multiline ? 0 : StringFormatFlagsNoWrap);
            // GetTextBounds already placed a single line; aligning it again would shift it twice.
            format.SetAlignment(m_multiline ? m_hAlign : StringAlignmentNear);
            format.SetTrimming(m_trimming);
            // The single-line y position was calculated manually; multi-line uses the full viewport.
            widget_detail::DrawText(g, *this, r, format, m_fontStyle, ResolveTextColor());
            g.Restore(saved);
            if (IsTabFocused()) DrawBorder(g, whole, 4, 0x78000000);
            return;
        }
        g.Restore(saved);
    }
    Hyperlink::Hyperlink() {
        SetActivationKind(ActivationKind::Hyperlink);
        SetTabStop(true);
    }
    Hyperlink::Hyperlink(int x, int y, int w, int h, std::wstring caption, std::wstring target, ARGB normal)
        : Label(x, y, w, h, std::move(caption), normal),
          m_targetUrl(std::move(target)),
          m_normalColor(normal) {
        SetActivationKind(ActivationKind::Hyperlink);
        SetTabStop(true);
    }
    void Hyperlink::SetColors(ARGB normal, ARGB hover, ARGB visited) {
        m_normalColor = normal;
        m_hoverColor = hover;
        m_visitedColor = visited;
        SetTextColor(m_state == WidgetState::Hover || m_state == WidgetState::Pressed ? hover
                             : m_visited                                              ? visited
                                                                                      : normal);
    }
    void Hyperlink::OnMouseEnter() {
        if (!IsEffectivelyEnabled()) return;
        BeginTransition(153);
        Widget::OnMouseEnter();
        m_fontStyle |= FontStyleUnderline;
        SetTextColor(m_hoverColor);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    void Hyperlink::OnMouseLeave() {
        BeginTransition(51);
        Widget::OnMouseLeave();
        m_fontStyle &= ~FontStyleUnderline;
        SetTextColor(m_visited ? m_visitedColor : m_normalColor);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    void Hyperlink::OnMouseCancel() {
        m_armed = false;
        OnMouseLeave();
    }
    bool Hyperlink::OnMouseMove(int, int, UINT) {
        if (!IsEffectivelyEnabled()) return false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return true;
    }
    bool Hyperlink::OnMouseDown(int, int, UINT button) {
        if (button != VK_LBUTTON || !IsEffectivelyEnabled() || !IsEffectivelyVisible()) return false;
        m_armed = true;
        m_state = WidgetState::Pressed;
        Invalidate();
        return true;
    }
    // Visual only: the window decides whether the release completed a click.
    bool Hyperlink::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON || !m_armed) return false;
        m_armed = false;
        m_state = Contains(x, y) ? WidgetState::Hover : WidgetState::Normal;
        Invalidate();
        return true;
    }
    void Hyperlink::Activate() {
        if (!IsEffectivelyEnabled() || !IsEffectivelyVisible()) return;
        m_visited = true;
        if (!m_targetUrl.empty()) {
            const std::wstring target = (m_mailMode ? L"mailto:" : L"") + m_targetUrl;
            if (OnOpenTarget)
                OnOpenTarget(target);
            else
                ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, L"", SW_SHOWNORMAL);
            SetTextColor(m_visitedColor);
        } // without a target the hover color stays until the pointer leaves
        Invalidate();
        if (OnClick) OnClick();
    }
} // namespace exui
