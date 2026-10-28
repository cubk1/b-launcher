#include "MenuItem.h"
#include "../panel/UiCommon.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

MenuItem::MenuItem(const std::string& text, int glyph)
    : text(text),
      glyph(glyph),
      rowX(0.0f),
      rowY(0.0f),
      rowW(0.0f),
      rowH(0.0f) {}

void MenuItem::place(float x, float y, float w, float h) {
    rowX = x;
    rowY = y;
    rowW = w;
    rowH = h;
}

std::string MenuItem::itemText() {
    return text;
}

int MenuItem::itemGlyph() {
    return glyph;
}

bool MenuItem::itemDanger() {
    return false;
}

bool MenuItem::itemEnabled() {
    return true;
}

std::vector<std::shared_ptr<MenuItem>> MenuItem::itemChildren() {
    return std::vector<std::shared_ptr<MenuItem>>();
}

float MenuItem::itemTrailWidth() {
    return itemChildren().empty() ? 0.0f : 16.0f;
}

void MenuItem::itemTrail(bool hovered) {
    (void) hovered;
    if (!itemChildren().empty()) {
        UiWidgets::icon(Ui::glyph(0xF054), rowX + rowW - 16, rowY + rowH * 0.5f, UiTheme::textMuted(), 8.5f);
    }
}

bool MenuItem::itemIcon(float cx, float cy, uint32_t tint) {
    (void) cx;
    (void) cy;
    (void) tint;
    return false;
}

void MenuItem::itemIdle() {}

void MenuItem::itemRun() {}
