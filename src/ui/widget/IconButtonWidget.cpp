#include "IconButtonWidget.h"
#include "../panel/UiSound.h"
#include "../panel/UiCommon.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

IconButtonWidget::IconButtonWidget(const std::string& id, const std::string& glyph, uint32_t color, uint32_t hoverColor)
    : Widget(id),
      glyph(glyph),
      color(color),
      hoverColor(hoverColor),
      pressed(false) {}

void IconButtonWidget::paint() {
    bool isHovered = hovered();
    pressed = clicked();
    if (pressed) {
        if (glyph == Ui::glyph(0xf00d) || glyph == Ui::glyph(0xf2ed)) {
            UiSound::request(UiSound::RELEASE);
        } else if (glyph == Ui::glyph(0xf053) || glyph == Ui::glyph(0xf35d)) {
            UiSound::request(UiSound::PAGE);
        }
    }
    float lit = UiState::motion(id + "#icon", isHovered ? 1.0f : 0.0f, 18.0f);
    if (lit > 0.001f) {
        UiRender::roundRect(x, y, w, h, 6.0f, UiTheme::overlay(Ui::round(16 * lit)));
    }
    UiWidgets::icon(glyph, x + w * 0.5f, y + h * 0.5f, UiRender::mix(color, hoverColor, lit), 11.0f);
}
