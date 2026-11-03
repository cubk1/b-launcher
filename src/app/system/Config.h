#pragma once

#include <nlohmann/json.hpp>

namespace Config {

    nlohmann::json& data();
    bool save();

    // Wipes every stored setting; callers restart so nothing stale stays live.
    void reset();
}
