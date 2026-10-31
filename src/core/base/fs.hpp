#pragma once

#include <core/base/error.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace b_launcher_base::fs
{

using std::filesystem::path;

struct file_stat
{
  bool exists = false;
  bool is_directory = false;
  bool is_link = false;
  std::uint64_t size = 0;
  std::int64_t mtime = 0;
};

enum class link_result : std::uint8_t
{
  hard_link,
  copied,
};

file_stat stat (const path &target) noexcept;
std::int64_t mtime_now () noexcept;
std::int64_t mtime_ticks_per_second () noexcept;
bool exists (const path &target) noexcept;
std::unordered_map<std::string, file_stat>
list_directory (const path &directory);

result<std::string> read_file (const path &target);
result<void> write_file (const path &target, std::string_view data);
result<void> write_file_atomic (const path &target, std::string_view data);
result<void> create_directories (const path &directory);
result<void> remove_file (const path &target);
result<void> remove_all (const path &target);
result<void> rename_file (const path &from, const path &to);
result<void> copy_file (const path &from, const path &to);
result<link_result> link_or_copy (const path &from, const path &to);
result<void> set_executable (const path &target);
path temp_sibling (const path &target);
bool same_volume (const path &a, const path &b) noexcept;
bool same_file (const path &a, const path &b) noexcept;
path absolute (const path &target);
bool safe_relative (std::string_view relative) noexcept;
bool is_inside (const path &root, const path &target);
path real_path (const path &target);
path current_directory ();
result<void> copy_tree (const path &from, const path &to);
result<void> create_symlink (const path &target, const path &link);

class native_file
{
public:
  enum class mode : std::uint8_t
  {
    read,
    write,
  };

  native_file () = default;
  native_file (native_file &&other) noexcept;
  native_file &operator= (native_file &&other) noexcept;
  native_file (const native_file &) = delete;
  native_file &operator= (const native_file &) = delete;
  ~native_file ();

  static result<native_file> open (const path &target, mode how);

  result<std::size_t> read (void *buffer, std::size_t size);
  result<void> write_all (const void *buffer, std::size_t size);
  result<void> write_at (std::uint64_t offset, const void *buffer,
                         std::size_t size);
  result<void> resize (std::uint64_t size);
  result<std::uint64_t> size () const;
  result<void> close ();
  bool is_open () const noexcept;

private:
  std::intptr_t m_handle = -1;
};

}
