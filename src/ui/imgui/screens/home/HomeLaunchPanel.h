#pragma once

namespace HomeLaunchPanel {

    // Account card over the launch card, with the version controls beside it.
    constexpr float HEIGHT = 168.0f;

    float width(float left, float right);
    // Returns the bottom edge of the panel.
    float draw(float left, float top, float right, bool blocked);
}
