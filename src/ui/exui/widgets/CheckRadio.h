#pragma once
#include "ui/exui/widgets/Button.h"

namespace exui {

    // Radio and check boxes are buttons with a sprite instead of a frame.
    // SetChecked only sets the state; clicking a radio button also unchecks the
    // radio buttons that share its parent.
    class RadioButton : public Button {
    public:
        RadioButton() = default;
        RadioButton(int x, int y, int width, int height, std::wstring caption);
        void SetChecked(bool checked, bool uncheckSiblings = false);
        bool IsChecked() const noexcept {
            return m_checked;
        }
        void Activate() override;

    protected:
        bool SupportsStateAnimation() const noexcept override {
            return false;
        }
        WidgetState PressedState() const noexcept override {
            return WidgetState::Hover;
        }
        void PaintBackground(Gdiplus::Graphics&, Gdiplus::RectF&) override {}
        bool PaintForeground(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds, Gdiplus::RectF& text) override;

    private:
        bool m_checked = false;
    };

    class CheckBox : public Button {
    public:
        CheckBox() = default;
        CheckBox(int x, int y, int width, int height, std::wstring caption);
        void SetChecked(bool checked);
        bool IsChecked() const noexcept {
            return m_checked;
        }
        void Activate() override;

    protected:
        void PaintBackground(Gdiplus::Graphics&, Gdiplus::RectF&) override {}
        bool PaintForeground(Gdiplus::Graphics& g, const Gdiplus::RectF& bounds, Gdiplus::RectF& text) override;

    private:
        bool m_checked = false;
    };
} // namespace exui
