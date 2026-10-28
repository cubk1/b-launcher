#pragma once

#include <core/base/error.hpp>

#include <nlohmann/json.hpp>

#include <string_view>

namespace b_launcher_base::modpack::detail
{

result<nlohmann::json> parse_toml (std::string_view text);

}
