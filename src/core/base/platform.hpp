#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#if defined(linux)
#undef linux
#endif

namespace b_launcher_base
{

enum class os_type : std::uint8_t
{
  windows,
  linux,
  osx,
  unknown,
};

enum class cpu_arch : std::uint8_t
{
  x86,
  x86_64,
  arm32,
  arm64,
  unknown,
};

struct platform_info
{
  os_type os = os_type::unknown;
  cpu_arch arch = cpu_arch::unknown;
  std::string os_version;
  std::uint64_t total_memory = 0;
  unsigned cpu_count = 1;
};

const platform_info &current_platform ();
std::uint64_t available_memory ();
std::string_view os_name (os_type os) noexcept;
std::string_view arch_name (cpu_arch arch) noexcept;
std::optional<cpu_arch> parse_arch (std::string_view text) noexcept;
unsigned arch_bits (cpu_arch arch) noexcept;
std::optional<std::string> environment (std::string_view name);
std::filesystem::path home_directory ();
std::filesystem::path app_data_directory ();
std::filesystem::path default_minecraft_directory ();
std::string_view executable_suffix () noexcept;
char path_list_separator () noexcept;

}
