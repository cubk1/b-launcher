#include "MenuItems.h"
#include "../panel/NewUi.h"
#include "../panel/UiCommon.h"
#include "../panel/UiFont.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"
#include "../widget/SwatchWidget.h"

float ToggleMenuItem::itemTrailWidth() {
    return 36.0f;
}

void ToggleMenuItem::itemTrail(bool hovered) {
    (void) hovered;
    UiWidgets::subToggle("menu#toggle#" + flag->owner, rowX + rowW - 44, rowY + (rowH - 18) * 0.5f, flag->isOn());
}

void ToggleMenuItem::itemRun() {
    flag->flip();
}

float ChoiceMenuItem::itemTrailWidth() {
    return UiFont::width(choice->label(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 24.0f;
}

void ChoiceMenuItem::itemTrail(bool hovered) {
    (void) hovered;
    std::string value = choice->label();
    float valueW = UiFont::width(value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
    UiRender::textY(value, rowX + rowW - 26 - valueW, rowY, rowH, UiTheme::textMuted(), UiFont::MEDIUM,
                    UiWidgets::TEXT_CONTROL);
    UiWidgets::icon(Ui::glyph(0xF054), rowX + rowW - 16, rowY + rowH * 0.5f, UiTheme::textMuted(), 8.5f);
}

void ChoiceMenuItem::itemRun() {
    NewUi::openChoice(choice, rowX + rowW + 6, rowY + rowH * 0.5f, 118.0f, "menu#" + text);
}

float ColorMenuItem::itemTrailWidth() {
    return 24.0f;
}

void ColorMenuItem::itemTrail(bool hovered) {
    (void) hovered;
    SwatchWidget(color)
            .swatchAt(rowX + rowW - 32, rowY + (rowH - 15) * 0.5f)
            .picker(rowX + rowW + 6, rowY - 120)
            .hitArea(0, 0, 0, 0)
            .paint();
}

void ColorMenuItem::itemRun() {
    NewUi::openPicker(color, rowX + rowW + 6, rowY - 120);
}

bool SwatchMenuItem::itemEnabled() {
    if (action == 1) {
        return NewUi::colorClipboardSet;
    }
    return action != 2 || color->resettable();
}

bool SwatchMenuItem::itemDanger() {
    return action == 2;
}

float SwatchMenuItem::itemTrailWidth() {
    return action == 1 ? 22.0f : 0.0f;
}

void SwatchMenuItem::itemTrail(bool hovered) {
    (void) hovered;
    if (action == 1 && NewUi::colorClipboardSet) {
        UiWidgets::checkerBoard(rowX + rowW - 30, rowY + rowH * 0.5f - 7, 14, 14, 4.0f, 5.0f);
        UiRender::roundRect(rowX + rowW - 30, rowY + rowH * 0.5f - 7, 14, 14, 4.0f, NewUi::colorClipboard);
    }
}

void SwatchMenuItem::itemRun() {
    if (action == 0) {
        NewUi::colorClipboard = color->get();
        NewUi::colorClipboardSet = true;
    } else if (action == 1) {
        color->set(NewUi::colorClipboard);
    } else {
        color->reset();
    }
    NewUi::swatchMenuOwner.clear();
}

float PickerModeItem::itemTrailWidth() {
    return 14.0f;
}

void PickerModeItem::itemTrail(bool hovered) {
    (void) hovered;
    if (NewUi::pickerMode == mode) {
        UiWidgets::check(rowX + rowW - 20, rowY + rowH * 0.5f, UiTheme::accentText(), 9.5f);
    }
}

void PickerModeItem::itemRun() {
    NewUi::pickerMode = mode;
    NewUi::pickerModeOwner.clear();
    NewUi::commitColorEdit();
}

static std::string armedAction;

ActionMenuItem::ActionMenuItem(const std::string& text, int glyph, std::function<void()> action)
    : MenuItem(text, glyph),
      action(std::move(action)),
      danger(false),
      confirm(false),
      stayOpen(false),
      trailGlyph(0),
      terminalKind(-1),
      imageTexture(0) {}

std::shared_ptr<ActionMenuItem> ActionMenuItem::make(const std::string& text, int glyph, std::function<void()> action) {
    return std::make_shared<ActionMenuItem>(text, glyph, std::move(action));
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::keys(const std::string& text) {
    shortcut = text;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::enabled(bool value) {
    enabledWhen = [value] { return value; };
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::enabledIf(std::function<bool()> predicate) {
    enabledWhen = std::move(predicate);
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::checked(bool value) {
    checkedWhen = [value] { return value; };
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::checkedIf(std::function<bool()> predicate) {
    checkedWhen = std::move(predicate);
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::dangerous() {
    danger = true;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::confirmFirst() {
    confirm = true;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::trailing(int codepoint) {
    trailGlyph = codepoint;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::image(uint32_t texture) {
    imageTexture = texture;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::terminalIcon(int kind) {
    terminalKind = kind;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::nest(const MenuItems& items) {
    children = items;
    return shared_from_this();
}

std::shared_ptr<ActionMenuItem> ActionMenuItem::keepOpen() {
    stayOpen = true;
    return shared_from_this();
}

bool ActionMenuItem::armed() const {
    return confirm && armedAction == text + "#" + std::to_string(glyph);
}

std::string ActionMenuItem::itemText() {
    return armed() ? "确认" + text : text;
}

int ActionMenuItem::itemGlyph() {
    if (armed()) {
        return 0xf071;
    }
    return checkedWhen && checkedWhen() ? 0xf00c : glyph;
}

bool ActionMenuItem::itemDanger() {
    return danger || armed();
}

bool ActionMenuItem::itemEnabled() {
    return !enabledWhen || enabledWhen();
}

MenuItems ActionMenuItem::itemChildren() {
    return children;
}

float ActionMenuItem::itemTrailWidth() {
    float textWidth =
            shortcut.empty() ? 0.0f : UiFont::width(shortcut, UiFont::MEDIUM, UiWidgets::TEXT_CAPTION) + 18.0f;
    if (!children.empty()) {
        return 16.0f + textWidth;
    }
    if (trailGlyph != 0) {
        return 16.0f + textWidth;
    }
    return textWidth;
}

void ActionMenuItem::itemTrail(bool hovered) {
    (void) hovered;
    float right = rowX + rowW - 16;
    if (!children.empty() || trailGlyph != 0) {
        UiWidgets::icon(Ui::glyph(children.empty() ? trailGlyph : 0xf054), rowX + rowW - 16, rowY + rowH * 0.5f,
                        UiTheme::textMuted(), 8.5f);
        right -= 14;
    }
    if (shortcut.empty()) {
        return;
    }
    float width = UiFont::width(shortcut, UiFont::MEDIUM, UiWidgets::TEXT_CAPTION);
    UiRender::textY(shortcut, right - width, rowY, rowH, UiTheme::textFaint(), UiFont::MEDIUM, UiWidgets::TEXT_CAPTION);
}

bool ActionMenuItem::itemIcon(float cx, float cy, uint32_t tint) {
    if (imageTexture != 0) {
        UiRender::texture(imageTexture, cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
        return true;
    }
    if (terminalKind < 0) {
        return false;
    }
    UiRender::cliIcon(cx, cy, 15.0f, terminalKind, tint);
    return true;
}

void ActionMenuItem::itemIdle() {
    if (armed()) {
        armedAction.clear();
    }
}

void ActionMenuItem::itemRun() {
    if (confirm && !armed()) {
        armedAction = text + "#" + std::to_string(glyph);
        return;
    }
    if (armed()) {
        armedAction.clear();
    }
    if (!stayOpen) {
        NewUi::closeMenu();
    }
    if (action) {
        action();
    }
}
