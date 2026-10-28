#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Flag {
public:
    std::string owner;

    explicit Flag(const std::string& owner) : owner(owner) {}

    virtual ~Flag() = default;

    virtual bool isOn() {
        return false;
    }

    virtual void flip() {}
};

class Choice {
public:
    std::string owner;

    explicit Choice(const std::string& owner) : owner(owner) {}

    virtual ~Choice() = default;

    virtual std::vector<std::string> options() {
        return std::vector<std::string>();
    }

    virtual bool multi() {
        return false;
    }

    virtual bool selected(const std::string& option) {
        (void) option;
        return false;
    }

    virtual void choose(const std::string& option) {
        (void) option;
    }

    virtual std::string label();

    virtual bool reorderable() {
        return false;
    }

    virtual void reorder(int from, int to) {
        (void) from;
        (void) to;
    }

    virtual std::string optionIcon(const std::string& option) {
        (void) option;
        return std::string();
    }

    virtual uint32_t optionImage(const std::string& option) {
        (void) option;
        return 0;
    }

    virtual bool anchorGlyphs() {
        return false;
    }

    virtual uint32_t optionColor(const std::string& option, bool selected);
};

class NumberValue {
public:
    std::string owner;

    explicit NumberValue(const std::string& owner) : owner(owner) {}

    virtual ~NumberValue() = default;

    virtual double min() {
        return 0.0;
    }

    virtual double max() {
        return 1.0;
    }

    virtual double step() {
        return 0.0;
    }

    virtual std::string suffix() {
        return "";
    }

    virtual double get() {
        return 0.0;
    }

    virtual void set(double value) {
        (void) value;
    }

    void nudge(double direction);
    void apply(double value);
};

class ColorValue {
public:
    std::string owner;

    explicit ColorValue(const std::string& owner) : owner(owner) {}

    virtual ~ColorValue() = default;

    virtual uint32_t get() {
        return 0xFFFFFFFFu;
    }

    virtual void set(uint32_t argb) {
        (void) argb;
    }

    virtual bool available() {
        return true;
    }

    virtual bool resettable() {
        return false;
    }

    virtual void reset() {}
};

class TextValue {
public:
    std::string owner;

    explicit TextValue(const std::string& owner) : owner(owner) {}

    virtual ~TextValue() = default;

    virtual std::string get() {
        return std::string();
    }

    virtual void set(const std::string& value) {
        (void) value;
    }

    virtual std::string hint() {
        return std::string();
    }

    virtual int maxLength() {
        return 32;
    }

    virtual bool secret() {
        return false;
    }
};

class KeyValue {
public:
    std::string owner;

    explicit KeyValue(const std::string& owner) : owner(owner) {}

    virtual ~KeyValue() = default;

    virtual int get() {
        return -1;
    }

    virtual void set(int key) {
        (void) key;
    }
};

class ShortcutValue {
public:
    std::string owner;

    explicit ShortcutValue(const std::string& owner) : owner(owner) {}

    virtual ~ShortcutValue() = default;

    virtual std::vector<int> get() {
        return std::vector<int>();
    }

    virtual void set(const std::vector<int>& combos) {
        (void) combos;
    }

    virtual void record(int combo) {
        set(std::vector<int>{combo});
    }

    virtual void reset() {}
};
