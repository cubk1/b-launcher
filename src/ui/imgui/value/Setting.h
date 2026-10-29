#pragma once

#include "Values.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Setting;
class SettingsPage;
using SettingPtr = std::shared_ptr<Setting>;
using SettingList = std::vector<SettingPtr>;
using SettingsPagePtr = std::shared_ptr<SettingsPage>;

class Setting : public std::enable_shared_from_this<Setting> {
public:
    enum class Kind { Toggle, Slider, Select, Color, Text, Key, Group };

    Kind kind;
    std::string id;
    std::string label;
    std::string glyph;
    std::string doc;
    std::shared_ptr<Flag> flag;
    std::shared_ptr<NumberValue> number;
    std::shared_ptr<Choice> choice;
    std::shared_ptr<ColorValue> color;
    std::shared_ptr<TextValue> text;
    std::shared_ptr<KeyValue> key;
    std::shared_ptr<ColorValue> swatchColor;
    std::function<SettingList()> children;
    std::function<bool()> shown;
    std::function<void(float x, float y)> context;

    Setting(Kind kind, const std::string& id, const std::string& label);

    static SettingPtr toggle(const std::string& label, std::shared_ptr<Flag> flag);
    static SettingPtr slider(const std::string& label, std::shared_ptr<NumberValue> number);
    static SettingPtr select(const std::string& label, std::shared_ptr<Choice> choice);
    static SettingPtr colorField(const std::string& label, std::shared_ptr<ColorValue> color);
    static SettingPtr textField(const std::string& label, std::shared_ptr<TextValue> text);
    static SettingPtr keyField(const std::string& label, std::shared_ptr<KeyValue> key);
    static SettingPtr group(const std::string& label, SettingList children);

    SettingPtr withId(const std::string& value);
    SettingPtr icon(int codepoint);
    SettingPtr describe(const std::string& text);
    SettingPtr swatch(std::shared_ptr<ColorValue> value);
    SettingPtr sub(SettingList list);
    SettingPtr sub(std::function<SettingList()> provider);
    SettingPtr visibleWhen(std::function<bool()> predicate);
    SettingPtr onContext(std::function<void(float x, float y)> handler);

    bool visible() const;
    bool hasChildren() const;
    SettingList visibleChildren() const;
    SettingsPagePtr page() const;
};

class SettingsPage {
public:
    std::string id;
    std::string title;
    std::function<SettingList()> items;

    SettingsPage(const std::string& id, const std::string& title, std::function<SettingList()> items);

    static SettingsPagePtr of(const std::string& id, const std::string& title, SettingList items);
    static SettingsPagePtr of(const std::string& id, const std::string& title, std::function<SettingList()> items);

    SettingList visibleItems() const;
};
