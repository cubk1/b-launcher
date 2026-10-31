#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/thread_pool.hpp>
#include <core/network/download_client.hpp>

#include "core/network/curl_global.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace b_launcher_base::net
{

namespace
{

constexpr log::logger s_log ("download");
constexpr std::size_t staging_limit = 1u << 20;
constexpr std::uint64_t io_backlog_limit = 32u << 20;

constexpr std::int64_t split_threshold = 5 << 20;
constexpr unsigned split_connections = 8;
constexpr std::uint64_t split_piece = 1u << 20;

struct completion
{
  std::size_t index = 0;
  unsigned attempt = 0;
  std::string url;
  std::optional<error> failure;
  bool retryable = true;
  std::uint64_t bytes = 0;
};

std::mutex claims_mutex;
std::unordered_set<std::string> claims;

std::string
claim_key (const std::filesystem::path &target)
{
  return to_lower (path_to_utf8 (target.lexically_normal ()));
}

bool
try_claim (const std::string &key)
{
  std::lock_guard lock (claims_mutex);
  return claims.insert (key).second;
}

void
release_claim (const std::string &key)
{
  std::lock_guard lock (claims_mutex);
  claims.erase (key);
}

bool
already_there (const download_task &task)
{
  auto st = fs::stat (task.target);
  if (!st.exists || st.is_directory)
    return false;
  if (task.size >= 0 && static_cast<std::int64_t> (st.size) != task.size)
    return false;
  if (task.sha1)
    {
      auto digest = sha1_file (task.target);
      return digest && *digest == *task.sha1;
    }
  return task.size >= 0;
}

}

struct download_client::impl
{
  struct split_job;

  struct transfer
  {
    impl *owner = nullptr;
    const download_task *task = nullptr;
    std::size_t index = 0;
    unsigned attempt = 0;
    std::string url;
    std::string source;
    std::chrono::steady_clock::time_point started;
    bool hedge = false;
    bool hedged = false;
    CURL *easy = nullptr;
    bool buffered = false;
    bool hash_inline = false;
    std::string buffer;
    std::string staging;
    fs::native_file file;
    fs::path temp;
    sha1 hasher;
    std::uint64_t received = 0;
    std::optional<error> write_error;
    char error_buffer[CURL_ERROR_SIZE] = {};
    split_job *job = nullptr;
    std::size_t segment_index = 0;
    std::uint64_t staged_at = 0;
    bool range_checked = false;
    bool range_ignored = false;
    bool reached_end = false;
  };

  struct segment
  {
    std::uint64_t begin = 0;
    std::uint64_t pos = 0;
    std::uint64_t end = 0;
    unsigned attempt = 0;
    transfer *active = nullptr;
  };

  struct split_job
  {
    const download_task *task = nullptr;
    std::size_t index = 0;
    unsigned attempt = 0;
    fs::path temp;
    fs::native_file file;
    std::vector<segment> segments;
    unsigned connections = 0;
    bool ranges = false;
    bool no_ranges = false;
    std::string url;
  };

  download_options options;
  CURLM *multi = nullptr;
  std::vector<CURL *> idle;
  std::mutex run_mutex;

  progress_tracker *progress = nullptr;
  std::mutex completion_mutex;
  std::vector<completion> completions;
  std::atomic<std::size_t> io_outstanding { 0 };
  std::atomic<std::uint64_t> io_backlog { 0 };
  std::mutex dir_mutex;
  std::unordered_set<std::string> known_dirs;
  std::unordered_map<CURL *, std::unique_ptr<transfer>> active;
  std::vector<std::unique_ptr<split_job>> jobs;

  struct source_stats
  {
    std::uint64_t bytes = 0;
    double seconds = 0;
    std::uint32_t done = 0;
    std::uint32_t failed = 0;
  };

  std::mutex stats_mutex;
  std::unordered_map<std::string, source_stats> sources;
  std::uint64_t assignments = 0;

  double
  score (const source_stats &st) const
  {
    double speed = st.seconds > 0 ? st.bytes / st.seconds : 0;
    double reliability = (st.done + 1.0) / (st.done + st.failed * 3.0 + 1.0);
    return (speed + 1) * reliability;
  }

  std::size_t
  choose_url (const download_task &task, unsigned attempt)
  {
    auto count = task.urls.size ();
    std::vector<std::size_t> healthy;
    for (std::size_t k = 0; k < count; ++k)
      {
        auto candidate = (attempt + k) % count;
        if (!detail::host_penalized (task.urls[candidate]))
          healthy.push_back (candidate);
      }
    if (healthy.empty ())
      return attempt % count;
    if (attempt > 0 || healthy.size () == 1)
      return healthy.front ();
    std::lock_guard lock (stats_mutex);
    auto turn = assignments++;
    auto stats_of = [&] (std::size_t i) -> source_stats & {
      return sources[detail::host_of (task.urls[i])];
    };
    auto preferred = healthy.front ();
    std::size_t least = healthy[1];
    for (auto i : healthy)
      if (i != preferred && stats_of (i).done < stats_of (least).done)
        least = i;
    bool exploring = false;
    for (auto i : healthy)
      if (stats_of (i).done < 4)
        exploring = true;
    if (exploring)
      return turn % 4 == 3 ? least : preferred;
    std::sort (healthy.begin (), healthy.end (), [&] (auto a, auto b) {
      return score (stats_of (a)) > score (stats_of (b));
    });
    auto best = score (stats_of (healthy[0]));
    auto second = score (stats_of (healthy[1]));
    auto period = second * 2 < best ? 16u : 4u;
    if (turn % period == period - 1)
      return healthy[1];
    return healthy.front ();
  }

  void
  record (const transfer &t, bool success)
  {
    std::lock_guard lock (stats_mutex);
    auto &st = sources[t.source];
    if (!success)
      {
        ++st.failed;
        return;
      }
    ++st.done;
    st.bytes += t.received;
    st.seconds += std::chrono::duration<double> (
                      std::chrono::steady_clock::now () - t.started)
                      .count ();
  }

  explicit impl (download_options opts)
      : options (opts)
  {
    detail::ensure_curl ();
    auto *info = curl_version_info (CURLVERSION_NOW);
    if (!(info->features & CURL_VERSION_HTTP2))
      options.http2 = false;
    multi = curl_multi_init ();
    curl_multi_setopt (multi, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                       static_cast<long> (options.concurrency));
    curl_multi_setopt (multi, CURLMOPT_MAXCONNECTS,
                       static_cast<long> (options.concurrency));
    if (options.per_host)
      curl_multi_setopt (multi, CURLMOPT_MAX_HOST_CONNECTIONS,
                         static_cast<long> (options.per_host));
    curl_multi_setopt (multi, CURLMOPT_PIPELINING,
                       options.http2 ? CURLPIPE_MULTIPLEX : CURLPIPE_NOTHING);
  }

  ~impl ()
  {
    for (auto *easy : idle)
      curl_easy_cleanup (easy);
    curl_multi_cleanup (multi);
  }

  result<void>
  ensure_dir (const fs::path &directory)
  {
    std::string utf8 = path_to_utf8 (directory);
    {
      std::lock_guard lock (dir_mutex);
      if (known_dirs.contains (utf8))
        return ok ();
    }
    BLB_TRY (fs::create_directories (directory));
    std::lock_guard lock (dir_mutex);
    known_dirs.insert (std::move (utf8));
    return ok ();
  }

  void
  push_completion (completion done)
  {
    {
      std::lock_guard lock (completion_mutex);
      completions.push_back (std::move (done));
    }
    curl_multi_wakeup (multi);
  }

  static std::size_t
  on_write (char *data, std::size_t size, std::size_t count, void *user)
  {
    auto *t = static_cast<transfer *> (user);
    auto n = size * count;
    if (t->job)
      return t->owner->on_segment_write (*t, data, n);
    if (t->task->size >= 0
        && t->received + n > static_cast<std::uint64_t> (t->task->size))
      {
        t->write_error = make_error (
            errc::size_mismatch,
            sformat ("response larger than expected {} bytes", t->task->size));
        return 0;
      }
    t->received += n;
    if (t->owner->progress)
      t->owner->progress->add_done (n);
    if (t->buffered)
      {
        t->buffer.append (data, n);
        return n;
      }
    if (t->hash_inline)
      t->hasher.update (data, n);
    t->staging.append (data, n);
    if (t->staging.size () >= staging_limit)
      {
        auto r = t->file.write_all (t->staging.data (), t->staging.size ());
        t->staging.clear ();
        if (!r)
          {
            t->write_error = std::move (r).err ();
            return 0;
          }
      }
    return n;
  }

  CURL *
  acquire_easy ()
  {
    if (!idle.empty ())
      {
        auto *easy = idle.back ();
        idle.pop_back ();
        curl_easy_reset (easy);
        return easy;
      }
    return curl_easy_init ();
  }

  void
  release_easy (CURL *easy)
  {
    idle.push_back (easy);
  }

  std::unique_ptr<transfer>
  start (const download_task &task, std::size_t index, unsigned attempt,
         const network_settings &settings, const std::string &proxy,
         std::optional<std::size_t> url_index = std::nullopt)
  {
    auto t = std::make_unique<transfer> ();
    t->owner = this;
    t->task = &task;
    t->index = index;
    t->attempt = attempt;
    t->url = task.urls[url_index ? *url_index : choose_url (task, attempt)];
    t->source = detail::host_of (t->url);
    t->started = std::chrono::steady_clock::now ();
    t->buffered
        = task.size >= 0
          && static_cast<std::size_t> (task.size) <= options.memory_threshold;
    t->hash_inline = !t->buffered && task.sha1.has_value ();
    if (t->buffered)
      t->buffer.reserve (static_cast<std::size_t> (task.size));
    else
      {
        if (auto r = ensure_dir (task.target.parent_path ()); !r)
          {
            t->write_error = std::move (r).err ();
            return t;
          }
        t->temp = fs::temp_sibling (task.target);
        auto file
            = fs::native_file::open (t->temp, fs::native_file::mode::write);
        if (!file)
          {
            t->write_error = std::move (file).err ();
            return t;
          }
        t->file = std::move (*file);
        t->staging.reserve (staging_limit + options.receive_buffer);
      }
    configure (*t, settings, proxy, options.http2);
    if (t->easy)
      curl_multi_add_handle (multi, t->easy);
    return t;
  }

  void
  configure (transfer &t, const network_settings &settings,
             const std::string &proxy, bool multiplex)
  {
    t.easy = acquire_easy ();
    auto *easy = t.easy;
    if (!easy)
      return;
    detail::apply_common_options (easy, settings, proxy);
    curl_easy_setopt (easy, CURLOPT_SHARE, detail::shared_handle ());
    curl_easy_setopt (easy, CURLOPT_URL, t.url.c_str ());
    curl_easy_setopt (easy, CURLOPT_PRIVATE, &t);
    curl_easy_setopt (easy, CURLOPT_WRITEFUNCTION, &impl::on_write);
    curl_easy_setopt (easy, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt (easy, CURLOPT_ERRORBUFFER, t.error_buffer);
    curl_easy_setopt (easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt (easy, CURLOPT_BUFFERSIZE,
                      static_cast<long> (options.receive_buffer));
    if (multiplex)
      {
        curl_easy_setopt (easy, CURLOPT_HTTP_VERSION,
                          static_cast<long> (CURL_HTTP_VERSION_2TLS));
        curl_easy_setopt (easy, CURLOPT_PIPEWAIT, 1L);
      }
    else
      curl_easy_setopt (easy, CURLOPT_HTTP_VERSION,
                        static_cast<long> (CURL_HTTP_VERSION_1_1));
  }

  void
  discard (transfer &t)
  {
    if (t.file.is_open ())
      (void)t.file.close ();
    if (!t.temp.empty ())
      (void)fs::remove_file (t.temp);
    if (progress && t.received)
      progress->sub_done (t.received);
  }

  void
  fail (transfer &t, error reason, bool retryable)
  {
    discard (t);
    push_completion (
        { t.index, t.attempt, t.url, std::move (reason), retryable, 0 });
  }

  void
  finish_buffered (std::unique_ptr<transfer> t)
  {
    ++io_outstanding;
    std::uint64_t held = t->buffer.capacity ();
    io_backlog += held;
    auto *raw = t.release ();
    thread_pool::io ().post ([this, raw, held] {
      std::unique_ptr<transfer> t (raw);
      const auto &task = *t->task;
      completion done { t->index,     t->attempt, t->url,
                        std::nullopt, true,       t->received };
      if (task.size >= 0
          && t->buffer.size () != static_cast<std::size_t> (task.size))
        done.failure = make_error (errc::size_mismatch,
                                   sformat ("expected {} bytes, got {}",
                                            task.size, t->buffer.size ()));
      else if (task.sha1
               && sha1::digest (t->buffer.data (), t->buffer.size ())
                      != *task.sha1)
        done.failure = make_error (errc::checksum_mismatch,
                                   sformat ("sha1 mismatch for {}", t->url));
      else
        {
          auto r = ensure_dir (task.target.parent_path ());
          if (r)
            {
              auto file = fs::native_file::open (task.target,
                                                 fs::native_file::mode::write);
              if (file)
                {
                  r = file->write_all (t->buffer.data (), t->buffer.size ());
                  if (r)
                    r = file->close ();
                  if (!r)
                    (void)fs::remove_file (task.target);
                }
              else
                r = std::move (file).err ();
            }
          if (r && task.executable)
            r = fs::set_executable (task.target);
          if (!r)
            {
              done.failure = std::move (r).err ();
              done.retryable = false;
            }
        }
      if (done.failure && progress)
        progress->sub_done (t->received);
      t.reset ();
      io_backlog -= held;
      push_completion (std::move (done));
      --io_outstanding;
    });
  }

  void
  finish_streamed (std::unique_ptr<transfer> t)
  {
    if (!t->staging.empty ())
      {
        auto r = t->file.write_all (t->staging.data (), t->staging.size ());
        t->staging.clear ();
        if (!r)
          return fail (*t, std::move (r).err (), false);
      }
    if (auto r = t->file.close (); !r)
      return fail (*t, std::move (r).err (), false);
    const auto &task = *t->task;
    if (task.size >= 0
        && t->received != static_cast<std::uint64_t> (task.size))
      return fail (*t,
                   make_error (errc::size_mismatch,
                               sformat ("expected {} bytes, got {}", task.size,
                                        t->received)),
                   true);
    if (task.sha1 && t->hasher.finish () != *task.sha1)
      return fail (*t,
                   make_error (errc::checksum_mismatch,
                               sformat ("sha1 mismatch for {}", t->url)),
                   true);
    ++io_outstanding;
    auto *raw = t.release ();
    thread_pool::io ().post ([this, raw] {
      std::unique_ptr<transfer> t (raw);
      const auto &task = *t->task;
      completion done { t->index,     t->attempt, t->url,
                        std::nullopt, true,       t->received };
      auto r = fs::rename_file (t->temp, task.target);
      if (r && task.executable)
        r = fs::set_executable (task.target);
      if (!r)
        {
          (void)fs::remove_file (t->temp);
          done.failure = std::move (r).err ();
          done.retryable = false;
          if (progress)
            progress->sub_done (t->received);
        }
      push_completion (std::move (done));
      --io_outstanding;
    });
  }

  static bool
  task_has_alternatives (const transfer &t)
  {
    return t.task->urls.size () > 1;
  }

  void
  on_done (std::unique_ptr<transfer> t, CURLcode code)
  {
    curl_multi_remove_handle (multi, t->easy);
    long status = 0;
    curl_easy_getinfo (t->easy, CURLINFO_RESPONSE_CODE, &status);
    char *effective = nullptr;
    curl_easy_getinfo (t->easy, CURLINFO_EFFECTIVE_URL, &effective);
    std::string effective_url = effective ? effective : t->url;
    curl_easy_setopt (t->easy, CURLOPT_ERRORBUFFER, nullptr);
    release_easy (t->easy);
    t->easy = nullptr;
    bool redirected
        = detail::host_of (effective_url) != detail::host_of (t->url);
    if (task_has_alternatives (*t))
      record (*t, code == CURLE_OK);
    if (detail::host_failure (code, status))
      detail::note_host (effective_url, false);
    else if (code == CURLE_OK)
      {
        detail::note_host (t->url, true);
        if (redirected)
          detail::note_host (effective_url, true);
      }
    if (code != CURLE_OK)
      {
        error reason
            = t->write_error
                  ? *t->write_error
                  : detail::curl_failure (
                        code, t->error_buffer,
                        redirected ? t->url + " -> " + effective_url : t->url);
        bool retryable
            = !t->write_error || t->write_error->code == errc::size_mismatch;
        return fail (*t, std::move (reason), retryable);
      }
    if (t->buffered)
      finish_buffered (std::move (t));
    else
      finish_streamed (std::move (t));
  }

  std::size_t
  max_attempts (const download_task &task) const
  {
    return std::max<std::size_t> (task.urls.size (), options.retries + 1);
  }

  static std::uint64_t
  written (const split_job &job)
  {
    std::uint64_t sum = 0;
    for (const auto &s : job.segments)
      sum += s.pos - s.begin;
    return sum;
  }

  static unsigned
  job_connections (const std::vector<std::unique_ptr<split_job>> &all)
  {
    unsigned sum = 0;
    for (const auto &job : all)
      sum += job->connections;
    return sum;
  }

  static result<void>
  flush (transfer &t)
  {
    if (t.staging.empty ())
      return b_launcher_base::ok ();
    auto r = t.job->file.write_at (t.staged_at, t.staging.data (),
                                   t.staging.size ());
    t.staging.clear ();
    return r;
  }

  std::size_t
  on_segment_write (transfer &t, const char *data, std::size_t n)
  {
    auto &job = *t.job;
    auto &seg = job.segments[t.segment_index];
    if (!t.range_checked)
      {
        t.range_checked = true;
        long status = 0;
        curl_easy_getinfo (t.easy, CURLINFO_RESPONSE_CODE, &status);
        if (status == 206)
          job.ranges = true;
        else if (seg.pos != 0)
          {
            t.range_ignored = true;
            return 0;
          }
      }
    auto room = seg.end - seg.pos;
    auto take = static_cast<std::size_t> (std::min<std::uint64_t> (room, n));
    if (take > 0)
      {
        if (t.staging.empty ())
          t.staged_at = seg.pos;
        t.staging.append (data, take);
        seg.pos += take;
        t.received += take;
        if (progress)
          progress->add_done (take);
        if (t.staging.size () >= staging_limit)
          if (auto r = flush (t); !r)
            {
              t.write_error = std::move (r).err ();
              return 0;
            }
      }
    if (take < n)
      {
        t.reached_end = true;
        return 0;
      }
    return n;
  }

  std::unique_ptr<transfer>
  start_segment (split_job &job, std::size_t which,
                 const network_settings &settings, const std::string &proxy)
  {
    auto &seg = job.segments[which];
    auto t = std::make_unique<transfer> ();
    t->owner = this;
    t->task = job.task;
    t->index = job.index;
    t->attempt = job.attempt + seg.attempt;
    t->url = job.task->urls[choose_url (*job.task, t->attempt)];
    t->source = detail::host_of (t->url);
    t->started = std::chrono::steady_clock::now ();
    t->job = &job;
    t->segment_index = which;
    t->staging.reserve (staging_limit + options.receive_buffer);
    configure (*t, settings, proxy, false);
    if (!t->easy)
      return t;
    if (!job.no_ranges)
      {
        auto range = sformat ("{}-{}", seg.pos, seg.end - 1);
        curl_easy_setopt (t->easy, CURLOPT_RANGE, range.c_str ());
      }
    curl_multi_add_handle (multi, t->easy);
    seg.active = t.get ();
    ++job.connections;
    job.url = t->url;
    return t;
  }

  void
  drop_transfers (split_job &job)
  {
    for (auto &seg : job.segments)
      {
        if (!seg.active)
          continue;
        auto *easy = seg.active->easy;
        seg.active = nullptr;
        curl_multi_remove_handle (multi, easy);
        release_easy (easy);
        active.erase (easy);
      }
    job.connections = 0;
  }

  void
  remove_job (split_job &job)
  {
    auto it = std::find_if (jobs.begin (), jobs.end (),
                            [&] (const auto &j) { return j.get () == &job; });
    if (it != jobs.end ())
      jobs.erase (it);
  }

  void
  fail_job (split_job &job, error reason, bool retryable)
  {
    drop_transfers (job);
    (void)job.file.close ();
    (void)fs::remove_file (job.temp);
    if (progress)
      progress->sub_done (written (job));
    push_completion (
        { job.index, job.attempt, job.url, std::move (reason), retryable, 0 });
    remove_job (job);
  }

  void
  finish_job (split_job &job)
  {
    if (auto r = job.file.close (); !r)
      return fail_job (job, std::move (r).err (), false);
    const auto *task = job.task;
    completion done { job.index, job.attempt,
                      job.url,   std::nullopt,
                      true,      static_cast<std::uint64_t> (task->size) };
    auto temp = job.temp;
    remove_job (job);
    ++io_outstanding;
    thread_pool::io ().post ([this, task, temp, done] () mutable {
      if (task->sha1)
        {
          auto digest = sha1_file (temp);
          if (!digest)
            {
              done.failure = std::move (digest).err ();
              done.retryable = false;
            }
          else if (*digest != *task->sha1)
            done.failure
                = make_error (errc::checksum_mismatch,
                              sformat ("sha1 mismatch for {}", done.url));
        }
      if (!done.failure)
        {
          auto r = fs::rename_file (temp, task->target);
          if (r && task->executable)
            r = fs::set_executable (task->target);
          if (!r)
            {
              done.failure = std::move (r).err ();
              done.retryable = false;
            }
        }
      if (done.failure)
        {
          (void)fs::remove_file (temp);
          if (progress)
            progress->sub_done (done.bytes);
        }
      push_completion (std::move (done));
      --io_outstanding;
    });
  }

  void
  on_segment_done (std::unique_ptr<transfer> t, CURLcode code)
  {
    curl_multi_remove_handle (multi, t->easy);
    long status = 0;
    curl_easy_getinfo (t->easy, CURLINFO_RESPONSE_CODE, &status);
    char *effective = nullptr;
    curl_easy_getinfo (t->easy, CURLINFO_EFFECTIVE_URL, &effective);
    std::string effective_url = effective ? effective : t->url;
    curl_easy_setopt (t->easy, CURLOPT_ERRORBUFFER, nullptr);
    release_easy (t->easy);
    t->easy = nullptr;
    auto &job = *t->job;
    auto &seg = job.segments[t->segment_index];
    seg.active = nullptr;
    --job.connections;
    if (auto r = flush (*t); !r)
      return fail_job (job, std::move (r).err (), false);
    if (t->write_error)
      return fail_job (job, *t->write_error, false);
    bool fine
        = code == CURLE_OK || (code == CURLE_WRITE_ERROR && t->reached_end);
    if (task_has_alternatives (*t))
      record (*t, fine);
    if (detail::host_failure (code, status))
      detail::note_host (effective_url, false);
    else if (fine)
      detail::note_host (t->url, true);
    if (t->range_ignored)
      {
        s_log.debug ("{} ignores ranges, {} comes over one connection",
                     detail::host_of (effective_url),
                     path_to_utf8 (job.task->target));
        drop_transfers (job);
        if (progress)
          progress->sub_done (written (job));
        auto size = static_cast<std::uint64_t> (job.task->size);
        job.segments.assign (1, segment { 0, 0, size, 0, nullptr });
        job.ranges = false;
        job.no_ranges = true;
        return;
      }
    if (fine && seg.pos >= seg.end)
      {
        bool complete = std::all_of (
            job.segments.begin (), job.segments.end (),
            [] (const segment &s) { return !s.active && s.pos >= s.end; });
        if (complete)
          finish_job (job);
        return;
      }
    error reason
        = fine ? make_error (errc::size_mismatch,
                             sformat ("connection ended at byte {} of {}",
                                      seg.pos, seg.end))
               : detail::curl_failure (code, t->error_buffer, t->url);
    if (++seg.attempt >= max_attempts (*job.task))
      return fail_job (job, std::move (reason), false);
    s_log.debug ("range of {} again after: {}",
                 path_to_utf8 (job.task->target), reason.message);
    if ((job.no_ranges || seg.pos >= seg.end) && seg.pos > seg.begin)
      {
        if (progress)
          progress->sub_done (seg.pos - seg.begin);
        seg.pos = seg.begin;
      }
  }

  bool
  launch (split_job &job, std::size_t which, const network_settings &settings,
          const std::string &proxy)
  {
    auto t = start_segment (job, which, settings, proxy);
    if (!t->easy)
      {
        fail_job (job, make_error (errc::network, "cannot create transfer"),
                  false);
        return false;
      }
    auto *easy = t->easy;
    active.emplace (easy, std::move (t));
    return true;
  }

  void
  open_job (const download_task &task, std::size_t index, unsigned attempt,
            const network_settings &settings, const std::string &proxy)
  {
    auto job = std::make_unique<split_job> ();
    job->task = &task;
    job->index = index;
    job->attempt = attempt;
    job->url = task.urls.front ();
    auto size = static_cast<std::uint64_t> (task.size);
    result<void> r = ensure_dir (task.target.parent_path ());
    if (r)
      {
        job->temp = fs::temp_sibling (task.target);
        auto file
            = fs::native_file::open (job->temp, fs::native_file::mode::write);
        if (file)
          {
            job->file = std::move (*file);
            r = job->file.resize (size);
          }
        else
          r = std::move (file).err ();
      }
    if (!r)
      {
        (void)job->file.close ();
        if (!job->temp.empty ())
          (void)fs::remove_file (job->temp);
        push_completion (
            { index, attempt, job->url, std::move (r).err (), false, 0 });
        return;
      }
    job->segments.push_back (segment { 0, 0, size, 0, nullptr });
    auto &added = *jobs.emplace_back (std::move (job));
    launch (added, 0, settings, proxy);
  }

  void
  schedule_segments (const network_settings &settings,
                     const std::string &proxy)
  {
    std::vector<split_job *> all;
    for (auto &job : jobs)
      all.push_back (job.get ());
    for (auto *job : all)
      for (std::size_t i = 0; i < job->segments.size (); ++i)
        {
          if (active.size () >= options.concurrency)
            return;
          const auto &seg = job->segments[i];
          if (seg.active || seg.pos >= seg.end)
            continue;
          if (!launch (*job, i, settings, proxy))
            break;
        }
    all.clear ();
    for (auto &job : jobs)
      all.push_back (job.get ());
    auto budget = std::max (split_connections, options.concurrency / 2);
    for (auto *job : all)
      while (job->ranges && !job->no_ranges
             && job->connections < split_connections
             && active.size () < options.concurrency
             && job_connections (jobs) < budget)
        {
          std::size_t widest = job->segments.size ();
          std::uint64_t most = 0;
          for (std::size_t i = 0; i < job->segments.size (); ++i)
            {
              const auto &seg = job->segments[i];
              if (seg.end - seg.pos > most)
                {
                  most = seg.end - seg.pos;
                  widest = i;
                }
            }
          if (widest == job->segments.size () || most < split_piece * 2)
            break;
          auto &seg = job->segments[widest];
          auto cut = seg.pos + most / 2;
          segment half { cut, cut, seg.end, 0, nullptr };
          seg.end = cut;
          job->segments.push_back (half);
          if (!launch (*job, job->segments.size () - 1, settings, proxy))
            break;
        }
  }

  download_report
  run (std::span<const download_task> tasks, progress_tracker *tracker,
       cancellation_token token)
  {
    std::lock_guard run_lock (run_mutex);
    auto started = std::chrono::steady_clock::now ();
    progress = tracker;
    download_report report;
    report.total = tasks.size ();
    auto settings = get_network_settings ();
    auto proxy = effective_proxy ();

    std::deque<std::pair<std::size_t, unsigned>> pending;
    for (std::size_t i = 0; i < tasks.size (); ++i)
      {
        if (tasks[i].urls.empty ())
          {
            report.failures.push_back (
                { i, {}, make_error (errc::invalid_argument, "no url") });
            continue;
          }
        pending.emplace_back (i, 0);
        if (progress)
          progress->add_total (tasks[i].size > 0
                                   ? static_cast<std::uint64_t> (tasks[i].size)
                                   : 0,
                               1);
      }

    auto wake = token.on_cancel ([this] { curl_multi_wakeup (multi); });
    active.clear ();
    jobs.clear ();
    bool cancelled = false;
    std::vector<std::string> keys (tasks.size ());
    std::vector<bool> owned (tasks.size (), false);
    std::deque<std::pair<std::size_t, unsigned>> waiting;
    auto claim = [&] (std::size_t index) {
      if (owned[index])
        return true;
      if (keys[index].empty ())
        keys[index] = claim_key (tasks[index].target);
      owned[index] = try_claim (keys[index]);
      return static_cast<bool> (owned[index]);
    };
    auto release = [&] (std::size_t index) {
      if (owned[index])
        release_claim (keys[index]);
      owned[index] = false;
    };

    for (;;)
      {
        std::vector<completion> done;
        {
          std::lock_guard lock (completion_mutex);
          done.swap (completions);
        }
        for (auto &c : done)
          {
            const auto &task = tasks[c.index];
            if (!c.failure)
              {
                release (c.index);
                ++report.succeeded;
                report.bytes += c.bytes;
                if (progress)
                  {
                    if (task.size < 0)
                      progress->add_total (c.bytes, 0);
                    progress->item_done ();
                  }
                continue;
              }
            auto max_attempts = std::max<std::size_t> (task.urls.size (),
                                                       options.retries + 1);
            if (!cancelled && c.retryable && c.attempt + 1 < max_attempts
                && c.failure->code != errc::cancelled)
              {
                s_log.debug ("retry {} after: {}", path_to_utf8 (task.target),
                             c.failure->message);
                pending.emplace_back (c.index, c.attempt + 1);
                continue;
              }
            s_log.warn ("download failed {}: {}", path_to_utf8 (task.target),
                        c.failure->to_string ());
            release (c.index);
            report.failures.push_back (
                { c.index, std::move (c.url), std::move (*c.failure) });
          }

        if (!cancelled && token.is_cancelled ())
          {
            cancelled = true;
            for (auto &[easy, t] : active)
              {
                curl_multi_remove_handle (multi, easy);
                release_easy (easy);
                if (t->job)
                  continue;
                discard (*t);
                report.failures.push_back (
                    { t->index, t->url,
                      make_error (errc::cancelled, "cancelled") });
              }
            active.clear ();
            for (auto &job : jobs)
              {
                (void)job->file.close ();
                (void)fs::remove_file (job->temp);
                report.failures.push_back (
                    { job->index, job->url,
                      make_error (errc::cancelled, "cancelled") });
              }
            jobs.clear ();
            for (auto &[index, attempt] : pending)
              report.failures.push_back (
                  { index, {}, make_error (errc::cancelled, "cancelled") });
            pending.clear ();
            for (auto &[index, attempt] : waiting)
              report.failures.push_back (
                  { index, {}, make_error (errc::cancelled, "cancelled") });
            waiting.clear ();
          }

        for (std::size_t n = waiting.size (); !cancelled && n > 0; --n)
          {
            auto entry = waiting.front ();
            waiting.pop_front ();
            if (!claim (entry.first))
              {
                waiting.push_back (entry);
                continue;
              }
            const auto &task = tasks[entry.first];
            if (!already_there (task))
              {
                pending.push_front (entry);
                continue;
              }
            release (entry.first);
            ++report.succeeded;
            if (progress)
              {
                if (task.size > 0)
                  progress->add_done (static_cast<std::uint64_t> (task.size));
                progress->item_done ();
              }
          }

        bool backlogged = io_backlog.load () >= io_backlog_limit;

        if (!cancelled && !backlogged)
          schedule_segments (settings, proxy);

        while (!cancelled && !backlogged
               && active.size () < options.concurrency && !pending.empty ())
          {
            auto [index, attempt] = pending.front ();
            pending.pop_front ();
            if (!claim (index))
              {
                waiting.emplace_back (index, attempt);
                continue;
              }
            if (tasks[index].size >= split_threshold)
              {
                open_job (tasks[index], index, attempt, settings, proxy);
                continue;
              }
            auto t = start (tasks[index], index, attempt, settings, proxy);
            if (!t->easy)
              {
                auto reason = t->write_error
                                  ? std::move (*t->write_error)
                                  : make_error (errc::network,
                                                "cannot create transfer");
                fail (*t, std::move (reason), false);
                continue;
              }
            auto *easy = t->easy;
            active.emplace (easy, std::move (t));
          }

        if (!cancelled && !backlogged && pending.empty () && !active.empty ()
            && active.size () < options.concurrency)
          {
            auto now = std::chrono::steady_clock::now ();
            std::vector<transfer *> candidates;
            for (auto &[easy, t] : active)
              {
                if (t->job || t->hedge || t->hedged
                    || t->task->urls.size () < 2)
                  continue;
                auto elapsed = std::chrono::duration<double> (now - t->started)
                                   .count ();
                if (elapsed < 2.0)
                  continue;
                double rate = t->received / elapsed;
                double remaining
                    = t->task->size > 0 && rate > 0
                          ? (t->task->size - static_cast<double> (t->received))
                                / rate
                          : 1e9;
                if (remaining >= 2.0)
                  candidates.push_back (t.get ());
              }
            for (auto *slow : candidates)
              {
                if (active.size () >= options.concurrency)
                  break;
                std::optional<std::size_t> other;
                const auto &urls = slow->task->urls;
                for (std::size_t i = 0; i < urls.size () && !other; ++i)
                  if (detail::host_of (urls[i]) != slow->source
                      && !detail::host_penalized (urls[i]))
                    other = i;
                if (!other)
                  continue;
                slow->hedged = true;
                auto hedge = start (*slow->task, slow->index, slow->attempt,
                                    settings, proxy, other);
                if (!hedge->easy)
                  {
                    discard (*hedge);
                    continue;
                  }
                hedge->hedge = true;
                s_log.debug ("hedging {} on {}",
                             path_to_utf8 (slow->task->target), hedge->source);
                auto *easy = hedge->easy;
                active.emplace (easy, std::move (hedge));
              }
          }

        if (active.empty () && pending.empty () && jobs.empty ()
            && waiting.empty () && io_outstanding.load () == 0)
          {
            std::lock_guard lock (completion_mutex);
            if (completions.empty ())
              break;
            continue;
          }

        int running = 0;
        curl_multi_perform (multi, &running);
        int queued = 0;
        while (auto *msg = curl_multi_info_read (multi, &queued))
          {
            if (msg->msg != CURLMSG_DONE)
              continue;
            auto it = active.find (msg->easy_handle);
            if (it == active.end ())
              continue;
            auto t = std::move (it->second);
            active.erase (it);
            auto code = msg->data.result;
            if (t->job)
              {
                on_segment_done (std::move (t), code);
                continue;
              }
            std::vector<CURL *> siblings;
            for (auto &[easy, other] : active)
              if (other->index == t->index)
                siblings.push_back (easy);
            if (!siblings.empty () && code != CURLE_OK)
              {
                curl_multi_remove_handle (multi, t->easy);
                release_easy (t->easy);
                t->easy = nullptr;
                discard (*t);
                continue;
              }
            for (auto *easy : siblings)
              {
                auto sibling = active.find (easy);
                curl_multi_remove_handle (multi, easy);
                release_easy (easy);
                sibling->second->easy = nullptr;
                discard (*sibling->second);
                active.erase (sibling);
              }
            on_done (std::move (t), code);
          }
        if (progress)
          progress->report ();
        curl_multi_poll (
            multi, nullptr, 0,
            active.empty () && !backlogged && waiting.empty () ? 5 : 100,
            nullptr);
      }

    for (std::size_t i = 0; i < tasks.size (); ++i)
      release (i);
    progress = nullptr;
    report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now () - started);
    if (tracker)
      tracker->report (true);
    s_log.info ("downloaded {}/{} files, {} bytes in {} ms", report.succeeded,
                report.total, report.bytes, report.elapsed.count ());
    {
      std::lock_guard lock (stats_mutex);
      for (const auto &[host, st] : sources)
        if (st.done || st.failed)
          s_log.debug ("source {}: {} ok, {} failed, {:.2f} MB/s per transfer",
                       host, st.done, st.failed,
                       st.seconds > 0 ? st.bytes / st.seconds / 1048576.0
                                      : 0.0);
    }
    return report;
  }
};

result<void>
download_report::to_result () const
{
  if (failures.empty ())
    return b_launcher_base::ok ();
  for (const auto &f : failures)
    if (f.reason.code == errc::cancelled)
      return f.reason;
  const auto &first = failures.front ();
  return make_error (first.reason.code,
                     sformat ("{} of {} downloads failed, first: {}",
                              failures.size (), total, first.reason.message));
}

download_client::download_client (download_options options)
    : m_impl (std::make_unique<impl> (options))
{
}

download_client::~download_client () = default;

download_report
download_client::run (std::span<const download_task> tasks,
                      progress_tracker *progress, cancellation_token token)
{
  return m_impl->run (tasks, progress, token);
}

const download_options &
download_client::options () const noexcept
{
  return m_impl->options;
}

}
