#pragma once

namespace Onboarding {

    // True while the guide is on screen; it owns the whole window.
    bool drawing();

    // The shell underneath must not be drawn or clicked.
    bool covering();

    void draw();

    // Runs the guide from the first step.
    void start();

    bool suggested();
    void dismiss();
}
