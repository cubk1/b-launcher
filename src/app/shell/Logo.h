#pragma once

#include <vector>

namespace Logo {

    void draw(float x, float y, float size, float alpha = 1.0f);
    std::vector<unsigned char> pixels(int size);
}
