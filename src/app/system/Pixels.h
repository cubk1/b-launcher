#pragma once

#include <cstddef>
#include <memory>
#include <vector>

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    static std::shared_ptr<const Pixels> decode(const void* data, size_t size);
};
