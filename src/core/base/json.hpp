#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

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
