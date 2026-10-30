#pragma once

#include <windows.h>

#include <string>

namespace ForgottenMelody {

    // The modpack export of an instance, in the look of the other dialogs; returns once it is closed, while the
    // export it started goes on.
    void showExport(HWND parent, const std::string& instanceKey);
}
