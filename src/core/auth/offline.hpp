#pragma once

#include <core/auth/account.hpp>
#include <core/base/error.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace b_launcher_base::auth
{

std::string offline_uuid (std::string_view name);
bool valid_player_name (std::string_view name) noexcept;
result<account> offline_account (std::string_view name,
                                 std::optional<std::string> uuid
                                 = std::nullopt);

}
