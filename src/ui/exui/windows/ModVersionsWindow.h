#pragma once

#include <filesystem>
#include <string>
#include <windows.h>

namespace ForgottenMelody {
    void showModVersions(HWND parent, const std::string& instance, const std::filesystem::path& file);
}
