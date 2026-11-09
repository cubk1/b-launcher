#pragma once

namespace DownloadButton {

    constexpr float SIZE = 52.0f;
    constexpr float MARGIN = 28.0f;

    bool visible();
    float shown();
    float inset();
    void draw(float x, float y);
}
