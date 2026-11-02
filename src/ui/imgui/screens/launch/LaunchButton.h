#pragma once

#include <string>

namespace LaunchButton {

    constexpr float SIZE = 52.0f;

    void draw(float x, float y);
    void drawStatus(float right, float bottom);
    void launchAnother(const std::string& key, float left, float top, float bottom);
}
