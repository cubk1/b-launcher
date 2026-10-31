#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace b_launcher_base::net
{

struct server_address
{
  std::string host;
  std::uint16_t port = 25565;
  bool explicit_port = false;
};

std::optional<server_address> parse_server_address (std::string_view text);

struct server_status
{
  std::string version;
  std::int32_t protocol = 0;
  std::int32_t online = -1;
  std::int32_t max = -1;
  std::string motd;
  std::string favicon;
  std::chrono::milliseconds latency { 0 };
};

result<server_status> ping_server (std::string_view address,
                                   cancellation_token token = {},
                                   std::chrono::milliseconds timeout
                                   = std::chrono::milliseconds (8000));

}
