#include "SwatchWidget.h"
#include "../panel/UiSound.h"
#include "../panel/NewUi.h"
#include "../panel/UiRender.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

SwatchWidget::SwatchWidget(std::shared_ptr<ColorValue> color)
    : Widget(color->owner),
      color(std::move(color)),
      pickX(0.0f),
      pickY(0.0f),
      ring(false) {}

SwatchWidget& SwatchWidget::swatchAt(float x, float y) {
    bounds(x, y, 15.0f, 15.0f);
    hitArea(x - 3, y - 3, 21.0f, 21.0f);
    return *this;
}

SwatchWidget& SwatchWidget::picker(float x, float y) {
    pickX = x;
    pickY = y;
    return *this;
}

SwatchWidget& SwatchWidget::hoverRing() {
    ring = true;
    return *this;
}

void SwatchWidget::paint() {
    UiWidgets::colorSwatch(x, y, color->get());
    bool overSwatch = !blocked && NewUi::hit(x - 3, y - 3, 21.0f, 21.0f);
    if (ring && overSwatch) {
        UiRender::outline(x - 1, y - 1, 17, 17, 6.0f, UiTheme::hoverStrong(), 0xF, 1.0f);
    }
    if (overSwatch && NewUi::rightClick) {
        NewUi::rightClick = false;
        NewUi::openSwatchMenu(color, NewUi::mouseX, NewUi::mouseY);
    }
    if (clicked()) {
        UiSound::request(UiSound::TICK);
        NewUi::openPicker(color, pickX, pickY);
    }
}
