#pragma once

namespace Onboarding {

    // The wizard still has to be painted; it owns the whole window.
    bool drawing();

    // The shell underneath must not be drawn or clicked.
    bool covering();

    void draw();

    // Runs the wizard again from the first step, without touching stored settings.
    void start();

    // Remembers an unfinished wizard so the next start asks again.
    void remember();
}
