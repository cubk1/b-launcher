#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/installer/download_plan.hpp>
#include <core/launcher/natives.hpp>

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace b_launcher_base::launch
{

namespace
{

constexpr log::logger s_log ("natives");
constexpr std::string_view stamp_name = ".blb-natives";

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
  auto stamp_file = directory / stamp_name;
  if (auto existing = fs::read_file (stamp_file);
      existing && *existing == stamp)
    {
      report.reused = true;
      return report;
    }

  BLB_TRY (fs::remove_all (directory));
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
  report.extracted = extracted.load ();
  BLB_TRY (fs::write_file (stamp_file, stamp));
  s_log.debug ("extracted {} native files from {} archives", report.extracted,
               report.archives);
  return report;
}

}
