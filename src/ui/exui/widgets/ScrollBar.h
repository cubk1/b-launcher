#pragma once
#include "ui/exui/Widget.h"
#include <functional>

namespace exui {

    // A thin scroll bar: 10px across, 9px arrows and a thumb of at least 30px.
    // The track and arrows only show while the pointer is over the bar.
    class ScrollBar : public Widget {
    public:
        ScrollBar() = default;
        ScrollBar(int x, int y, int width, int height, bool vertical = true);
        bool IsVertical() const noexcept {
            return m_vertical;
        }
        bool AcceptsPointerInput() const noexcept override {
            return true;
        }
        void SetRange(int minPos, int maxPos, int pageSize);
        void SetPosition(int pos);
        int GetPosition() const noexcept {
            return m_pos;
        }
        int GetMaxPosition() const noexcept {
            return m_maxPos;
        }
        void SetLineStep(int amount) noexcept {
            m_lineStep = std::max(1, amount);
        }
        void SetAlwaysShowTrack(bool value);
        struct Geometry {
            Gdiplus::RectF decrement, increment, track, thumb;
        };
        Geometry GetGeometry() const;
        // Moves by amount and reports a change through OnScroll.
        void ScrollBy(int amount);
        std::function<void(int)> OnScroll;

        void OnPaint(Gdiplus::Graphics& g) override;
        void OnMouseEnter() override;
        void OnMouseLeave() override;
        void OnMouseCancel() override;
        bool OnMouseMove(int x, int y, UINT flags) override;
        bool OnMouseDown(int x, int y, UINT button) override;
        bool OnMouseUp(int x, int y, UINT button) override;

    protected:
        void OnTimer() override;

    private:
        enum Part { None, Decrement, Increment, Thumb, Track };
        Part HitPart(int x, int y) const;
        void SetParts(Part hover, Part pressed, bool inside);
        void StepArrow();
        bool m_vertical = true, m_alwaysShow = false;
        int m_minPos = 0, m_maxPos = 100, m_pageSize = 10, m_lineStep = 1, m_pos = 0;
        Part m_hoverPart = None, m_pressedPart = None;
        int m_thumbGrabOffset = 0;
    };

} // namespace exui
