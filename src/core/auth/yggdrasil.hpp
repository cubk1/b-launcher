#pragma once

#include <core/auth/account.hpp>
#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::auth
{

inline constexpr std::string_view littleskin_api
    = "https://littleskin.cn/api/yggdrasil";
inline constexpr std::string_view nide8_admin_url
    = "https://login.mc-user.com:233/server/intro";

struct yggdrasil_server
{
  std::string api;
  std::string name;
  std::string homepage;
  std::string register_url;
  std::string metadata;
};

struct yggdrasil_profile
{
  std::string id;
  std::string name;
};

struct yggdrasil_session
{
  account base;
  std::vector<yggdrasil_profile> profiles;

  bool
  ready () const noexcept
  {
    return !base.uuid.empty ();
  }
};

bool is_yggdrasil (account_type type) noexcept;
bool valid_nide8_id (std::string_view id) noexcept;
std::string nide8_api (std::string_view server_id);
std::string nide8_register_url (std::string_view server_id);
std::string yggdrasil_api (const account &a);
bool same_yggdrasil_server (std::string_view a, std::string_view b);

result<yggdrasil_server> resolve_yggdrasil (std::string_view url,
                                            cancellation_token token = {});
result<yggdrasil_session> yggdrasil_login (account_type type,
                                           std::string_view server,
                                           std::string_view username,
                                           std::string_view password,
                                           cancellation_token token = {});
result<account> yggdrasil_select (const account &base,
                                  const yggdrasil_profile &profile,
                                  cancellation_token token = {});
result<account> yggdrasil_refresh (const account &current,
                                   cancellation_token token = {});
result<account> yggdrasil_ensure (const account &current,
                                  cancellation_token token = {});
result<std::vector<std::string>>
yggdrasil_agent (const account &a, const std::filesystem::path &directory,
                 cancellation_token token = {});

}
