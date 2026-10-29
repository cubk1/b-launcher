#pragma once
#include "ui/exui/Widget.h"
#include <functional>
#include <vector>

namespace exui {

    // A push button with a vector frame in the window's base color. Subclasses
    // replace the background and foreground (check boxes, image buttons).
    class Button : public Widget {
    public:
        Button();
        Button(int x, int y, int width, int height, std::wstring caption, bool visible = true);
        void SetBaseColor(COLORREF color) {
            m_baseColor = color;
            m_explicitBaseColor = true;
            Invalidate();
        }
        void SetRounded(bool rounded) {
            m_rounded = rounded;
            m_explicitRounded = true;
            Invalidate();
        }
        COLORREF GetBaseColor() const noexcept;
        bool IsRounded() const noexcept;
        void SetAutoTextColor(bool automatic) {
            m_autoTextColor = automatic;
            Invalidate();
        }
        Gdiplus::RectF GetTextBounds(Gdiplus::Graphics& g) const;
        std::function<void()> OnClick;
        void OnPaint(Gdiplus::Graphics& g) override;
        void OnMouseEnter() override;
        void OnMouseLeave() override;
        void OnFocusChanged(bool focused) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        void OnMouseCancel() override;
        void Activate() override;

    protected:
        virtual bool SupportsStateAnimation() const noexcept {
            return true;
        }
        // The look while the button is held down with the pointer inside it.
        virtual WidgetState PressedState() const noexcept {
            return WidgetState::Pressed;
        }
        bool Contains(int x, int y) const noexcept {
            return x >= 0 && y >= 0 && x < GetWidth() && y < GetHeight();
        }
        virtual void PaintBackground(Gdiplus::Graphics& g, Gdiplus::RectF& paintBounds);
        // Returns true if the derived hook handled focus decoration.
        virtual bool PaintForeground(Gdiplus::Graphics& g, const Gdiplus::RectF& paintBounds,
                                     Gdiplus::RectF& textBounds);
        int PaintState() const noexcept {
            return m_state == WidgetState::Normal && m_focusPaint ? 3 : int(m_state);
        }
        bool m_armed = false, m_rounded = true, m_autoTextColor = false, m_focusPaint = false;
        COLORREF m_baseColor = 0xFFFFFF;
        bool m_explicitBaseColor = false, m_explicitRounded = false;
    };

    enum class ImagePlacement : int { Left = 0, Right = 1, Above = 2 };
    enum class ImageScaleMode : int { Native = 0, Stretch = 1, Center = 2 };

    // A button with one image beside or above its caption. Without a valid image
    // it looks like a plain button.
    class ImageButton : public Button {
    public:
        ImageButton() = default;
        ImageButton(int x, int y, int width, int height, std::wstring caption = L"");
        void SetNormalImageBytes(std::span<const uint8_t> bytes);
        void SetSelected(bool selected);
        bool IsSelected() const noexcept {
            return m_selected;
        }
        bool HasImage() const noexcept {
            return m_image != nullptr;
        }
        void SetImagePlacement(ImagePlacement placement) {
            m_placement = placement;
            Invalidate();
        }
        void SetImageScaleMode(ImageScaleMode scale) {
            m_scale = scale;
            Invalidate();
        }
        void SetImagePadding(int x, int y) {
            m_paddingX = x;
            m_paddingY = y;
            Invalidate();
        }

    protected:
        void PaintBackground(Gdiplus::Graphics& g, Gdiplus::RectF& paintBounds) override;
        bool PaintForeground(Gdiplus::Graphics& g, const Gdiplus::RectF& paintBounds,
                             Gdiplus::RectF& textBounds) override;

    private:
        bool m_selected = false;
        ImagePlacement m_placement = ImagePlacement::Left;
        ImageScaleMode m_scale = ImageScaleMode::Center;
        int m_paddingX = 0, m_paddingY = 0;
        std::vector<uint8_t> m_imageBytes;
        bool m_imageAttempted = false;
        std::unique_ptr<Gdiplus::Bitmap> m_image;
    };
} // namespace exui
