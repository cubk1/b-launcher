#pragma once
#include "ui/exui/Widget.h"

namespace exui {

    // A caption button drawn from the frame atlas, cross-fading between its
    // normal, hover and pressed images. type is one CaptionButtonMask bit.
    class CaptionButton : public Widget {
    public:
        explicit CaptionButton(uint32_t type);
        uint32_t GetType() const noexcept {
            return m_type;
        }
        // Atlas slice of a button; state 1 hover, 2 pressed, anything else normal.
        static Gdiplus::Rect Slice(uint32_t type, int state, bool zoomed = false);
        static int Width(uint32_t type) noexcept;

        bool AcceptsPointerInput() const noexcept override {
            return true;
        }
        void OnPaint(Gdiplus::Graphics& g) override;
        void OnMouseEnter() override;
        void OnMouseLeave() override;
        void OnMouseCancel() override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        // Notifies OnCaptionButtonClicked, then closes, maximizes or minimizes the window.
        void Activate() override;

    private:
        void Show(WidgetState state);
        uint32_t m_type;
        bool m_armed = false;
    };

} // namespace exui
