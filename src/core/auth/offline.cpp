#include <core/auth/offline.hpp>
#include <core/base/format.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>

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
  if (a.uuid.size () != 32)
    return make_error (errc::invalid_argument, "invalid uuid");
  a.access_token = random_bytes_hex (16);
  return a;
}

}
