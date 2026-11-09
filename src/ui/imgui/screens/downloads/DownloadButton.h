#pragma once

namespace DownloadButton {

    constexpr float SIZE = 52.0f;
    constexpr float MARGIN = 28.0f;

    bool visible();
    // How far the button has come in, so callers can keep their own content out of its way.
    float shown();
    // Room the button takes above a page's bottom edge, so lists and pagination stay clear of it.
    float inset();
    void draw(float x, float y);}
