#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/installer/integrity.hpp>

#include <atomic>
#include <cstring>

namespace b_launcher_base::install
{

namespace
{

constexpr log::logger s_log ("integrity");
constexpr char cache_magic[8] = { 'B', 'L', 'B', 'I', 'C', '0', '1', '\n' };

template <typename T>
void
put (std::string &out, T value)
{
  char bytes[sizeof (T)];
  std::memcpy (bytes, &value, sizeof (T));
  out.append (bytes, sizeof (T));
}

template <typename T>
bool
take (std::string_view &in, T &value)
{
  if (in.size () < sizeof (T))
    return false;
  std::memcpy (&value, in.data (), sizeof (T));
  in.remove_prefix (sizeof (T));
  return true;
}

}

integrity_cache::integrity_cache (std::filesystem::path file)
    : m_file (std::move (file))
{
}

result<void>
integrity_cache::load ()
{
  auto data = fs::read_file (m_file);
  if (!data)
    return data.err ().code == errc::not_found ? ok ()
                                               : result<void> (data.err ());
  std::string_view in = *data;
  if (in.size () < sizeof (cache_magic)
      || std::memcmp (in.data (), cache_magic, sizeof (cache_magic)) != 0)
    return ok ();
  in.remove_prefix (sizeof (cache_magic));
  std::lock_guard lock (m_mutex);
  while (!in.empty ())
    {
      std::uint16_t key_size = 0;
      entry e;
      if (!take (in, key_size) || in.size () < key_size)
        break;
      std::string key (in.substr (0, key_size));
      in.remove_prefix (key_size);
      if (!take (in, e.size) || !take (in, e.mtime)
          || in.size () < e.hash.size ())
        break;
      std::memcpy (e.hash.data (), in.data (), e.hash.size ());
      in.remove_prefix (e.hash.size ());
      m_entries.insert_or_assign (std::move (key), e);
    }
  return ok ();
}

result<void>
integrity_cache::save ()
{
  std::string out;
  {
    std::lock_guard lock (m_mutex);
    if (!m_dirty || m_file.empty ())
      return ok ();
    out.reserve (m_entries.size () * 96 + sizeof (cache_magic));
    out.append (cache_magic, sizeof (cache_magic));
    for (const auto &[key, e] : m_entries)
      {
        put (out, static_cast<std::uint16_t> (key.size ()));
        out += key;
        put (out, e.size);
        put (out, e.mtime);
        out.append (reinterpret_cast<const char *> (e.hash.data ()),
                    e.hash.size ());
      }
    m_dirty = false;
  }
  return fs::write_file_atomic (m_file, out);
}

bool
integrity_cache::trusted (std::string_view key, const fs::file_stat &st,
                          const sha1_digest &expected) const
{
  std::lock_guard lock (m_mutex);
  auto it = m_entries.find (std::string (key));
  if (it == m_entries.end ())
    return false;
  return it->second.size == st.size && it->second.mtime == st.mtime
         && it->second.hash == expected;
}

void
integrity_cache::remember (std::string_view key, const fs::file_stat &st,
                           const sha1_digest &hash)
{
  if (st.mtime > fs::mtime_now () - 3 * fs::mtime_ticks_per_second ())
    return;
  std::lock_guard lock (m_mutex);
  m_entries.insert_or_assign (std::string (key),
                              entry { st.size, st.mtime, hash });
  m_dirty = true;
}

void
integrity_cache::forget (std::string_view key)
{
  std::lock_guard lock (m_mutex);
  if (m_entries.erase (std::string (key)))
    m_dirty = true;
}

std::size_t
integrity_cache::size () const
{
  std::lock_guard lock (m_mutex);
  return m_entries.size ();
}

const std::filesystem::path &
integrity_cache::file () const noexcept
{
  return m_file;
}

verify_report
verify_files (std::span<const planned_file> files,
              const verify_options &options, progress_tracker *progress,
              cancellation_token token)
{
  auto started = std::chrono::steady_clock::now ();
  auto &pool = options.pool ? *options.pool : thread_pool::shared ();
  verify_report report;
  report.checked = files.size ();
  std::vector<fs::file_stat> stats (files.size ());

  std::unordered_map<std::string, std::vector<std::size_t>> by_dir;
  for (std::size_t i = 0; i < files.size (); ++i)
    by_dir[path_to_utf8 (files[i].target.parent_path ())].push_back (i);
  std::vector<const std::pair<const std::string, std::vector<std::size_t>> *>
      groups;
  groups.reserve (by_dir.size ());
  for (const auto &entry : by_dir)
    groups.push_back (&entry);

  parallel_for (pool, groups.size (), [&] (std::size_t g) {
    const auto &[dir, indices] = *groups[g];
    if (indices.size () >= 4)
      {
        auto listing
            = fs::list_directory (files[indices[0]].target.parent_path ());
        for (auto i : indices)
          {
            auto it
                = listing.find (path_to_utf8 (files[i].target.filename ()));
            if (it != listing.end () && !it->second.is_directory)
              stats[i] = it->second;
          }
      }
    else
      for (auto i : indices)
        stats[i] = fs::stat (files[i].target);
  });

  std::vector<std::size_t> to_hash;
  std::vector<char> bad (files.size (), 0);
  for (std::size_t i = 0; i < files.size (); ++i)
    {
      const auto &file = files[i];
      const auto &st = stats[i];
      if (!st.exists)
        {
          bad[i] = 1;
          continue;
        }
      if (options.mode == verify_mode::existence)
        continue;
      if (file.size >= 0 && st.size != static_cast<std::uint64_t> (file.size))
        {
          bad[i] = 1;
          continue;
        }
      if (options.mode == verify_mode::size || !file.sha1)
        continue;
      if (options.mode == verify_mode::cached_hash && options.cache
          && options.cache->trusted (file.relative, st, *file.sha1))
        {
          ++report.cache_hits;
          continue;
        }
      to_hash.push_back (i);
    }

  if (progress)
    {
      std::uint64_t bytes = 0;
      for (auto i : to_hash)
        bytes += stats[i].size;
      progress->add_total (bytes, to_hash.size ());
    }
  std::atomic<std::size_t> hashed { 0 };
  parallel_for (pool, to_hash.size (), [&] (std::size_t k) {
    if (token.is_cancelled ())
      return;
    auto i = to_hash[k];
    const auto &file = files[i];
    auto digest = sha1_file (file.target);
    ++hashed;
    if (progress)
      {
        progress->add_done (stats[i].size);
        progress->item_done ();
        progress->report ();
      }
    if (!digest || *digest != *file.sha1)
      {
        bad[i] = 1;
        if (options.cache)
          options.cache->forget (file.relative);
        return;
      }
    if (options.cache)
      options.cache->remember (file.relative, stats[i], *digest);
  });
  report.hashed = hashed.load ();
  for (std::size_t i = 0; i < files.size (); ++i)
    if (bad[i])
      report.invalid.push_back (i);
  report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
      std::chrono::steady_clock::now () - started);
  s_log.debug ("verified {} files: {} invalid, {} hashed, {} cached in {} ms",
               report.checked, report.invalid.size (), report.hashed,
               report.cache_hits, report.elapsed.count ());
  return report;
}

void
remember_files (std::span<const planned_file> files,
                std::span<const std::size_t> indices, integrity_cache &cache)
{
  for (auto i : indices)
    {
      const auto &file = files[i];
      if (!file.sha1)
        continue;
      auto st = fs::stat (file.target);
      if (st.exists)
        cache.remember (file.relative, st, *file.sha1);
    }
}

}
