#pragma once

#include "ui/exui/Widget.h"
#include <vector>

namespace exui {

    // Rows and columns of controls in one parent's coordinate space. A positive
    // extent reserves pixels along the parent's axis; zero shares the remaining space,
    // and Auto measures a container from its children. A cross extent fixes the other axis.
    // Controls remain owned by their form, and must outlive the layout.
    class Layout {
    public:
        static constexpr int Auto = -1;
        enum class Align { Start, Center, End };

        Layout(Widget& control, int extent = 0, int crossExtent = 0, Align align = Align::Center);
        static Layout Row(std::vector<Layout> children, int gap = 8, int extent = 0);
        static Layout Column(std::vector<Layout> children, int gap = 8, int extent = 0);
        // The frame and its contents stay in the same widget parent.
        static Layout Frame(Widget& frame, Layout content, RECT padding = {});
        Gdiplus::Size Measure() const;
        void Apply(Gdiplus::Rect bounds) const;

    private:
        enum class Axis { Row, Column };
        Layout(Axis axis, std::vector<Layout> children, int gap, int extent);
        int Extent(Axis parentAxis) const;

        Widget* control = nullptr;
        std::vector<Layout> children;
        Axis axis = Axis::Row;
        int gap = 0, extent = 0;
        int crossExtent = 0;
        Align align = Align::Center;
        RECT padding{};
    };
}
