#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

struct ImageSample {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    static ImageSample of(const unsigned char* rgba, int width, int height);
    nlohmann::json json() const;
    bool operator==(const ImageSample& other) const = default;
};
