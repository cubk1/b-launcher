#pragma once

#include <windows.h>

#include <string>

namespace ForgottenMelody {

    // 修改加载器 of an instance, in the look of the other dialogs; returns once it is closed, while the change it
    // started goes on.
    void showLoaders(HWND parent, const std::string& instanceKey);
}
