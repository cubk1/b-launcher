#include <core/base/format.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/modpack.hpp>

#include "core/modpack/modpack_impl.hpp"

#include <algorithm>
#include <unordered_set>

namespace b_launcher_base::modpack
{

namespace
{

using json = nlohmann::json;

std::string
text_of (const json &node, const char *key)
{
  if (!node.is_object ())
    return {};
  auto it = node.find (key);
  if (it == node.end ())
    return {};
  if (it->is_string ())
    return it->get<std::string> ();
  if (it->is_number ())
    return it->dump ();
  return {};
}

std::int64_t
int_of (const json &node, const char *key)
{
  if (!node.is_object ())
    return 0;
  auto it = node.find (key);
  if (it == node.end ())
    return 0;
  if (it->is_number ())
    return it->get<std::int64_t> ();
  if (it->is_string ())
    return parse_int (it->get<std::string> ()).value_or (0);
  return 0;
}

std::string
strip_minecraft_prefix (std::string version, std::string_view minecraft)
{
  auto prefix = std::string (minecraft) + "-";
  if (!minecraft.empty () && version.starts_with (prefix))
    version.erase (0, prefix.size ());
  return version;
}

std::optional<install::loader_request>
loader_from_name (std::string_view name, std::string version,
                  std::string_view minecraft)
{
  auto lower = to_lower (name);
  install::loader_request request;
  if (lower == "forge" || lower == "net.minecraftforge")
    request.type = install::loader_type::forge;
  else if (lower == "neoforge" || lower == "net.neoforged")
    request.type = install::loader_type::neoforge;
  else if (lower == "fabric" || lower == "fabric-loader"
           || lower == "net.fabricmc.fabric-loader")
    request.type = install::loader_type::fabric;
  else if (lower == "quilt" || lower == "quilt-loader"
           || lower == "org.quiltmc.quilt-loader")
    request.type = install::loader_type::quilt;
  else if (lower == "optifine")
    request.type = install::loader_type::optifine;
  else if (lower == "liteloader" || lower == "com.mumfrey.liteloader")
    request.type = install::loader_type::liteloader;
  else if (lower == "cleanroom")
    request.type = install::loader_type::cleanroom;
  else
    return std::nullopt;
  request.version = strip_minecraft_prefix (std::move (version), minecraft);
  return request;
}

json
load_json (zip_reader &zip, const std::string &name)
{
  auto text = zip.read (name);
  if (!text)
    return json ();
  std::string_view view = *text;
  if (view.starts_with ("\xEF\xBB\xBF"))
    view.remove_prefix (3);
  return json::parse (view, nullptr, false, true);
}

result<pack_manifest>
read_modrinth (zip_reader &zip)
{
  auto doc = load_json (zip, "modrinth.index.json");
  if (!doc.is_object ())
    return make_error (errc::parse, "invalid modrinth.index.json");
  pack_manifest m;
  m.format = pack_format::modrinth;
  m.name = text_of (doc, "name");
  m.version = text_of (doc, "versionId");
  m.summary = text_of (doc, "summary");
  if (auto deps = doc.find ("dependencies"); deps != doc.end ())
    {
      m.minecraft = text_of (*deps, "minecraft");
      for (auto it = deps->begin (); it != deps->end (); ++it)
        if (it.key () != "minecraft" && it->is_string ())
          if (auto loader = loader_from_name (
                  it.key (), it->get<std::string> (), m.minecraft))
            m.loaders.push_back (*loader);
    }
  if (auto files = doc.find ("files");
      files != doc.end () && files->is_array ())
    for (const auto &f : *files)
      {
        pack_file file;
        file.path = text_of (f, "path");
        if (auto hashes = f.find ("hashes"); hashes != f.end ())
          {
            file.sha1 = to_lower (text_of (*hashes, "sha1"));
            file.sha512 = to_lower (text_of (*hashes, "sha512"));
          }
        file.size = f.contains ("fileSize") ? int_of (f, "fileSize") : -1;
        if (auto env = f.find ("env"); env != f.end ())
          {
            auto client = text_of (*env, "client");
            if (client == "unsupported")
              continue;
            file.optional = client == "optional";
          }
        if (auto downloads = f.find ("downloads");
            downloads != f.end () && downloads->is_array ())
          for (const auto &url : *downloads)
            if (url.is_string ())
              file.urls.push_back (url.get<std::string> ());
        m.files.push_back (std::move (file));
      }
  m.overrides = { "overrides/", "client-overrides/" };
  return m;
}

void
read_curse_files (const json &doc, pack_manifest &m)
{
  if (auto files = doc.find ("files");
      files != doc.end () && files->is_array ())
    for (const auto &f : *files)
      {
        if (f.contains ("type") && text_of (f, "type") != "curse")
          continue;
        pack_file file;
        file.curseforge_project = int_of (f, "projectID");
        file.curseforge_file = int_of (f, "fileID");
        file.optional = f.contains ("required") && f["required"].is_boolean ()
                        && !f["required"].get<bool> ();
        if (file.curseforge_file)
          m.files.push_back (std::move (file));
      }
}

result<pack_manifest>
read_curseforge (zip_reader &zip)
{
  auto doc = load_json (zip, "manifest.json");
  if (!doc.is_object ())
    return make_error (errc::parse, "invalid manifest.json");
  pack_manifest m;
  m.format = pack_format::curseforge;
  m.name = text_of (doc, "name");
  m.version = text_of (doc, "version");
  m.author = text_of (doc, "author");
  if (auto mc = doc.find ("minecraft"); mc != doc.end ())
    {
      m.minecraft = text_of (*mc, "version");
      if (auto loaders = mc->find ("modLoaders");
          loaders != mc->end () && loaders->is_array ())
        for (const auto &l : *loaders)
          {
            auto id = text_of (l, "id");
            auto dash = id.find ('-');
            if (dash == std::string::npos)
              continue;
            if (auto loader = loader_from_name (
                    id.substr (0, dash), id.substr (dash + 1), m.minecraft))
              m.loaders.push_back (*loader);
          }
    }
  read_curse_files (doc, m);
  auto overrides = text_of (doc, "overrides");
  m.overrides
      = { (overrides.empty () ? std::string ("overrides") : overrides) + "/" };
  return m;
}

result<pack_manifest>
read_mcbbs (zip_reader &zip)
{
  auto doc = load_json (zip, "mcbbs.packmeta");
  if (!doc.is_object ())
    return make_error (errc::parse, "invalid mcbbs.packmeta");
  pack_manifest m;
  m.format = pack_format::mcbbs;
  m.name = text_of (doc, "name");
  m.version = text_of (doc, "version");
  m.author = text_of (doc, "author");
  m.summary = text_of (doc, "description");
  if (auto addons = doc.find ("addons");
      addons != doc.end () && addons->is_array ())
    {
      for (const auto &a : *addons)
        if (text_of (a, "id") == "game")
          m.minecraft = text_of (a, "version");
      for (const auto &a : *addons)
        if (text_of (a, "id") != "game")
          if (auto loader = loader_from_name (
                  text_of (a, "id"), text_of (a, "version"), m.minecraft))
            m.loaders.push_back (*loader);
    }
  read_curse_files (doc, m);
  if (m.files.empty () && zip.contains ("manifest.json"))
    read_curse_files (load_json (zip, "manifest.json"), m);
  if (auto info = doc.find ("launchInfo"); info != doc.end ())
    {
      m.min_memory_mb
          = static_cast<std::uint32_t> (int_of (*info, "minMemory"));
      for (const char *key : { "javaArgument", "launchArgument" })
        if (auto args = info->find (key);
            args != info->end () && args->is_array ())
          for (const auto &arg : *args)
            if (arg.is_string ())
              (std::string_view (key) == "javaArgument" ? m.jvm_args
                                                        : m.game_args)
                  .push_back (arg.get<std::string> ());
    }
  m.overrides = { "overrides/" };
  return m;
}

result<pack_manifest>
read_multimc (zip_reader &zip, const std::string &prefix,
              const std::unordered_set<std::string> &names)
{
  auto doc = load_json (zip, prefix + "mmc-pack.json");
  if (!doc.is_object ())
    return make_error (errc::parse, "invalid mmc-pack.json");
  pack_manifest m;
  m.format = pack_format::multimc;
  if (auto components = doc.find ("components");
      components != doc.end () && components->is_array ())
    {
      for (const auto &c : *components)
        if (text_of (c, "uid") == "net.minecraft")
          m.minecraft = text_of (c, "version");
      for (const auto &c : *components)
        if (auto loader = loader_from_name (
                text_of (c, "uid"), text_of (c, "version"), m.minecraft))
          m.loaders.push_back (*loader);
    }
  if (auto cfg = zip.read (prefix + "instance.cfg"))
    for (auto line : split (*cfg, '\n'))
      {
        auto eq = line.find ('=');
        if (eq == std::string_view::npos)
          continue;
        auto key = trim (line.substr (0, eq));
        auto value = std::string (trim (line.substr (eq + 1)));
        if (key == "name")
          m.name = value;
        else if (key == "JvmArgs")
          {
            for (auto arg : split (value, ' '))
              if (!trim (arg).empty ())
                m.jvm_args.emplace_back (trim (arg));
          }
        else if (key == "MaxMemAlloc")
          m.max_memory_mb
              = static_cast<std::uint32_t> (parse_int (value).value_or (0));
        else if (key == "MinMemAlloc")
          m.min_memory_mb
              = static_cast<std::uint32_t> (parse_int (value).value_or (0));
      }
  for (const char *dir : { ".minecraft/", "minecraft/" })
    {
      auto candidate = prefix + dir;
      bool found = std::any_of (
          names.begin (), names.end (),
          [&] (const std::string &n) { return n.starts_with (candidate); });
      if (found)
        m.overrides.push_back (candidate);
    }
  return m;
}

}

std::string_view
format_name (pack_format format) noexcept
{
  switch (format)
    {
    case pack_format::modrinth:
      return "modrinth";
    case pack_format::curseforge:
      return "curseforge";
    case pack_format::mcbbs:
      return "mcbbs";
    case pack_format::multimc:
      return "multimc";
    }
  return "unknown";
}

namespace detail
{

bool
safe_relative (std::string_view path)
{
  if (path.empty () || path.front () == '/' || path.front () == '\\')
    return false;
  if (path.size () > 1 && path[1] == ':')
    return false;
  for (auto part : split (replace_all (path, "\\", "/"), '/'))
    if (part == "..")
      return false;
  return true;
}

}

result<pack_manifest>
read_modpack (const std::filesystem::path &archive)
{
  BLB_TRY_ASSIGN (auto zip, zip_reader::open (archive));
  std::unordered_set<std::string> names;
  BLB_TRY (zip.for_each ([&] (const zip_entry &entry, zip_reader &) {
    names.insert (replace_all (entry.name, "\\", "/"));
    return true;
  }));
  result<pack_manifest> parsed
      = make_error (errc::unsupported, "unrecognized modpack format");
  if (names.contains ("modrinth.index.json"))
    parsed = read_modrinth (zip);
  else if (names.contains ("mcbbs.packmeta"))
    parsed = read_mcbbs (zip);
  else if (names.contains ("manifest.json"))
    parsed = read_curseforge (zip);
  else
    for (const auto &name : names)
      if (name == "mmc-pack.json" || name.ends_with ("/mmc-pack.json"))
        {
          auto prefix = name.substr (0, name.size () - 13);
          if (std::count (prefix.begin (), prefix.end (), '/') <= 1)
            {
              parsed = read_multimc (zip, prefix, names);
              break;
            }
        }
  if (!parsed)
    return parsed;
  if (parsed->minecraft.empty ())
    return make_error (errc::parse, "modpack does not specify minecraft");
  std::erase_if (parsed->files, [] (const pack_file &f) {
    return f.curseforge_file == 0 && !detail::safe_relative (f.path);
  });
  if (parsed->name.empty ())
    parsed->name = path_to_utf8 (archive.stem ());
  return parsed;
}

}
