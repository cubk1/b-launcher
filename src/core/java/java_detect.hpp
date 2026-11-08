#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/platform.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::java
{

enum class java_source : std::uint8_t
{
  user,
  java_home,
  path,
  registry,
  system,
  minecraft_runtime,
  managed,
};

struct java_installation
{
  std::filesystem::path home;
  std::filesystem::path executable;
  std::filesystem::path gui_executable;
  std::string version;
  int major = 0;
  cpu_arch arch = cpu_arch::unknown;
  std::string vendor;
  bool jdk = false;
  java_source source = java_source::user;
};

struct scan_options
{
  std::vector<std::filesystem::path> extra_homes;
  std::vector<std::filesystem::path> extra_roots;
  std::vector<std::filesystem::path> runtime_roots;
  std::vector<std::filesystem::path> excluded;
  std::vector<std::string> wrapper;
  std::function<void (std::size_t done, std::size_t total)> progress;
  bool probe_process = true;
  bool system_locations = true;
};

int parse_java_major (std::string_view version) noexcept;
std::string_view java_source_name (java_source source) noexcept;
std::filesystem::path
java_home_from_executable (const std::filesystem::path &executable);
// On NixOS without nix-ld, a Java from outside /nix/store must start to pass.
// It starts through wrapper (the instance wrapper command, e.g. steam-run).
result<java_installation>
probe_java (const std::filesystem::path &home_or_executable,
            bool allow_process = true, cancellation_token token = {},
            const std::vector<std::string> &wrapper = {},
            bool verify_execution = false);
std::vector<java_installation> scan_java (const scan_options &options = {},
                                          cancellation_token token = {});
// Drops a Java that is gone, and on NixOS without nix-ld a Java that starts only through a wrapper.
void prune_unusable (std::vector<java_installation> &installs,
                     cancellation_token token = {});
}
