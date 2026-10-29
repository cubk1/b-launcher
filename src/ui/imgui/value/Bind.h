#pragma once

#include "Values.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class FlagBinding final : public Flag {
public:
    std::function<bool()> getter;
    std::function<void(bool)> setter;

    FlagBinding(const std::string& owner, std::function<bool()> getter, std::function<void(bool)> setter);
    bool isOn() override;
    void flip() override;
};

class ModeBinding final : public Choice {
public:
    std::function<std::string()> getter;
    std::function<void(const std::string&)> setter;
    std::function<std::vector<std::string>()> source;
    std::unordered_map<std::string, std::string> icons;
    std::unordered_map<std::string, uint32_t> images;
    bool anchors;

    ModeBinding(const std::string& owner, std::function<std::string()> getter,
                std::function<void(const std::string&)> setter, std::function<std::vector<std::string>()> source);
    std::vector<std::string> options() override;
    bool selected(const std::string& option) override;
    void choose(const std::string& option) override;
    std::string label() override;
    std::string optionIcon(const std::string& option) override;
    uint32_t optionImage(const std::string& option) override;
    bool anchorGlyphs() override;

    ModeBinding& icon(const std::string& option, int codepoint);
    ModeBinding& image(const std::string& option, uint32_t texture);
    ModeBinding& anchorIcons();
};

class MultiBinding final : public Choice {
public:
    std::vector<std::string>* selection;
    std::vector<std::string>* order;
    std::vector<std::string> ownedOrder;
    std::unordered_map<std::string, std::string> icons;
    bool canReorder;

    MultiBinding(const std::string& owner, std::vector<std::string>& selection, std::vector<std::string>* order,
                 const std::vector<std::string>& options);
    std::vector<std::string> options() override;
    bool multi() override;
    bool selected(const std::string& option) override;
    void choose(const std::string& option) override;
    std::string label() override;
    bool reorderable() override;
    void reorder(int from, int to) override;
    std::string optionIcon(const std::string& option) override;

    MultiBinding& icon(const std::string& option, int codepoint);
};

class NumberBinding final : public NumberValue {
public:
    std::function<double()> getter;
    std::function<void(double)> setter;
    double low;
    double high;
    double increment;
    std::string unit;

    NumberBinding(const std::string& owner, std::function<double()> getter, std::function<void(double)> setter,
                  double low, double high, double increment, const std::string& unit);
    double min() override;
    double max() override;
    double step() override;
    std::string suffix() override;
    double get() override;
    void set(double value) override;
};

class ColorBinding final : public ColorValue {
public:
    std::function<uint32_t()> getter;
    std::function<void(uint32_t)> setter;
    bool hasDefault;
    uint32_t defaultColor;

    ColorBinding(const std::string& owner, std::function<uint32_t()> getter, std::function<void(uint32_t)> setter);
    uint32_t get() override;
    void set(uint32_t argb) override;
    bool resettable() override;
    void reset() override;

    ColorBinding& resetTo(uint32_t argb);
};

class TextBinding final : public TextValue {
public:
    std::function<std::string()> getter;
    std::function<void(const std::string&)> setter;
    std::string placeholder;
    int limit;
    bool masked;

    TextBinding(const std::string& owner, std::function<std::string()> getter,
                std::function<void(const std::string&)> setter, const std::string& placeholder);
    std::string get() override;
    void set(const std::string& value) override;
    std::string hint() override;
    int maxLength() override;
    bool secret() override;
};

class KeyBinding final : public KeyValue {
public:
    std::function<int()> getter;
    std::function<void(int)> setter;

    KeyBinding(const std::string& owner, std::function<int()> getter, std::function<void(int)> setter);
    int get() override;
    void set(int key) override;
};

class ShortcutBinding final : public ShortcutValue {
public:
    std::vector<int>* combos;
    std::vector<int> defaults;
    std::function<void(int combo)> onRecord;

    ShortcutBinding(const std::string& owner, std::vector<int>& combos, const std::vector<int>& defaults);
    std::vector<int> get() override;
    void set(const std::vector<int>& next) override;
    void record(int combo) override;
    void reset() override;
};

namespace Bind {

    std::string idOf(const void* address);

    std::shared_ptr<FlagBinding> flag(bool& value, const std::string& id = "");
    std::shared_ptr<FlagBinding> flag(const std::string& id, std::function<bool()> getter,
                                      std::function<void(bool)> setter);

    std::shared_ptr<ModeBinding> mode(std::string& value, const std::vector<std::string>& options,
                                      const std::string& id = "");
    std::shared_ptr<ModeBinding> mode(const std::string& id, std::function<std::string()> getter,
                                      std::function<void(const std::string&)> setter,
                                      std::function<std::vector<std::string>()> options);

    std::shared_ptr<MultiBinding> multi(std::vector<std::string>& selection, const std::vector<std::string>& options,
                                        const std::string& id = "");
    std::shared_ptr<MultiBinding> reorderable(std::vector<std::string>& selection, std::vector<std::string>& options,
                                              const std::string& id = "");

    std::shared_ptr<NumberBinding> number(double& value, double min, double max, double step,
                                          const std::string& suffix = "", const std::string& id = "");
    std::shared_ptr<NumberBinding> number(float& value, double min, double max, double step,
                                          const std::string& suffix = "", const std::string& id = "");
    std::shared_ptr<NumberBinding> number(int& value, int min, int max, int step = 1, const std::string& suffix = "",
                                          const std::string& id = "");
    std::shared_ptr<NumberBinding> number(const std::string& id, std::function<double()> getter,
                                          std::function<void(double)> setter, double min, double max, double step,
                                          const std::string& suffix = "");

    std::shared_ptr<ColorBinding> color(uint32_t& argb, const std::string& id = "");
    std::shared_ptr<ColorBinding> color(const std::string& id, std::function<uint32_t()> getter,
                                        std::function<void(uint32_t)> setter);

    std::shared_ptr<TextBinding> text(std::string& value, const std::string& hint = "", const std::string& id = "");
    std::shared_ptr<TextBinding> text(const std::string& id, std::function<std::string()> getter,
                                      std::function<void(const std::string&)> setter, const std::string& hint = "");

    std::shared_ptr<KeyBinding> key(int& key, const std::string& id = "");
    std::shared_ptr<KeyBinding> key(const std::string& id, std::function<int()> getter,
                                    std::function<void(int)> setter);

    std::shared_ptr<ShortcutBinding> shortcut(std::vector<int>& combos, const std::vector<int>& defaults,
                                              const std::string& id = "");
}
