#pragma once

#include <string>

namespace ImGuiApp {

    // Runs the default interface until its window closes and returns the exit code.
    int run(const std::string& pendingLaunch, bool headless);
}
