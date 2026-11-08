#pragma once

namespace Compact {

    constexpr float WIDTH = 360.0f;
    constexpr float HEIGHT = 540.0f;

    bool active();
    void setActive(bool on);
    bool stored();
    void request(bool on);
    bool takeRequest(bool& on);
    bool takeDismiss();
    void shortcut();
    void hidden();
    void reveal();
    bool takeReveal();
    void draw();
}
