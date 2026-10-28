#include <core/auth/yggdrasil.hpp>
#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include <nlohmann/json.hpp>

namespace b_launcher_base::auth
{

namespace
{

using json = nlohmann::json;

constexpr log::logger s_log ("yggdrasil");
constexpr std::string_view nide8_root = "https://auth.mc-user.com:233/";
constexpr std::string_view nide8_jar_url
    = "https://login.mc-user.com:233/index/jar";
constexpr std::string_view authlib_sources[] = {
  "https://bmclapi2.bangbang93.com/mirrors/authlib-injector/artifact/"
  "latest.json",
  "https://authlib-injector.yushi.moe/artifact/latest.json",
};

std::string
text_of (const json &doc, const char *key)
{
  auto it = doc.find (key);
  if (it == doc.end () || !it->is_string ())
    return {};
  return it->get<std::string> ();
}

std::string
trim_slash (std::string url)
{
  while (!url.empty () && url.back () == '/')
    url.pop_back ();
  return url;
}

std::string
normalize_url (std::string_view input)
{
  std::string url (trim (input));
  if (url.empty ())
    return url;
  if (url.find ("://") == std::string::npos)
    url = "https://" + url;
  return trim_slash (url);
}

std::string
origin_of (std::string_view url)
{
  auto scheme = url.find ("://");
  auto start = scheme == std::string_view::npos ? 0 : scheme + 3;
  return std::string (url.substr (0, url.find ('/', start)));
}

std::string
host_of (std::string_view url)
{
  auto scheme = url.find ("://");
  auto start = scheme == std::string_view::npos ? 0 : scheme + 3;
  auto end = url.find ('/', start);
  return to_lower (url.substr (start, end == std::string_view::npos
                                          ? std::string_view::npos
                                          : end - start));
}

std::string
resolve_location (std::string_view base, std::string_view location)
{
  if (location.find ("://") != std::string_view::npos)
    return std::string (location);
  if (location.starts_with ("//"))
    return std::string (base.substr (0, base.find ("://") + 1))
           + std::string (location);
  if (location.starts_with ('/'))
    return origin_of (base) + std::string (location);
  std::string dir (base);
  auto scheme = dir.find ("://");
  auto slash = dir.rfind ('/');
  if (slash == std::string::npos
      || (scheme != std::string::npos && slash < scheme + 3))
    dir += '/';
  else
    dir.erase (slash + 1);
  return dir + std::string (location);
}

result<net::http_response>
post_json (const std::string &url, const json &body, cancellation_token token)
{
  return net::http_post_json (url, body.dump (),
                              { { "Accept-Language", "zh-CN" } }, token);
}

error
server_error (const net::http_response &response, std::string_view fallback)
{
  auto doc = json::parse (response.body, nullptr, false);
  std::string message;
  if (!doc.is_discarded () && doc.is_object ())
    message = text_of (doc, "errorMessage");
  if (message.empty ())
    message = sformat ("{}（HTTP {}）", fallback, response.status);
  return make_error (errc::auth, message);
}

result<json>
object_of (const net::http_response &response)
{
  auto doc = json::parse (response.body, nullptr, false);
  if (doc.is_discarded () || !doc.is_object ())
    return make_error (
        errc::auth,
        sformat ("认证服务器返回的内容无效（HTTP {}）", response.status));
  return doc;
}

yggdrasil_profile
profile_of (const json &doc)
{
  return { strip_uuid (text_of (doc, "id")), text_of (doc, "name") };
}

std::string
fetch_skin (const std::string &api, const std::string &uuid,
            cancellation_token token)
{
  auto body = net::http_get (
      api + "/sessionserver/session/minecraft/profile/" + uuid, token);
  if (!body)
    return {};
  auto doc = json::parse (*body, nullptr, false);
  if (doc.is_discarded () || !doc.is_object ())
    return {};
  auto properties = doc.find ("properties");
  if (properties == doc.end () || !properties->is_array ())
    return {};
  for (const auto &property : *properties)
    {
      if (!property.is_object () || text_of (property, "name") != "textures")
        continue;
      auto decoded = base64_decode (text_of (property, "value"));
      if (!decoded)
        return {};
      auto textures = json::parse (*decoded, nullptr, false);
      if (textures.is_discarded () || !textures.is_object ())
        return {};
      auto all = textures.find ("textures");
      if (all == textures.end () || !all->is_object ())
        return {};
      auto skin = all->find ("SKIN");
      if (skin == all->end () || !skin->is_object ())
        return {};
      return text_of (*skin, "url");
    }
  return {};
}

account
with_profile (account a, const yggdrasil_profile &profile,
              cancellation_token token)
{
  bool same = a.uuid == profile.id;
  a.uuid = profile.id;
  if (!profile.name.empty ())
    a.name = profile.name;
  auto skin = fetch_skin (yggdrasil_api (a), a.uuid, token);
  if (!skin.empty () || !same)
    a.skin_url = skin;
  return a;
}

result<account>
refresh_with (const account &current, const yggdrasil_profile *profile,
              cancellation_token token)
{
  json body
      = { { "accessToken", current.access_token }, { "requestUser", true } };
  if (!current.client_token.empty ())
    body["clientToken"] = current.client_token;
  if (profile)
    body["selectedProfile"]
        = { { "id", profile->id }, { "name", profile->name } };
  BLB_TRY_ASSIGN (auto response,
                  post_json (yggdrasil_api (current) + "/authserver/refresh",
                             body, token));
  if (!response.ok ())
    {
      auto e = server_error (response, "刷新登录失败");
      if (response.status == 401 || response.status == 403)
        e.code = errc::auth_expired;
      return e;
    }
  BLB_TRY_ASSIGN (auto doc, object_of (response));
  account out = current;
  out.access_token = text_of (doc, "accessToken");
  auto client = text_of (doc, "clientToken");
  if (!client.empty ())
    out.client_token = client;
  if (out.access_token.empty ())
    return make_error (errc::auth, "认证服务器没有返回登录令牌");
  auto selected = doc.find ("selectedProfile");
  if (selected != doc.end () && selected->is_object ()
      && !text_of (*selected, "id").empty ())
    return with_profile (std::move (out), profile_of (*selected), token);
  yggdrasil_profile kept
      = profile ? *profile : yggdrasil_profile { out.uuid, out.name };
  return with_profile (std::move (out), kept, token);
}

result<std::filesystem::path>
authlib_jar (const std::filesystem::path &directory, cancellation_token token)
{
  auto jar = directory / "authlib-injector.jar";
  if (fs::exists (jar))
    return jar;
  error last = make_error (errc::network, "无法下载 authlib-injector");
  for (auto source : authlib_sources)
    {
      BLB_TRY (token.check ());
      auto text = net::http_get (source, token);
      if (!text)
        {
          last = text.err ();
          continue;
        }
      auto doc = json::parse (*text, nullptr, false);
      if (doc.is_discarded () || !doc.is_object ())
        continue;
      auto url = text_of (doc, "download_url");
      std::string expected;
      if (auto sums = doc.find ("checksums");
          sums != doc.end () && sums->is_object ())
        expected = text_of (*sums, "sha256");
      if (url.empty ())
        continue;
      auto data = net::http_get (url, token);
      if (!data)
        {
          last = data.err ();
          continue;
        }
      auto digest = sha256::digest (data->data (), data->size ());
      if (!expected.empty ()
          && !iequals (to_hex (std::span<const std::uint8_t> (digest)),
                       expected))
        {
          last = make_error (errc::checksum_mismatch,
                             "authlib-injector 校验失败");
          continue;
        }
      BLB_TRY (fs::create_directories (directory));
      BLB_TRY (fs::write_file_atomic (jar, *data));
      s_log.info ("downloaded authlib-injector {}", text_of (doc, "version"));
      return jar;
    }
  return last;
}

result<std::filesystem::path>
nide8_jar (const std::filesystem::path &directory, const std::string &metadata,
           cancellation_token token)
{
  auto jar = directory / "nide8auth.jar";
  std::string expected;
  auto doc = json::parse (metadata, nullptr, false);
  if (!doc.is_discarded () && doc.is_object ())
    expected = text_of (doc, "jarHash");
  if (fs::exists (jar))
    {
      if (expected.empty ())
        return jar;
      if (auto data = fs::read_file (jar);
          data && iequals (to_hex (sha1::digest (*data)), expected))
        return jar;
    }
  BLB_TRY_ASSIGN (auto data, net::http_get (nide8_jar_url, token));
  if (!expected.empty () && !iequals (to_hex (sha1::digest (data)), expected))
    return make_error (errc::checksum_mismatch, "nide8auth.jar 校验失败");
  BLB_TRY (fs::create_directories (directory));
  BLB_TRY (fs::write_file_atomic (jar, data));
  s_log.info ("downloaded nide8auth.jar");
  return jar;
}

}

bool
is_yggdrasil (account_type type) noexcept
{
  return type == account_type::yggdrasil || type == account_type::nide8;
}

bool
valid_nide8_id (std::string_view id) noexcept
{
  if (id.size () != 32)
    return false;
  for (char c : id)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
          || (c >= 'A' && c <= 'F')))
      return false;
  return true;
}

std::string
nide8_api (std::string_view server_id)
{
  return std::string (nide8_root) + std::string (trim (server_id));
}

std::string
nide8_register_url (std::string_view server_id)
{
  return "https://login.mc-user.com:233/" + std::string (trim (server_id))
         + "/register";
}

std::string
yggdrasil_api (const account &a)
{
  return a.type == account_type::nide8 ? nide8_api (a.server) : a.server;
}

bool
same_yggdrasil_server (std::string_view a, std::string_view b)
{
  auto left = normalize_url (a);
  auto right = normalize_url (b);
  if (left.empty () || right.empty ())
    return false;
  return iequals (left, right) || host_of (left) == host_of (right);
}

result<yggdrasil_server>
resolve_yggdrasil (std::string_view input, cancellation_token token)
{
  std::string url = normalize_url (input);
  if (url.empty ())
    return make_error (errc::invalid_argument, "请填写认证服务器地址");
  net::http_request request;
  request.url = url;
  BLB_TRY_ASSIGN (auto response, net::http_send (request, token));
  std::string base = trim_slash (
      response.effective_url.empty () ? url : response.effective_url);
  auto location = response.header ("X-Authlib-Injector-API-Location");
  if (!location.empty ())
    {
      auto api = trim_slash (resolve_location (base, location));
      if (api != base)
        {
          request.url = api;
          BLB_TRY_ASSIGN (response, net::http_send (request, token));
          base = api;
        }
    }
  if (!response.ok ())
    return make_error (errc::auth, sformat ("无法连接认证服务器（HTTP {}）",
                                            response.status));
  auto doc = json::parse (response.body, nullptr, false);
  if (doc.is_discarded () || !doc.is_object ()
      || (!doc.contains ("meta") && !doc.contains ("signaturePublickey")
          && !doc.contains ("skinDomains")))
    return make_error (errc::auth, "这个地址不是 Yggdrasil 认证服务器");
  yggdrasil_server out;
  out.api = base;
  out.metadata = response.body;
  if (auto meta = doc.find ("meta"); meta != doc.end () && meta->is_object ())
    {
      out.name = text_of (*meta, "serverName");
      if (auto links = meta->find ("links");
          links != meta->end () && links->is_object ())
        {
          out.homepage = text_of (*links, "homepage");
          out.register_url = text_of (*links, "register");
        }
    }
  return out;
}

result<yggdrasil_session>
yggdrasil_login (account_type type, std::string_view server,
                 std::string_view username, std::string_view password,
                 cancellation_token token)
{
  account base;
  base.type = type;
  if (type == account_type::nide8)
    {
      std::string id (trim (server));
      if (!valid_nide8_id (id))
        return make_error (errc::invalid_argument,
                           "服务器 ID 应该是 32 位的字母和数字");
      base.server = id;
      if (auto metadata = net::http_get (nide8_api (id), token))
        {
          auto doc = json::parse (*metadata, nullptr, false);
          if (!doc.is_discarded () && doc.is_object ())
            if (auto meta = doc.find ("meta");
                meta != doc.end () && meta->is_object ())
              base.server_name = text_of (*meta, "serverName");
        }
    }
  else
    {
      BLB_TRY_ASSIGN (auto resolved, resolve_yggdrasil (server, token));
      base.server = resolved.api;
      base.server_name = resolved.name;
    }
  base.username = std::string (trim (username));
  base.client_token = random_bytes_hex (16);
  json body = { { "agent", { { "name", "Minecraft" }, { "version", 1 } } },
                { "username", base.username },
                { "password", std::string (password) },
                { "clientToken", base.client_token },
                { "requestUser", true } };
  BLB_TRY_ASSIGN (auto response,
                  post_json (yggdrasil_api (base) + "/authserver/authenticate",
                             body, token));
  if (!response.ok ())
    return server_error (response, response.status == 403
                                       ? "账号或密码错误，或者登录太频繁"
                                       : "登录失败");
  BLB_TRY_ASSIGN (auto doc, object_of (response));
  base.access_token = text_of (doc, "accessToken");
  auto client = text_of (doc, "clientToken");
  if (!client.empty ())
    base.client_token = client;
  if (base.access_token.empty ())
    return make_error (errc::auth, "认证服务器没有返回登录令牌");
  yggdrasil_session session;
  if (auto list = doc.find ("availableProfiles");
      list != doc.end () && list->is_array ())
    for (const auto &entry : *list)
      if (entry.is_object ())
        {
          auto profile = profile_of (entry);
          if (!profile.id.empty ())
            session.profiles.push_back (std::move (profile));
        }
  auto selected = doc.find ("selectedProfile");
  if (selected != doc.end () && selected->is_object ()
      && !text_of (*selected, "id").empty ())
    {
      session.base
          = with_profile (std::move (base), profile_of (*selected), token);
      return session;
    }
  if (session.profiles.empty ())
    return make_error (errc::auth,
                       type == account_type::nide8
                           ? "这个账号还没有加入这个服务器"
                           : "这个账号还没有角色，先到皮肤站创建一个角色");
  if (session.profiles.size () == 1)
    {
      BLB_TRY_ASSIGN (
          session.base,
          yggdrasil_select (base, session.profiles.front (), token));
      return session;
    }
  session.base = std::move (base);
  return session;
}

result<account>
yggdrasil_select (const account &base, const yggdrasil_profile &profile,
                  cancellation_token token)
{
  return refresh_with (base, &profile, token);
}

result<account>
yggdrasil_refresh (const account &current, cancellation_token token)
{
  if (!is_yggdrasil (current.type))
    return current;
  return refresh_with (current, nullptr, token);
}

result<account>
yggdrasil_ensure (const account &current, cancellation_token token)
{
  if (!is_yggdrasil (current.type))
    return current;
  json body = { { "accessToken", current.access_token } };
  if (!current.client_token.empty ())
    body["clientToken"] = current.client_token;
  BLB_TRY_ASSIGN (auto response,
                  post_json (yggdrasil_api (current) + "/authserver/validate",
                             body, token));
  if (response.ok ())
    return current;
  s_log.info ("token of {} is no longer valid, refreshing", current.name);
  return refresh_with (current, nullptr, token);
}

result<std::vector<std::string>>
yggdrasil_agent (const account &a, const std::filesystem::path &directory,
                 cancellation_token token)
{
  std::vector<std::string> out;
  if (a.type == account_type::nide8)
    {
      auto metadata = net::http_get (nide8_api (a.server), token);
      BLB_TRY_ASSIGN (
          auto jar,
          nide8_jar (directory, metadata ? *metadata : std::string (), token));
      out.push_back ("-javaagent:" + path_to_native (jar) + "=" + a.server);
      out.emplace_back ("-Dnide8auth.client=true");
      return out;
    }
  BLB_TRY_ASSIGN (auto jar, authlib_jar (directory, token));
  auto metadata = net::http_get (a.server, token);
  if (!metadata)
    return make_error (metadata.err ().code,
                       "无法连接第三方认证服务器：" + metadata.err ().message);
  if (current_platform ().os == os_type::windows)
    out.emplace_back ("-Djavax.net.ssl.trustStoreType=WINDOWS-ROOT");
  out.push_back ("-javaagent:" + path_to_native (jar) + "=" + a.server);
  out.emplace_back ("-Dauthlibinjector.side=client");
  out.push_back (
      "-Dauthlibinjector.yggdrasil.prefetched="
      + base64_encode (std::span<const std::uint8_t> (
          reinterpret_cast<const std::uint8_t *> (metadata->data ()),
          metadata->size ())));
  return out;
}

}
