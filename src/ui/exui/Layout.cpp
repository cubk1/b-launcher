#include "ui/exui/Layout.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace exui {

    Layout::Layout(Widget& control, int extent, int crossExtent, Align align)
        : control(&control), extent(std::max(Auto, extent)), crossExtent(std::max(0, crossExtent)), align(align) {}

    Layout::Layout(Axis axis, std::vector<Layout> children, int gap, int extent)
        : children(std::move(children)), axis(axis), gap(std::max(0, gap)), extent(std::max(Auto, extent)) {}

    Layout Layout::Row(std::vector<Layout> children, int gap, int extent) {
        return {Axis::Row, std::move(children), gap, extent};
    }

    Layout Layout::Column(std::vector<Layout> children, int gap, int extent) {
        return {Axis::Column, std::move(children), gap, extent};
    }

    Layout Layout::Frame(Widget& frame, Layout content, RECT padding) {
        if (content.extent == 0) content.extent = Auto;
        Layout result = Column({std::move(content)}, 0, Auto);
        result.control = &frame;
        result.padding = {std::max(0L, padding.left), std::max(0L, padding.top),
                          std::max(0L, padding.right), std::max(0L, padding.bottom)};
        return result;
    }

    int Layout::Extent(Axis parentAxis) const {
        if (extent != Auto) return extent;
        auto size = Measure();
        return parentAxis == Axis::Row ? size.Width : size.Height;
    }

    Gdiplus::Size Layout::Measure() const {
        int length = 0, cross = 0;
        for (const Layout& child : children) {
            auto size = child.Measure();
            length += child.extent == Auto ? (axis == Axis::Row ? size.Width : size.Height) : child.extent;
            cross = std::max(cross, child.crossExtent > 0 ? child.crossExtent
                                                         : axis == Axis::Row ? size.Height : size.Width);
        }
        if (!children.empty()) length += gap * (int) (children.size() - 1);
        return {int(padding.left + padding.right) + (axis == Axis::Row ? length : cross),
                int(padding.top + padding.bottom) + (axis == Axis::Row ? cross : length)};
    }

    void Layout::Apply(Gdiplus::Rect bounds) const {
        bounds.Width = std::max(0, bounds.Width);
        bounds.Height = std::max(0, bounds.Height);
        if (control != nullptr) {
            control->SetBounds(bounds.X, bounds.Y, bounds.Width, bounds.Height);
        }
        if (children.empty()) return;
        bounds.X += padding.left;
        bounds.Y += padding.top;
        bounds.Width = std::max(0, bounds.Width - int(padding.left + padding.right));
        bounds.Height = std::max(0, bounds.Height - int(padding.top + padding.bottom));
        int length = axis == Axis::Row ? bounds.Width : bounds.Height;
        int spacing = children.size() > 1 ? std::min(gap, length / (int) (children.size() - 1)) : 0;
        int available = length - spacing * (int) (children.size() - 1);
        int flexible = 0;
        int64_t fixed = 0;
        for (const Layout& child : children) {
            fixed += child.Extent(axis);
            flexible += child.extent == 0;
        }
        int fixedSpace = (int) std::min<int64_t>(fixed, available);
        int remaining = available - fixedSpace;
        int64_t fixedUsed = 0;
        int allocated = 0, offset = 0;
        for (const Layout& child : children) {
            int size;
            if (child.extent != 0) {
                fixedUsed += child.Extent(axis);
                int until = fixed > 0 ? (int) (fixedUsed * fixedSpace / fixed) : 0;
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
            if (child.crossExtent > 0) {
                int free = (axis == Axis::Row ? cell.Height : cell.Width) - child.crossExtent;
                int crossOffset = child.align == Align::Start ? 0 : child.align == Align::Center ? free / 2 : free;
                if (axis == Axis::Row) {
                    cell.Y += crossOffset;
                    cell.Height = child.crossExtent;
                } else {
                    cell.X += crossOffset;
                    cell.Width = child.crossExtent;
                }
            }
            child.Apply(cell);
            offset += size + spacing;
        }
    }
}
