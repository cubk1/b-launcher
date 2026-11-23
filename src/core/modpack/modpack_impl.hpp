#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/modpack.hpp>
#include <core/network/mirror_provider.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::modpack::detail
{

struct remote_file
{
  std::string url;
  std::string file_name;
  std::string sha1;
  std::string sha512;
  std::int64_t size = -1;
  std::string client_side;
  std::string server_side;
};

result<void> write_pack_info (const std::filesystem::path &directory,
                              const pack_manifest &manifest,
                              const pack_origin &origin,
                              const std::map<std::string, owned_file> &owned);
result<std::optional<std::string>>
read_multimc_file (const std::filesystem::path &source, zip_reader *zip,
                   std::string_view name);
result<void> copy_multimc_game (const std::filesystem::path &source,
                                const pack_manifest &manifest,
                                const meta::game_layout &layout,
                                const std::string &id, progress_sink *sink,
                                cancellation_token token);
result<void> install_multimc (install::game_installer &installer,
                              const std::filesystem::path &source,
                              const pack_manifest &manifest,
                              const std::string &id, progress_sink *sink,
                              cancellation_token token);

bool glob_match (std::string_view pattern, std::string_view path);
std::uint32_t curseforge_fingerprint (std::string_view data);
result<void> resolve_curseforge (std::vector<pack_file> &files,
                                 const curseforge_settings &settings,
                                 const net::mirror_provider &mirror,
                                 cancellation_token token);
result<std::map<std::string, remote_file>>
lookup_modrinth (std::span<const std::string> sha1s,
                 const net::mirror_provider &mirror, cancellation_token token);
result<std::map<std::uint32_t, remote_file>>
lookup_curseforge (std::span<const std::uint32_t> fingerprints,
                   const curseforge_settings &settings,
                   const net::mirror_provider &mirror,
                   cancellation_token token);
}
