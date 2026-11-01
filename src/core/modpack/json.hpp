#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace b_launcher_base::modpack::detail
{

// Mod and modpack metadata also accepts numeric values in text fields.
inline std::string
text_of (const nlohmann::json &node, const char *key)
{
  if (!node.is_object ())
    return {};
  auto it = node.find (key);
  if (it == node.end ())
    return {};
  if (it->is_string ())
    return it->get<std::string> ();
  if (it->is_number ())
    return it->dump ();
  return {};
}

inline nlohmann::json
parse_json (std::string_view text)
{
  if (text.starts_with ("\xEF\xBB\xBF"))
    text.remove_prefix (3);
  return nlohmann::json::parse (text, nullptr, false, true);
}

}
