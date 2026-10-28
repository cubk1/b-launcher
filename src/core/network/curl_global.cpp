#include "core/network/curl_global.hpp"

#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>

#include "core/base/win_util.hpp"

#include <array>
#include <chrono>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace b_launcher_base::net
{

namespace
{

constexpr log::logger s_log ("network");

struct host_health
{
  int consecutive = 0;
  std::chrono::steady_clock::time_point until {};
};

struct global_state
{
  std::mutex health_mutex;
  std::unordered_map<std::string, host_health> health;
  std::mutex settings_mutex;
  network_settings settings;
  std::once_flag init_once;
  CURLSH *share = nullptr;
  std::array<std::mutex, CURL_LOCK_DATA_LAST> locks;
};

global_state &
state ()
{
  static global_state *instance = new global_state ();
  return *instance;
}

void
lock_cb (CURL *, curl_lock_data data, curl_lock_access, void *)
{
  state ().locks[static_cast<std::size_t> (data)].lock ();
}

void
unlock_cb (CURL *, curl_lock_data data, void *)
{
  state ().locks[static_cast<std::size_t> (data)].unlock ();
}

#if defined(_WIN32)

std::string
system_proxy ()
{
  HKEY key;
  if (RegOpenKeyExW (
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
          0, KEY_READ, &key)
      != ERROR_SUCCESS)
    return {};
  DWORD enabled = 0;
  DWORD size = sizeof (enabled);
  std::string proxy;
  if (RegQueryValueExW (key, L"ProxyEnable", nullptr, nullptr,
                        reinterpret_cast<LPBYTE> (&enabled), &size)
          == ERROR_SUCCESS
      && enabled)
    {
      wchar_t buffer[512];
      DWORD bytes = sizeof (buffer);
      DWORD type = 0;
      if (RegQueryValueExW (key, L"ProxyServer", nullptr, &type,
                            reinterpret_cast<LPBYTE> (buffer), &bytes)
              == ERROR_SUCCESS
          && type == REG_SZ)
        {
          std::wstring value (buffer, bytes / sizeof (wchar_t));
          while (!value.empty () && value.back () == L'\0')
            value.pop_back ();
          proxy = b_launcher_base::detail::wide_to_utf8 (value);
        }
    }
  RegCloseKey (key);
  if (proxy.find ('=') != std::string::npos)
    {
      std::string chosen;
      for (auto part : split (proxy, ';'))
        {
          auto eq = part.find ('=');
          if (eq == std::string_view::npos)
            continue;
          auto scheme = part.substr (0, eq);
          auto value = part.substr (eq + 1);
          if (scheme == "https" || (chosen.empty () && scheme == "http"))
            chosen = std::string (value);
          if (scheme == "socks" && chosen.empty ())
            chosen = "socks5h://" + std::string (value);
        }
      proxy = chosen;
    }
  return std::string (trim (proxy));
}

#else

std::string
system_proxy ()
{
  return {};
}

#endif

}

void
set_network_settings (network_settings settings)
{
  auto &s = state ();
  std::lock_guard lock (s.settings_mutex);
  s.settings = std::move (settings);
}

network_settings
get_network_settings ()
{
  auto &s = state ();
  std::lock_guard lock (s.settings_mutex);
  return s.settings;
}

std::string
effective_proxy ()
{
  auto settings = get_network_settings ();
  if (!settings.proxy.empty ())
    return settings.proxy;
  if (settings.use_system_proxy)
    return system_proxy ();
  return {};
}

namespace detail
{

void
ensure_curl ()
{
  auto &s = state ();
  std::call_once (s.init_once, [&s] {
    curl_global_init (CURL_GLOBAL_DEFAULT);
    s.share = curl_share_init ();
    curl_share_setopt (s.share, CURLSHOPT_LOCKFUNC, lock_cb);
    curl_share_setopt (s.share, CURLSHOPT_UNLOCKFUNC, unlock_cb);
    curl_share_setopt (s.share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt (s.share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    auto *info = curl_version_info (CURLVERSION_NOW);
    s_log.debug ("libcurl {} ssl={}", info->version,
                 info->ssl_version ? info->ssl_version : "none");
  });
}

CURLSH *
shared_handle ()
{
  ensure_curl ();
  return state ().share;
}

void
apply_common_options (CURL *easy, const network_settings &settings,
                      const std::string &proxy)
{
  curl_easy_setopt (easy, CURLOPT_USERAGENT, settings.user_agent.c_str ());
  curl_easy_setopt (easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt (easy, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt (easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (easy, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt (easy, CURLOPT_CONNECTTIMEOUT,
                    static_cast<long> (settings.connect_timeout.count ()));
  curl_easy_setopt (easy, CURLOPT_LOW_SPEED_LIMIT, settings.low_speed_limit);
  curl_easy_setopt (easy, CURLOPT_LOW_SPEED_TIME,
                    static_cast<long> (settings.low_speed_time.count ()));
  curl_easy_setopt (easy, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt (easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#if defined(_WIN32)
  curl_easy_setopt (easy, CURLOPT_SSL_OPTIONS,
                    static_cast<long> (CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif
  if (!settings.verify_tls)
    {
      curl_easy_setopt (easy, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt (easy, CURLOPT_SSL_VERIFYHOST, 0L);
    }
  if (!proxy.empty ())
    curl_easy_setopt (easy, CURLOPT_PROXY, proxy.c_str ());
  if (settings.verbose)
    curl_easy_setopt (easy, CURLOPT_VERBOSE, 1L);
}

error
curl_failure (CURLcode code, const char *buffer, std::string_view url)
{
  errc kind = errc::network;
  if (code == CURLE_OPERATION_TIMEDOUT)
    kind = errc::timeout;
  else if (code == CURLE_ABORTED_BY_CALLBACK)
    kind = errc::cancelled;
  std::string detail = buffer && *buffer ? buffer : curl_easy_strerror (code);
  return make_error (kind, sformat ("{} ({})", detail, url));
}

std::string
host_of (std::string_view url)
{
  auto scheme = url.find ("://");
  auto begin = scheme == std::string_view::npos ? 0 : scheme + 3;
  auto end = url.find ('/', begin);
  return to_lower (url.substr (begin, end == std::string_view::npos
                                          ? std::string_view::npos
                                          : end - begin));
}

void
note_host (std::string_view url, bool healthy)
{
  auto &s = state ();
  auto host = host_of (url);
  std::lock_guard lock (s.health_mutex);
  auto &entry = s.health[host];
  if (healthy)
    {
      entry.consecutive = 0;
      entry.until = {};
      return;
    }
  if (++entry.consecutive >= 3)
    {
      if (entry.until <= std::chrono::steady_clock::now ())
        s_log.warn ("source {} is failing, preferring alternatives", host);
      entry.until
          = std::chrono::steady_clock::now () + std::chrono::seconds (60);
    }
}

bool
host_penalized (std::string_view url)
{
  auto &s = state ();
  auto host = host_of (url);
  std::lock_guard lock (s.health_mutex);
  auto it = s.health.find (host);
  return it != s.health.end ()
         && it->second.until > std::chrono::steady_clock::now ();
}

bool
host_failure (CURLcode code, long status)
{
  if (code == CURLE_ABORTED_BY_CALLBACK || code == CURLE_WRITE_ERROR)
    return false;
  if (code == CURLE_HTTP_RETURNED_ERROR || code == CURLE_OK)
    return status == 429 || status >= 500;
  return true;
}
}

}
