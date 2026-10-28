#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/progress.hpp>
#include <core/base/thread_pool.hpp>
#include <core/installer/download_plan.hpp>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace b_launcher_base::install
{

enum class verify_mode : std::uint8_t
{
  existence,
  size,
  cached_hash,
  full_hash,
};

class integrity_cache
{
public:
  integrity_cache () = default;
  explicit integrity_cache (std::filesystem::path file);
  integrity_cache (const integrity_cache &) = delete;
  integrity_cache &operator= (const integrity_cache &) = delete;

  result<void> load ();
  result<void> save ();
  bool trusted (std::string_view key, const fs::file_stat &st,
                const sha1_digest &expected) const;
  void remember (std::string_view key, const fs::file_stat &st,
                 const sha1_digest &hash);
  void forget (std::string_view key);
  std::size_t size () const;
  const std::filesystem::path &file () const noexcept;

private:
  struct entry
  {
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    sha1_digest hash {};
  };

  std::filesystem::path m_file;
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, entry> m_entries;
  bool m_dirty = false;
};

struct verify_options
{
  verify_mode mode = verify_mode::cached_hash;
  integrity_cache *cache = nullptr;
  thread_pool *pool = nullptr;
};

struct verify_report
{
  std::vector<std::size_t> invalid;
  std::size_t checked = 0;
  std::size_t hashed = 0;
  std::size_t cache_hits = 0;
  std::chrono::milliseconds elapsed { 0 };
};

verify_report verify_files (std::span<const planned_file> files,
                            const verify_options &options,
                            progress_tracker *progress = nullptr,
                            cancellation_token token = {});
void remember_files (std::span<const planned_file> files,
                     std::span<const std::size_t> indices,
                     integrity_cache &cache);

}
