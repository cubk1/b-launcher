#pragma once

#include "MenuItem.h"
#include "../widget/Widget.h"

#include <memory>

class ContextMenu final : public Widget {
public:
    MenuItems items;
    int openChild;
    std::unique_ptr<ContextMenu> child;
    float minWidth;
    bool drawn;

    explicit ContextMenu(const std::string& id);

    ContextMenu& fill(const MenuItems& next);
    void closeChild();
    float menuWidth();
    float menuHeight();
    bool covers(float mx, float my);
    void paint() override;
    void paintRows(float rowsX, float rowsY, float rowsW);
    void paintChild(float rowsX, float rowsY, float rowsW);
};
