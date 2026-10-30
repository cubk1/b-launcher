#pragma once

#include <windows.h>

#include <string>

namespace ForgottenMelody {

    // The shortcut maker of an instance, in the look of the login dialog; returns once it is closed.
    void showShortcut(HWND parent, const std::string& instanceKey);
}
