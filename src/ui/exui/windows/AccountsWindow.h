#pragma once

#include <windows.h>

#include <string>

namespace ForgottenMelody {

    // The account manager, in the look of the 2014 更新游戏 window; returns once it is closed. Given how an
    // instance that has no account logs in, the login dialog opens over it, set to the instance's server, when that
    // is a third-party one.
    void showAccounts(HWND parent, const std::string& login = {}, const std::string& authServer = {});
}
