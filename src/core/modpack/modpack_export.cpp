#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/thread_pool.hpp>
#include <core/base/zip.hpp>
#include <core/meta/version.hpp>
#include <core/modpack/modpack.hpp>

#include "core/modpack/modpack_impl.hpp"

#include <algorithm>
#include <set>

namespace b_launcher_base::modpack
{

namespace
{

using json = nlohmann::json;

constexpr log::logger s_log ("modpack");

void
walk (const std::filesystem::path &root, const std::string &relative,
      std::vector<std::string> &out)
{
  auto dir = relative.empty () ? root : root / path_from_utf8 (relative);
  for (const auto &[name, st] : fs::list_directory (dir))
    {
      auto child = relative.empty () ? name : relative + "/" + name;
      if (st.is_link)
        continue;
      if (st.is_directory)
        walk (root, child, out);
      else
        out.push_back (child);
    }
}

bool
lookup_candidate (std::string_view path)
{
  auto lower = to_lower (path);
  if (lower.ends_with (".disabled"))
    return false;
  if (lower.starts_with ("mods/") && lower.find ('/', 5) == std::string::npos)
    return lower.ends_with (".jar");
  for (std::string_view dir : { "resourcepacks/", "shaderpacks/" })
    if (lower.starts_with (dir)
        && lower.find ('/', dir.size ()) == std::string::npos)
      return lower.ends_with (".zip");
  return false;
}

std::string
env_value (const std::string &side)
{
  if (side == "required" || side == "optional" || side == "unsupported")
    return side;
  return "required";
}

}

std::vector<std::string>
collect_export_files (const std::filesystem::path &game_directory,
                      const export_options &options)
{
  std::set<std::string> picked;
  auto add_if_exists = [&] (const std::string &relative) {
    if (fs::stat (game_directory / path_from_utf8 (relative)).exists)
      picked.insert (relative);
  };
  auto add_tree = [&] (const std::string &relative) {
    std::vector<std::string> found;
    if (fs::stat (game_directory / path_from_utf8 (relative)).is_directory)
      walk (game_directory, relative, found);
    picked.insert (found.begin (), found.end ());
  };
  if (options.game_settings)
    for (auto name : { "options.txt", "optionsof.txt", "optionsshaders.txt",
                       "optionsviveprofiles.txt" })
      add_if_exists (name);
  if (options.personal_data)
    for (auto name : { "hotbar.nbt", "command_history.txt" })
      add_if_exists (name);
  if (options.mods || options.disabled_mods)
    for (const auto &[name, st] : fs::list_directory (game_directory / "mods"))
      {
        if (st.is_directory)
          continue;
        bool disabled = to_lower (name).ends_with (".disabled");
        if ((disabled && options.disabled_mods) || (!disabled && options.mods))
          picked.insert ("mods/" + name);
      }
  if (options.mod_settings)
    for (auto dir : { "config", "defaultconfigs" })
      add_tree (dir);
  if (options.saves)
    add_tree ("saves");
  if (options.servers)
    add_if_exists ("servers.dat");
  if (options.resource_packs)
    add_tree ("resourcepacks");
  if (options.shader_packs)
    add_tree ("shaderpacks");
  if (!options.include.empty ())
    {
      std::vector<std::string> all;
      walk (game_directory, "", all);
      for (const auto &path : all)
        for (const auto &pattern : options.include)
          if (detail::glob_match (pattern, path))
            picked.insert (path);
    }
  std::vector<std::string> out;
  for (const auto &path : picked)
    {
      bool excluded = false;
      for (const auto &pattern : options.exclude)
        if (detail::glob_match (pattern, path))
          excluded = true;
      if (!excluded && detail::safe_relative (path))
        out.push_back (path);
    }
  return out;
}

result<export_report>
export_modpack (const install::game_installer &installer,
                std::string_view instance_id,
                const std::filesystem::path &game_directory,
                const std::filesystem::path &output,
                const export_options &options, progress_sink *sink,
                cancellation_token token)
{
  export_report report;
  const auto &layout = installer.layout ();
  BLB_TRY_ASSIGN (auto resolved, meta::resolve_version (layout, instance_id));
  auto minecraft = resolved.minecraft ();
  json dependencies = { { "minecraft", minecraft } };
  for (const auto &l : install::detect_loaders (resolved))
    switch (l.type)
      {
      case install::loader_type::forge:
        dependencies["forge"] = l.version;
        break;
      case install::loader_type::neoforge:
        dependencies["neoforge"] = l.version;
        break;
      case install::loader_type::fabric:
        dependencies["fabric-loader"] = l.version;
        break;
      case install::loader_type::quilt:
        dependencies["quilt-loader"] = l.version;
        break;
      case install::loader_type::legacy_fabric:
        dependencies["fabric-loader"] = l.version;
        report.warnings.push_back (
            "legacy fabric is exported as fabric-loader; importers must use "
            "legacy intermediary");
        break;
      default:
        report.warnings.push_back (
            sformat ("{} {} cannot be described in a modrinth pack",
                     install::loader_name (l.type), l.version));
        break;
      }

  auto files = collect_export_files (game_directory, options);
  BLB_TRY (token.check ());
  std::vector<std::size_t> candidates;
  if (!options.bundle_files)
    for (std::size_t i = 0; i < files.size (); ++i)
      if (lookup_candidate (files[i]))
        candidates.push_back (i);

  struct digest_entry
  {
    std::string sha1;
    std::string sha512;
    std::uint64_t size = 0;
    std::uint32_t fingerprint = 0;
  };
  std::vector<digest_entry> digests (candidates.size ());
  progress_tracker hashing (sink, "export:hash");
  hashing.add_total (0, candidates.size ());
  parallel_for (
      thread_pool::shared (), candidates.size (), [&] (std::size_t k) {
        auto file = game_directory / path_from_utf8 (files[candidates[k]]);
        if (auto d = digest_file (file))
          {
            digests[k].sha1 = to_hex (d->sha1);
            digests[k].sha512
                = to_hex (std::span<const std::uint8_t> (d->sha512));
            digests[k].size = d->size;
          }
        if (!options.modrinth_only)
          if (auto data = fs::read_file (file))
            digests[k].fingerprint = detail::curseforge_fingerprint (*data);
        hashing.item_done ();
        hashing.report ();
      });
  hashing.finish ();

  std::vector<std::string> sha1s;
  for (const auto &d : digests)
    if (!d.sha1.empty ())
      sha1s.push_back (d.sha1);
  std::map<std::string, detail::remote_file> modrinth;
  if (!sha1s.empty ())
    {
      auto found = detail::lookup_modrinth (sha1s, token);
      if (found)
        modrinth = std::move (*found);
      else
        report.warnings.push_back ("modrinth lookup failed: "
                                   + found.err ().message);
    }
  std::map<std::uint32_t, detail::remote_file> curseforge;
  if (!options.modrinth_only)
    {
      std::vector<std::uint32_t> prints;
      for (const auto &d : digests)
        if (d.fingerprint && !modrinth.contains (d.sha1))
          prints.push_back (d.fingerprint);
      if (!prints.empty ())
        {
          auto found = detail::lookup_curseforge (
              prints, curseforge_settings {}, token);
          if (found)
            curseforge = std::move (*found);
          else
            report.warnings.push_back ("curseforge lookup failed: "
                                       + found.err ().message);
        }
    }

  json index_files = json::array ();
  std::vector<char> referenced (files.size (), 0);
  for (std::size_t k = 0; k < candidates.size (); ++k)
    {
      const auto &d = digests[k];
      const auto &path = files[candidates[k]];
      json entry;
      if (auto it = modrinth.find (d.sha1); it != modrinth.end ())
        entry = { { "path", path },
                  { "hashes", { { "sha1", d.sha1 }, { "sha512", d.sha512 } } },
                  { "env",
                    { { "client", env_value (it->second.client_side) },
                      { "server", env_value (it->second.server_side) } } },
                  { "downloads", json::array ({ it->second.url }) },
                  { "fileSize", d.size } };
      else if (auto cf = curseforge.find (d.fingerprint);
               d.fingerprint && cf != curseforge.end ()
               && (cf->second.sha1.empty () || cf->second.sha1 == d.sha1))
        entry = { { "path", path },
                  { "hashes", { { "sha1", d.sha1 }, { "sha512", d.sha512 } } },
                  { "downloads", json::array ({ cf->second.url }) },
                  { "fileSize", d.size } };
      else
        continue;
      index_files.push_back (std::move (entry));
      referenced[candidates[k]] = 1;
      ++report.referenced;
    }

  json index = { { "formatVersion", 1 },
                 { "game", "minecraft" },
                 { "versionId", options.version },
                 { "name", options.name.empty () ? std::string (instance_id)
                                                 : options.name },
                 { "files", std::move (index_files) },
                 { "dependencies", std::move (dependencies) } };
  if (!options.summary.empty ())
    index["summary"] = options.summary;

  auto temp = fs::temp_sibling (output);
  {
    BLB_TRY_ASSIGN (auto zip, zip_writer::create (temp));
    BLB_TRY (zip.add_buffer ("modrinth.index.json", index.dump (2)));
    progress_tracker writing (sink, "export:write");
    writing.add_total (0, files.size ());
    for (std::size_t i = 0; i < files.size (); ++i)
      {
        BLB_TRY (token.check ());
        if (referenced[i])
          continue;
        auto source = game_directory / path_from_utf8 (files[i]);
        BLB_TRY (zip.add_file ("overrides/" + files[i], source));
        report.bundled += 1;
        report.bytes += fs::stat (source).size;
        writing.item_done ();
        writing.report ();
      }
    for (const auto &[source, name] : options.extra_files)
      if (detail::safe_relative (name))
        BLB_TRY (zip.add_file (name, source));
    writing.finish ();
    BLB_TRY (zip.close ());
  }
  BLB_TRY (fs::rename_file (temp, output));
  s_log.info ("exported {} to {} ({} referenced, {} bundled)", instance_id,
              path_to_utf8 (output), report.referenced, report.bundled);
  return report;
}

std::string
serialize_export_options (const export_options &o)
{
  json extra = json::array ();
  for (const auto &[source, name] : o.extra_files)
    extra.push_back (
        { { "source", path_to_utf8 (source) }, { "path", name } });
  json doc = { { "name", o.name },
               { "version", o.version },
               { "author", o.author },
               { "summary", o.summary },
               { "gameSettings", o.game_settings },
               { "personalData", o.personal_data },
               { "mods", o.mods },
               { "disabledMods", o.disabled_mods },
               { "modSettings", o.mod_settings },
               { "saves", o.saves },
               { "servers", o.servers },
               { "resourcePacks", o.resource_packs },
               { "shaderPacks", o.shader_packs },
               { "bundleFiles", o.bundle_files },
               { "modrinthOnly", o.modrinth_only },
               { "include", o.include },
               { "exclude", o.exclude },
               { "extraFiles", std::move (extra) } };
  return doc.dump (2);
}

result<export_options>
parse_export_options (std::string_view text)
{
  auto doc = json::parse (text, nullptr, false, true);
  if (doc.is_discarded () || !doc.is_object ())
    return make_error (errc::parse, "invalid export options");
  export_options o;
  o.name = doc.value ("name", o.name);
  o.version = doc.value ("version", o.version);
  o.author = doc.value ("author", o.author);
  o.summary = doc.value ("summary", o.summary);
  o.game_settings = doc.value ("gameSettings", o.game_settings);
  o.personal_data = doc.value ("personalData", o.personal_data);
  o.mods = doc.value ("mods", o.mods);
  o.disabled_mods = doc.value ("disabledMods", o.disabled_mods);
  o.mod_settings = doc.value ("modSettings", o.mod_settings);
  o.saves = doc.value ("saves", o.saves);
  o.servers = doc.value ("servers", o.servers);
  o.resource_packs = doc.value ("resourcePacks", o.resource_packs);
  o.shader_packs = doc.value ("shaderPacks", o.shader_packs);
  o.bundle_files = doc.value ("bundleFiles", o.bundle_files);
  o.modrinth_only = doc.value ("modrinthOnly", o.modrinth_only);
  for (const char *key : { "include", "exclude" })
    if (auto it = doc.find (key); it != doc.end () && it->is_array ())
      for (const auto &v : *it)
        if (v.is_string ())
          (std::string_view (key) == "include" ? o.include : o.exclude)
              .push_back (v.get<std::string> ());
  if (auto it = doc.find ("extraFiles"); it != doc.end () && it->is_array ())
    for (const auto &e : *it)
      o.extra_files.emplace_back (
          path_from_utf8 (e.value ("source", std::string ())),
          e.value ("path", std::string ()));
  return o;
}

}
