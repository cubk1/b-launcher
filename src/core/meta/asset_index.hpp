#pragma once

#include <core/base/error.hpp>
#include <core/base/hash.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::meta
{

inline constexpr std::string_view asset_object_base_url
    = "https://resources.download.minecraft.net/";

struct asset_object
{
  std::string name;
  sha1_digest hash {};
  std::uint64_t size = 0;
};

struct asset_index
{
  bool is_virtual = false;
  bool map_to_resources = false;
  std::vector<asset_object> objects;
};

result<asset_index> parse_asset_index (std::string_view json);
std::string asset_object_path (const sha1_digest &hash);
std::string asset_object_url (const sha1_digest &hash);

}
