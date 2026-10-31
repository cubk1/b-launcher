#pragma once

#include <core/auth/account.hpp>
#include <core/base/error.hpp>
#include <core/installer/integrity.hpp>
#include <core/java/java_detect.hpp>
#include <core/meta/game_layout.hpp>
#include <core/meta/rules.hpp>
#include <core/meta/version.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::launch
{

enum class gc_mode : std::uint8_t
{
  tuned_g1,
  g1,
  zgc,
  generational_zgc,
  none,
};

struct launch_settings
{
  std::string version_id;
  std::filesystem::path game_directory;
  std::optional<std::filesystem::path> java;
  bool auto_download_java = true;
  std::uint32_t max_memory_mb = 0;
  std::uint32_t min_memory_mb = 0;
  std::vector<std::string> jvm_args;
  std::vector<std::string> game_args;
  std::optional<int> width;
  std::optional<int> height;
  bool fullscreen = false;
  bool demo = false;
  std::string server;
  std::string quick_play_world;
  std::string launcher_name = "b-launcher-base";
  std::string launcher_version = "0.3.0";
  install::verify_mode verify = install::verify_mode::cached_hash;
  bool check_integrity = true;
  bool default_jvm_tuning = true;
  gc_mode gc = gc_mode::tuned_g1;
  bool use_argument_file = true;
  std::vector<std::string> wrapper;
  std::vector<std::pair<std::string, std::string>> environment;
};

struct launch_context
{
  const meta::version *version = nullptr;
  const meta::game_layout *layout = nullptr;
  auth::account account;
  java::java_installation java;
  std::filesystem::path game_directory;
  std::filesystem::path natives_directory;
  meta::rule_context rules;
  launch_settings settings;
};

struct launch_command
{
  std::filesystem::path executable;
  std::vector<std::string> jvm_args;
  std::string main_class;
  std::vector<std::string> game_args;
  std::vector<std::string> wrapper;
  std::filesystem::path working_directory;
  std::vector<std::pair<std::string, std::string>> environment;
  std::optional<std::filesystem::path> argument_file;

  std::vector<std::string> command_line () const;
  std::string redacted (std::string_view secret) const;
};

std::string
substitute (std::string_view text,
            const std::map<std::string, std::string, std::less<>> &values);
std::vector<std::filesystem::path>
build_classpath (const meta::version &resolved,
                 const meta::game_layout &layout,
                 const meta::rule_context &context);
std::uint32_t auto_memory_mb (const java::java_installation &java);
result<launch_command> build_launch_command (const launch_context &context);
std::string quote_argument_file_entry (std::string_view arg);

}
