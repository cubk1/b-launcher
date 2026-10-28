#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/network/http_client.hpp>

#include "core/modpack/modpack_impl.hpp"

#include <algorithm>
#include <set>

namespace b_launcher_base::modpack::detail
{

namespace
{

using json = nlohmann::json;

constexpr log::logger s_log ("modpack");
constexpr std::string_view modrinth_api = "https://api.modrinth.com/v2";

std::string
text_of (const json &node, const char *key)
{
  if (!node.is_object ())
    return {};
  auto it = node.find (key);
  if (it == node.end () || !it->is_string ())
    return {};
  return it->get<std::string> ();
}

std::int64_t
int_of (const json &node, const char *key, std::int64_t fallback = 0)
{
  if (!node.is_object ())
    return fallback;
  auto it = node.find (key);
  if (it == node.end () || !it->is_number ())
    return fallback;
  return it->get<std::int64_t> ();
}

result<json>
post_json (std::string_view url, const json &body,
           std::vector<net::http_header> headers, cancellation_token token)
{
  BLB_TRY_ASSIGN (
      auto response,
      net::http_post_json (url, body.dump (), std::move (headers), token));
  if (!response.ok ())
    return make_error (errc::http_status,
                       sformat ("HTTP {} ({})", response.status, url));
  auto doc = json::parse (response.body, nullptr, false);
  if (doc.is_discarded ())
    return make_error (errc::parse, sformat ("invalid json from {}", url));
  return doc;
}

result<json>
curseforge_post (const curseforge_settings &settings, std::string_view path,
                 const json &body, cancellation_token token)
{
  error last = make_error (errc::network, "no curseforge api configured");
  for (const auto &root : settings.api_roots)
    {
      std::vector<net::http_header> headers;
      bool official = root.find ("api.curseforge.com") != std::string::npos;
      if (official)
        {
          if (settings.api_key.empty ())
            continue;
          headers.push_back ({ "x-api-key", settings.api_key });
        }
      auto doc = post_json (root + std::string (path), body, headers, token);
      if (doc)
        return doc;
      last = std::move (doc).err ();
      if (last.code == errc::cancelled)
        return last;
      s_log.warn ("curseforge api {} failed: {}", root, last.message);
    }
  return last;
}

remote_file
curseforge_file (const json &f)
{
  remote_file out;
  out.file_name = text_of (f, "fileName");
  out.url = text_of (f, "downloadUrl");
  out.size = int_of (f, "fileLength", -1);
  if (auto hashes = f.find ("hashes");
      hashes != f.end () && hashes->is_array ())
    for (const auto &h : *hashes)
      if (int_of (h, "algo") == 1)
        out.sha1 = to_lower (text_of (h, "value"));
  if (out.url.empty ())
    {
      auto id = int_of (f, "id");
      out.url = sformat ("https://edge.forgecdn.net/files/{}/{}/{}", id / 1000,
                         id % 1000, url_encode (out.file_name));
    }
  return out;
}

std::vector<std::string>
curseforge_urls (const std::string &url)
{
  std::vector<std::string> out { url };
  for (std::string_view host :
       { "https://edge.forgecdn.net", "https://mediafilez.forgecdn.net" })
    if (url.starts_with (host))
      {
        auto rest = url.substr (host.size ());
        if (host != "https://mediafilez.forgecdn.net")
          out.push_back ("https://mediafilez.forgecdn.net" + rest);
        out.push_back ("https://mod.mcimirror.top" + rest);
      }
  return out;
}

}

bool
glob_match (std::string_view pattern, std::string_view path)
{
  if (pattern.empty ())
    return path.empty ();
  if (pattern.starts_with ("**"))
    {
      auto rest = pattern.substr (2);
      if (rest.starts_with ('/'))
        rest.remove_prefix (1);
      for (std::size_t i = 0; i <= path.size (); ++i)
        if ((i == 0 || path[i - 1] == '/' || rest.empty ())
            && glob_match (rest, path.substr (i)))
          return true;
      return false;
    }
  if (pattern.front () == '*')
    {
      for (std::size_t i = 0; i <= path.size (); ++i)
        {
          if (glob_match (pattern.substr (1), path.substr (i)))
            return true;
          if (i < path.size () && path[i] == '/')
            return false;
        }
      return false;
    }
  if (path.empty ())
    return false;
  if (pattern.front () == '?' ? path.front () != '/'
                              : pattern.front () == path.front ())
    return glob_match (pattern.substr (1), path.substr (1));
  return false;
}

std::uint32_t
curseforge_fingerprint (std::string_view data)
{
  std::string normalized;
  normalized.reserve (data.size ());
  for (char c : data)
    if (c != 9 && c != 10 && c != 13 && c != 32)
      normalized.push_back (c);
  constexpr std::uint32_t m = 0x5bd1e995;
  auto length = static_cast<std::uint32_t> (normalized.size ());
  std::uint32_t h = 1 ^ length;
  auto *bytes = reinterpret_cast<const std::uint8_t *> (normalized.data ());
  std::size_t remaining = normalized.size ();
  while (remaining >= 4)
    {
      std::uint32_t k = bytes[0] | bytes[1] << 8 | bytes[2] << 16
                        | static_cast<std::uint32_t> (bytes[3]) << 24;
      k *= m;
      k ^= k >> 24;
      k *= m;
      h *= m;
      h ^= k;
      bytes += 4;
      remaining -= 4;
    }
  switch (remaining)
    {
    case 3:
      h ^= static_cast<std::uint32_t> (bytes[2]) << 16;
      [[fallthrough]];
    case 2:
      h ^= static_cast<std::uint32_t> (bytes[1]) << 8;
      [[fallthrough]];
    case 1:
      h ^= bytes[0];
      h *= m;
      break;
    default:
      break;
    }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

std::string
modrinth_mirror (std::string_view url)
{
  constexpr std::string_view host = "https://cdn.modrinth.com";
  if (url.starts_with (host))
    return "https://mod.mcimirror.top"
           + std::string (url.substr (host.size ()));
  return {};
}

result<void>
resolve_curseforge (std::vector<pack_file> &files,
                    const curseforge_settings &settings,
                    cancellation_token token)
{
  json file_ids = json::array ();
  json mod_ids = json::array ();
  std::set<std::int64_t> seen_mods;
  for (const auto &f : files)
    if (f.curseforge_file && f.urls.empty ())
      {
        file_ids.push_back (f.curseforge_file);
        if (seen_mods.insert (f.curseforge_project).second)
          mod_ids.push_back (f.curseforge_project);
      }
  if (file_ids.empty ())
    return ok ();
  BLB_TRY_ASSIGN (auto file_doc,
                  curseforge_post (settings, "/v1/mods/files",
                                   { { "fileIds", file_ids } }, token));
  std::map<std::int64_t, json> by_file;
  if (auto data = file_doc.find ("data");
      data != file_doc.end () && data->is_array ())
    for (const auto &f : *data)
      by_file[int_of (f, "id")] = f;
  std::map<std::int64_t, std::int64_t> class_of;
  if (auto mod_doc = curseforge_post (settings, "/v1/mods",
                                      { { "modIds", mod_ids } }, token))
    if (auto data = mod_doc->find ("data");
        data != mod_doc->end () && data->is_array ())
      for (const auto &m : *data)
        class_of[int_of (m, "id")] = int_of (m, "classId");
  for (auto &f : files)
    {
      if (!f.curseforge_file || !f.urls.empty ())
        continue;
      auto it = by_file.find (f.curseforge_file);
      if (it == by_file.end ())
        return make_error (
            errc::not_found,
            sformat ("curseforge file {} not found", f.curseforge_file));
      auto remote = curseforge_file (it->second);
      std::string folder = "mods";
      switch (class_of[f.curseforge_project])
        {
        case 12:
          folder = "resourcepacks";
          break;
        case 6552:
          folder = "shaderpacks";
          break;
        case 4546:
          folder = "config";
          break;
        default:
          break;
        }
      f.path = folder + "/" + remote.file_name;
      f.urls = curseforge_urls (remote.url);
      f.sha1 = remote.sha1;
      f.size = remote.size;
    }
  return ok ();
}

result<std::map<std::string, remote_file>>
lookup_modrinth (std::span<const std::string> sha1s, cancellation_token token)
{
  std::map<std::string, remote_file> out;
  std::map<std::string, std::vector<std::string>> by_project;
  for (std::size_t start = 0; start < sha1s.size (); start += 500)
    {
      json hashes = json::array ();
      for (std::size_t i = start; i < std::min (sha1s.size (), start + 500);
           ++i)
        hashes.push_back (sha1s[i]);
      BLB_TRY_ASSIGN (
          auto doc,
          post_json (std::string (modrinth_api) + "/version_files",
                     { { "hashes", hashes }, { "algorithm", "sha1" } }, {},
                     token));
      for (auto it = doc.begin (); it != doc.end (); ++it)
        {
          auto files = it->find ("files");
          if (files == it->end () || !files->is_array ())
            continue;
          for (const auto &f : *files)
            {
              auto hashes_node = f.find ("hashes");
              if (hashes_node == f.end ()
                  || to_lower (text_of (*hashes_node, "sha1")) != it.key ())
                continue;
              remote_file file;
              file.url = text_of (f, "url");
              file.file_name = text_of (f, "filename");
              file.sha1 = it.key ();
              file.sha512 = to_lower (text_of (*hashes_node, "sha512"));
              file.size = int_of (f, "size", -1);
              out[it.key ()] = file;
              by_project[text_of (*it, "project_id")].push_back (it.key ());
            }
        }
    }
  json ids = json::array ();
  for (const auto &[project, hashes] : by_project)
    if (!project.empty ())
      ids.push_back (project);
  for (std::size_t start = 0; start < ids.size (); start += 200)
    {
      json chunk = json::array ();
      for (std::size_t i = start; i < std::min (ids.size (), start + 200); ++i)
        chunk.push_back (ids[i]);
      auto body = net::http_get (sformat ("{}/projects?ids={}", modrinth_api,
                                          url_encode (chunk.dump ())),
                                 token);
      if (!body)
        continue;
      auto doc = json::parse (*body, nullptr, false);
      if (!doc.is_array ())
        continue;
      for (const auto &p : doc)
        for (const auto &hash : by_project[text_of (p, "id")])
          {
            out[hash].client_side = text_of (p, "client_side");
            out[hash].server_side = text_of (p, "server_side");
          }
    }
  return out;
}

result<std::map<std::uint32_t, remote_file>>
lookup_curseforge (std::span<const std::uint32_t> fingerprints,
                   const curseforge_settings &settings,
                   cancellation_token token)
{
  std::map<std::uint32_t, remote_file> out;
  if (fingerprints.empty ())
    return out;
  json list = json::array ();
  for (auto f : fingerprints)
    list.push_back (f);
  BLB_TRY_ASSIGN (auto doc,
                  curseforge_post (settings, "/v1/fingerprints/432",
                                   { { "fingerprints", list } }, token));
  auto data = doc.find ("data");
  if (data == doc.end ())
    return out;
  auto matches = data->find ("exactMatches");
  if (matches == data->end () || !matches->is_array ())
    return out;
  for (const auto &match : *matches)
    {
      auto file = match.find ("file");
      if (file == match.end ())
        continue;
      auto fingerprint
          = static_cast<std::uint32_t> (int_of (*file, "fileFingerprint"));
      out[fingerprint] = curseforge_file (*file);
    }
  return out;
}

}
