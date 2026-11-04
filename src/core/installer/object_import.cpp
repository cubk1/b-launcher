#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/installer/object_import.hpp>

#include <atomic>
#include <mutex>
#include <utility>

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

std::filesystem::path
moved_library (const planned_file &file)
{
  if ((file.kind != file_kind::library && file.kind != file_kind::native)
      || !file.sha1 || file.size < 0 || split (file.relative, '/').size () < 5)
    return {};
  std::vector<std::pair<std::filesystem::path, int>> pending {
    { file.target.parent_path ().parent_path (), 0 }
  };
  while (!pending.empty ())
    {
      auto [directory, depth] = std::move (pending.back ());
      pending.pop_back ();
      for (const auto &[name, st] : fs::list_directory (directory))
        {
          auto candidate = directory / path_from_utf8 (name);
          if (st.is_directory)
            {
              if (depth < 2 && !st.is_link)
                pending.emplace_back (std::move (candidate), depth + 1);
              continue;
            }
          if (st.size != static_cast<std::uint64_t> (file.size)
              || same_path (candidate, file.target))
            continue;
          auto digest = sha1_file (candidate);
          if (digest && *digest == *file.sha1)
            return candidate;
        }
    }
  return {};
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
  if (missing.empty ())
    return report;
  auto &workers = pool ? *pool : thread_pool::shared ();
  std::vector<std::filesystem::path> roots;
  const auto &probe = files[missing.front ()];
  auto target_root = probe.target;
  for (auto parts = split (probe.relative, '/').size (); parts > 0; --parts)
    target_root = target_root.parent_path ();
  for (const auto &source : sources)
    {
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
        auto take = [&] (const std::filesystem::path &candidate,
                         std::uint64_t size) {
          auto result = fs::link_or_copy (candidate, file.target);
          if (!result)
            {
              s_log.debug ("cannot import {}: {}", file.relative,
                           result.err ().message);
              return false;
            }
          if (*result == fs::link_result::hard_link)
            ++linked;
          else
            ++copied;
          bytes += size;
          done[k] = 1;
          return true;
        };
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
            if (take (candidate, st.size))
              return;
          }
        if (auto moved = moved_library (file); !moved.empty ())
          take (moved, static_cast<std::uint64_t> (file.size));
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
