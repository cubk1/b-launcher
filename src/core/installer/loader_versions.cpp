#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/installer/download_plan.hpp>
#include <core/network/http_client.hpp>

#include "core/installer/loader_impl.hpp"

#include <algorithm>

namespace b_launcher_base::install
{

namespace
{

constexpr log::logger s_log ("loader");

std::string
json_text (const detail::json &node, const char *key)
{
  if (!node.is_object ())
    return {};
  auto it = node.find (key);
  if (it == node.end () || !it->is_string ())
    return {};
  return it->get<std::string> ();
}

bool
unstable_name (std::string_view version)
{
  auto lower = to_lower (version);
  return lower.find ("beta") != std::string::npos
         || lower.find ("alpha") != std::string::npos
         || lower.find ("pre") != std::string::npos
         || lower.find ("rc") != std::string::npos;
}

result<std::vector<loader_version>>
list_fabric_like (loader_type type, std::string_view mc,
                  const net::mirror_provider &mirror, cancellation_token token)
{
  auto root = detail::fabric_meta_root (type);
  auto path = type == loader_type::quilt ? "/v3/versions/loader/"
                                         : "/v2/versions/loader/";
  BLB_TRY_ASSIGN (
      auto body,
      net::http_get (mirror.resolve (root + path + url_encode (mc)), token));
  auto doc = detail::json::parse (body, nullptr, false);
  if (doc.is_discarded () || !doc.is_array ())
    return make_error (errc::parse, "invalid loader list");
  std::vector<loader_version> out;
  for (const auto &entry : doc)
    {
      auto loader = entry.find ("loader");
      if (loader == entry.end ())
        continue;
      loader_version v;
      v.type = type;
      v.minecraft = std::string (mc);
      v.version = json_text (*loader, "version");
      if (auto stable = loader->find ("stable");
          stable != loader->end () && stable->is_boolean ())
        v.stable = stable->get<bool> ();
      else
        v.stable = !unstable_name (v.version);
      if (!v.version.empty ())
        out.push_back (std::move (v));
    }
  std::stable_sort (out.begin (), out.end (),
                    [] (const auto &a, const auto &b) {
                      return compare_versions (a.version, b.version) > 0;
                    });
  return out;
}

result<std::vector<loader_version>>
list_forge (std::string_view mc, const net::mirror_provider &mirror,
            cancellation_token token)
{
  BLB_TRY_ASSIGN (
      auto body, net::http_get (mirror.resolve (
                                    "https://maven.minecraftforge.net/net/"
                                    "minecraftforge/forge/maven-metadata.xml"),
                                token));
  std::vector<loader_version> out;
  auto prefix = std::string (mc) + "-";
  for (auto &full : detail::parse_maven_versions (body))
    {
      if (!full.starts_with (prefix))
        continue;
      loader_version v;
      v.type = loader_type::forge;
      v.minecraft = std::string (mc);
      v.version = full.substr (prefix.size ());
      auto suffix = "-" + std::string (mc);
      if (v.version.ends_with (suffix))
        v.version.resize (v.version.size () - suffix.size ());
      v.extra = full;
      v.url = sformat ("https://maven.minecraftforge.net/net/"
                       "minecraftforge/forge/{0}/forge-{0}-installer.jar",
                       full);
      out.push_back (std::move (v));
    }
  std::stable_sort (out.begin (), out.end (),
                    [] (const auto &a, const auto &b) {
                      return compare_versions (a.version, b.version) > 0;
                    });
  return out;
}

result<std::vector<loader_version>>
list_neoforge (std::string_view mc, const net::mirror_provider &mirror,
               cancellation_token token)
{
  std::vector<loader_version> out;
  if (mc == "1.20.1")
    {
      BLB_TRY_ASSIGN (
          auto body, net::http_get (mirror.resolve (
                                        "https://maven.neoforged.net/releases/"
                                        "net/neoforged/forge/"
                                        "maven-metadata.xml"),
                                    token));
      for (auto &full : detail::parse_maven_versions (body))
        {
          if (!full.starts_with ("1.20.1-"))
            continue;
          loader_version v;
          v.type = loader_type::neoforge;
          v.minecraft = "1.20.1";
          v.version = full.substr (7);
          v.extra = full;
          v.stable = !unstable_name (v.version);
          v.url = sformat ("https://maven.neoforged.net/releases/net/"
                           "neoforged/forge/{0}/forge-{0}-installer.jar",
                           full);
          out.push_back (std::move (v));
        }
    }
  else
    {
      BLB_TRY_ASSIGN (
          auto body, net::http_get (mirror.resolve (
                                        "https://maven.neoforged.net/releases/"
                                        "net/neoforged/neoforge/"
                                        "maven-metadata.xml"),
                                    token));
      for (auto &full : detail::parse_maven_versions (body))
        {
          if (detail::neoforge_minecraft (full) != mc)
            continue;
          loader_version v;
          v.type = loader_type::neoforge;
          v.minecraft = std::string (mc);
          v.version = full;
          v.extra = full;
          v.stable = !unstable_name (full);
          v.url = sformat ("https://maven.neoforged.net/releases/net/"
                           "neoforged/neoforge/{0}/neoforge-{0}-"
                           "installer.jar",
                           full);
          out.push_back (std::move (v));
        }
    }
  std::sort (out.begin (), out.end (), [] (const auto &a, const auto &b) {
    return compare_versions (a.version, b.version) > 0;
  });
  return out;
}

result<std::vector<loader_version>>
list_cleanroom (std::string_view mc, cancellation_token token)
{
  if (mc != "1.12.2")
    return std::vector<loader_version> {};
  net::http_request request;
  request.url = "https://api.github.com/repos/CleanroomMC/Cleanroom/releases";
  request.headers.push_back ({ "Accept", "application/vnd.github+json" });
  BLB_TRY_ASSIGN (auto response, net::http_send (request, token));
  if (!response.ok ())
    return make_error (errc::http_status,
                       sformat ("github api HTTP {}", response.status));
  auto doc = detail::json::parse (response.body, nullptr, false);
  if (doc.is_discarded () || !doc.is_array ())
    return make_error (errc::parse, "invalid cleanroom release list");
  std::vector<loader_version> out;
  for (const auto &release : doc)
    {
      loader_version v;
      v.type = loader_type::cleanroom;
      v.minecraft = "1.12.2";
      v.version = json_text (release, "tag_name");
      v.release_time = json_text (release, "published_at");
      v.stable = !(release.value ("prerelease", false));
      if (auto assets = release.find ("assets");
          assets != release.end () && assets->is_array ())
        for (const auto &asset : *assets)
          if (json_text (asset, "name").ends_with ("installer.jar"))
            v.url = json_text (asset, "browser_download_url");
      if (!v.version.empty () && !v.url.empty ())
        out.push_back (std::move (v));
    }
  return out;
}

result<std::vector<loader_version>>
list_liteloader (std::string_view mc, const net::mirror_provider &mirror,
                 cancellation_token token)
{
  BLB_TRY_ASSIGN (
      auto body,
      net::http_get (mirror.resolve ("http://dl.liteloader.com/versions/"
                                     "versions.json"),
                     token));
  auto doc = detail::json::parse (body, nullptr, false);
  if (doc.is_discarded ())
    return make_error (errc::parse, "invalid liteloader list");
  std::vector<loader_version> out;
  auto versions = doc.find ("versions");
  if (versions == doc.end ())
    return out;
  auto entry = versions->find (std::string (mc));
  if (entry == versions->end ())
    return out;
  for (const char *channel : { "artefacts", "snapshots" })
    {
      auto group = entry->find (channel);
      if (group == entry->end ())
        continue;
      auto artefacts = group->find ("com.mumfrey:liteloader");
      if (artefacts == group->end () || !artefacts->is_object ())
        continue;
      for (auto it = artefacts->begin (); it != artefacts->end (); ++it)
        {
          if (it.key () == "latest" || !it->is_object ())
            continue;
          loader_version v;
          v.type = loader_type::liteloader;
          v.minecraft = std::string (mc);
          v.version = json_text (*it, "version");
          v.stable = json_text (*it, "stream") == "RELEASE";
          v.release_time = json_text (*it, "timestamp");
          v.extra = it->dump ();
          if (!v.version.empty ()
              && std::none_of (out.begin (), out.end (), [&] (const auto &o) {
                   return o.version == v.version;
                 }))
            out.push_back (std::move (v));
        }
    }
  std::sort (out.begin (), out.end (), [] (const auto &a, const auto &b) {
    return a.release_time > b.release_time;
  });
  return out;
}

result<std::vector<loader_version>>
list_optifine (std::string_view mc, cancellation_token token)
{
  BLB_TRY_ASSIGN (auto body,
                  net::http_get (sformat ("https://bmclapi2.bangbang93.com/"
                                          "optifine/{}",
                                          mc),
                                 token));
  auto doc = detail::json::parse (body, nullptr, false);
  if (doc.is_discarded () || !doc.is_array ())
    return make_error (errc::parse, "invalid optifine list");
  std::vector<loader_version> out;
  for (const auto &entry : doc)
    {
      loader_version v;
      v.type = loader_type::optifine;
      v.minecraft = json_text (entry, "mcversion");
      auto type = json_text (entry, "type");
      auto patch = json_text (entry, "patch");
      v.version = type + "_" + patch;
      v.stable = !patch.starts_with ("pre");
      v.extra = json_text (entry, "forge");
      v.url = sformat ("https://bmclapi2.bangbang93.com/optifine/{}/{}/{}",
                       v.minecraft, type, patch);
      if (!type.empty () && !patch.empty ())
        out.push_back (std::move (v));
    }
  std::reverse (out.begin (), out.end ());
  return out;
}

std::string
modrinth_slug (loader_type type)
{
  switch (type)
    {
    case loader_type::fabric_api:
      return "fabric-api";
    case loader_type::legacy_fabric_api:
      return "legacy-fabric-api";
    case loader_type::quilt_api:
      return "qsl";
    default:
      return {};
    }
}

std::string
modrinth_loader (loader_type type)
{
  switch (type)
    {
    case loader_type::legacy_fabric_api:
      return "legacy-fabric";
    case loader_type::quilt_api:
      return "quilt";
    default:
      return "fabric";
    }
}

result<std::vector<loader_version>>
list_mod (loader_type type, std::string_view mc, cancellation_token token)
{
  auto url = sformat (
      "https://api.modrinth.com/v2/project/{}/version?game_versions={}"
      "&loaders={}",
      modrinth_slug (type), url_encode (sformat ("[\"{}\"]", mc)),
      url_encode (sformat ("[\"{}\"]", modrinth_loader (type))));
  BLB_TRY_ASSIGN (auto body, net::http_get (url, token));
  auto doc = detail::json::parse (body, nullptr, false);
  if (doc.is_discarded () || !doc.is_array ())
    return make_error (errc::parse, "invalid modrinth response");
  std::vector<loader_version> out;
  for (const auto &entry : doc)
    {
      loader_version v;
      v.type = type;
      v.minecraft = std::string (mc);
      v.version = json_text (entry, "version_number");
      v.stable = json_text (entry, "version_type") == "release";
      v.release_time = json_text (entry, "date_published");
      if (auto files = entry.find ("files");
          files != entry.end () && files->is_array ())
        for (const auto &file : *files)
          if (v.url.empty () || file.value ("primary", false))
            {
              v.url = json_text (file, "url");
              v.extra = file.dump ();
            }
      if (!v.version.empty () && !v.url.empty ())
        out.push_back (std::move (v));
    }
  return out;
}

}

namespace detail
{

std::string
fabric_meta_root (loader_type type)
{
  switch (type)
    {
    case loader_type::legacy_fabric:
      return "https://meta.legacyfabric.net";
    case loader_type::quilt:
      return "https://meta.quiltmc.org";
    default:
      return "https://meta.fabricmc.net";
    }
}

std::string
neoforge_minecraft (std::string_view version)
{
  auto core = version.substr (0, version.find ('-'));
  auto parts = split (core, '.');
  if (parts.size () < 2)
    return {};
  auto major = parse_int (parts[0]).value_or (0);
  auto minor = parse_int (parts[1]).value_or (0);
  if (major >= 25)
    {
      std::string out = sformat ("{}.{}", major, minor);
      if (parts.size () >= 4)
        if (auto hotfix = parse_int (parts[2]); hotfix && *hotfix > 0)
          out += sformat (".{}", *hotfix);
      return out;
    }
  if (minor == 0)
    return sformat ("1.{}", major);
  return sformat ("1.{}.{}", major, minor);
}

result<std::string>
fetch_text (const loader_context &ctx, std::string_view url)
{
  return net::http_get (ctx.mirror ().resolve (url), ctx.token);
}

result<json>
fetch_json (const loader_context &ctx, std::string_view url)
{
  BLB_TRY_ASSIGN (auto body, fetch_text (ctx, url));
  auto doc = json::parse (body, nullptr, false);
  if (doc.is_discarded ())
    return make_error (errc::parse, sformat ("invalid json from {}", url));
  return doc;
}

std::vector<std::string>
parse_maven_versions (std::string_view xml)
{
  std::vector<std::string> out;
  std::size_t pos = 0;
  for (;;)
    {
      auto start = xml.find ("<version>", pos);
      if (start == std::string_view::npos)
        break;
      start += 9;
      auto end = xml.find ("</version>", start);
      if (end == std::string_view::npos)
        break;
      out.emplace_back (trim (xml.substr (start, end - start)));
      pos = end + 10;
    }
  return out;
}

std::string
join_url (std::string_view base, std::string_view path)
{
  std::string out (base);
  if (!out.empty () && out.back () != '/')
    out += '/';
  out += path;
  return out;
}

result<void>
download_libraries (loader_context &ctx,
                    const std::vector<meta::library> &libraries,
                    std::string_view task)
{
  meta::version holder;
  holder.id = ctx.target_id;
  holder.libraries = libraries;
  plan_options options;
  options.client = false;
  options.asset_index = false;
  options.logging = false;
  auto files = plan_version_files (holder, ctx.layout (), ctx.mirror (),
                                   meta::rule_context::current (), options);
  if (files.empty ())
    return ok ();
  BLB_TRY (ctx.installer->ensure_files (std::move (files), ctx.sink, ctx.token,
                                        task));
  return ok ();
}

result<void>
download_file (loader_context &ctx, std::string_view url,
               const std::filesystem::path &target, std::string_view task,
               std::string_view sha1, std::int64_t size)
{
  planned_file file;
  file.kind = file_kind::other;
  file.target = target;
  file.urls = ctx.mirror ().resolve (url);
  if (!sha1.empty ())
    file.sha1 = parse_sha1 (sha1);
  file.size = size;
  std::vector<planned_file> files { std::move (file) };
  BLB_TRY (ctx.installer->ensure_files (std::move (files), ctx.sink, ctx.token,
                                        task));
  return ok ();
}

}

result<void>
detail::loader_job::resolve_version (loader_context &ctx)
{
  if (!m_request.version.empty ())
    return ok ();
  BLB_TRY_ASSIGN (auto versions,
                  list_loader_versions (m_request.type, ctx.minecraft,
                                        ctx.mirror (), ctx.token));
  for (const auto &v : versions)
    if (v.stable)
      {
        m_request.version = v.version;
        m_resolved = v;
        return ok ();
      }
  if (!versions.empty ())
    {
      m_request.version = versions.front ().version;
      m_resolved = versions.front ();
      return ok ();
    }
  return make_error (errc::loader_not_found,
                     sformat ("no {} version for minecraft {}",
                              loader_name (m_request.type), ctx.minecraft));
}

std::string_view
loader_name (loader_type type) noexcept
{
  switch (type)
    {
    case loader_type::forge:
      return "forge";
    case loader_type::neoforge:
      return "neoforge";
    case loader_type::cleanroom:
      return "cleanroom";
    case loader_type::fabric:
      return "fabric";
    case loader_type::legacy_fabric:
      return "legacyfabric";
    case loader_type::quilt:
      return "quilt";
    case loader_type::liteloader:
      return "liteloader";
    case loader_type::optifine:
      return "optifine";
    case loader_type::fabric_api:
      return "fabric-api";
    case loader_type::legacy_fabric_api:
      return "legacy-fabric-api";
    case loader_type::quilt_api:
      return "quilt-api";
    }
  return "unknown";
}

std::optional<loader_type>
parse_loader_type (std::string_view text)
{
  auto lower = to_lower (text);
  for (auto type :
       { loader_type::forge, loader_type::neoforge, loader_type::cleanroom,
         loader_type::fabric, loader_type::legacy_fabric, loader_type::quilt,
         loader_type::liteloader, loader_type::optifine,
         loader_type::fabric_api, loader_type::legacy_fabric_api,
         loader_type::quilt_api })
    if (lower == loader_name (type))
      return type;
  if (lower == "legacy-fabric" || lower == "legacy_fabric")
    return loader_type::legacy_fabric;
  if (lower == "qsl" || lower == "qfapi")
    return loader_type::quilt_api;
  return std::nullopt;
}

bool
is_addon_mod (loader_type type) noexcept
{
  return type == loader_type::fabric_api
         || type == loader_type::legacy_fabric_api
         || type == loader_type::quilt_api;
}

bool
is_primary_loader (loader_type type) noexcept
{
  return type == loader_type::forge || type == loader_type::neoforge
         || type == loader_type::cleanroom || type == loader_type::fabric
         || type == loader_type::legacy_fabric || type == loader_type::quilt;
}

result<void>
check_loader_combination (std::span<const loader_request> loaders,
                          std::string_view minecraft)
{
  std::optional<loader_type> primary;
  bool optifine = false;
  bool liteloader = false;
  for (const auto &l : loaders)
    {
      if (is_primary_loader (l.type))
        {
          if (primary)
            return make_error (errc::incompatible,
                               sformat ("{} cannot be combined with {}",
                                        loader_name (l.type),
                                        loader_name (*primary)));
          primary = l.type;
        }
      optifine |= l.type == loader_type::optifine;
      liteloader |= l.type == loader_type::liteloader;
    }
  auto allowed_with = [&] (loader_type addon, loader_type base) {
    return primary && *primary == base
           && std::any_of (loaders.begin (), loaders.end (),
                           [&] (const auto &l) { return l.type == addon; });
  };
  for (const auto &l : loaders)
    {
      if (l.type == loader_type::fabric_api
          && !allowed_with (loader_type::fabric_api, loader_type::fabric)
          && !allowed_with (loader_type::fabric_api, loader_type::quilt))
        return make_error (errc::incompatible, "fabric-api requires fabric");
      if (l.type == loader_type::legacy_fabric_api
          && !allowed_with (loader_type::legacy_fabric_api,
                            loader_type::legacy_fabric))
        return make_error (errc::incompatible,
                           "legacy-fabric-api requires legacyfabric");
      if (l.type == loader_type::quilt_api
          && !allowed_with (loader_type::quilt_api, loader_type::quilt))
        return make_error (errc::incompatible, "quilt-api requires quilt");
    }
  if ((optifine || liteloader) && primary && *primary != loader_type::forge)
    return make_error (errc::incompatible,
                       sformat ("{} cannot be combined with {}",
                                optifine ? "optifine" : "liteloader",
                                loader_name (*primary)));
  if (liteloader && compare_versions (minecraft, "1.12.2") > 0)
    return make_error (errc::incompatible,
                       "liteloader is only available up to 1.12.2");
  return ok ();
}

std::vector<loader_info>
detect_loaders (const meta::version &resolved)
{
  std::vector<loader_info> out;
  auto add = [&] (loader_type type, std::string version) {
    for (const auto &e : out)
      if (e.type == type)
        return;
    out.push_back ({ type, std::move (version) });
  };
  bool legacy_intermediary = false;
  for (const auto &lib : resolved.libraries)
    if (lib.artifact.group == "net.legacyfabric")
      legacy_intermediary = true;
  for (const auto &lib : resolved.libraries)
    {
      const auto &a = lib.artifact;
      auto strip_mc = [&] (const std::string &v) {
        auto dash = v.find ('-');
        return dash == std::string::npos ? v : v.substr (dash + 1);
      };
      if (a.group == "net.minecraftforge"
          && (a.name == "forge" || a.name == "fmlloader"
              || a.name == "minecraftforge"))
        add (loader_type::forge, strip_mc (a.version));
      else if (a.group == "net.neoforged"
               && (a.name == "neoforge" || a.name == "forge"))
        add (loader_type::neoforge,
             a.name == "forge" ? strip_mc (a.version) : a.version);
      else if (a.group == "com.cleanroommc" && a.name == "cleanroom")
        add (loader_type::cleanroom, a.version);
      else if (a.group == "net.fabricmc" && a.name == "fabric-loader")
        add (legacy_intermediary ? loader_type::legacy_fabric
                                 : loader_type::fabric,
             a.version);
      else if (a.group == "org.quiltmc" && a.name == "quilt-loader")
        add (loader_type::quilt, a.version);
      else if (a.group == "com.mumfrey" && a.name == "liteloader")
        add (loader_type::liteloader, a.version);
      else if (a.group == "optifine" && a.name == "OptiFine")
        {
          auto underscore = a.version.find ('_');
          add (loader_type::optifine, underscore == std::string::npos
                                          ? a.version
                                          : a.version.substr (underscore + 1));
        }
    }
  return out;
}

result<std::vector<loader_version>>
list_loader_versions (loader_type type, std::string_view minecraft,
                      const net::mirror_provider &mirror,
                      cancellation_token token)
{
  switch (type)
    {
    case loader_type::fabric:
    case loader_type::legacy_fabric:
    case loader_type::quilt:
      return list_fabric_like (type, minecraft, mirror, token);
    case loader_type::forge:
      return list_forge (minecraft, mirror, token);
    case loader_type::neoforge:
      return list_neoforge (minecraft, mirror, token);
    case loader_type::cleanroom:
      return list_cleanroom (minecraft, token);
    case loader_type::liteloader:
      return list_liteloader (minecraft, mirror, token);
    case loader_type::optifine:
      return list_optifine (minecraft, token);
    case loader_type::fabric_api:
    case loader_type::legacy_fabric_api:
    case loader_type::quilt_api:
      return list_mod (type, minecraft, token);
    }
  return make_error (errc::unsupported, "unknown loader");
}

std::string
default_version_id (const install_request &request,
                    std::span<const loader_info> resolved)
{
  if (!request.id.empty ())
    return request.id;
  std::vector<loader_info> loaders;
  for (const auto &l : resolved)
    if (!is_addon_mod (l.type))
      loaders.push_back (l);
  if (loaders.empty ())
    return request.minecraft;
  if (loaders.size () == 1)
    {
      const auto &l = loaders.front ();
      switch (l.type)
        {
        case loader_type::fabric:
          return sformat ("fabric-loader-{}-{}", l.version, request.minecraft);
        case loader_type::legacy_fabric:
          return sformat ("legacyfabric-loader-{}-{}", l.version,
                          request.minecraft);
        case loader_type::quilt:
          return sformat ("quilt-loader-{}-{}", l.version, request.minecraft);
        case loader_type::neoforge:
          return sformat ("neoforge-{}", l.version);
        case loader_type::optifine:
          return sformat ("{}-OptiFine_{}", request.minecraft, l.version);
        case loader_type::liteloader:
          return sformat ("{}-LiteLoader{}", request.minecraft,
                          request.minecraft);
        default:
          return sformat ("{}-{}-{}", request.minecraft, loader_name (l.type),
                          l.version);
        }
    }
  std::string id = request.minecraft;
  for (const auto &l : loaders)
    id += sformat ("-{}-{}", loader_name (l.type), l.version);
  return id;
}

}
