#include <core/auth/account.hpp>
#include <core/base/strings.hpp>

#include <nlohmann/json.hpp>

#include <chrono>

namespace b_launcher_base::auth
{

using json = nlohmann::json;

std::int64_t
unix_now ()
{
  return std::chrono::duration_cast<std::chrono::seconds> (
             std::chrono::system_clock::now ().time_since_epoch ())
      .count ();
}

std::string
account::user_type () const
{
  return type == account_type::microsoft ? "msa" : "legacy";
}

bool
account::needs_refresh (std::chrono::seconds margin) const
{
  if (type != account_type::microsoft)
    return false;
  return access_token.empty () || expires_at <= unix_now () + margin.count ();
}

std::string_view
account_type_name (account_type type) noexcept
{
  return type == account_type::microsoft ? "microsoft" : "offline";
}

std::string
strip_uuid (std::string_view uuid)
{
  std::string out;
  for (char c : uuid)
    if (c != '-')
      out.push_back (c >= 'A' && c <= 'F' ? static_cast<char> (c - 'A' + 'a')
                                          : c);
  return out;
}

std::string
format_uuid (std::string_view hex)
{
  auto plain = strip_uuid (hex);
  if (plain.size () != 32)
    return plain;
  return plain.substr (0, 8) + "-" + plain.substr (8, 4) + "-"
         + plain.substr (12, 4) + "-" + plain.substr (16, 4) + "-"
         + plain.substr (20);
}

std::string
serialize_accounts (std::span<const account> accounts)
{
  json list = json::array ();
  for (const auto &a : accounts)
    list.push_back ({ { "type", account_type_name (a.type) },
                      { "name", a.name },
                      { "uuid", a.uuid },
                      { "accessToken", a.access_token },
                      { "refreshToken", a.refresh_token },
                      { "xuid", a.xuid },
                      { "clientId", a.client_id },
                      { "expiresAt", a.expires_at },
                      { "skin", a.skin_url } });
  return json { { "accounts", std::move (list) } }.dump (2);
}

result<std::vector<account>>
parse_accounts (std::string_view text)
{
  auto doc = json::parse (text, nullptr, false);
  if (doc.is_discarded () || !doc.is_object ())
    return make_error (errc::parse, "invalid accounts json");
  std::vector<account> out;
  auto it = doc.find ("accounts");
  if (it == doc.end () || !it->is_array ())
    return out;
  for (const auto &entry : *it)
    {
      if (!entry.is_object ())
        continue;
      account a;
      a.type = entry.value ("type", std::string ()) == "microsoft"
                   ? account_type::microsoft
                   : account_type::offline;
      a.name = entry.value ("name", std::string ());
      a.uuid = entry.value ("uuid", std::string ());
      a.access_token = entry.value ("accessToken", std::string ());
      a.refresh_token = entry.value ("refreshToken", std::string ());
      a.xuid = entry.value ("xuid", std::string ());
      a.client_id = entry.value ("clientId", std::string ());
      a.expires_at = entry.value ("expiresAt", std::int64_t (0));
      a.skin_url = entry.value ("skin", std::string ());
      out.push_back (std::move (a));
    }
  return out;
}

}
