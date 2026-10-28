#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/modpack/modpack.hpp>

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

bool safe_relative (std::string_view path);
bool glob_match (std::string_view pattern, std::string_view path);
std::uint32_t curseforge_fingerprint (std::string_view data);
std::string modrinth_mirror (std::string_view url);
result<void> resolve_curseforge (std::vector<pack_file> &files,
                                 const curseforge_settings &settings,
                                 cancellation_token token);
result<std::map<std::string, remote_file>>
lookup_modrinth (std::span<const std::string> sha1s, cancellation_token token);
result<std::map<std::uint32_t, remote_file>>
lookup_curseforge (std::span<const std::uint32_t> fingerprints,
                   const curseforge_settings &settings,
                   cancellation_token token);

}
