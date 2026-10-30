#pragma once

#include <windows.h>

#include <string>

namespace ForgottenMelody {

    // The mods of an instance, in the look of the instance manager; returns once it is closed.
    void showMods(HWND parent, const std::string& instanceKey);
}
