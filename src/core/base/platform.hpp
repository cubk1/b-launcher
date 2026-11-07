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
std::filesystem::path launcher_data_directory ();
std::filesystem::path default_minecraft_directory ();
std::string_view executable_suffix () noexcept;
char path_list_separator () noexcept;

// NixOS starts generic Linux programs, such as Mojang's Java, only with nix-ld.
bool is_nixos ();
// False on NixOS without nix-ld: the ELF loader is missing or is the stub-ld program.
bool runs_generic_binaries ();

// Lists that nix/package.nix builds in, separated by ':'. All empty when not on NixOS.
struct nixos_paths
{
  std::string_view java_homes;
  // For LD_LIBRARY_PATH of the game: the GPU driver and the libraries that the game opens.
  std::string_view library_path;
  // For PATH of the game: xrandr for LWJGL 2 and lspci for OSHI.
  std::string_view program_path;
};
const nixos_paths &packaged_nixos_paths ();

}
