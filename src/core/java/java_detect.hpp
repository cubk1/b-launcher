#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/platform.hpp>

#include <filesystem>
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
  bool probe_process = true;
  bool system_locations = true;
};

int parse_java_major (std::string_view version) noexcept;
std::string_view java_source_name (java_source source) noexcept;
std::filesystem::path
java_home_from_executable (const std::filesystem::path &executable);
result<java_installation>
probe_java (const std::filesystem::path &home_or_executable,
            bool allow_process = true);
std::vector<java_installation> scan_java (const scan_options &options = {},
                                          cancellation_token token = {});

}
