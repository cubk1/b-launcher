#pragma once

#include "MenuItem.h"
#include "../value/Values.h"

#include <functional>
#include <memory>

class ToggleMenuItem final : public MenuItem {
public:
    std::shared_ptr<Flag> flag;

    ToggleMenuItem(std::shared_ptr<Flag> flag, const std::string& text, int glyph)
        : MenuItem(text, glyph),
          flag(std::move(flag)) {}

    float itemTrailWidth() override;
    void itemTrail(bool hovered) override;
    void itemRun() override;
};

class ChoiceMenuItem final : public MenuItem {
public:
    std::shared_ptr<Choice> choice;

    ChoiceMenuItem(std::shared_ptr<Choice> choice, const std::string& text, int glyph)
        : MenuItem(text, glyph),
          choice(std::move(choice)) {}

    float itemTrailWidth() override;
    void itemTrail(bool hovered) override;
    void itemRun() override;
};

class ColorMenuItem final : public MenuItem {
public:
    std::shared_ptr<ColorValue> color;

    ColorMenuItem(std::shared_ptr<ColorValue> color, const std::string& text, int glyph)
        : MenuItem(text, glyph),
          color(std::move(color)) {}

    float itemTrailWidth() override;
    void itemTrail(bool hovered) override;
    void itemRun() override;
};

class SwatchMenuItem final : public MenuItem {
public:
    std::shared_ptr<ColorValue> color;
    int action;

    SwatchMenuItem(std::shared_ptr<ColorValue> color, int action, const std::string& text, int glyph)
        : MenuItem(text, glyph),
          color(std::move(color)),
          action(action) {}

    bool itemEnabled() override;
    bool itemDanger() override;
    float itemTrailWidth() override;
    void itemTrail(bool hovered) override;
    void itemRun() override;
};

class PickerModeItem final : public MenuItem {
public:
    int mode;

    PickerModeItem(int mode, const std::string& text, int glyph) : MenuItem(text, glyph), mode(mode) {}

    float itemTrailWidth() override;
    void itemTrail(bool hovered) override;
    void itemRun() override;
};

class ActionMenuItem final : public MenuItem, public std::enable_shared_from_this<ActionMenuItem> {
public:
    std::function<void()> action;
    std::string shortcut;
    std::function<bool()> enabledWhen;
    std::function<bool()> checkedWhen;
    bool danger;
    bool confirm;
    bool stayOpen;
    int trailGlyph;
    int terminalKind;
    uint32_t imageTexture;
    MenuItems children;

    ActionMenuItem(const std::string& text, int glyph, std::function<void()> action);
    static std::shared_ptr<ActionMenuItem> make(const std::string& text, int glyph,
                                                std::function<void()> action = nullptr);

    std::shared_ptr<ActionMenuItem> keys(const std::string& text);
    std::shared_ptr<ActionMenuItem> enabled(bool value);
    std::shared_ptr<ActionMenuItem> enabledIf(std::function<bool()> predicate);
    std::shared_ptr<ActionMenuItem> checked(bool value);
    std::shared_ptr<ActionMenuItem> checkedIf(std::function<bool()> predicate);
    std::shared_ptr<ActionMenuItem> dangerous();
    std::shared_ptr<ActionMenuItem> confirmFirst();
    std::shared_ptr<ActionMenuItem> trailing(int codepoint);
    std::shared_ptr<ActionMenuItem> terminalIcon(int kind);
    std::shared_ptr<ActionMenuItem> image(uint32_t texture);
    std::shared_ptr<ActionMenuItem> nest(const MenuItems& items);
    std::shared_ptr<ActionMenuItem> keepOpen();

    bool armed() const;
    std::string itemText() override;
    int itemGlyph() override;
    bool itemDanger() override;
    bool itemEnabled() override;
    MenuItems itemChildren() override;
    float itemTrailWidth() override;
    void itemTrail(bool hovered) override;
    bool itemIcon(float cx, float cy, uint32_t tint) override;
    void itemIdle() override;
    void itemRun() override;
};
