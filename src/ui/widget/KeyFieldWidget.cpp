#include "KeyFieldWidget.h"
#include "../gl/RenderUtil.h"
#include "../panel/NewUi.h"
#include "../panel/UiCommon.h"
#include "../panel/UiFont.h"
#include "../panel/UiKeys.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

KeyFieldWidget::KeyFieldWidget(const std::string& id, std::shared_ptr<KeyValue> value)
    : Widget(id),
      value(std::move(value)) {}

bool KeyFieldWidget::capturing() const {
    return NewUi::bindingKey != nullptr && NewUi::bindingKey->owner == value->owner;
}

void KeyFieldWidget::paint() {
    bool isHovered = hovered();
    if (clicked()) {
        NewUi::bindingKey = capturing() ? nullptr : value;
    }
    bool isCapturing = capturing();
    float lit = UiState::motion(id + "#key", isCapturing ? 1.0f : (isHovered ? 0.5f : 0.0f), 18.0f);
    uint32_t fill = UiRender::over(UiTheme::overlay(Ui::round(18 * lit)), UiTheme::surfaceSunken());
    UiRender::roundRect(x, y, w, h, 5.0f, fill);
    UiRender::outline(x, y, w, h, 5.0f,
                      isCapturing ? UiTheme::accent() : UiRender::over(UiTheme::overlay(10), UiTheme::surfaceSunken()),
                      0xF, 1.0f);
    int key = value->get();
    std::string text = isCapturing ? "..." : (!UiKeys::isValidBind(key) ? "-" : RenderUtil::keyName(key));
    float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
    UiRender::textY(text, x + (w - textW) * 0.5f, y, h, isCapturing ? UiTheme::accentText() : UiTheme::textSecondary(),
                    UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
}
