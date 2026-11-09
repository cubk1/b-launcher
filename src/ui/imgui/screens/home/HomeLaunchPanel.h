#pragma once

namespace HomeLaunchPanel {

    constexpr float ACCOUNT_H = 60.0f;
    constexpr float LAUNCH_H = 64.0f;

    void account(float x, float y, float w, bool blocked);
    void launch(float x, float y, float w, bool blocked);
    void column(float x, float top, float w, float bottom, bool blocked, float avatar, float avatarRadius);
}
