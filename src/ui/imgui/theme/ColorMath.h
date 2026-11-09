#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ColorMath {

    struct Oklch {
        float l = 0.0f;
        float c = 0.0f;
        float h = 0.0f;
    };

    struct Hsl {
        float h = 0.0f;
        float s = 0.0f;
        float l = 0.0f;
    };

    bool parse(std::string_view text, uint32_t& argb);
    std::string format(uint32_t argb);
    uint32_t add(uint32_t argb, int amount);
    uint32_t scale(uint32_t argb, float factor);
    uint32_t withAlpha(uint32_t argb, int alpha);

    Oklch toOklch(uint32_t argb);
    uint32_t fromOklch(float l, float c, float h, int alpha = 255);
    Hsl toHsl(uint32_t argb);
    uint32_t fromHsl(double h, double s, double l, int alpha = 255);
    uint32_t fromHsl2(double h, double s, double l, int alpha = 255);
    float luminance(uint32_t argb);
    float contrast(uint32_t a, uint32_t b);
    uint32_t over(uint32_t top, uint32_t bottom);
    uint32_t ensureContrast(uint32_t foreground, uint32_t background, float ratio);
    uint32_t rotate(uint32_t argb, float degrees);
    uint32_t lighten(uint32_t argb, float amount);
}
