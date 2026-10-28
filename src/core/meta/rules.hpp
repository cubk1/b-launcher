#pragma once

#include <core/base/platform.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::meta
{

struct os_rule
{
  std::string name;
  std::string version;
  std::string arch;
};

struct rule
{
  bool allow = true;
  std::optional<os_rule> os;
  std::vector<std::pair<std::string, bool>> features;
};

struct rule_context
{
  os_type os = os_type::unknown;
  std::string os_version;
  cpu_arch arch = cpu_arch::unknown;
  std::map<std::string, bool, std::less<>> features;

  static rule_context current ();
  bool feature (std::string_view name) const;
};

bool rule_matches (const rule &value, const rule_context &context);
bool rules_allow (std::span<const rule> rules, const rule_context &context);
bool simple_regex_search (std::string_view pattern, std::string_view text);

}
