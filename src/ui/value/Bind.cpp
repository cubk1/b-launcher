#include "Bind.h"
#include "../panel/UiCommon.h"
#include "../panel/UiTheme.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

std::string Choice::label() {
    std::vector<std::string> all = options();
    if (!multi()) {
        for (const std::string& option : all) {
            if (selected(option)) {
                return option;
            }
        }
        return "Select";
    }
    std::vector<std::string> picked;
    for (const std::string& option : all) {
        if (selected(option)) {
            picked.push_back(option);
        }
    }
    return picked.empty() ? std::string("无") : Ui::join(picked, "、");
}

uint32_t Choice::optionColor(const std::string& option, bool selected) {
    (void) option;
    return selected ? UiTheme::textPrimary() : UiTheme::textMuted();
}

void NumberValue::nudge(double direction) {
    double stepValue = step();
    double amount = stepValue > 0 ? stepValue : (max() - min()) / 100.0;
    apply(get() + (direction > 0 ? amount : -amount));
}

void NumberValue::apply(double value) {
    double stepValue = step();
    if (stepValue > 0) {
        value = (double) Ui::roundl(value / stepValue) * stepValue;
    }
    set(std::max(min(), std::min(max(), value)));
}

FlagBinding::FlagBinding(const std::string& owner, std::function<bool()> getter, std::function<void(bool)> setter)
    : Flag(owner),
      getter(std::move(getter)),
      setter(std::move(setter)) {}

bool FlagBinding::isOn() {
    return getter ? getter() : false;
}

void FlagBinding::flip() {
    if (setter) {
        setter(!isOn());
    }
}

ModeBinding::ModeBinding(const std::string& owner, std::function<std::string()> getter,
                         std::function<void(const std::string&)> setter,
                         std::function<std::vector<std::string>()> source)
    : Choice(owner),
      getter(std::move(getter)),
      setter(std::move(setter)),
      source(std::move(source)),
      anchors(false) {}

std::vector<std::string> ModeBinding::options() {
    return source ? source() : std::vector<std::string>();
}

bool ModeBinding::selected(const std::string& option) {
    return getter && option == getter();
}

void ModeBinding::choose(const std::string& option) {
    if (setter) {
        setter(option);
    }
}

std::string ModeBinding::label() {
    return getter ? getter() : std::string();
}

std::string ModeBinding::optionIcon(const std::string& option) {
    auto found = icons.find(option);
    return found == icons.end() ? std::string() : found->second;
}

uint32_t ModeBinding::optionImage(const std::string& option) {
    auto found = images.find(option);
    return found == images.end() ? 0u : found->second;
}

ModeBinding& ModeBinding::image(const std::string& option, uint32_t texture) {
    images[option] = texture;
    return *this;
}

bool ModeBinding::anchorGlyphs() {
    return anchors;
}

ModeBinding& ModeBinding::icon(const std::string& option, int codepoint) {
    icons[option] = Ui::glyph(codepoint);
    return *this;
}

ModeBinding& ModeBinding::anchorIcons() {
    anchors = true;
    return *this;
}

MultiBinding::MultiBinding(const std::string& owner, std::vector<std::string>& selection,
                           std::vector<std::string>* order, const std::vector<std::string>& options)
    : Choice(owner),
      selection(&selection),
      order(order),
      ownedOrder(options),
      canReorder(order != nullptr) {}

std::vector<std::string> MultiBinding::options() {
    return order != nullptr ? *order : ownedOrder;
}

bool MultiBinding::multi() {
    return true;
}

bool MultiBinding::selected(const std::string& option) {
    return Ui::contains(*selection, option);
}

void MultiBinding::choose(const std::string& option) {
    if (Ui::contains(*selection, option)) {
        Ui::remove(*selection, option);
    } else {
        selection->push_back(option);
    }
}

std::string MultiBinding::label() {
    return selection->empty() ? std::string("无") : Ui::join(*selection, "、");
}

bool MultiBinding::reorderable() {
    return canReorder;
}

void MultiBinding::reorder(int from, int to) {
    if (order == nullptr) {
        return;
    }
    std::vector<std::string>& list = *order;
    int size = (int) list.size();
    if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
        return;
    }
    std::string moved = list[from];
    list.erase(list.begin() + from);
    list.insert(list.begin() + to, moved);
    std::vector<std::string> sorted;
    for (const std::string& option : list) {
        if (Ui::contains(*selection, option)) {
            sorted.push_back(option);
        }
    }
    *selection = sorted;
}

std::string MultiBinding::optionIcon(const std::string& option) {
    auto found = icons.find(option);
    return found == icons.end() ? std::string() : found->second;
}

MultiBinding& MultiBinding::icon(const std::string& option, int codepoint) {
    icons[option] = Ui::glyph(codepoint);
    return *this;
}

NumberBinding::NumberBinding(const std::string& owner, std::function<double()> getter,
                             std::function<void(double)> setter, double low, double high, double increment,
                             const std::string& unit)
    : NumberValue(owner),
      getter(std::move(getter)),
      setter(std::move(setter)),
      low(low),
      high(high),
      increment(increment),
      unit(unit) {}

double NumberBinding::min() {
    return low;
}

double NumberBinding::max() {
    return high;
}

double NumberBinding::step() {
    return increment;
}

std::string NumberBinding::suffix() {
    return unit;
}

double NumberBinding::get() {
    return getter ? getter() : 0.0;
}

void NumberBinding::set(double value) {
    if (setter) {
        setter(value);
    }
}

ColorBinding::ColorBinding(const std::string& owner, std::function<uint32_t()> getter,
                           std::function<void(uint32_t)> setter)
    : ColorValue(owner),
      getter(std::move(getter)),
      setter(std::move(setter)),
      hasDefault(false),
      defaultColor(0) {}

uint32_t ColorBinding::get() {
    return getter ? getter() : 0xFFFFFFFFu;
}

void ColorBinding::set(uint32_t argb) {
    if (setter) {
        setter(argb);
    }
}

bool ColorBinding::resettable() {
    return hasDefault;
}

void ColorBinding::reset() {
    if (hasDefault) {
        set(defaultColor);
    }
}

ColorBinding& ColorBinding::resetTo(uint32_t argb) {
    hasDefault = true;
    defaultColor = argb;
    return *this;
}

TextBinding::TextBinding(const std::string& owner, std::function<std::string()> getter,
                         std::function<void(const std::string&)> setter, const std::string& placeholder)
    : TextValue(owner),
      getter(std::move(getter)),
      setter(std::move(setter)),
      placeholder(placeholder),
      limit(32),
      masked(false) {}

std::string TextBinding::get() {
    return getter ? getter() : std::string();
}

void TextBinding::set(const std::string& value) {
    if (setter) {
        setter(value);
    }
}

std::string TextBinding::hint() {
    return placeholder;
}

int TextBinding::maxLength() {
    return limit;
}

bool TextBinding::secret() {
    return masked;
}

KeyBinding::KeyBinding(const std::string& owner, std::function<int()> getter, std::function<void(int)> setter)
    : KeyValue(owner),
      getter(std::move(getter)),
      setter(std::move(setter)) {}

int KeyBinding::get() {
    return getter ? getter() : -1;
}

void KeyBinding::set(int key) {
    if (setter) {
        setter(key);
    }
}

ShortcutBinding::ShortcutBinding(const std::string& owner, std::vector<int>& combos, const std::vector<int>& defaults)
    : ShortcutValue(owner),
      combos(&combos),
      defaults(defaults) {}

std::vector<int> ShortcutBinding::get() {
    return *combos;
}

void ShortcutBinding::set(const std::vector<int>& next) {
    *combos = next;
}

void ShortcutBinding::record(int combo) {
    if (onRecord) {
        onRecord(combo);
        return;
    }
    set(std::vector<int>{combo});
}

void ShortcutBinding::reset() {
    *combos = defaults;
}

namespace Bind {

    static std::unordered_map<const void*, uint32_t> colorDefaults;

    std::string idOf(const void* address) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "bind#%llx", (unsigned long long) (uintptr_t) address);
        return buffer;
    }

    static std::string ownerFor(const void* address, const std::string& id) {
        return id.empty() ? idOf(address) : id;
    }

    std::shared_ptr<FlagBinding> flag(bool& value, const std::string& id) {
        bool* target = &value;
        return std::make_shared<FlagBinding>(
                ownerFor(target, id), [target] { return *target; }, [target](bool next) { *target = next; });
    }

    std::shared_ptr<FlagBinding> flag(const std::string& id, std::function<bool()> getter,
                                      std::function<void(bool)> setter) {
        return std::make_shared<FlagBinding>(id, std::move(getter), std::move(setter));
    }

    std::shared_ptr<ModeBinding> mode(std::string& value, const std::vector<std::string>& options,
                                      const std::string& id) {
        std::string* target = &value;
        return std::make_shared<ModeBinding>(
                ownerFor(target, id), [target] { return *target; },
                [target](const std::string& next) { *target = next; }, [options] { return options; });
    }

    std::shared_ptr<ModeBinding> mode(const std::string& id, std::function<std::string()> getter,
                                      std::function<void(const std::string&)> setter,
                                      std::function<std::vector<std::string>()> options) {
        return std::make_shared<ModeBinding>(id, std::move(getter), std::move(setter), std::move(options));
    }

    std::shared_ptr<MultiBinding> multi(std::vector<std::string>& selection, const std::vector<std::string>& options,
                                        const std::string& id) {
        return std::make_shared<MultiBinding>(ownerFor(&selection, id), selection, nullptr, options);
    }

    std::shared_ptr<MultiBinding> reorderable(std::vector<std::string>& selection, std::vector<std::string>& options,
                                              const std::string& id) {
        return std::make_shared<MultiBinding>(ownerFor(&selection, id), selection, &options, options);
    }

    std::shared_ptr<NumberBinding> number(double& value, double min, double max, double step, const std::string& suffix,
                                          const std::string& id) {
        double* target = &value;
        return std::make_shared<NumberBinding>(
                ownerFor(target, id), [target] { return *target; }, [target](double next) { *target = next; }, min, max,
                step, suffix);
    }

    std::shared_ptr<NumberBinding> number(float& value, double min, double max, double step, const std::string& suffix,
                                          const std::string& id) {
        float* target = &value;
        return std::make_shared<NumberBinding>(
                ownerFor(target, id), [target] { return (double) *target; },
                [target](double next) { *target = (float) next; }, min, max, step, suffix);
    }

    std::shared_ptr<NumberBinding> number(int& value, int min, int max, int step, const std::string& suffix,
                                          const std::string& id) {
        int* target = &value;
        return std::make_shared<NumberBinding>(
                ownerFor(target, id), [target] { return (double) *target; },
                [target](double next) { *target = (int) Ui::roundl(next); }, (double) min, (double) max,
                (double) std::max(1, step), suffix);
    }

    std::shared_ptr<NumberBinding> number(const std::string& id, std::function<double()> getter,
                                          std::function<void(double)> setter, double min, double max, double step,
                                          const std::string& suffix) {
        return std::make_shared<NumberBinding>(id, std::move(getter), std::move(setter), min, max, step, suffix);
    }

    std::shared_ptr<ColorBinding> color(uint32_t& argb, const std::string& id) {
        uint32_t* target = &argb;
        auto known = colorDefaults.find(target);
        uint32_t initial = known == colorDefaults.end() ? (colorDefaults[target] = argb) : known->second;
        std::shared_ptr<ColorBinding> binding = std::make_shared<ColorBinding>(
                ownerFor(target, id), [target] { return *target; }, [target](uint32_t next) { *target = next; });
        binding->resetTo(initial);
        return binding;
    }

    std::shared_ptr<ColorBinding> color(const std::string& id, std::function<uint32_t()> getter,
                                        std::function<void(uint32_t)> setter) {
        return std::make_shared<ColorBinding>(id, std::move(getter), std::move(setter));
    }

    std::shared_ptr<TextBinding> text(std::string& value, const std::string& hint, const std::string& id) {
        std::string* target = &value;
        return std::make_shared<TextBinding>(
                ownerFor(target, id), [target] { return *target; },
                [target](const std::string& next) { *target = next; }, hint);
    }

    std::shared_ptr<TextBinding> text(const std::string& id, std::function<std::string()> getter,
                                      std::function<void(const std::string&)> setter, const std::string& hint) {
        return std::make_shared<TextBinding>(id, std::move(getter), std::move(setter), hint);
    }

    std::shared_ptr<KeyBinding> key(int& key, const std::string& id) {
        int* target = &key;
        return std::make_shared<KeyBinding>(
                ownerFor(target, id), [target] { return *target; }, [target](int next) { *target = next; });
    }

    std::shared_ptr<KeyBinding> key(const std::string& id, std::function<int()> getter,
                                    std::function<void(int)> setter) {
        return std::make_shared<KeyBinding>(id, std::move(getter), std::move(setter));
    }

    std::shared_ptr<ShortcutBinding> shortcut(std::vector<int>& combos, const std::vector<int>& defaults,
                                              const std::string& id) {
        return std::make_shared<ShortcutBinding>(ownerFor(&combos, id), combos, defaults);
    }
}
