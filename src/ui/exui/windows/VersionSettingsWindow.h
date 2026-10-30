#pragma once

#include <windows.h>

#include <string>

namespace ForgottenMelody {

    // The settings of one instance, in the frame of the settings window with the groups of the default interface as
    // its sections; returns once it is closed.
    void showVersionSettings(HWND parent, const std::string& key);
}
