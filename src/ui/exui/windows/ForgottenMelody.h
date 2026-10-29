#pragma once

#include <string>

namespace ForgottenMelody {

    // Runs the 忘却的旋律 interface until its main window closes and returns the exit code.
    int run(const std::string& pendingLaunch, bool headless);
}
