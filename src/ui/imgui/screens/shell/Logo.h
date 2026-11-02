#pragma once

#include <string>
#include <vector>

namespace Logo {

    void draw(float x, float y, float size, float alpha = 1.0f);
    void brand(float x, float y, float size, float alpha = 1.0f);
    std::vector<unsigned char> pixels(int size);

    std::string customPath();
    void setCustomPath(const std::string& path);
    bool preparing();
    void poll();
    void shutdown();
}
