#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::modpack
{

struct remote_entry
{
  std::string name;
  std::uint64_t size = 0;
  std::uint64_t compressed = 0;
  std::uint32_t crc = 0;
  std::uint16_t method = 0;
  std::uint64_t offset = 0;
};

struct remote_range
{
  long status = 0;
  std::string body;
  std::optional<std::uint64_t> total;
};

using range_fetch = std::function<result<remote_range> (
    std::string_view url, std::uint64_t begin, std::uint64_t end,
    cancellation_token token)>;

class remote_zip
{
public:
  static result<remote_zip> open (std::vector<std::string> urls,
                                  std::uint64_t size_hint = 0,
                                  cancellation_token token = {},
                                  range_fetch fetch = {});

  const std::vector<remote_entry> &entries () const noexcept;
  const remote_entry *find (std::string_view name) const noexcept;
  result<std::string> read (std::string_view name,
                            cancellation_token token = {});
  std::uint64_t size () const noexcept;
  std::uint64_t downloaded () const noexcept;
  int requests () const noexcept;

private:
  struct block
  {
    std::uint64_t start = 0;
    std::string data;
  };

  result<std::string> bytes (std::uint64_t begin, std::uint64_t end,
                             cancellation_token token);
  result<remote_range> fetch (std::uint64_t begin, std::uint64_t end,
                              cancellation_token token);
  result<void> load_directory (cancellation_token token);

  std::vector<std::string> m_urls;
  range_fetch m_fetch;
  std::uint64_t m_size = 0;
  std::vector<block> m_blocks;
  std::vector<remote_entry> m_entries;
  std::uint64_t m_downloaded = 0;
  int m_requests = 0;
};

}
