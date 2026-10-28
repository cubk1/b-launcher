#include "ShortcutWidget.h"
#include "../panel/UiKeys.h"
#include "../panel/NewUi.h"
#include "../panel/UiCommon.h"
#include "../panel/UiFont.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

ShortcutWidget::ShortcutWidget(const std::string& id, std::shared_ptr<ShortcutValue> value)
    : Widget(id),
      value(std::move(value)) {}

bool ShortcutWidget::recording() const {
    return NewUi::recordingShortcut != nullptr && NewUi::recordingShortcut->owner == value->owner;
}

void ShortcutWidget::paint() {
    bool isHovered = hovered();
    if (clicked()) {
        NewUi::recordingShortcut = recording() ? nullptr : value;
    }
    if (isHovered && NewUi::rightClick) {
        NewUi::rightClick = false;
        if (recording()) {
            NewUi::recordingShortcut = nullptr;
        }
        value->reset();
    }
    bool capturing = recording();
    float lit = UiState::motion(id + "#chip", capturing ? 1.0f : (isHovered ? 0.5f : 0.0f), 18.0f);
    uint32_t fill = UiRender::over(UiTheme::overlay(Ui::round(18 * lit)), UiTheme::surfaceSunken());
    UiRender::roundRect(x, y, w, h, 5.0f, fill);
    UiRender::outline(x, y, w, h, 5.0f,
                      capturing ? UiTheme::accent() : UiRender::over(UiTheme::overlay(10), UiTheme::surfaceSunken()),
                      0xF, 0.5f);
    std::vector<int> keys = value->get();
    std::string text;
    for (size_t i = 0; i < keys.size(); i++) {
        if (i > 0) {
            text += ", ";
        }
        text += UiKeys::comboLabel(keys[i]);
    }
    std::string shown = capturing ? "Press keys..." : (text.empty() ? "None" : text);
    float textW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
    uint32_t color =
            capturing ? UiTheme::accentText() : (keys.empty() ? UiTheme::textFaint() : UiTheme::textSecondary());
    if (textW > w - 14) {
        UiRender::textClipped(shown, x + 7, y, h, w - 14, color, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, fill);
    } else {
        UiRender::textY(shown, x + (w - textW) * 0.5f, y, h, color, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
    }
}
