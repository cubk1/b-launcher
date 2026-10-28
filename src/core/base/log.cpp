#include <core/base/fs.hpp>
#include <core/base/log.hpp>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

namespace b_launcher_base::log
{

namespace
{

struct sink_entry
{
  int id;
  sink callback;
};

struct log_state
{
  std::atomic<level> threshold { level::info };
  std::mutex mutex;
  std::vector<sink_entry> sinks;
  int next_id = 1;
  bool console = false;
  std::FILE *file = nullptr;
};

log_state &
state ()
{
  static log_state instance;
  return instance;
}

std::uint64_t
current_thread_id ()
{
  return static_cast<std::uint64_t> (
      std::hash<std::thread::id> {}(std::this_thread::get_id ()));
}

}

std::string_view
level_name (level value) noexcept
{
  switch (value)
    {
    case level::trace:
      return "TRACE";
    case level::debug:
      return "DEBUG";
    case level::info:
      return "INFO";
    case level::warn:
      return "WARN";
    case level::error:
      return "ERROR";
    case level::off:
      return "OFF";
    }
  return "?";
}

void
set_level (level value) noexcept
{
  state ().threshold.store (value, std::memory_order_relaxed);
}

level
get_level () noexcept
{
  return state ().threshold.load (std::memory_order_relaxed);
}

bool
enabled (level value) noexcept
{
  return value != level::off && value >= get_level ();
}

int
add_sink (sink callback)
{
  auto &s = state ();
  std::lock_guard lock (s.mutex);
  int id = s.next_id++;
  s.sinks.push_back ({ id, std::move (callback) });
  return id;
}

void
remove_sink (int id)
{
  auto &s = state ();
  std::lock_guard lock (s.mutex);
  std::erase_if (s.sinks, [id] (const sink_entry &e) { return e.id == id; });
}

void
set_console (bool enabled)
{
  auto &s = state ();
  std::lock_guard lock (s.mutex);
  s.console = enabled;
}

result<void>
open_file (const std::filesystem::path &path)
{
  if (path.has_parent_path ())
    (void)fs::create_directories (path.parent_path ());
  auto &s = state ();
  std::lock_guard lock (s.mutex);
  if (s.file)
    std::fclose (s.file);
#if defined(_WIN32)
  s.file = _wfopen (path.c_str (), L"ab");
#else
  s.file = std::fopen (path.c_str (), "ab");
#endif
  if (!s.file)
    return make_error (errc::io, "cannot open log file");
  return ok ();
}

void
close_file ()
{
  auto &s = state ();
  std::lock_guard lock (s.mutex);
  if (s.file)
    std::fclose (s.file);
  s.file = nullptr;
}

std::string
format_record (const record &entry)
{
  auto seconds = std::chrono::system_clock::to_time_t (entry.time);
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds> (
                    entry.time.time_since_epoch ())
                    .count ()
                % 1000;
  std::tm parts {};
#if defined(_WIN32)
  localtime_s (&parts, &seconds);
#else
  localtime_r (&seconds, &parts);
#endif
  return sformat ("{:02}:{:02}:{:02}.{:03} [{}] [{}] {}", parts.tm_hour,
                  parts.tm_min, parts.tm_sec, millis,
                  level_name (entry.severity), entry.module, entry.message);
}

void
write (level severity, std::string_view module, std::string_view message)
{
  if (!enabled (severity))
    return;
  record entry { severity, std::chrono::system_clock::now (), module, message,
                 current_thread_id () };
  auto &s = state ();
  std::lock_guard lock (s.mutex);
  if (s.console || s.file)
    {
      auto line = format_record (entry);
      line.push_back ('\n');
      if (s.console)
        std::fwrite (line.data (), 1, line.size (), stderr);
      if (s.file)
        {
          std::fwrite (line.data (), 1, line.size (), s.file);
          if (severity >= level::warn)
            std::fflush (s.file);
        }
    }
  for (auto &e : s.sinks)
    e.callback (entry);
}

}
