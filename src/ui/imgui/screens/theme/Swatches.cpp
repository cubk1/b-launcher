#include "ui/imgui/screens/theme/Swatches.h"
#include "ui/imgui/screens/theme/MaterialYou.h"

#include "ui/imgui/theme/ColorMath.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr int THUMB_W = 112;
    constexpr int THUMB_H = 70;
    constexpr float VIVID = 0.07f;

    struct Target {
        uint32_t Swatches::* slot;
        float lightness;
        bool vivid;
    };

    const Target TARGETS[] = {
            {&Swatches::vibrant, 0.62f, true},      {&Swatches::darkVibrant, 0.38f, true},
            {&Swatches::lightVibrant, 0.82f, true}, {&Swatches::muted, 0.62f, false},
            {&Swatches::darkMuted, 0.36f, false},   {&Swatches::lightMuted, 0.86f, false},
    };
}

std::vector<uint32_t> Swatches::thumbnail(const unsigned char* rgba, int width, int height) {
    std::vector<uint32_t> out;
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return out;
    }
    out.reserve((size_t) THUMB_W * THUMB_H);
    for (int ty = 0; ty < THUMB_H; ty++) {
        int y0 = ty * height / THUMB_H;
        int y1 = std::max(y0 + 1, (ty + 1) * height / THUMB_H);
        for (int tx = 0; tx < THUMB_W; tx++) {
            int x0 = tx * width / THUMB_W;
            int x1 = std::max(x0 + 1, (tx + 1) * width / THUMB_W);
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
            if (n == 0) {
                continue;
            }
            out.push_back(0xFF000000u | ((uint32_t) (r / n) << 16) | ((uint32_t) (g / n) << 8) | (uint32_t) (b / n));
        }
    }
    return out;
}

Swatches Swatches::of(const std::vector<uint32_t>& pixels) {
    Swatches out;
    std::vector<std::pair<uint32_t, uint32_t>> clusters = MaterialYou::quantize(pixels, 64);
    if (clusters.empty()) {
        return out;
    }
    out.valid = true;
    out.dominant = clusters.front().first;
    out.ranked = MaterialYou::ranked(pixels, 4);
    float top = (float) clusters.front().second;
    std::vector<uint32_t> used;
    for (const Target& target : TARGETS) {
        float best = -1.0f;
        uint32_t chosen = out.dominant;
        for (const auto& [argb, population] : clusters) {
            ColorMath::Oklch value = ColorMath::toOklch(argb);
            float closeness = 1.0f - std::min(1.0f, std::abs(value.l - target.lightness) / 0.5f);
            float tint = target.vivid ? std::min(1.0f, value.c / 0.25f) : 1.0f - std::min(1.0f, value.c / VIVID);
            float score = 0.3f * ((float) population / top) + 0.4f * closeness + 0.3f * tint;
            if (target.vivid ? value.c < VIVID * 0.5f : value.c > VIVID * 2.0f) {
                score *= 0.25f;
            }
            if (std::find(used.begin(), used.end(), argb) != used.end()) {
                score *= 0.5f;
            }
            if (score > best) {
                best = score;
                chosen = argb;
            }
        }
        out.*(target.slot) = chosen;
        used.push_back(chosen);
    }
    return out;
}

nlohmann::json Swatches::json() const {
    if (!valid) {
        return nullptr;
    }
    return {{"dominant", dominant},         {"vibrant", vibrant}, {"darkVibrant", darkVibrant},
            {"lightVibrant", lightVibrant}, {"muted", muted},     {"darkMuted", darkMuted},
            {"lightMuted", lightMuted},     {"ranked", ranked}};
}
