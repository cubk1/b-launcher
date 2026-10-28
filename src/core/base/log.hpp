#pragma once

#include <core/base/error.hpp>
#include <core/base/format.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace b_launcher_base::log
{

enum class level : std::uint8_t
{
  trace,
  debug,
  info,
  warn,
  error,
  off,
};

struct record
{
  level severity = level::info;
  std::chrono::system_clock::time_point time;
  std::string_view module;
  std::string_view message;
  std::uint64_t thread_id = 0;
};

using sink = std::function<void (const record &)>;

std::string_view level_name (level value) noexcept;
void set_level (level value) noexcept;
level get_level () noexcept;
bool enabled (level value) noexcept;
int add_sink (sink callback);
void remove_sink (int id);
void set_console (bool enabled);
result<void> open_file (const std::filesystem::path &path);
void close_file ();
void write (level severity, std::string_view module, std::string_view message);
std::string format_record (const record &entry);

class logger
{
public:
  constexpr explicit logger (std::string_view module)
      : m_module (module)
  {
  }

  template <typename... Args>
  void
  log (level severity, std::string_view fmt, Args &&...args) const
  {
    if (!enabled (severity))
      return;
    write (severity, m_module, sformat (fmt, std::forward<Args> (args)...));
  }

  template <typename... Args>
  void
  trace (std::string_view fmt, Args &&...args) const
  {
    log (level::trace, fmt, std::forward<Args> (args)...);
  }

  template <typename... Args>
  void
  debug (std::string_view fmt, Args &&...args) const
  {
    log (level::debug, fmt, std::forward<Args> (args)...);
  }

  template <typename... Args>
  void
  info (std::string_view fmt, Args &&...args) const
  {
    log (level::info, fmt, std::forward<Args> (args)...);
  }

  template <typename... Args>
  void
  warn (std::string_view fmt, Args &&...args) const
  {
    log (level::warn, fmt, std::forward<Args> (args)...);
  }

  template <typename... Args>
  void
  error (std::string_view fmt, Args &&...args) const
  {
    log (level::error, fmt, std::forward<Args> (args)...);
  }

  std::string_view
  module () const noexcept
  {
    return m_module;
  }

private:
  std::string_view m_module;
};

}
