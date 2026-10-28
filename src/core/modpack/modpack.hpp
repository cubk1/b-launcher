#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/installer/loader.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::modpack
{

enum class pack_format : std::uint8_t
{
  modrinth,
  curseforge,
  mcbbs,
  multimc,
};

struct pack_file
{
  std::string path;
  std::vector<std::string> urls;
  std::string sha1;
  std::string sha512;
  std::int64_t size = -1;
  bool optional = false;
  std::int64_t curseforge_project = 0;
  std::int64_t curseforge_file = 0;
};

struct pack_manifest
{
  pack_format format = pack_format::modrinth;
  std::string name;
  std::string version;
  std::string author;
  std::string summary;
  std::string minecraft;
  std::vector<install::loader_request> loaders;
  std::vector<pack_file> files;
  std::vector<std::string> overrides;
  std::uint32_t min_memory_mb = 0;
  std::uint32_t max_memory_mb = 0;
  std::vector<std::string> jvm_args;
  std::vector<std::string> game_args;
};

struct curseforge_settings
{
  std::vector<std::string> api_roots
      = { "https://mod.mcimirror.top/curseforge",
          "https://api.curseforge.com" };
  std::string api_key;
};

struct import_options
{
  std::string id;
  bool include_optional = true;
  bool overwrite = false;
  curseforge_settings curseforge;
};

struct export_options
{
  std::string name;
  std::string version = "1.0.0";
  std::string author;
  std::string summary;
  bool game_settings = true;
  bool personal_data = false;
  bool mods = true;
  bool disabled_mods = false;
  bool mod_settings = true;
  bool saves = false;
  bool servers = false;
  bool resource_packs = false;
  bool shader_packs = false;
  bool bundle_files = false;
  bool modrinth_only = false;
  std::vector<std::string> include;
  std::vector<std::string> exclude;
  std::vector<std::pair<std::filesystem::path, std::string>> extra_files;
};

struct export_report
{
  std::size_t referenced = 0;
  std::size_t bundled = 0;
  std::uint64_t bytes = 0;
  std::vector<std::string> warnings;
};

std::string_view format_name (pack_format format) noexcept;
result<pack_manifest> read_modpack (const std::filesystem::path &archive);
result<std::string> import_modpack (install::game_installer &installer,
                                    const std::filesystem::path &archive,
                                    const import_options &options = {},
                                    progress_sink *sink = nullptr,
                                    cancellation_token token = {});
result<export_report> export_modpack (
    const install::game_installer &installer, std::string_view instance_id,
    const std::filesystem::path &game_directory,
    const std::filesystem::path &output, const export_options &options,
    progress_sink *sink = nullptr, cancellation_token token = {});
std::vector<std::string>
collect_export_files (const std::filesystem::path &game_directory,
                      const export_options &options);
std::string serialize_export_options (const export_options &options);
result<export_options> parse_export_options (std::string_view json);

}
