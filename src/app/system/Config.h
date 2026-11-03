#pragma once

#include <nlohmann/json.hpp>

namespace Config {

    nlohmann::json& data();
    bool save();

    // Restores defaults; callers restart after success so no stale state stays live.
    bool reset();
}
