#pragma once

#include <core/base/hash.hpp>
#include <core/meta/asset_index.hpp>
#include <core/meta/game_layout.hpp>
#include <core/meta/rules.hpp>
#include <core/meta/version.hpp>
#include <core/network/download_client.hpp>
#include <core/network/mirror_provider.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace b_launcher_base::install
{

enum class file_kind : std::uint8_t
{
  client,
  library,
  native,
  asset_index,
  asset,
  logging,
  runtime,
  other,
};

struct planned_file
{
  file_kind kind = file_kind::other;
  std::filesystem::path target;
  std::string relative;
  std::vector<std::string> urls;
  std::optional<sha1_digest> sha1;
  std::int64_t size = -1;
  bool executable = false;
};

struct plan_options
{
  bool client = true;
  bool libraries = true;
  bool natives = true;
  bool asset_index = true;
  bool logging = true;
};

std::vector<const meta::library *>
applicable_libraries (const meta::version &resolved,
                      const meta::rule_context &context);
bool native_matches_arch (const meta::library &lib,
                          const meta::rule_context &context);
std::vector<planned_file> plan_version_files (
    const meta::version &resolved, const meta::game_layout &layout,
    const net::mirror_provider &mirror, const meta::rule_context &context,
    const plan_options &options = {});
std::vector<planned_file>
plan_asset_files (const meta::asset_index &index,
                  const meta::game_layout &layout,
                  const net::mirror_provider &mirror);
std::vector<net::download_task>
to_download_tasks (std::span<const planned_file> files);
std::uint64_t total_size (std::span<const planned_file> files);

}
