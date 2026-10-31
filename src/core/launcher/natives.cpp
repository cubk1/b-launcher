#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/installer/download_plan.hpp>
#include <core/launcher/natives.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <system_error>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace b_launcher_base::launch
{

namespace
{

constexpr log::logger s_log ("natives");
constexpr std::string_view stamp_name = ".blb-natives";
constexpr int unused_days = 7;

}

std::vector<native_archive>
collect_native_archives (const meta::version &resolved,
                         const meta::game_layout &layout,
                         const meta::rule_context &context)
{
  std::vector<native_archive> out;
  for (const auto *lib : install::applicable_libraries (resolved, context))
    {
      if (!lib->is_legacy_native ())
        continue;
      auto classifier = lib->native_classifier (context);
      if (!classifier)
        continue;
      std::string relative;
      auto it = lib->classifiers.find (*classifier);
      if (it != lib->classifiers.end () && !it->second.path.empty ())
        relative = it->second.path;
      else
        relative = lib->artifact.with_classifier (*classifier).path ();
      out.push_back ({ layout.library (relative), lib->extract_exclude });
    }
  return out;
}

namespace
{

result<std::size_t>
extract_into (const std::vector<native_archive> &archives,
              const std::filesystem::path &directory, thread_pool *pool)
{
  BLB_TRY (fs::create_directories (directory));
  std::atomic<std::size_t> extracted { 0 };
  std::mutex error_mutex;
  std::optional<error> failure;
  std::unordered_set<std::string> claimed;
  parallel_for (
      pool ? *pool : thread_pool::shared (), archives.size (),
      [&] (std::size_t i) {
        const auto &archive = archives[i];
        auto reader = zip_reader::open (archive.jar);
        if (!reader)
          {
            std::lock_guard lock (error_mutex);
            failure = reader.err ();
            return;
          }
        auto r
            = reader->for_each ([&] (const zip_entry &entry, zip_reader &zip) {
                if (entry.directory || entry.name.starts_with ("META-INF/"))
                  return true;
                for (const auto &prefix : archive.exclude)
                  if (entry.name.starts_with (prefix))
                    return true;
                if (!fs::safe_relative (entry.name))
                  return true;
                auto target = directory / path_from_utf8 (entry.name);
                if (!fs::is_inside (directory, target))
                  return true;
                {
                  std::lock_guard lock (error_mutex);
                  if (!claimed.insert (to_lower (entry.name)).second)
                    return true;
                }
                if (auto w = zip.extract_current (target); !w)
                  {
                    std::lock_guard lock (error_mutex);
                    failure = w.err ();
                    return false;
                  }
                ++extracted;
                return true;
              });
        if (!r)
          {
            std::lock_guard lock (error_mutex);
            failure = r.err ();
          }
      });
  if (failure)
    return *failure;
  return extracted.load ();
}

bool
stamped (const std::filesystem::path &directory, const std::string &stamp)
{
  auto existing = fs::read_file (directory / stamp_name);
  return existing && *existing == stamp;
}

bool
hex_name (std::wstring_view text, std::size_t length)
{
  return text.size () == length
         && std::all_of (text.begin (), text.end (), [] (wchar_t c) {
              return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
            });
}

bool
launcher_name (std::wstring_view name, std::wstring_view base)
{
  if (name == base)
    return true;
  if (!name.starts_with (base) || name.size () <= base.size ()
      || name[base.size ()] != L'-')
    return false;
  auto rest = name.substr (base.size () + 1);
  if (rest.starts_with (L"gone-"))
    return hex_name (rest.substr (5), 8);
  if (!hex_name (rest.substr (0, std::min<std::size_t> (12, rest.size ())),
                 12))
    return false;
  auto tail = rest.substr (12);
  return tail.empty ()
         || (tail.starts_with (L".part-") && hex_name (tail.substr (6), 8))
         || (tail.starts_with (L"-gone-") && hex_name (tail.substr (6), 8));
}

bool
in_use (const std::filesystem::path &directory)
{
#if defined(_WIN32)
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator it (directory, error),
       end;
       !error && it != end; it.increment (error))
    {
      std::error_code kind;
      if (!it->is_regular_file (kind))
        continue;
      HANDLE file = CreateFileW (
          it->path ().c_str (), GENERIC_READ | GENERIC_WRITE | DELETE, 0,
          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE)
        {
          auto code = GetLastError ();
          if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
            return true;
          continue;
        }
      CloseHandle (file);
    }
  return static_cast<bool> (error);
#else
  (void)directory;
  return false;
#endif
}

void
sweep (const std::filesystem::path &base, const std::filesystem::path &keep)
{
  auto now = fs::mtime_now ();
  auto stale = static_cast<std::int64_t> (unused_days) * 24 * 3600
               * fs::mtime_ticks_per_second ();
  auto base_name = base.filename ().wstring ();
  std::vector<std::filesystem::path> found;
  std::error_code error;
  for (std::filesystem::directory_iterator it (base.parent_path (), error),
       end;
       !error && it != end; it.increment (error))
    {
      std::error_code kind;
      if (!it->is_directory (kind) || it->path () == keep
          || !launcher_name (it->path ().filename ().wstring (), base_name))
        continue;
      found.push_back (it->path ());
    }
  for (const auto &directory : found)
    {
      auto marker = fs::stat (directory / stamp_name);
      if (!marker.exists || now - marker.mtime < stale || in_use (directory))
        continue;
      if (fs::remove_all (directory))
        s_log.debug ("removed unused natives {}", path_to_utf8 (directory));
    }
}

}

result<natives_report>
prepare_natives (const meta::version &resolved,
                 const meta::game_layout &layout,
                 const meta::rule_context &context,
                 const std::filesystem::path &directory, thread_pool *pool)
{
  natives_report report;
  auto archives = collect_native_archives (resolved, layout, context);
  report.archives = archives.size ();

  sha1 signature;
  for (const auto &archive : archives)
    {
      auto st = fs::stat (archive.jar);
      if (!st.exists)
        return make_error (errc::not_found,
                           sformat ("native library '{}' is missing",
                                    path_to_utf8 (archive.jar)));
      signature.update (path_to_utf8 (archive.jar));
      signature.update (sformat ("|{}|{}\n", st.size, st.mtime));
    }
  auto stamp = to_hex (signature.finish ());
  auto target = directory;
  target += "-" + stamp.substr (0, 12);
  report.directory = target;
  if (stamped (target, stamp))
    {
      (void)fs::write_file_atomic (target / stamp_name, stamp);
      report.reused = true;
      sweep (directory, target);
      return report;
    }

  if (fs::stat (target).exists)
    BLB_TRY (fs::remove_all (target));
  auto temp = target;
  temp += ".part-" + random_bytes_hex (4);
  BLB_TRY (fs::create_directories (temp));
  auto extracted = [&] () -> result<std::size_t> {
    BLB_TRY (fs::write_file (temp / stamp_name, std::string_view ()));
    BLB_TRY_ASSIGN (auto count, extract_into (archives, temp, pool));
    BLB_TRY (fs::write_file (temp / stamp_name, stamp));
    return count;
  }();
  if (!extracted)
    {
      (void)fs::remove_all (temp);
      return std::move (extracted).err ();
    }
  std::error_code error;
  std::filesystem::rename (temp, target, error);
  if (error)
    {
      (void)fs::remove_all (temp);
      if (!stamped (target, stamp))
        return make_error (errc::io, sformat ("cannot place natives in {}",
                                              path_to_utf8 (target)));
    }
  report.extracted = *extracted;
  s_log.debug ("extracted {} native files from {} archives", report.extracted,
               report.archives);
  sweep (directory, target);
  return report;
}

}
