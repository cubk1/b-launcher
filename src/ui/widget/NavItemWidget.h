#pragma once

#include "Widget.h"

#include <cstdint>

class NavItemWidget final : public Widget {
public:
    std::string glyph;
    uint32_t texture;
    float textureSize;
    std::string label;
    std::string aside;
    bool active;
    bool child;
    bool pressed;

    NavItemWidget(const std::string& id, const std::string& glyph, const std::string& label);
    NavItemWidget& selected(bool value);
    NavItemWidget& nested(bool value);
    NavItemWidget& image(uint32_t texture, float size = 16.0f);
    NavItemWidget& note(const std::string& text);
    void paint() override;
};
