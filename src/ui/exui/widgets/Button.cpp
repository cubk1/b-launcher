#include "ui/exui/widgets/Button.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/Window.h"
#include <cmath>

namespace exui {
    using namespace Gdiplus;
    Button::Button() {
        SetTabStop(true);
        SetActivationKind(ActivationKind::Button);
    }
    Button::Button(int x, int y, int w, int h, std::wstring caption, bool visible) : Button() {
        SetBounds(x, y, w, h);
        SetCaption(std::move(caption));
        SetVisible(visible);
    }
    COLORREF Button::GetBaseColor() const noexcept {
        return !m_explicitBaseColor && GetWindow() ? GetWindow()->GetBaseColor() : m_baseColor;
    }
    bool Button::IsRounded() const noexcept {
        return m_explicitRounded ? m_rounded : WindowIsRounded();
    }
    RectF Button::GetTextBounds(Graphics& g) const {
        auto font = widget_detail::MakeFont(*this, m_fontStyle);
        if (!font) return {};
        StringFormat format(StringFormatFlagsNoWrap);
        format.SetHotkeyPrefix(HotkeyPrefixShow);
        format.SetTrimming(StringTrimmingNone);
        const auto size = widget_detail::MeasureText(g, m_caption, *font,
                                                     RectF(0, 0, float(GetWidth()), font->GetHeight(&g)), format);
        const float pressed = m_state == WidgetState::Pressed ? 1.0f : 0.0f;
        return {(GetWidth() - size.Width) / 2 + 1 + pressed, (GetHeight() - size.Height) / 2 + pressed, size.Width,
                size.Height};
    }
    void Button::PaintBackground(Graphics& g, RectF&) {
        const bool focus = PaintState() == 3;
        if (IsRounded() && !focus) {
            DrawButtonFrame(g, float(GetWidth()), float(GetHeight()), int(m_state), GetBaseColor(), false);
            return;
        }
        // Square buttons, and the focused look (state 3) of either shape.
        const RectF r(0, 0, float(GetWidth() - 1), float(GetHeight() - 1));
        const auto saved = g.Save();
        if (IsRounded()) {
            GraphicsPath clip;
            RoundedRectPath(clip, RectF(0, 0, float(GetWidth()), float(GetHeight())), 4);
            g.SetClip(&clip, CombineModeIntersect);
        }
        FillRoundedRect(g, r, 3, 0xCCECECEC);
        const bool down = m_state == WidgetState::Pressed, hover = m_state == WidgetState::Hover;
        FillGradient(g, r,
                     ToArgb(GetBaseColor(),
                            down ? 153
                                    : hover ? 21
                                    : focus ? 51
                                            : 0),
                     ToArgb(GetBaseColor(),
                            down ? 21
                                    : hover || focus ? 153
                                                     : 102));
        g.Restore(saved);
        const ARGB outer = down || hover ? 0x46000000 : 0x50000000, inner = focus ? 0x3DFFFFFF : 0x33FFFFFF;
        if (IsRounded()) {
            DrawRoundedRect(g, r, 3, outer);
            DrawRoundedRect(g, Adjust(r, 1, 1, -2, -2), 3, inner);
        } else {
            DrawBorder(g, r, outer);
            DrawBorder(g, Adjust(r, 1, 1, -2, -2), inner);
        }
        Pen shadow(Color(down ? 0x0A000000U : hover ? 0x1E000000U : 0x19000000U));
        g.DrawLine(&shadow, 1.0f, float(GetHeight() - 1), float(GetWidth() - 3), float(GetHeight() - 1));
    }
    bool Button::PaintForeground(Graphics&, const RectF&, RectF&) {
        return false;
    }
    void Button::OnPaint(Graphics& g) {
        const auto saved = g.Save();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RectF paintBounds(0, 0, float(GetWidth()), float(GetHeight()));
        PaintBackground(g, paintBounds);
        bool customFocus = false;
        if (GetWidth() > 6 && GetHeight() > 6) {
            const auto content = g.Save();
            const auto paintSize = GetPaintSize();
            g.SetClip(RectF(3, 3, float(paintSize.cx - 6), float(GetHeight() - 6)), CombineModeIntersect);
            auto text = GetTextBounds(g);
            customFocus = PaintForeground(g, paintBounds, text);
            StringFormat format(StringFormatFlagsNoWrap);
            format.SetHotkeyPrefix(HotkeyPrefixShow);
            format.SetTrimming(StringTrimmingNone);
            widget_detail::DrawText(g, *this, text, format, m_fontStyle,
                                    m_autoTextColor ? widget_detail::ContrastTextColor(*this, m_textColor)
                                                    : m_textColor);
            g.Restore(content);
        }
        if (IsTabFocused() && !customFocus)
            DrawBorder(g, RectF(2, 2, float(GetWidth() - 5), float(GetHeight() - 5)), 4, 0x78000000);
        g.Restore(saved);
    }
    bool Button::OnMouseDown(int, int, UINT button) {
        if (button != VK_LBUTTON || !IsEffectivelyEnabled() || !IsEffectivelyVisible()) return false;
        if (SupportsStateAnimation()) BeginTransition(153);
        m_focusPaint = false;
        m_armed = true;
        m_state = PressedState();
        Invalidate();
        return true;
    }
    // While held, the button looks pressed only when the pointer is over it.
    bool Button::OnMouseMove(int x, int y, UINT) {
        if (!m_armed) return false;
        const auto state = Contains(x, y) ? PressedState() : WidgetState::Normal;
        if (state != m_state) {
            m_state = state;
            Invalidate();
        }
        return true;
    }
    // Visual only: the window decides whether the release completed a click.
    bool Button::OnMouseUp(int x, int y, UINT button) {
        if (button != VK_LBUTTON || !m_armed) return false;
        m_armed = false;
        if (SupportsStateAnimation()) BeginTransition(153);
        m_focusPaint = false;
        m_state = Contains(x, y) ? WidgetState::Hover : WidgetState::Normal;
        Invalidate();
        return true;
    }
    void Button::Activate() {
        if (IsEffectivelyEnabled() && IsEffectivelyVisible() && OnClick) OnClick();
    }
    void Button::OnMouseEnter() {
        if (SupportsStateAnimation()) BeginTransition(51);
        m_focusPaint = false;
        Widget::OnMouseEnter();
    }
    void Button::OnMouseLeave() {
        if (SupportsStateAnimation()) BeginTransition(51);
        m_focusPaint = false;
        Widget::OnMouseLeave();
    }
    void Button::OnFocusChanged(bool focused) {
        if (SupportsStateAnimation()) BeginTransition(focused ? 204 : 102);
        m_focusPaint = focused;
        m_state = WidgetState::Normal;
        Widget::OnFocusChanged(focused);
    }
    void Button::OnMouseCancel() {
        if (SupportsStateAnimation()) BeginTransition(51);
        m_armed = false;
        m_focusPaint = false;
        Widget::OnMouseCancel();
    }

    ImageButton::ImageButton(int x, int y, int w, int h, std::wstring caption)
        : Button(x, y, w, h, std::move(caption)) {}
    void ImageButton::SetNormalImageBytes(std::span<const uint8_t> bytes) {
        // Kept as bytes too: buttons may be set up before GDI+ starts.
        m_imageBytes.assign(bytes.begin(), bytes.end());
        m_image.reset();
        m_imageAttempted = DefaultFont() != nullptr;
        if (m_imageAttempted) m_image = DecodeImage(m_imageBytes);
        Invalidate();
    }
    void ImageButton::SetSelected(bool value) {
        if (m_selected != value) {
            m_selected = value;
            Invalidate();
        }
    }
    void ImageButton::PaintBackground(Graphics& g, RectF& bounds) {
        // 0x504C8C/CB0/CD4/CF8: {outerBgr, alpha, innerBgr, alpha, fillBgr, alpha1, alpha2}.
        const bool down = PaintState() == 2, hover = PaintState() == 1, selected = m_selected && PaintState() == 0;
        const BYTE first = down ? 10 : hover ? 100 : selected ? 80 : 0;
        const BYTE last = down ? 120 : hover ? 10 : selected ? 20 : 0;
        const auto saved = g.Save();
        if (IsRounded()) {
            GraphicsPath clip;
            RoundedRectPath(clip, bounds, 4);
            g.SetClip(&clip, CombineModeIntersect);
        }
        bounds = Adjust(bounds, 0, 0, -1, -1);
        FillGradient(g, bounds, ToArgb(0xFFFFFF, first), ToArgb(0xFFFFFF, last));
        g.Restore(saved);
        const bool framed = down || hover || selected;
        const ARGB outer = framed ? 0x50000000 : 0;
        const ARGB inner = framed ? (down ? 0x33FFFFFF : 0x51FFFFFF) : 0;
        if (IsRounded())
            DrawRoundedRect(g, bounds, 6, outer);
        else
            DrawBorder(g, bounds, outer);
        bounds = Adjust(bounds, 1, 1, -2, -2);
        if (IsRounded())
            DrawRoundedRect(g, bounds, 4, inner);
        else
            DrawBorder(g, bounds, inner);
    }
    bool ImageButton::PaintForeground(Graphics& g, const RectF& bounds, RectF& text) {
        if (!m_imageAttempted) {
            m_imageAttempted = true;
            m_image = DecodeImage(m_imageBytes);
        }
        if (!m_image) return true; // foreground hook still suppresses generic focus rectangle
        const float iw = float(m_image->GetWidth()), ih = float(m_image->GetHeight());
        const bool down = m_state == WidgetState::Pressed;
        RectF box;
        switch (m_placement) {
            case ImagePlacement::Right:
                box = RectF(bounds.Width - (iw + m_paddingX + 8), float(m_paddingY + 4), iw + 4,
                            bounds.Height - m_paddingY - 8);
                if (text.Width + box.Width >= bounds.Width - 12 - 1e-7f) {
                    text.X = 4 + float(down);
                    text.Width = bounds.Width - box.Width - 8;
                } else
                    text.X = text.X - box.Width + 12;
                break;
            case ImagePlacement::Above:
                text.Y = bounds.Height - text.Height - 4 - m_paddingY + float(down);
                box = RectF(float(m_paddingX + 4), float(m_paddingY + 4), bounds.Width - m_paddingX * 2 - 4,
                            bounds.Height - text.Height - 12 - m_paddingY * 2);
                break;
            default:
                box = RectF(float(m_paddingX + 4), float(m_paddingY + 4), iw + 4, bounds.Height - m_paddingY - 8);
                text.X = box.X + box.Width + float(down);
                break;
        }
        if (down) box = Adjust(box, 1, 1, 0, 0);
        if (box.Width <= 0 || box.Height <= 0) return true;
        const auto saved = g.Save();
        g.SetClip(box, CombineModeReplace);
        if (m_scale == ImageScaleMode::Stretch) {
            g.DrawImage(m_image.get(), box, 0, 0, iw, ih, UnitPixel);
        } else {
            float x = box.X, y = box.Y;
            if (m_scale == ImageScaleMode::Center) {
                x = std::floor(box.X + (box.Width - iw) / 2 + 0.5f);
                y = std::floor(box.Y + (box.Height - ih) / 2 + 0.5f);
            }
            g.DrawImage(m_image.get(), RectF(x, y, iw, ih), 0, 0, iw, ih, UnitPixel);
        }
        g.Restore(saved);
        return true;
    }
} // namespace exui
