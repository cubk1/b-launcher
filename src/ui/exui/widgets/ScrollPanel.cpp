#include "ui/exui/widgets/ScrollPanel.h"
#include "ui/exui/widgets/ScrollBar.h"
#include <algorithm>

namespace exui {
    namespace {
        constexpr int BAR = 10, GAP = 2;
        constexpr int WHEEL_STEP = 60; // pixels per notch
    } // namespace

    ScrollPanel::ScrollPanel(int x, int y, int width, int height) {
        SetBounds(x, y, width, height);
        m_content = &Add<Panel>(0, 0, std::max(0, width - BAR - GAP), height);
        m_bar = &Add<ScrollBar>(width - BAR, 0, BAR, height, true);
        m_bar->SetVisible(false);
        m_bar->SetLineStep(20);
        m_bar->OnScroll = [this](int position) { ScrollTo(position); };
        m_contentHeight = height;
    }
    void ScrollPanel::SetContentHeight(int height) {
        m_contentHeight = std::max(0, height);
        Layout();
    }
    void ScrollPanel::ScrollTo(int position) {
        m_scroll = position;
        Layout();
    }
    bool ScrollPanel::OnMouseWheel(int delta, int, int) {
        if (m_contentHeight <= GetHeight()) return false;
        ScrollTo(m_scroll - delta * WHEEL_STEP / WHEEL_DELTA);
        return true;
    }
    void ScrollPanel::Layout() {
        const int most = std::max(0, m_contentHeight - GetHeight());
        m_scroll = std::clamp(m_scroll, 0, most);
        m_content->SetBounds(0, -m_scroll, std::max(0, GetWidth() - BAR - GAP), m_contentHeight);
        m_bar->SetVisible(most > 0);
        m_bar->SetBounds(GetWidth() - BAR, 0, BAR, GetHeight());
        m_bar->SetRange(0, most, GetHeight());
        m_bar->SetPosition(m_scroll);
    }
} // namespace exui
