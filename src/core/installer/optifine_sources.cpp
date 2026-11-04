#include "core/installer/loader_impl.hpp"

#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/network/http_client.hpp>

#include <algorithm>
#include <set>

namespace b_launcher_base::install::detail
{

namespace
{

constexpr std::string_view official_root = "https://optifine.net/";
constexpr std::string_view space = " \t\r\n";

// Only the opening tags are needed to read links and the Forge compatibility cell.
std::vector<std::string_view>
tags (std::string_view html, std::string_view name)
{
  std::vector<std::string_view> out;
  std::string prefix = "<" + std::string (name);
  for (std::size_t at = 0; (at = html.find (prefix, at)) != html.npos;)
    {
      auto end = at + prefix.size ();
      if (end == html.size ())
        break;
      if (space.find (html[end]) == space.npos && html[end] != '>')
        {
          at = end;
          continue;
        }
      char quote = 0;
      for (; end < html.size (); ++end)
        {
          char c = html[end];
          if (quote)
            {
              if (c == quote)
                quote = 0;
            }
          else if (c == '\'' || c == '"')
            quote = c;
          else if (c == '>')
            break;
        }
      if (end == html.size ())
        break;
      out.push_back (html.substr (at, end - at + 1));
      at = end + 1;
    }
  return out;
}

std::string_view
attribute (std::string_view tag, std::string_view name)
{
  auto at = tag.find_first_of (space);
  while (at != tag.npos && at < tag.size ())
    {
      at = tag.find_first_not_of (space, at);
      if (at == tag.npos || tag[at] == '>' || tag[at] == '/')
        break;
      auto end = tag.find_first_of ("= \t\r\n>/", at);
      if (end == tag.npos)
        break;
      auto key = tag.substr (at, end - at);
      at = tag.find_first_not_of (space, end);
      if (at == tag.npos)
        break;
      if (tag[at] != '=')
        continue;
      at = tag.find_first_not_of (space, at + 1);
      if (at == tag.npos)
        break;
      char quote = tag[at];
      bool quoted = quote == '\'' || quote == '"';
      if (quoted)
        ++at;
      end = quoted ? tag.find (quote, at)
                   : tag.find_first_of (" \t\r\n>", at);
      if (end == tag.npos)
        break;
      if (iequals (key, name))
        return tag.substr (at, end - at);
      at = end + (quoted ? 1 : 0);
    }
  return {};
}

std::string
site_path (std::string_view href)
{
  href = trim (href);
  for (auto root : { official_root, std::string_view ("http://optifine.net/"),
                     std::string_view ("//optifine.net/") })
    if (href.starts_with (root))
      return replace_all (href.substr (root.size ()), "&amp;", "&");
  if (href.starts_with ("//") || href.find (':') != href.npos)
    return {};
  if (href.starts_with ('/'))
    href.remove_prefix (1);
  return replace_all (href, "&amp;", "&");
}

std::string_view
query_value (std::string_view path, std::string_view key)
{
  auto query = path.find ('?');
  if (query == path.npos)
    return {};
  path.remove_prefix (query + 1);
  path = path.substr (0, path.find ('#'));
  for (auto part : split (path, '&'))
    {
      auto equals = part.find ('=');
      if (equals != part.npos && part.substr (0, equals) == key)
        return part.substr (equals + 1);
    }
  return {};
}

std::string
forge_hint (std::string_view row)
{
  for (auto tag : tags (row, "td"))
    {
      auto classes = split (attribute (tag, "class"), ' ');
      if (std::find (classes.begin (), classes.end (), "colForge")
          == classes.end ())
        continue;
      auto start = static_cast<std::size_t> (tag.data () - row.data ())
                   + tag.size ();
      auto end = row.find ("</td", start);
      if (end == row.npos)
        break;
      return std::string (trim (row.substr (start, end - start)));
    }
  return {};
}

result<std::vector<loader_version>>
official_versions (std::string_view html, std::string_view minecraft)
{
  std::vector<loader_version> out;
  std::set<std::string> seen;
  bool recognized = false;
  std::string prefix = "OptiFine_" + std::string (minecraft) + "_";
  for (auto tag : tags (html, "a"))
    {
      auto path = site_path (attribute (tag, "href"));
      if (!path.starts_with ("adloadx?"))
        continue;
      auto file = query_value (path, "f");
      if (!fs::safe_filename (file) || !file.ends_with (".jar"))
        continue;
      bool preview = file.starts_with ("preview_");
      auto name = preview ? file.substr (8) : file;
      if (!name.starts_with ("OptiFine_"))
        continue;
      recognized = true;
      if (!name.starts_with (prefix) || name.size () <= prefix.size () + 4)
        continue;
      loader_version v;
      v.type = loader_type::optifine;
      v.minecraft = minecraft;
      v.version = name.substr (prefix.size (), name.size () - prefix.size () - 4);
      if (!seen.insert (v.version).second)
        continue;
      v.stable = !preview;
      v.url = std::string (official_root) + "adloadx?f=" + url_encode (file);
      auto at = static_cast<std::size_t> (tag.data () - html.data ());
      auto start = html.rfind ("<tr", at);
      auto end = html.find ("</tr", at);
      if (start != html.npos && end != html.npos)
        v.extra = forge_hint (html.substr (start, end - start));
      out.push_back (std::move (v));
    }
  if (!recognized)
    return make_error (errc::parse, "invalid OptiFine downloads page");
  return out;
}

result<std::vector<loader_version>>
mirror_versions (std::string_view body, std::string_view root,
                  std::string_view minecraft)
{
  auto doc = json::parse (body, nullptr, false);
  if (!doc.is_array ())
    return make_error (errc::parse, "invalid OptiFine mirror list");
  std::vector<loader_version> out;
  for (const auto &entry : doc)
    {
      auto type = json_text (entry, "type");
      auto patch = json_text (entry, "patch");
      if (json_text (entry, "mcversion") != minecraft
          || !fs::safe_component (type) || !fs::safe_component (patch))
        continue;
      loader_version v;
      v.type = loader_type::optifine;
      v.minecraft = minecraft;
      v.version = type + "_" + patch;
      v.stable = !patch.starts_with ("pre");
      v.extra = json_text (entry, "forge");
      v.url = sformat ("{}{}/{}/{}", root, url_encode (minecraft),
                        url_encode (type), url_encode (patch));
      out.push_back (std::move (v));
    }
  std::reverse (out.begin (), out.end ());
  return out;
}

result<std::string>
fetch_page (const std::string &url, cancellation_token token)
{
  net::http_request request;
  request.url = url;
  request.max_response_size = 2u << 20;
  request.timeout = std::chrono::seconds (20);
  BLB_TRY_ASSIGN (auto response, net::http_send (request, token));
  if (!response.ok ())
    return make_error (errc::http_status,
                       sformat ("HTTP {} ({})", response.status, url));
  return std::move (response.body);
}

result<std::string>
official_download (std::string_view file, cancellation_token token)
{
  BLB_TRY_ASSIGN (auto html,
                  fetch_page (std::string (official_root) + "adloadx?f="
                                  + url_encode (file), token));
  for (auto tag : tags (html, "a"))
    {
      auto path = site_path (attribute (tag, "href"));
      if (path.starts_with ("downloadx?") && query_value (path, "f") == file
          && !query_value (path, "x").empty ())
        return std::string (official_root) + path;
    }
  return make_error (errc::parse, "OptiFine download link not found");
}

}

result<std::vector<loader_version>>
list_optifine (std::string_view minecraft, const net::mirror_provider &mirror,
               cancellation_token token)
{
  error last = make_error (errc::network, "no OptiFine source configured");
  for (const auto &root : mirror.resolve (official_root))
    {
      BLB_TRY (token.check ());
      auto page = fetch_page (root + (root == official_root ? "downloads"
                                                          : url_encode (minecraft)), token);
      auto parsed = !page ? result<std::vector<loader_version>> (page.err ())
                    : root == official_root ? official_versions (*page, minecraft)
                                            : mirror_versions (*page, root, minecraft);
      if (parsed)
        return parsed;
      last = std::move (parsed).err ();
      if (last.code == errc::cancelled)
        return last;
    }
  return last;
}

result<void>
download_optifine (loader_context &ctx, std::string_view version,
                   const std::filesystem::path &target)
{
  auto underscore = version.rfind ('_');
  if (!fs::safe_component (ctx.minecraft) || !fs::safe_component (version)
      || underscore == version.npos || underscore == 0
      || underscore + 1 == version.size ())
    return make_error (errc::invalid_argument, "invalid OptiFine version");
  BLB_TRY (ctx.token.check ());
  if (fs::exists (target))
    {
      if (zip_reader::open (target))
        return ok ();
      BLB_TRY (fs::remove_file (target));
    }
  auto type = version.substr (0, underscore);
  auto patch = version.substr (underscore + 1);
  auto file = sformat ("{}OptiFine_{}_{}.jar", patch.starts_with ("pre")
                                                ? "preview_" : "",
                       ctx.minecraft, version);
  error last = make_error (errc::network, "no OptiFine source configured");
  for (const auto &root : ctx.mirror ().resolve (official_root))
    {
      BLB_TRY (ctx.token.check ());
      auto url = root == official_root
                     ? official_download (file, ctx.token)
                     : result<std::string> (sformat ("{}{}/{}/{}", root,
                           url_encode (ctx.minecraft), url_encode (type), url_encode (patch)));
      if (url)
        {
          std::vector<std::string> urls { *url };
          auto downloaded = download_file (ctx, urls, target, "optifine");
          if (downloaded)
            {
              if (zip_reader::open (target))
                return ok ();
              BLB_TRY (fs::remove_file (target));
              last = make_error (errc::zip, "invalid OptiFine installer archive");
            }
          else
            last = std::move (downloaded).err ();
        }
      else
        last = std::move (url).err ();
      if (last.code == errc::cancelled)
        return last;
    }
  return last;
}

}
