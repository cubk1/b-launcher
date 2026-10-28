#pragma once

#include <core/base/error.hpp>
#include <core/meta/version.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace b_launcher_base::meta::detail
{

using json = nlohmann::json;

const json *member (const json &node, std::string_view key);
std::string string_of (const json &node, std::string_view key,
                       std::string fallback = {});
std::int64_t int_of (const json &node, std::string_view key,
                     std::int64_t fallback = 0);
bool bool_of (const json &node, std::string_view key, bool fallback = false);
result<json> parse_json (std::string_view text);

rule rule_from_json (const json &node);
json rule_to_json (const rule &value);
std::vector<rule> rules_from_json (const json *node);
library library_from_json (const json &node);
json library_to_json (const library &value);
version version_from_json (const json &node);
json version_to_json (const version &value);
download_artifact artifact_from_json (const json &node);
json artifact_to_json (const download_artifact &value, bool with_path);

}
