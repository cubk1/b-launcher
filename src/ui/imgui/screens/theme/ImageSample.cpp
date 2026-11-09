#include "ui/imgui/screens/theme/ImageSample.h"

#include <algorithm>

namespace {
    constexpr int SAMPLE_W = 48;
    constexpr int SAMPLE_H = 30;
}

ImageSample ImageSample::of(const unsigned char* rgba, int width, int height) {
    ImageSample out;
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return out;
    }
    out.width = SAMPLE_W;
    out.height = SAMPLE_H;
    out.pixels.reserve((size_t) SAMPLE_W * SAMPLE_H);
    for (int sy = 0; sy < SAMPLE_H; sy++) {
        int y0 = sy * height / SAMPLE_H;
        int y1 = std::max(y0 + 1, (sy + 1) * height / SAMPLE_H);
        for (int sx = 0; sx < SAMPLE_W; sx++) {
            int x0 = sx * width / SAMPLE_W;
            int x1 = std::max(x0 + 1, (sx + 1) * width / SAMPLE_W);
            uint64_t r = 0;
            uint64_t g = 0;
            uint64_t b = 0;
            uint64_t n = 0;
            for (int y = y0; y < y1 && y < height; y++) {
                for (int x = x0; x < x1 && x < width; x++) {
                    const unsigned char* p = rgba + ((size_t) y * width + x) * 4;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    n++;
                }
            }
            n = std::max<uint64_t>(n, 1);
            out.pixels.push_back(0xFF000000u | ((uint32_t) (r / n) << 16) | ((uint32_t) (g / n) << 8) |
                                 (uint32_t) (b / n));
        }
    }
    return out;
}

nlohmann::json ImageSample::json() const {
    if (pixels.empty()) {
        return nullptr;
    }
    return {{"width", width}, {"height", height}, {"pixels", pixels}};
}
