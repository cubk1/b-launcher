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

TextFieldWidget::TextFieldWidget(std::shared_ptr<TextValue> value)
    : Widget(value->owner),
      value(std::move(value)),
      boxX(0.0f),
      boxY(0.0f),
      boxW(0.0f),
      boxH(0.0f),
      trail(0.0f) {}

TextFieldWidget& TextFieldWidget::room(float right) {
    trail = right;
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
    UiRender::roundRect(boxX, boxY, boxW, boxH, 7.0f, UiTheme::surfaceRaised());
    if (isEditing) {
        UiRender::outline(boxX, boxY, boxW, boxH, 7.0f, UiTheme::accent());
    } else if (isHovered) {
        UiRender::outline(boxX, boxY, boxW, boxH, 7.0f, UiTheme::outlineSoft());
    }
    if (isEditing) {
        InputWidget(NewUi::INPUT_TEXT)
                .area(boxX + 9, boxW - 18 - trail, UiWidgets::TEXT_CONTROL, 14)
                .limit(value->maxLength())
                .bounds(boxX, boxY, boxW, boxH)
                .block(blocked)
                .paint();
        return;
    }
    std::string text = value->secret() ? UiInput::masked(value->get()) : value->get();
    bool empty = text.empty();
    UiRender::textClipped(empty ? value->hint() : text, boxX + 9, boxY, boxH, boxW - 18 - trail,
                          empty ? UiTheme::textFaint() : UiTheme::textPrimary(), UiFont::MEDIUM,
                          UiWidgets::TEXT_CONTROL, UiTheme::surfaceRaised());
    if (isHovered) {
        UiCursor::text();
    }
    if (clicked()) {
        UiSound::request(UiSound::TICK);
        NewUi::beginTextEdit(value);
    }
}
