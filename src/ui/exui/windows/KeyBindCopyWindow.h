#pragma once
#include <filesystem>
#include <windows.h>

namespace ForgottenMelody {
    void showCopyKeyBinds(HWND parent, const std::filesystem::path& source);
}
