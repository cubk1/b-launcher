#pragma once

#include "ui/exui/Widget.h"
#include <initializer_list>
#include <vector>

namespace exui {

    // Rows and columns of controls in one parent's coordinate space. A positive
    // extent reserves pixels along the parent's axis; zero shares the remaining space.
    // Controls remain owned by their form, and must outlive the layout.
    class Layout {
    public:
        Layout(Widget& control, int extent = 0);
        static Layout Row(std::initializer_list<Layout> children, int gap = 8, int extent = 0);
        static Layout Column(std::initializer_list<Layout> children, int gap = 8, int extent = 0);
        void Apply(Gdiplus::Rect bounds) const;

    private:
        enum class Axis { Row, Column };
        Layout(Axis axis, std::initializer_list<Layout> children, int gap, int extent);

        Widget* control = nullptr;
        std::vector<Layout> children;
        Axis axis = Axis::Row;
        int gap = 0, extent = 0;
    };
}
