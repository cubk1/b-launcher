#pragma once

#include <string>

namespace LaunchButton {

    constexpr float SIZE = 52.0f;
    constexpr float GAP = 12.0f;

    void draw(float x, float y);
    float besideX(float buttonX);
    void drawStatus(float right, float bottom);
    void launchAnother(const std::string& key, float left, float top, float bottom);
    void start(const std::string& key);
}
