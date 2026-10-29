#pragma once

#include "Widget.h"

#include <cstdint>

class IconButtonWidget final : public Widget {
public:
    std::string glyph;
    uint32_t color;
    uint32_t hoverColor;
    bool pressed;

    IconButtonWidget(const std::string& id, const std::string& glyph, uint32_t color, uint32_t hoverColor);
    void paint() override;
};
