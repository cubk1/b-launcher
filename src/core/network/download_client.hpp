#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/hash.hpp>
#include <core/base/progress.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace b_launcher_base::net
{

struct download_task
{
  std::vector<std::string> urls;
  std::filesystem::path target;
  std::optional<sha1_digest> sha1;
  std::int64_t size = -1;
  bool executable = false;
};

struct download_options
{
  unsigned concurrency = 64;
  unsigned per_host = 0;
  unsigned retries = 3;
  std::size_t memory_threshold = 4u << 20;
  std::size_t receive_buffer = 128u << 10;
  bool http2 = true;
};

struct download_failure
{
  std::size_t index = 0;
  std::string url;
  error reason;
};

struct download_report
{
  std::size_t total = 0;
  std::size_t succeeded = 0;
  std::uint64_t bytes = 0;
  std::chrono::milliseconds elapsed { 0 };
  std::vector<download_failure> failures;

  bool
  ok () const noexcept
  {
    return failures.empty ();
  }

  result<void> to_result () const;
};

class download_client
{
public:
  explicit download_client (download_options options = {});
  download_client (const download_client &) = delete;
  download_client &operator= (const download_client &) = delete;
  ~download_client ();

  download_report run (std::span<const download_task> tasks,
                       progress_tracker *progress = nullptr,
                       cancellation_token token = {});

  const download_options &options () const noexcept;

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

}
