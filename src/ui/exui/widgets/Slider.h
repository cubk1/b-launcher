#pragma once
#include "ui/exui/Widget.h"
#include <functional>

namespace exui {
    // An integer slider. Dragging previews values; release commits once.
    // Keyboard and focused wheel changes commit immediately. SetPosition is silent.
    class Slider : public Widget {
    public:
        Slider();
        void SetRange(int minimum, int maximum);
        void SetPosition(int value);
        int GetPosition() const noexcept { return position; }
        void SetStep(int value) noexcept;
        bool IsDragging() const noexcept { return dragging; }
        void CancelInteraction();
        Gdiplus::RectF GetThumbBounds() const;
        std::function<void(int)> OnChanged;
        std::function<void(int)> OnCommit;

        void OnPaint(Gdiplus::Graphics& g) override;
        void OnMouseEnter() override;
        void OnMouseLeave() override;
        void OnMouseCancel() override;
        void OnFocusChanged(bool focused) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseUp(int x, int y, UINT button) override;
        bool OnMouseWheel(int delta, int x, int y) override;
        bool OnKeyDown(UINT key, UINT flags) override;

    private:
        int minimum = 0, maximum = 100, position = 0, step = 1;
        int beforeDrag = 0, wheel = 0;
        float grabOffset = 0;
        bool dragging = false;
        bool Preview(int value);
        void MoveTo(int x);
        void StepBy(long long amount);
    };
}
