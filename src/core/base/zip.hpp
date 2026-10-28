#pragma once

#include <core/base/error.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base
{

struct zip_entry
{
  std::string name;
  std::uint64_t size = 0;
  std::uint64_t compressed_size = 0;
  std::uint32_t crc = 0;
  bool directory = false;
};

class zip_reader
{
public:
  zip_reader () = default;
  zip_reader (zip_reader &&other) noexcept;
  zip_reader &operator= (zip_reader &&other) noexcept;
  zip_reader (const zip_reader &) = delete;
  zip_reader &operator= (const zip_reader &) = delete;
  ~zip_reader ();

  static result<zip_reader> open (const std::filesystem::path &path);
  static result<zip_reader> open_memory (std::string data);

  result<std::vector<zip_entry>> entries ();
  bool contains (std::string_view name);
  result<std::string> read (std::string_view name);
  result<void> extract (std::string_view name,
                        const std::filesystem::path &target);
  result<void> for_each (
      const std::function<bool (const zip_entry &, zip_reader &)> &visit);
  result<std::string> read_current ();
  result<void> extract_current (const std::filesystem::path &target);

private:
  void release () noexcept;
  result<void> locate (std::string_view name);

  void *m_handle = nullptr;
  std::unique_ptr<std::string> m_memory;
};

class zip_writer
{
public:
  zip_writer () = default;
  zip_writer (zip_writer &&other) noexcept;
  zip_writer &operator= (zip_writer &&other) noexcept;
  zip_writer (const zip_writer &) = delete;
  zip_writer &operator= (const zip_writer &) = delete;
  ~zip_writer ();

  static result<zip_writer> create (const std::filesystem::path &path);
  static bool should_store (std::string_view name) noexcept;

  result<void> add_buffer (std::string_view name, std::string_view data);
  result<void> add_file (std::string_view name,
                         const std::filesystem::path &source);
  result<void> close ();

private:
  result<void> open_entry (std::string_view name, std::int64_t size,
                           std::int64_t modified);

  void *m_handle = nullptr;
};

}
