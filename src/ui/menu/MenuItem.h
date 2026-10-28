#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MenuItem {
public:
    std::string text;
    int glyph;
    float rowX;
    float rowY;
    float rowW;
    float rowH;

    MenuItem(const std::string& text, int glyph);
    virtual ~MenuItem() = default;

    void place(float x, float y, float w, float h);
    virtual std::string itemText();
    virtual int itemGlyph();
    virtual bool itemDanger();
    virtual bool itemEnabled();
    virtual std::vector<std::shared_ptr<MenuItem>> itemChildren();
    virtual float itemTrailWidth();
    virtual void itemTrail(bool hovered);
    virtual bool itemIcon(float cx, float cy, uint32_t tint);
    virtual void itemIdle();
    virtual void itemRun();
};

using MenuItems = std::vector<std::shared_ptr<MenuItem>>;
