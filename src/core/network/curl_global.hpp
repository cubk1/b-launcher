#pragma once

#include <core/network/http_client.hpp>

#include <curl/curl.h>

namespace b_launcher_base::net::detail
{

void ensure_curl ();
CURLSH *shared_handle ();
void apply_common_options (CURL *easy, const network_settings &settings,
                           const std::string &proxy);
error curl_failure (CURLcode code, const char *buffer, std::string_view url);
std::string host_of (std::string_view url);
void note_host (std::string_view url, bool healthy);
bool host_penalized (std::string_view url);
bool host_failure (CURLcode code, long status);

}
