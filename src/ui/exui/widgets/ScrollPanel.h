#pragma once
#include "ui/exui/Widget.h"
#include "ui/exui/Layout.h"
#include <optional>

namespace exui {

    class ScrollBar;

    // A view onto a taller panel: the wheel and a scroll bar along the right edge move the panel up and down
    // behind the view, which clips it. Its own area takes the pointer, so the wheel works between the controls.
    class ScrollPanel : public Widget {
    public:
        ScrollPanel(int x, int y, int width, int height);
        // Where the controls go, as wide as the view without the bar.
        Panel& GetContent() noexcept {
            return *m_content;
        }
        // The bar shows while the content is taller than the view. Setting an
        // explicit height replaces any automatic content layout.
        void SetContentHeight(int height);
        void SetContentLayout(exui::Layout layout, RECT margins = {});
        int GetContentHeight() const noexcept {
            return m_contentHeight;
        }
        void ScrollTo(int position);
        int GetScrollPosition() const noexcept {
            return m_scroll;
        }
        bool OnMouseWheel(int delta, int x, int y) override;
        void OnPaint(Gdiplus::Graphics&) override;

    private:
        void Layout();
        Panel* m_content = nullptr;
        ScrollBar* m_bar = nullptr;
        int m_contentHeight = 0, m_scroll = 0;
        std::optional<exui::Layout> m_layout;
        RECT m_margins{};
    };
} // namespace exui
