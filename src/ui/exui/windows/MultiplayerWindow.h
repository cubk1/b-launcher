#pragma once

#include <windows.h>

namespace ForgottenMelody {
    // A modeless window; closing it keeps the room connected.
    void showMultiplayer(HWND parent);
    void closeMultiplayer();
}
