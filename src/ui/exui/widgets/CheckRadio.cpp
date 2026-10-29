#include "ui/exui/widgets/CheckRadio.h"
#include "ui/exui/Window.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/Skins.h"
#include <cmath>

namespace exui {
    using namespace Gdiplus;
    namespace {
        bool DrawChoice(Graphics& g, const Widget& widget, bool checked, const RectF& bounds, RectF& text,
                        Skin sprites) {
            auto* atlas = SkinBitmap(sprites);
            const RectF iconBox(4, 3, 20, bounds.Height - 8);
            text.X = 24;
            if (widget.GetState() == WidgetState::Pressed) text.Y -= 1; // no pressed offset for the text
            const auto saved = g.Save();
            if (atlas && iconBox.Height > 0) {
                const bool hot = widget.GetState() != WidgetState::Normal;
                const int sourceX = (checked ? 34 : 0) + (hot ? 17 : 0);
                // Cropped first, so the neighbouring sprites cannot bleed in.
                std::unique_ptr<Bitmap> icon(atlas->Clone(sourceX, 0, 17, 17, PixelFormat32bppPARGB));
                g.SetClip(iconBox, CombineModeReplace);
                const float x = std::floor(iconBox.X + (iconBox.Width - 17) / 2 + 0.5f);
                const float y = std::floor(iconBox.Y + (iconBox.Height - 17) / 2 + 0.5f);
                if (icon) g.DrawImage(icon.get(), RectF(x, y, 17, 17), 0, 0, 17, 17, UnitPixel);
            }
            // The focus rectangle may extend past the button's text clip.
            g.ResetClip();
            if (widget.IsTabFocused()) {
                auto focus = Adjust(text, -1, -2, 2, 3);
                if (focus.X + focus.Width > bounds.Width - 8 + 1e-7f) focus.Width = bounds.Width - focus.X - 2;
                DrawBorder(g, focus, 4, 0x78000000);
            }
            g.Restore(saved);
            return true;
        }
    }
    RadioButton::RadioButton(int x, int y, int w, int h, std::wstring caption)
        : Button(x, y, w, h, std::move(caption)) {}
    void RadioButton::SetChecked(bool checked, bool uncheckSiblings) {
        if (m_checked == checked) return;
        if (checked && uncheckSiblings && GetParent())
            for (const auto& sibling : GetParent()->GetChildren())
                if (auto* radio = dynamic_cast<RadioButton*>(sibling.get()); radio && radio != this)
                    radio->SetChecked(false);
        m_checked = checked;
        Invalidate();
    }
    bool RadioButton::PaintForeground(Graphics& g, const RectF& bounds, RectF& text) {
        return DrawChoice(g, *this, m_checked, bounds, text, Skin::RadioSprites);
    }
    void RadioButton::Activate() {
        if (!IsEffectivelyEnabled() || !IsEffectivelyVisible()) return;
        SetChecked(true, true);
        Button::Activate();
    }
    CheckBox::CheckBox(int x, int y, int w, int h, std::wstring caption) : Button(x, y, w, h, std::move(caption)) {}
    void CheckBox::SetChecked(bool checked) {
        m_checked = checked;
        FinishTransition();
        Invalidate();
    }
    bool CheckBox::PaintForeground(Graphics& g, const RectF& bounds, RectF& text) {
        return DrawChoice(g, *this, m_checked, bounds, text, Skin::CheckSprites);
    }
    void CheckBox::Activate() {
        if (!IsEffectivelyEnabled() || !IsEffectivelyVisible()) return;
        SetChecked(!m_checked);
        Button::Activate();
    }
} // namespace exui
