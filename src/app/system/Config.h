#pragma once

#include <nlohmann/json.hpp>

namespace Config {

    nlohmann::json& data();
    bool save();

    // Restores defaults while retaining registered game folders. Restart after success.
    bool reset();
}
