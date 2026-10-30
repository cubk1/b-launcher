#pragma once

#include <core/base/error.hpp>
#include <core/installer/loader.hpp>
#include <core/launcher/arguments.hpp>
#include <core/meta/game_layout.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::instance
{

enum class isolation_mode : std::uint8_t
{
  none,
  all,
  modded,
};

struct instance_settings
{
  std::optional<bool> isolated;
  std::optional<std::filesystem::path> java;
  std::uint32_t max_memory_mb = 0;
  std::uint32_t min_memory_mb = 0;
  std::vector<std::string> jvm_args;
  std::vector<std::string> game_args;
  std::optional<int> width;
  std::optional<int> height;
  bool fullscreen = false;
  std::string server;
  std::string login;
  std::string auth_server;
  std::string notes;
  std::string wrapper;
  std::string pre_launch;
  std::string post_exit;
  std::vector<std::pair<std::string, std::string>> environment;
  bool default_jvm_args = true;
  bool check_integrity = true;
  std::string launcher_visibility;
  std::string process_priority;
  std::optional<bool> show_logs;
  std::optional<bool> patch;
};

struct instance
{
  std::string id;
  std::filesystem::path directory;
  std::string minecraft_version;
  std::string type;
  std::string release_time;
  std::vector<install::loader_info> loaders;
  instance_settings settings;
  bool valid = true;
  std::string problem;
  // The versions it is made of, the one it inherits from first and itself
  // last; only itself when it does not resolve.
  std::vector<std::string> chain;

  bool
  modded () const noexcept
  {
    return !loaders.empty ();
  }
};

std::string_view isolation_name (isolation_mode mode) noexcept;
std::optional<isolation_mode> parse_isolation (std::string_view text) noexcept;

// The file beside an instance's version json that holds its settings.
inline constexpr std::string_view settings_file = "blb-instance.json";

class instance_manager
{
public:
  explicit instance_manager (meta::game_layout layout,
                             isolation_mode mode = isolation_mode::modded);

  const meta::game_layout &layout () const noexcept;
  isolation_mode isolation () const noexcept;
  void set_isolation (isolation_mode mode) noexcept;

  std::vector<instance> list () const;
  result<instance> get (std::string_view id) const;
  bool exists (std::string_view id) const;
  bool is_isolated (const instance &value) const noexcept;
  std::filesystem::path game_directory (const instance &value) const;

  result<instance_settings> load_settings (std::string_view id) const;
  result<void> save_settings (std::string_view id,
                              const instance_settings &settings) const;
  result<void> rename (std::string_view from, std::string_view to) const;
  result<void> remove (std::string_view id) const;
  result<void> duplicate (std::string_view from, std::string_view to,
                          bool copy_game_files = true) const;
  launch::launch_settings launch_settings_for (const instance &value) const;

private:
  meta::game_layout m_layout;
  isolation_mode m_mode;
};

}
