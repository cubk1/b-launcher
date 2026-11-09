#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MaterialYou {

    uint32_t hct(double hue, double chroma, double tone);
    void toHct(uint32_t argb, double& hue, double& chroma, double& tone);
    uint32_t tone(double hue, double chroma, double tone);
    uint32_t harmonize(uint32_t design, uint32_t key);
    std::vector<uint32_t> ranked(const std::vector<uint32_t>& pixels, int count);
    std::vector<std::pair<uint32_t, uint32_t>> quantize(const std::vector<uint32_t>& pixels, int count);
}
