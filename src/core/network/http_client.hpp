#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::net
{

struct network_settings
{
  std::string proxy;
  bool use_system_proxy = true;
  std::string user_agent = "b-launcher-base/0.1";
  std::chrono::seconds connect_timeout { 15 };
  std::chrono::seconds low_speed_time { 30 };
  long low_speed_limit = 1024;
  bool verify_tls = true;
  bool verbose = false;
};

void set_network_settings (network_settings settings);
network_settings get_network_settings ();
std::string effective_proxy ();

struct http_header
{
  std::string name;
  std::string value;
};

struct http_request
{
  std::string method = "GET";
  std::string url;
  std::vector<http_header> headers;
  std::string body;
  std::chrono::milliseconds timeout { 60000 };
  bool follow_redirects = true;
};

struct http_response
{
  long status = 0;
  std::string body;
  std::vector<http_header> headers;
  std::string effective_url;

  std::string_view header (std::string_view name) const;

  bool
  ok () const noexcept
  {
    return status >= 200 && status < 300;
  }
};

result<http_response> http_send (const http_request &request,
                                 cancellation_token token = {});
result<std::string> http_get (std::string_view url,
                              cancellation_token token = {});
result<std::string> http_get (std::span<const std::string> urls,
                              cancellation_token token = {});
result<http_response> http_post_json (std::string_view url, std::string body,
                                      std::vector<http_header> headers = {},
                                      cancellation_token token = {});
result<http_response>
http_post_form (std::string_view url,
                std::span<const std::pair<std::string, std::string>> fields,
                cancellation_token token = {});

}
