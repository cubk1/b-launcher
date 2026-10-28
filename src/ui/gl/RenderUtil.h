#pragma once

#include <cstdint>
#include <string>

namespace RenderUtil {

    void gradientH(float x, float y, float w, float h, uint32_t left, uint32_t right);
    void gradientV(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color);
    void smoothTriangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color);
    uint32_t fade(uint32_t a, uint32_t b, double t);
    uint32_t withAlpha(uint32_t color, int alpha);
    std::string keyName(int key);
}
