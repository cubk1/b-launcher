#pragma once

#include <cstdint>
#include <string>

namespace ThemeEffects {

    void frame();
    void button(const std::string& id, const char* kind, float x, float y, float w, float h, float radius,
                int cornerMask, uint32_t ink, bool hovered);
}
