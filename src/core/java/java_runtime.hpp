#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/platform.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/java/java_detect.hpp>
#include <core/meta/game_layout.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::java
{

inline constexpr std::string_view runtime_index_url
    = "https://piston-meta.mojang.com/v1/products/java-runtime/"
      "2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";

struct runtime_entry
{
  std::string platform;
  std::string component;
  std::string version;
  std::string released;
  std::string manifest_url;
  std::string manifest_sha1;
  std::int64_t manifest_size = -1;
};

enum class runtime_file_type : std::uint8_t
{
  file,
  directory,
  link,
};

struct runtime_file
{
  std::string path;
  runtime_file_type type = runtime_file_type::file;
  std::string url;
  std::string sha1;
  std::int64_t size = -1;
  bool executable = false;
  std::string target;
};

std::string mojang_platform (os_type os, cpu_arch arch);
result<std::vector<runtime_entry>> parse_runtime_index (std::string_view json);
result<std::vector<runtime_file>>
parse_runtime_manifest (std::string_view json);
result<std::vector<runtime_entry>>
fetch_runtime_index (const net::mirror_provider &mirror,
                     cancellation_token token = {});
std::filesystem::path runtime_home (const meta::game_layout &layout,
                                    std::string_view component,
                                    std::string_view platform);
result<java_installation>
install_runtime (install::game_installer &installer,
                 std::string_view component, progress_sink *sink = nullptr,
                 cancellation_token token = {},
                 std::optional<cpu_arch> arch = std::nullopt);

}
