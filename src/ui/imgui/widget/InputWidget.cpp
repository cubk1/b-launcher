#include "InputWidget.h"
#include "../panel/NewUi.h"
#include "../panel/UiFont.h"
#include "../panel/UiInput.h"
#include "../panel/UiRender.h"
#include "../panel/UiTheme.h"

#include <cmath>

InputWidget::InputWidget(const std::string& id)
    : Widget(id),
      textX(0.0f),
      room(0.0f),
      family(UiFont::MEDIUM),
      size(13.0f),
      caretH(14.0f),
      maxLength(32),
      centered(false),
      placeholder(""),
      textColor(UiTheme::textPrimary()),
      trailingText("") {}

InputWidget& InputWidget::trailing(const std::string& text) {
    trailingText = text;
    return *this;
}

InputWidget& InputWidget::area(float textX, float room, float size, float caretH) {
    this->textX = textX;
    this->room = room;
    this->size = size;
    this->caretH = caretH;
    return *this;
}

InputWidget& InputWidget::limit(int maxLength) {
    this->maxLength = maxLength;
    return *this;
}

InputWidget& InputWidget::center() {
    centered = true;
    return *this;
}

InputWidget& InputWidget::hint(const std::string& placeholder) {
    this->placeholder = placeholder;
    return *this;
}

InputWidget& InputWidget::tint(uint32_t textColor) {
    this->textColor = textColor;
    return *this;
}

void InputWidget::paint() {
    bool focused = UiInput::isFocused(id);
    std::string value = UiInput::shown(id);
    float total = UiFont::width(value, family, size);
    float originX = centered && total <= room ? textX + (room - total) * 0.5f
                                              : textX - NewUi::fieldScroll(id, room, family, size, focused);
    float caretY = y + (h - caretH) * 0.5f;
    UiRender::enableScissor((int) UiRender::transformX(textX - 2), (int) UiRender::transformY(y),
                            (int) std::ceil(UiRender::transformX(textX + room + 2)),
                            (int) std::ceil(UiRender::transformY(y + h)));
    if (!trailingText.empty()) {
        float after = value.empty() && !focused ? textX + UiFont::width(placeholder, family, size) : originX + total;
        UiRender::textY(trailingText, after, y, h, UiTheme::textFaint(), family, size);
    }
    if (value.empty() && !focused) {
        UiRender::textY(placeholder, textX, y, h, UiTheme::textFaint(), family, size);
    } else {
        if (focused) {
            NewUi::drawSelection(id, originX, caretY, caretH, family, size);
        }
        UiRender::textY(value, originX, y, h, textColor, family, size);
        if (focused) {
            NewUi::drawCaret(id, originX, caretY, caretH, family, size);
        }
    }
    UiRender::disableScissor();
    if (!blocked) {
        NewUi::fieldMouse(id, hitX, hitY, hitW, hitH, originX, family, size, maxLength);
    }
}
