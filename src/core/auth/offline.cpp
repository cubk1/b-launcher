#include <core/auth/offline.hpp>
#include <core/base/format.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>

#include <array>

namespace b_launcher_base::auth
{

std::string
offline_uuid (std::string_view name)
{
  std::string seed = "OfflinePlayer:";
  seed += name;
  auto digest = md5::digest (seed.data (), seed.size ());
  digest[6] = static_cast<std::uint8_t> ((digest[6] & 0x0f) | 0x30);
  digest[8] = static_cast<std::uint8_t> ((digest[8] & 0x3f) | 0x80);
  return to_hex (std::span<const std::uint8_t> (digest));
}

std::string
pcl_offline_uuid (std::string_view name)
{
  std::u16string units = path_from_utf8 (name).u16string ();
  std::uint64_t hash = 5381;
  for (char16_t unit : units)
    hash = (hash << 5) ^ hash ^ unit;
  hash ^= 0xa98f501bc684032fULL;
  auto hex = [] (std::uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out (16, '0');
    for (int i = 15; i >= 0; --i, value >>= 4)
      out[i] = digits[value & 0xf];
    return out;
  };
  std::string full = hex (units.size ()) + hex (hash);
  full[12] = '3';
  full[16] = '9';
  return full;
}

bool
valid_player_name (std::string_view name) noexcept
{
  if (name.size () < 3 || name.size () > 16)
    return false;
  for (char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
          || (c >= '0' && c <= '9') || c == '_'))
      return false;
  return true;
}

result<account>
offline_account (std::string_view name, std::optional<std::string> uuid)
{
  if (name.empty () || name.size () > 64)
    return make_error (errc::invalid_argument,
                       sformat ("invalid player name '{}'", name));
  account a;
  a.type = account_type::offline;
  a.name = std::string (name);
  a.uuid = uuid ? strip_uuid (*uuid) : offline_uuid (name);
  std::array<std::uint8_t, 16> bytes;
  if (!from_hex (a.uuid, bytes))
    return make_error (errc::invalid_argument, "invalid uuid");
  a.access_token = random_bytes_hex (16);
  return a;
}

}
