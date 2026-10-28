#include "SelectWidget.h"
#include "../panel/UiSound.h"
#include "../panel/NewUi.h"
#include "../panel/UiWidgets.h"

#include <algorithm>

SelectWidget::SelectWidget(const std::string& id, std::shared_ptr<Choice> choice)
    : Widget(id),
      choice(std::move(choice)),
      compact(false),
      right(0.0f),
      room(0.0f) {}

SelectWidget& SelectWidget::compactRow(float rowX, float rowY, float rowW, float rowH, float right, float room) {
    bounds(rowX, rowY, rowW, rowH);
    compact = true;
    this->right = right;
    this->room = room;
    return *this;
}

bool SelectWidget::showing() const {
    return NewUi::popupOpen && choice->owner == NewUi::popupOwner && NewUi::popupSource == id;
}

void SelectWidget::open() {
    UiSound::request(UiSound::TICK);
    NewUi::openChoice(choice, x, y, w, id);
}

void SelectWidget::paint() {
    if (compact) {
        std::string value = choice->label();
        UiWidgets::subMode(id, right, y, h, value, hovered(), showing(), room);
        if (clicked()) {
            float cw = std::min(160.0f, UiWidgets::subModeWidth(value));
            NewUi::openChoice(choice, right - cw, y + (h - 23) * 0.5f, std::max(cw, 120.0f), id);
        }
        return;
    }
    bool isHovered = hovered();
    if (clicked()) {
        open();
    } else if (showing()) {
        NewUi::popupAnchorX = x;
        NewUi::popupAnchorY = y;
        NewUi::popupWidth = w;
    }
    UiWidgets::select(id, x, y, w, choice->label(), isHovered, showing());
}
