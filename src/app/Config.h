#pragma once

#include <nlohmann/json.hpp>

namespace Config {

    nlohmann::json& data();
    bool save();
}
