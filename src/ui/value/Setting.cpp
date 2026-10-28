#include "Setting.h"
#include "../panel/UiCommon.h"

Setting::Setting(Kind kind, const std::string& id, const std::string& label) : kind(kind), id(id), label(label) {}

static std::string ownerOr(const std::string& owner, const std::string& label) {
    return owner.empty() ? label : owner;
}

SettingPtr Setting::toggle(const std::string& label, std::shared_ptr<Flag> flag) {
    SettingPtr setting =
            std::make_shared<Setting>(Kind::Toggle, ownerOr(flag ? flag->owner : std::string(), label), label);
    setting->flag = std::move(flag);
    return setting;
}

SettingPtr Setting::slider(const std::string& label, std::shared_ptr<NumberValue> number) {
    SettingPtr setting =
            std::make_shared<Setting>(Kind::Slider, ownerOr(number ? number->owner : std::string(), label), label);
    setting->number = std::move(number);
    return setting;
}

SettingPtr Setting::select(const std::string& label, std::shared_ptr<Choice> choice) {
    SettingPtr setting =
            std::make_shared<Setting>(Kind::Select, ownerOr(choice ? choice->owner : std::string(), label), label);
    setting->choice = std::move(choice);
    return setting;
}

SettingPtr Setting::colorField(const std::string& label, std::shared_ptr<ColorValue> color) {
    SettingPtr setting =
            std::make_shared<Setting>(Kind::Color, ownerOr(color ? color->owner : std::string(), label), label);
    setting->color = std::move(color);
    return setting;
}

SettingPtr Setting::textField(const std::string& label, std::shared_ptr<TextValue> text) {
    SettingPtr setting =
            std::make_shared<Setting>(Kind::Text, ownerOr(text ? text->owner : std::string(), label), label);
    setting->text = std::move(text);
    return setting;
}

SettingPtr Setting::keyField(const std::string& label, std::shared_ptr<KeyValue> key) {
    SettingPtr setting = std::make_shared<Setting>(Kind::Key, ownerOr(key ? key->owner : std::string(), label), label);
    setting->key = std::move(key);
    return setting;
}

SettingPtr Setting::group(const std::string& label, SettingList children) {
    SettingPtr setting = std::make_shared<Setting>(Kind::Group, "group#" + label, label);
    setting->sub(std::move(children));
    return setting;
}

SettingPtr Setting::withId(const std::string& value) {
    id = value;
    return shared_from_this();
}

SettingPtr Setting::icon(int codepoint) {
    glyph = Ui::glyph(codepoint);
    return shared_from_this();
}

SettingPtr Setting::describe(const std::string& text) {
    doc = text;
    return shared_from_this();
}

SettingPtr Setting::swatch(std::shared_ptr<ColorValue> value) {
    swatchColor = std::move(value);
    return shared_from_this();
}

SettingPtr Setting::sub(SettingList list) {
    children = [list] { return list; };
    return shared_from_this();
}

SettingPtr Setting::sub(std::function<SettingList()> provider) {
    children = std::move(provider);
    return shared_from_this();
}

SettingPtr Setting::visibleWhen(std::function<bool()> predicate) {
    shown = std::move(predicate);
    return shared_from_this();
}

SettingPtr Setting::onContext(std::function<void(float x, float y)> handler) {
    context = std::move(handler);
    return shared_from_this();
}

bool Setting::visible() const {
    return !shown || shown();
}

bool Setting::hasChildren() const {
    return !visibleChildren().empty();
}

SettingList Setting::visibleChildren() const {
    SettingList out;
    if (!children) {
        return out;
    }
    SettingList all = children();
    for (const SettingPtr& child : all) {
        if (child && child->visible()) {
            out.push_back(child);
        }
    }
    return out;
}

SettingsPagePtr Setting::page() const {
    std::function<SettingList()> provider = children;
    return std::make_shared<SettingsPage>(id, label, [provider] { return provider ? provider() : SettingList(); });
}

SettingsPage::SettingsPage(const std::string& id, const std::string& title, std::function<SettingList()> items)
    : id(id),
      title(title),
      items(std::move(items)) {}

SettingsPagePtr SettingsPage::of(const std::string& id, const std::string& title, SettingList items) {
    return std::make_shared<SettingsPage>(id, title, [items] { return items; });
}

SettingsPagePtr SettingsPage::of(const std::string& id, const std::string& title, std::function<SettingList()> items) {
    return std::make_shared<SettingsPage>(id, title, std::move(items));
}

SettingList SettingsPage::visibleItems() const {
    SettingList out;
    if (!items) {
        return out;
    }
    SettingList all = items();
    for (const SettingPtr& item : all) {
        if (item && item->visible()) {
            out.push_back(item);
        }
    }
    return out;
}
