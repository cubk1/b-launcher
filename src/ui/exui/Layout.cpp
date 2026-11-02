#include "ui/exui/Layout.h"

#include <algorithm>
#include <cstdint>

namespace exui {

    Layout::Layout(Widget& control, int extent) : control(&control), extent(std::max(0, extent)) {}

    Layout::Layout(Axis axis, std::initializer_list<Layout> children, int gap, int extent)
        : children(children), axis(axis), gap(std::max(0, gap)), extent(std::max(0, extent)) {}

    Layout Layout::Row(std::initializer_list<Layout> children, int gap, int extent) {
        return {Axis::Row, children, gap, extent};
    }

    Layout Layout::Column(std::initializer_list<Layout> children, int gap, int extent) {
        return {Axis::Column, children, gap, extent};
    }

    void Layout::Apply(Gdiplus::Rect bounds) const {
        bounds.Width = std::max(0, bounds.Width);
        bounds.Height = std::max(0, bounds.Height);
        if (control != nullptr) {
            control->SetBounds(bounds.X, bounds.Y, bounds.Width, bounds.Height);
            return;
        }
        if (children.empty()) return;
        int length = axis == Axis::Row ? bounds.Width : bounds.Height;
        int spacing = children.size() > 1 ? std::min(gap, length / (int) (children.size() - 1)) : 0;
        int available = length - spacing * (int) (children.size() - 1);
        int flexible = 0;
        int64_t fixed = 0;
        for (const Layout& child : children) {
            fixed += child.extent;
            flexible += child.extent == 0;
        }
        int fixedSpace = (int) std::min<int64_t>(fixed, available);
        int remaining = available - fixedSpace;
        int64_t fixedUsed = 0;
        int allocated = 0, offset = 0;
        for (const Layout& child : children) {
            int size;
            if (child.extent > 0) {
                fixedUsed += child.extent;
                int until = (int) (fixedUsed * fixedSpace / fixed);
                size = until - allocated;
                allocated = until;
            } else {
                size = remaining / flexible;
                remaining -= size;
                --flexible;
            }
            Gdiplus::Rect cell = bounds;
            if (axis == Axis::Row) {
                cell.X += offset;
                cell.Width = size;
            } else {
                cell.Y += offset;
                cell.Height = size;
            }
            child.Apply(cell);
            offset += size + spacing;
        }
    }
}
