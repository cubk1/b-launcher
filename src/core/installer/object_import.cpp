#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/installer/object_import.hpp>

#include <atomic>
#include <mutex>

namespace b_launcher_base::install
{

namespace
{

constexpr log::logger s_log ("import");

bool
same_path (const std::filesystem::path &a, const std::filesystem::path &b)
{
  if (fs::same_file (a, b))
    return true;
  return to_lower (path_to_utf8 (fs::absolute (a)))
         == to_lower (path_to_utf8 (fs::absolute (b)));
}

}

std::vector<std::filesystem::path>
discover_minecraft_roots (const std::filesystem::path &exclude)
{
  std::vector<std::filesystem::path> candidates;
  candidates.push_back (default_minecraft_directory ());
  if (auto extra = environment ("BLB_MINECRAFT_ROOTS"))
    for (auto part : split (*extra, path_list_separator ()))
      if (!trim (part).empty ())
        candidates.push_back (path_from_utf8 (trim (part)));
  std::vector<std::filesystem::path> out;
  for (auto &candidate : candidates)
    {
      if (!fs::stat (candidate).is_directory)
        continue;
      if (!exclude.empty () && same_path (candidate, exclude))
        continue;
      bool duplicate = false;
      for (const auto &existing : out)
        if (same_path (existing, candidate))
          duplicate = true;
      if (!duplicate)
        out.push_back (candidate);
    }
  return out;
}

import_report
import_local_objects (std::span<const planned_file> files,
                      std::span<const std::size_t> missing,
                      std::span<const std::filesystem::path> sources,
                      bool verify_hash, thread_pool *pool,
                      cancellation_token token)
{
  import_report report;
  if (sources.empty () || missing.empty ())
    {
      report.remaining.assign (missing.begin (), missing.end ());
      return report;
    }
  auto &workers = pool ? *pool : thread_pool::shared ();
  std::vector<std::filesystem::path> roots;
  const auto &probe = files[missing.front ()];
  for (const auto &source : sources)
    {
      auto target_root = probe.target;
      for (auto parts = split (probe.relative, '/').size (); parts > 0;
           --parts)
        target_root = target_root.parent_path ();
      if (!same_path (source, target_root))
        roots.push_back (source);
    }
  std::vector<char> done (missing.size (), 0);
  std::atomic<std::size_t> linked { 0 };
  std::atomic<std::size_t> copied { 0 };
  std::atomic<std::uint64_t> bytes { 0 };
  parallel_for (
      workers, missing.size (),
      [&] (std::size_t k) {
        if (token.is_cancelled ())
          return;
        const auto &file = files[missing[k]];
        if (file.relative.empty ())
          return;
        auto relative = path_from_utf8 (file.relative);
        for (const auto &source : roots)
          {
            auto candidate = source / relative;
            auto st = fs::stat (candidate);
            if (!st.exists || st.is_directory)
              continue;
            if (file.size >= 0
                && st.size != static_cast<std::uint64_t> (file.size))
              continue;
            if (verify_hash && file.sha1)
              {
                auto digest = sha1_file (candidate);
                if (!digest || *digest != *file.sha1)
                  continue;
              }
            auto result = fs::link_or_copy (candidate, file.target);
            if (!result)
              {
                s_log.debug ("cannot import {}: {}", file.relative,
                             result.err ().message);
                continue;
              }
            if (*result == fs::link_result::hard_link)
              ++linked;
            else
              ++copied;
            bytes += st.size;
            done[k] = 1;
            return;
          }
      },
      16);
  report.linked = linked.load ();
  report.copied = copied.load ();
  report.bytes = bytes.load ();
  for (std::size_t k = 0; k < missing.size (); ++k)
    if (!done[k])
      report.remaining.push_back (missing[k]);
  if (report.linked || report.copied)
    s_log.info ("imported {} files ({} linked, {} copied, {} bytes)",
                report.linked + report.copied, report.linked, report.copied,
                report.bytes);
  return report;
}

}
