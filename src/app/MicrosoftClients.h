#pragma once

#include <core/auth/microsoft.hpp>

#include <string>

namespace MicrosoftClients {

    enum class App { Launcher, Official, Hmcl, Pcl };

    constexpr int REDIRECT_PORT = 41275;
    constexpr const char* REDIRECT_PATH = "/auth";
    constexpr const char* REDIRECT_URI = "http://localhost:41275/auth";

    b_launcher_base::auth::microsoft_client client(App app);
    App of(const std::string& clientId);
    const char* name(App app);
}
