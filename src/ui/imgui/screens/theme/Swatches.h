#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

struct Swatches {
    bool valid = false;
    uint32_t dominant = 0;
    uint32_t vibrant = 0;
    uint32_t darkVibrant = 0;
    uint32_t lightVibrant = 0;
    uint32_t muted = 0;
    uint32_t darkMuted = 0;
    uint32_t lightMuted = 0;
    std::vector<uint32_t> ranked;

    static Swatches of(const std::vector<uint32_t>& pixels);
    static std::vector<uint32_t> thumbnail(const unsigned char* rgba, int width, int height);
    nlohmann::json json() const;
    bool operator==(const Swatches& other) const = default;
};
