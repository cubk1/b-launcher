#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base
{

// Read a string member, using the fallback for missing members or other types.
inline std::string
json_text (const nlohmann::json &node, std::string_view key,
           std::string_view fallback = {})
{
  auto it = node.find (key);
  if (it == node.end () || !it->is_string ())
    return std::string (fallback);
  return it->get<std::string> ();
}

inline std::vector<std::string>
json_strings (const nlohmann::json &node, std::string_view key)
{
  std::vector<std::string> out;
  auto it = node.find (key);
  if (it != node.end () && it->is_array ())
    for (const auto &item : *it)
      if (item.is_string ())
        out.push_back (item.get<std::string> ());
  return out;
}

}
