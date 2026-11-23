#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/installer/loader.hpp>
#include <core/network/mirror_provider.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::modpack
{

inline constexpr std::string_view info_file = "blb-modpack.json";
inline constexpr std::string_view icon_file = "blb-icon.png";

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

struct multimc_component
{
  std::string uid;
  std::string version;
  bool standard = false;
};

struct multimc_profile
{
  std::string prefix;
  std::string notes;
  std::vector<multimc_component> components;
};

struct pack_origin
{
  std::string source;
  std::string project;
  std::string version;
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
  std::optional<multimc_profile> multimc;
  pack_origin origin;
};

struct curseforge_settings
{
  std::string api_key;
};


struct owned_file
{
  std::string sha1;
  std::optional<std::uint32_t> crc;
  std::int64_t size = -1;
};

struct pack_info
{
  std::string name;
  std::string version;
  std::string format;
  std::string minecraft;
  pack_origin origin;
  std::map<std::string, owned_file> files;
  bool tracked = false;
};

struct import_options
{
  std::string id;
  bool include_optional = true;
  bool overwrite = false;
  curseforge_settings curseforge;
  pack_origin origin;
};

enum class update_action : std::uint8_t
{
  add,
  change,
  remove,
};

struct update_item
{
  std::string path;
  update_action action = update_action::add;
  bool modified = false;
  std::optional<std::size_t> file;
  std::string entry;
  std::uint64_t bytes = 0;
  std::string replaces;
  bool disabled = false;
  bool extra = false;
};

struct pack_candidate
{
  pack_origin origin;
  std::string title;
  std::string icon_url;
  std::string version_name;
  std::vector<std::string> urls;
  std::uint64_t archive_size = 0;
  std::size_t matched = 0;
  std::size_t total = 0;
  bool exact = false;
};

struct update_plan
{
  std::string id;
  pack_manifest manifest;
  std::vector<std::string> urls;
  std::uint64_t archive_size = 0;
  std::vector<update_item> items;
  std::map<std::string, owned_file> owned;
  bool loaders_changed = false;
  bool tracked = false;
  bool restore = false;
};

using file_remover
    = std::function<result<void> (const std::vector<std::filesystem::path> &)>;

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
  bool pick_files = false;
  std::map<std::string, bool> picks;
  std::vector<std::pair<std::filesystem::path, std::string>> extra_files;
  curseforge_settings curseforge;
};

struct export_report
{
  std::size_t referenced = 0;
  std::size_t bundled = 0;
  std::uint64_t bytes = 0;
  std::vector<std::string> warnings;
};

std::string_view format_name (pack_format format) noexcept;
result<pack_manifest> read_modpack (const std::filesystem::path &source);
result<pack_manifest> parse_pack_index (std::string_view name,
                                        std::string_view text);
result<std::string> import_modpack (install::game_installer &installer,
                                    const std::filesystem::path &source,
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
                      const export_options &options,
                      std::string_view instance_id = {});
bool export_hidden (std::string_view relative, std::string_view instance_id);
bool pick_state (const std::map<std::string, bool> &picks,
                 std::string_view path);
bool picks_below (const std::map<std::string, bool> &picks,
                  std::string_view path);
std::vector<std::string>
collect_picked_files (const std::filesystem::path &game_directory,
                      const std::map<std::string, bool> &picks,
                      std::string_view instance_id);
result<std::vector<std::string>>
detect_client_only (const std::filesystem::path &game_directory,
                    const net::mirror_provider &mirror,
                    cancellation_token token = {});
std::string serialize_export_options (const export_options &options);
result<export_options> parse_export_options (std::string_view json);
result<pack_info> read_pack_info (const std::filesystem::path &directory);
result<std::vector<pack_candidate>>
identify_pack (install::game_installer &installer, std::string_view id,
               const curseforge_settings &curseforge,
               cancellation_token token = {});
result<void> bind_pack (install::game_installer &installer,
                        std::string_view id, const pack_candidate &candidate,
                        const curseforge_settings &curseforge,
                        cancellation_token token = {});
result<bool> record_pack_files (install::game_installer &installer,
                                std::string_view id,
                                const curseforge_settings &curseforge,
                                cancellation_token token = {});
result<update_plan>
plan_update (install::game_installer &installer, std::string_view id,
             std::vector<std::string> urls, std::uint64_t archive_size,
             const curseforge_settings &curseforge,
             progress_sink *sink = nullptr, cancellation_token token = {});
result<update_plan> plan_restore (install::game_installer &installer,
                                  std::string_view id,
                                  const curseforge_settings &curseforge,
                                  progress_sink *sink = nullptr,
                                  cancellation_token token = {});
result<void> apply_update (install::game_installer &installer,
                           const update_plan &plan, const pack_origin &origin,
                           const std::set<std::string> &keep,
                           progress_sink *sink = nullptr,
                           cancellation_token token = {},
                           const file_remover &discard = {});
}
