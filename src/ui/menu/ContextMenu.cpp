#include "ContextMenu.h"
#include "../panel/UiSound.h"
#include "../panel/NewUi.h"
#include "../panel/UiCommon.h"
#include "../panel/UiFont.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

#include <algorithm>

ContextMenu::ContextMenu(const std::string& id) : Widget(id), openChild(-1), minWidth(120.0f), drawn(false) {}

ContextMenu& ContextMenu::fill(const MenuItems& next) {
    if (next.size() != items.size()) {
        closeChild();
    }
    items = next;
    return *this;
}

void ContextMenu::closeChild() {
    if (openChild >= 0) {
        UiState::remove(id + "#child" + std::to_string(openChild));
    }
    openChild = -1;
    if (child != nullptr) {
        child->drawn = false;
        child->closeChild();
    }
}

float ContextMenu::menuWidth() {
    float widest = minWidth;
    for (size_t i = 0; i < items.size(); i++) {
        MenuItem& item = *items[i];
        widest = std::max(widest,
                          UiFont::width(item.itemText(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 60 +
                                  item.itemTrailWidth());
    }
    return widest;
}

float ContextMenu::menuHeight() {
    return items.size() * 32.0f + 8.0f;
}

bool ContextMenu::covers(float mx, float my) {
    if (drawn && mx >= x && mx <= x + w && my >= y && my <= y + h) {
        return true;
    }
    return openChild >= 0 && child != nullptr && child->covers(mx, my);
}

void ContextMenu::paint() {
    w = menuWidth();
    h = menuHeight();
    drawn = true;
    UiWidgets::menuPanel(x, y, w, h);
    NewUi::markOverlayUnion(x, y, w, h);
    NewUi::markAbove(x, y, w, h);
    paintRows(x, y + 4.0f, w);
}

void ContextMenu::paintRows(float rowsX, float rowsY, float rowsW) {
    bool overChild = openChild >= 0 && child != nullptr && child->covers(NewUi::mouseX, NewUi::mouseY);
    MenuItems snapshot = items;
    for (size_t index = 0; index < snapshot.size(); index++) {
        int i = (int) index;
        MenuItem& item = *snapshot[index];
        float ry = rowsY + i * 32.0f;
        item.place(rowsX, ry, rowsW, 32.0f);
        bool enabled = item.itemEnabled();
        bool rowHover = !blocked && !overChild && enabled && NewUi::hit(rowsX, ry, rowsW, 32.0f);
        bool hasChildren = !item.itemChildren().empty();
        if (!rowHover) {
            item.itemIdle();
        } else if (openChild != i) {
            closeChild();
            if (hasChildren) {
                openChild = i;
            }
        }
        UiWidgets::menuRowHover(id + "#row" + std::to_string(i), rowsX, ry, rowsW, 32.0f, rowHover || openChild == i);
        bool danger = item.itemDanger();
        uint32_t tint = !enabled ? UiTheme::textFaint() : danger ? UiTheme::danger() : UiTheme::textPrimary();
        uint32_t iconTint = !enabled ? UiTheme::textFaint() : danger ? UiTheme::dangerSoft() : UiTheme::textMuted();
        if (!item.itemIcon(rowsX + 22, ry + 16.0f, enabled ? UiTheme::textPrimary() : UiTheme::textFaint())) {
            UiWidgets::iconBeside(Ui::glyph(item.itemGlyph()), rowsX + 22, item.itemText(), UiFont::MEDIUM,
                                  UiWidgets::TEXT_CONTROL, ry, 32.0f, iconTint, 11.0f);
        }
        UiRender::textY(item.itemText(), rowsX + 38, ry, 32.0f, tint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        item.itemTrail(rowHover);
        if (!blocked && !overChild && enabled && NewUi::click(rowsX, ry, rowsW, 32.0f)) {
            if (hasChildren) {
                UiSound::request(UiSound::TICK);
                openChild = i;
            } else {
                UiSound::request(danger ? UiSound::RELEASE : UiSound::forLabel(item.itemText()));
                item.itemRun();
            }
        }
    }
    paintChild(rowsX, rowsY, rowsW);
}

void ContextMenu::paintChild(float rowsX, float rowsY, float rowsW) {
    if (openChild < 0 || openChild >= (int) items.size()) {
        return;
    }
    MenuItems nested = items[openChild]->itemChildren();
    if (nested.empty()) {
        openChild = -1;
        return;
    }
    if (child == nullptr) {
        child = std::make_unique<ContextMenu>(id + "#child");
    }
    child->fill(nested);
    float childW = child->menuWidth();
    float childH = child->menuHeight();
    float limitX = NewUi::limitX();
    float limitY = NewUi::limitY();
    float childX = rowsX + rowsW + 4.0f;
    if (childX + childW > limitX - 6.0f) {
        childX = rowsX - childW - 4.0f;
    }
    float childY = UiRender::clamp(rowsY + openChild * 32.0f - 4.0f, 6.0f, std::max(6.0f, limitY - childH - 6.0f));
    float open = UiState::motionFrom(id + "#child" + std::to_string(openChild), 1.0f, 14.0f, 0.0f);
    float savedAlpha = UiRender::alphaMul;
    UiRender::alphaMul = savedAlpha * open;
    child->blocked = blocked;
    child->bounds(childX, childY, childW, childH).paint();
    UiRender::alphaMul = savedAlpha;
}
