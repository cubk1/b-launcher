#pragma once

#include <core/base/error.hpp>
#include <core/base/thread_pool.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::modpack
{

enum class mod_platform : std::uint8_t
{
  fabric,
  quilt,
  forge,
  neoforge,
  legacy_forge,
  liteloader,
};

struct mod_dependency
{
  std::string id;
  std::string version;
  bool required = true;
};

struct mod_info
{
  std::filesystem::path file;
  std::string file_name;
  bool enabled = true;
  std::uint64_t size = 0;
  std::vector<mod_platform> platforms;
  std::string id;
  std::string name;
  std::string version;
  std::string description;
  std::vector<std::string> authors;
  std::string homepage;
  std::string icon;
  std::vector<mod_dependency> dependencies;
  std::vector<std::string> provides;
  std::string problem;
};

struct mod_issue
{
  std::string mod_id;
  std::string missing;
  std::string message;
};

std::string_view platform_name (mod_platform platform) noexcept;
bool is_mod_file (const std::filesystem::path &file) noexcept;
bool is_disabled_file (const std::filesystem::path &file) noexcept;
result<mod_info> read_mod (const std::filesystem::path &file);
std::vector<mod_info> scan_mods (const std::filesystem::path &directory,
                                 thread_pool *pool = nullptr);
result<std::filesystem::path> set_mod_enabled (const mod_info &mod,
                                               bool enabled);
result<std::string> read_mod_icon (const mod_info &mod);
std::vector<mod_issue>
check_dependencies (std::span<const mod_info> mods,
                    std::span<const std::string> builtin_ids);
const mod_info *find_mod (std::span<const mod_info> mods, std::string_view id);

}
