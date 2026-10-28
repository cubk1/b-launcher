#include <core/auth/microsoft.hpp>
#include <core/base/format.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <thread>

namespace b_launcher_base::auth
{

namespace
{

using json = nlohmann::json;
using form = std::vector<std::pair<std::string, std::string>>;

constexpr log::logger s_log ("auth");
constexpr std::string_view xbl_url
    = "https://user.auth.xboxlive.com/user/authenticate";
constexpr std::string_view xsts_url
    = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr std::string_view mc_login_url
    = "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr std::string_view mc_profile_url
    = "https://api.minecraftservices.com/minecraft/profile";
constexpr std::string_view live_token_url
    = "https://login.live.com/oauth20_token.srf";
constexpr std::string_view live_authorize_url
    = "https://login.live.com/oauth20_authorize.srf";
constexpr std::string_view live_desktop_redirect
    = "https://login.live.com/oauth20_desktop.srf";

result<json>
parse_body (const net::http_response &response)
{
  auto doc = json::parse (response.body, nullptr, false);
  if (doc.is_discarded ())
    return make_error (
        errc::auth, sformat ("invalid response (HTTP {})", response.status));
  return doc;
}

std::string
text_of (const json &doc, const char *key)
{
  auto it = doc.find (key);
  if (it == doc.end () || !it->is_string ())
    return {};
  return it->get<std::string> ();
}

std::int64_t
int_of (const json &doc, const char *key, std::int64_t fallback)
{
  auto it = doc.find (key);
  if (it == doc.end () || !it->is_number ())
    return fallback;
  return it->get<std::int64_t> ();
}

result<microsoft_token>
token_from (const net::http_response &response)
{
  BLB_TRY_ASSIGN (auto doc, parse_body (response));
  if (!response.ok ())
    {
      auto code = text_of (doc, "error");
      auto detail = text_of (doc, "error_description");
      errc kind = errc::auth;
      if (code == "authorization_pending" || code == "slow_down")
        kind = errc::auth_pending;
      else if (code == "authorization_declined" || code == "access_denied")
        kind = errc::auth_declined;
      else if (code == "expired_token" || code == "invalid_grant")
        kind = errc::auth_expired;
      auto e
          = make_error (kind, code.empty () ? detail : code + ": " + detail);
      if (code == "slow_down")
        e.message = "slow_down";
      return e;
    }
  microsoft_token token;
  token.access_token = text_of (doc, "access_token");
  token.refresh_token = text_of (doc, "refresh_token");
  token.expires_in = int_of (doc, "expires_in", 3600);
  if (token.access_token.empty ())
    return make_error (errc::auth, "no access token in response");
  return token;
}

std::string
jwt_claim (std::string_view token, const char *claim)
{
  auto first = token.find ('.');
  if (first == std::string_view::npos)
    return {};
  auto second = token.find ('.', first + 1);
  if (second == std::string_view::npos)
    return {};
  auto payload = base64_decode (token.substr (first + 1, second - first - 1));
  if (!payload)
    return {};
  auto doc = json::parse (*payload, nullptr, false);
  if (doc.is_discarded () || !doc.is_object ())
    return {};
  auto it = doc.find (claim);
  if (it == doc.end ())
    return {};
  if (it->is_string ())
    return it->get<std::string> ();
  if (it->is_number_integer ())
    return std::to_string (it->get<std::int64_t> ());
  return {};
}

error
xsts_error (const json &doc)
{
  auto code = int_of (doc, "XErr", 0);
  switch (code)
    {
    case 2148916233:
      return make_error (errc::no_xbox_account,
                         "this microsoft account has no xbox profile");
    case 2148916235:
      return make_error (errc::auth, "xbox live is not available in your "
                                     "country or region");
    case 2148916236:
    case 2148916237:
      return make_error (errc::auth, "the account needs adult verification");
    case 2148916238:
      return make_error (errc::child_account,
                         "child accounts must be added to a family");
    default:
      return make_error (errc::auth, sformat ("xsts error {}", code));
    }
}

}

pkce_pair
pkce_pair::generate ()
{
  std::array<std::uint8_t, 32> bytes;
  random_bytes (bytes);
  pkce_pair pair;
  pair.verifier = base64_encode (bytes, true, false);
  auto digest = sha256::digest (pair.verifier.data (), pair.verifier.size ());
  pair.challenge = base64_encode (digest, true, false);
  return pair;
}

microsoft_authenticator::microsoft_authenticator (std::string client_id,
                                                  std::string scope,
                                                  std::string tenant)
    : m_client_id (std::move (client_id))
    , m_scope (std::move (scope))
    , m_tenant (std::move (tenant))
{
}

microsoft_authenticator::microsoft_authenticator (microsoft_client client)
    : m_client_id (std::move (client.client_id))
    , m_scope (std::move (client.scope))
    , m_tenant (std::move (client.tenant))
    , m_live (client.live)
    , m_rps_prefix (std::move (client.rps_prefix))
{
}

const std::string &
microsoft_authenticator::client_id () const noexcept
{
  return m_client_id;
}

std::string
microsoft_authenticator::token_url () const
{
  if (m_live)
    return std::string (live_token_url);
  return sformat ("https://login.microsoftonline.com/{}/oauth2/v2.0/token",
                  m_tenant);
}

result<device_code>
microsoft_authenticator::request_device_code (cancellation_token token) const
{
  if (m_client_id.empty ())
    return make_error (errc::invalid_argument, "microsoft client id is empty");
  form fields { { "client_id", m_client_id }, { "scope", m_scope } };
  BLB_TRY_ASSIGN (
      auto response,
      net::http_post_form (sformat ("https://login.microsoftonline.com/{}/"
                                    "oauth2/v2.0/devicecode",
                                    m_tenant),
                           fields, token));
  BLB_TRY_ASSIGN (auto doc, parse_body (response));
  if (!response.ok ())
    return make_error (errc::auth,
                       sformat ("device code request failed: {}",
                                text_of (doc, "error_description")));
  device_code code;
  code.user_code = text_of (doc, "user_code");
  code.device_code = text_of (doc, "device_code");
  code.verification_uri = text_of (doc, "verification_uri");
  code.message = text_of (doc, "message");
  code.interval = std::chrono::seconds (int_of (doc, "interval", 5));
  code.expires_in = std::chrono::seconds (int_of (doc, "expires_in", 900));
  return code;
}

result<microsoft_token>
microsoft_authenticator::poll_device_code (const device_code &code,
                                           cancellation_token token) const
{
  auto deadline = std::chrono::steady_clock::now () + code.expires_in;
  auto interval = code.interval;
  auto url = token_url ();
  while (std::chrono::steady_clock::now () < deadline)
    {
      if (token.wait_for (interval))
        return make_error (errc::cancelled, "login cancelled");
      form fields {
        { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
        { "client_id", m_client_id },
        { "device_code", code.device_code },
      };
      BLB_TRY_ASSIGN (auto response, net::http_post_form (url, fields, token));
      auto result = token_from (response);
      if (result)
        return result;
      if (result.err ().code != errc::auth_pending)
        return result;
      if (result.err ().message == "slow_down")
        interval += std::chrono::seconds (5);
    }
  return make_error (errc::auth_expired, "device code expired");
}

std::string
microsoft_authenticator::authorization_url (std::string_view redirect_uri,
                                            std::string_view state,
                                            const pkce_pair &pkce) const
{
  if (m_live)
    return sformat ("{}?client_id={}&response_type=code&redirect_uri={}"
                    "&scope={}&state={}&display=touch&prompt=select_account",
                    live_authorize_url, url_encode (m_client_id),
                    url_encode (redirect_uri), url_encode (m_scope),
                    url_encode (state));
  return sformat (
      "https://login.microsoftonline.com/{}/oauth2/v2.0/authorize?client_id={}"
      "&response_type=code&redirect_uri={}&response_mode=query&scope={}"
      "&state={}&code_challenge={}&code_challenge_method=S256"
      "&prompt=select_account",
      m_tenant, url_encode (m_client_id), url_encode (redirect_uri),
      url_encode (m_scope), url_encode (state), pkce.challenge);
}

result<microsoft_token>
microsoft_authenticator::exchange_code (std::string_view code,
                                        std::string_view redirect_uri,
                                        const pkce_pair &pkce,
                                        cancellation_token token) const
{
  form fields {
    { "client_id", m_client_id },
    { "grant_type", "authorization_code" },
    { "code", std::string (code) },
    { "redirect_uri", std::string (redirect_uri) },
    { "scope", m_scope },
  };
  if (!m_live)
    fields.push_back ({ "code_verifier", pkce.verifier });
  BLB_TRY_ASSIGN (auto response,
                  net::http_post_form (token_url (), fields, token));
  return token_from (response);
}

result<microsoft_token>
microsoft_authenticator::refresh_token (std::string_view refresh,
                                        cancellation_token token) const
{
  form fields {
    { "client_id", m_client_id },
    { "grant_type", "refresh_token" },
    { "refresh_token", std::string (refresh) },
    { "scope", m_scope },
  };
  if (m_live)
    fields.push_back ({ "redirect_uri", std::string (live_desktop_redirect) });
  BLB_TRY_ASSIGN (auto response,
                  net::http_post_form (token_url (), fields, token));
  return token_from (response);
}

result<account>
microsoft_authenticator::login (const microsoft_token &ms,
                                cancellation_token token) const
{
  json xbl_request = { { "Properties",
                         { { "AuthMethod", "RPS" },
                           { "SiteName", "user.auth.xboxlive.com" },
                           { "RpsTicket", m_rps_prefix + ms.access_token } } },
                       { "RelyingParty", "http://auth.xboxlive.com" },
                       { "TokenType", "JWT" } };
  BLB_TRY_ASSIGN (
      auto xbl_response,
      net::http_post_json (xbl_url, xbl_request.dump (), {}, token));
  BLB_TRY_ASSIGN (auto xbl, parse_body (xbl_response));
  if (!xbl_response.ok ())
    return make_error (errc::auth, sformat ("xbox live login failed (HTTP {})",
                                            xbl_response.status));
  auto xbl_token = text_of (xbl, "Token");
  std::string uhs;
  if (auto claims = xbl.find ("DisplayClaims"); claims != xbl.end ())
    if (auto xui = claims->find ("xui");
        xui != claims->end () && xui->is_array () && !xui->empty ())
      uhs = text_of ((*xui)[0], "uhs");
  if (xbl_token.empty () || uhs.empty ())
    return make_error (errc::auth, "invalid xbox live response");

  json xsts_request = { { "Properties",
                          { { "SandboxId", "RETAIL" },
                            { "UserTokens", json::array ({ xbl_token }) } } },
                        { "RelyingParty", "rp://api.minecraftservices.com/" },
                        { "TokenType", "JWT" } };
  BLB_TRY_ASSIGN (
      auto xsts_response,
      net::http_post_json (xsts_url, xsts_request.dump (), {}, token));
  BLB_TRY_ASSIGN (auto xsts, parse_body (xsts_response));
  if (xsts_response.status == 401)
    return xsts_error (xsts);
  if (!xsts_response.ok ())
    return make_error (
        errc::auth, sformat ("xsts failed (HTTP {})", xsts_response.status));
  auto xsts_token = text_of (xsts, "Token");

  json mc_request
      = { { "identityToken", sformat ("XBL3.0 x={};{}", uhs, xsts_token) } };
  BLB_TRY_ASSIGN (
      auto mc_response,
      net::http_post_json (mc_login_url, mc_request.dump (), {}, token));
  BLB_TRY_ASSIGN (auto mc, parse_body (mc_response));
  if (!mc_response.ok ())
    return make_error (errc::auth, sformat ("minecraft login failed (HTTP {})",
                                            mc_response.status));
  account out;
  out.type = account_type::microsoft;
  out.access_token = text_of (mc, "access_token");
  out.expires_at = unix_now () + int_of (mc, "expires_in", 86400);
  out.refresh_token = ms.refresh_token;
  out.client_id = m_client_id;
  out.xuid = jwt_claim (out.access_token, "xuid");
  if (out.access_token.empty ())
    return make_error (errc::auth, "no minecraft access token");
  return fetch_profile (std::move (out), token);
}

result<account>
microsoft_authenticator::login_device_flow (
    const std::function<void (const device_code &)> &show,
    cancellation_token token) const
{
  BLB_TRY_ASSIGN (auto code, request_device_code (token));
  if (show)
    show (code);
  BLB_TRY_ASSIGN (auto ms, poll_device_code (code, token));
  return login (ms, token);
}

result<account>
microsoft_authenticator::refresh (const account &current,
                                  cancellation_token token) const
{
  if (current.type != account_type::microsoft)
    return current;
  if (current.refresh_token.empty ())
    return make_error (errc::auth_expired, "no refresh token");
  BLB_TRY_ASSIGN (auto ms, refresh_token (current.refresh_token, token));
  if (ms.refresh_token.empty ())
    ms.refresh_token = current.refresh_token;
  s_log.info ("refreshed microsoft token for {}", current.name);
  return login (ms, token);
}

result<account>
microsoft_authenticator::fetch_profile (account current,
                                        cancellation_token token)
{
  net::http_request request;
  request.url = std::string (mc_profile_url);
  request.headers.push_back (
      { "Authorization", "Bearer " + current.access_token });
  BLB_TRY_ASSIGN (auto response, net::http_send (request, token));
  if (response.status == 404)
    return make_error (errc::no_minecraft_profile,
                       "this account does not own minecraft");
  BLB_TRY_ASSIGN (auto doc, parse_body (response));
  if (!response.ok ())
    return make_error (errc::auth, sformat ("profile request failed (HTTP {})",
                                            response.status));
  current.uuid = strip_uuid (text_of (doc, "id"));
  current.name = text_of (doc, "name");
  if (auto skins = doc.find ("skins");
      skins != doc.end () && skins->is_array ())
    for (const auto &skin : *skins)
      if (text_of (skin, "state") == "ACTIVE")
        current.skin_url = text_of (skin, "url");
  return current;
}

}
