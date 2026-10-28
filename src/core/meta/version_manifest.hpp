#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/network/mirror_provider.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::meta
{

inline constexpr std::string_view version_manifest_url
    = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

struct manifest_version
{
  std::string id;
  std::string type;
  std::string url;
  std::string time;
  std::string release_time;
  std::string sha1;
  int compliance_level = 0;
};

struct version_manifest
{
  std::string latest_release;
  std::string latest_snapshot;
  std::vector<manifest_version> versions;

  const manifest_version *find (std::string_view id) const;
};

result<version_manifest> parse_version_manifest (std::string_view json);
result<version_manifest>
fetch_version_manifest (const net::mirror_provider &mirror,
                        cancellation_token token = {});

}
