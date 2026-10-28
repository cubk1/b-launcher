#pragma once

#include <core/base/error.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::auth
{

enum class account_type : std::uint8_t
{
  offline,
  microsoft,
};

struct account
{
  account_type type = account_type::offline;
  std::string name;
  std::string uuid;
  std::string access_token;
  std::string refresh_token;
  std::string xuid;
  std::string client_id;
  std::int64_t expires_at = 0;
  std::string skin_url;

  std::string user_type () const;
  bool needs_refresh (std::chrono::seconds margin
                      = std::chrono::seconds (300)) const;
};

std::string_view account_type_name (account_type type) noexcept;
std::string format_uuid (std::string_view hex);
std::string strip_uuid (std::string_view uuid);
std::string serialize_accounts (std::span<const account> accounts);
result<std::vector<account>> parse_accounts (std::string_view json);
std::int64_t unix_now ();

}
