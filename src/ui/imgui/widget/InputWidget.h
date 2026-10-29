#pragma once

#include "Widget.h"

#include <cstdint>

class InputWidget final : public Widget {
public:
    float textX;
    float room;
    int family;
    float size;
    float caretH;
    int maxLength;
    bool centered;
    std::string placeholder;
    uint32_t textColor;
    std::string trailingText;

    explicit InputWidget(const std::string& id);

    InputWidget& trailing(const std::string& text);
    InputWidget& area(float textX, float room, float size, float caretH);
    InputWidget& limit(int maxLength);
    InputWidget& center();
    InputWidget& hint(const std::string& placeholder);
    InputWidget& tint(uint32_t textColor);
    void paint() override;
};
