#include "TextFieldWidget.h"
#include "../panel/UiSound.h"
#include "InputWidget.h"
#include "../panel/NewUi.h"
#include "../panel/UiCursor.h"
#include "../panel/UiFont.h"
#include "../panel/UiInput.h"
#include "../panel/UiRender.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

#include <algorithm>

TextFieldWidget::TextFieldWidget(std::shared_ptr<TextValue> value)
    : Widget(value->owner),
      value(std::move(value)),
      boxX(0.0f),
      boxY(0.0f),
      boxW(0.0f),
      boxH(0.0f),
      trail(0.0f),
      leading(0.0f),
      corner(7.0f) {}

TextFieldWidget& TextFieldWidget::room(float right) {
    trail = right;
    return *this;
}

TextFieldWidget& TextFieldWidget::lead(float left) {
    leading = left;
    return *this;
}

TextFieldWidget& TextFieldWidget::radius(float value) {
    corner = value;
    return *this;
}

TextFieldWidget& TextFieldWidget::box(float x, float y, float w, float h) {
    boxX = x;
    boxY = y;
    boxW = w;
    boxH = h;
    return *this;
}

bool TextFieldWidget::editing() const {
    return id == NewUi::textEditing;
}

void TextFieldWidget::paint() {
    bool isEditing = editing();
    if (!blocked || isEditing) {
        NewUi::tabStop(value);
    }
    bool isHovered = !blocked && NewUi::hit(boxX, boxY, boxW, boxH);
    UiRender::roundRect(boxX, boxY, boxW, boxH, corner, UiTheme::surfaceRaised());
    if (isEditing) {
        UiRender::outline(boxX, boxY, boxW, boxH, corner, UiTheme::accent());
    } else if (isHovered) {
        UiRender::outline(boxX, boxY, boxW, boxH, corner, UiTheme::border(UiTheme::outlineSoft()));
    }
    if (isEditing) {
        InputWidget(NewUi::INPUT_TEXT)
                .area(boxX + 9 + leading, boxW - 18 - trail - leading, UiWidgets::TEXT_CONTROL, 14)
                .limit(value->maxLength())
                .bounds(boxX, boxY, std::min(boxW, hitX + hitW - boxX), boxH)
                .block(blocked)
                .paint();
        return;
    }
    std::string text = value->secret() ? UiInput::masked(value->get()) : value->get();
    bool empty = text.empty();
    UiRender::textClipped(empty ? value->hint() : text, boxX + 9 + leading, boxY, boxH, boxW - 18 - trail - leading,
                          empty ? UiTheme::textFaint() : UiTheme::textPrimary(), UiFont::MEDIUM,
                          UiWidgets::TEXT_CONTROL, UiTheme::surfaceRaised());
    if (hovered()) {
        UiCursor::text();
    }
    // A field takes the caret on press, so that dragging from there selects text.
    if (!blocked && NewUi::press(hitX, hitY, hitW, hitH)) {
        UiSound::request(UiSound::TICK);
        NewUi::beginTextEditAt(value, boxX + 9 + leading, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
    }
}
