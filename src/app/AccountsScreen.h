#pragma once

#include <core/auth/account.hpp>

#include <string>

namespace AccountsScreen {

    void draw();
    void openAdd();
    void openServerLogin(b_launcher_base::auth::account_type type, const std::string& server,
                         const std::string& username);
}
