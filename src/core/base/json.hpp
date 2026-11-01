#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace b_launcher_base
{

// Read a string member. Missing members and other JSON types yield empty text.
inline std::string
json_text (const nlohmann::json &node, std::string_view key)
{
  auto it = node.find (key);
  if (it == node.end () || !it->is_string ())
    return {};
  return it->get<std::string> ();
}

}
