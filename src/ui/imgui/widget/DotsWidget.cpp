#include "DotsWidget.h"
#include "../panel/NewUi.h"
#include "../panel/UiWidgets.h"

DotsWidget::DotsWidget(const std::string& id, SettingsPagePtr page)
    : Widget(id),
      page(std::move(page)),
      anchorX(0.0f),
      anchorY(0.0f) {}

DotsWidget& DotsWidget::dotsAt(float x, float y) {
    bounds(x, y, 26.0f, 22.0f);
    return *this;
}

DotsWidget& DotsWidget::anchor(float x, float y) {
    anchorX = x;
    anchorY = y;
    return *this;
}

bool DotsWidget::expanded() const {
    const std::string& owner = page->id;
    return owner == NewUi::subOwner || owner == NewUi::subChildOwner ||
            (!NewUi::subPanelDrawing && owner == NewUi::subParent);
}

void DotsWidget::paint() {
    bool isHovered = hovered();
    if (clicked()) {
        NewUi::openSettings(page, anchorX, anchorY);
    }
    UiWidgets::dotsButton(id, x, y, expanded(), isHovered);
}
