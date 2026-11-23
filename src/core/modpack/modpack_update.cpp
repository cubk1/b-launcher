#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/log.hpp>
#include <core/base/progress.hpp>
#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>
#include <core/instance/instance_manager.hpp>
#include <core/modpack/modpack.hpp>
#include <core/modpack/remote_zip.hpp>

#include "core/modpack/modpack_impl.hpp"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace b_launcher_base::modpack
{

namespace
{

using json = nlohmann::json;

constexpr log::logger s_log ("modpack");
constexpr std::uint64_t info_limit = 16u << 20;
constexpr std::size_t batch_items = 256;
constexpr std::uint64_t batch_bytes = 48ull << 20;

std::string
text_of (const json &node, const char *key)
{
  auto it = node.find (key);
  return it != node.end () && it->is_string () ? it->get<std::string> ()
                                               : std::string ();
}

result<std::uint32_t>
crc32_file (const std::filesystem::path &path, cancellation_token token)
{
  BLB_TRY_ASSIGN (auto file,
                  fs::native_file::open (path, fs::native_file::mode::read));
  std::vector<char> buffer (1u << 20);
  uLong crc = crc32 (0L, Z_NULL, 0);
  for (;;)
    {
      BLB_TRY (token.check ());
      BLB_TRY_ASSIGN (auto got, file.read (buffer.data (), buffer.size ()));
      if (got == 0)
        break;
      crc = crc32 (crc, reinterpret_cast<const Bytef *> (buffer.data ()),
                   static_cast<uInt> (got));
    }
  return static_cast<std::uint32_t> (crc);
}

struct local_file
{
  std::filesystem::path path;
  fs::file_stat stat;
  std::optional<std::string> sha1;
  std::optional<std::uint32_t> crc;
};

local_file
local_at (const std::filesystem::path &game_dir, const std::string &relative)
{
  local_file local;
  local.path = game_dir / path_from_utf8 (relative);
  local.stat = fs::stat (local.path);
  return local;
}

bool
present (const local_file &local)
{
  return local.stat.exists && !local.stat.is_directory && !local.stat.is_link;
}

result<bool>
matches (local_file &local, const owned_file &owned, cancellation_token token)
{
  if (owned.size >= 0
      && local.stat.size != static_cast<std::uint64_t> (owned.size))
    return false;
  if (!owned.sha1.empty ())
    {
      if (!local.sha1)
        {
          BLB_TRY_ASSIGN (auto digest, sha1_file (local.path, token));
          local.sha1 = to_lower (to_hex (digest));
        }
      return *local.sha1 == owned.sha1;
    }
  if (owned.crc)
    {
      if (!local.crc)
        {
          BLB_TRY_ASSIGN (auto crc, crc32_file (local.path, token));
          local.crc = crc;
        }
      return *local.crc == *owned.crc;
    }
  return false;
}

bool
same_owned (const owned_file &a, const owned_file &b)
{
  if (a.size >= 0 && b.size >= 0 && a.size != b.size)
    return false;
  if (!a.sha1.empty () && !b.sha1.empty ())
    return a.sha1 == b.sha1;
  if (a.crc && b.crc)
    return *a.crc == *b.crc;
  return false;
}

std::set<std::pair<int, std::string>>
primary_loaders (const auto &list)
{
  std::set<std::pair<int, std::string>> out;
  for (const auto &loader : list)
    if (install::is_primary_loader (loader.type))
      out.emplace (static_cast<int> (loader.type), loader.version);
  return out;
}

std::int64_t
int_of (const json &node, const char *key)
{
  auto it = node.find (key);
  return it != node.end () && it->is_number_integer ()
             ? it->get<std::int64_t> ()
             : 0;
}

struct target
{
  std::optional<std::size_t> file;
  std::string entry;
  std::uint64_t bytes = 0;
};

struct remote_pack
{
  pack_manifest manifest;
  std::map<std::string, owned_file> owned;
  std::map<std::string, target> targets;
  std::uint64_t archive_size = 0;
  std::uint64_t fetched = 0;
};

result<remote_pack>
load_remote_pack (install::game_installer &installer,
                  std::vector<std::string> urls, std::uint64_t archive_size,
                  const curseforge_settings &curseforge,
                  cancellation_token token)
{
  remote_pack pack;
  BLB_TRY_ASSIGN (auto zip,
                  remote_zip::open (std::move (urls), archive_size, token));
  pack.archive_size = zip.size ();
  std::string index;
  for (const char *name : { "modrinth.index.json", "manifest.json" })
    if (zip.find (name) != nullptr)
      {
        index = name;
        break;
      }
  if (index.empty ())
    return make_error (errc::unsupported,
                       "not a Modrinth or CurseForge modpack");
  BLB_TRY_ASSIGN (auto text, zip.read (index, token));
  BLB_TRY_ASSIGN (pack.manifest, parse_pack_index (index, text));
  auto &files = pack.manifest.files;
  BLB_TRY (detail::resolve_curseforge (files, curseforge, installer.mirror (),
                                       token));
  for (std::size_t i = 0; i < files.size (); ++i)
    {
      auto &f = files[i];
      f.sha1 = to_lower (f.sha1);
      if (f.urls.empty () || !fs::safe_relative (f.path))
        continue;
      pack.targets[f.path]
          = { i, {}, f.size > 0 ? static_cast<std::uint64_t> (f.size) : 0 };
      pack.owned[f.path] = { f.sha1, std::nullopt, f.size };
    }
  for (const auto &prefix : pack.manifest.overrides)
    for (const auto &entry : zip.entries ())
      {
        if (entry.name.ends_with ('/') || !entry.name.starts_with (prefix))
          continue;
        auto relative = entry.name.substr (prefix.size ());
        if (relative.empty () || !fs::safe_relative (relative))
          continue;
        pack.targets[relative]
            = { std::nullopt, entry.name, entry.compressed };
        pack.owned[relative] = { std::string (), entry.crc,
                                 static_cast<std::int64_t> (entry.size) };
      }
  pack.fetched = zip.downloaded ();
  return pack;
}

result<std::pair<std::size_t, std::size_t>>
score_local (const remote_pack &pack, const std::filesystem::path &game_dir,
             cancellation_token token)
{
  bool indexed = std::any_of (
      pack.targets.begin (), pack.targets.end (),
      [] (const auto &entry) { return entry.second.file.has_value (); });
  std::size_t matched = 0;
  std::size_t total = 0;
  for (const auto &[relative, t] : pack.targets)
    {
      if (indexed != t.file.has_value ())
        continue;
      BLB_TRY (token.check ());
      ++total;
      const auto &want = pack.owned.at (relative);
      for (const auto &name : { relative, relative + ".disabled" })
        {
          auto local = local_at (game_dir, name);
          if (!present (local))
            continue;
          BLB_TRY_ASSIGN (bool same, matches (local, want, token));
          if (same)
            {
              ++matched;
              break;
            }
        }
    }
  return std::pair { matched, total };
}

bool
fill_modrinth (const json &version, const net::mirror_provider &mirror,
               pack_candidate &out)
{
  auto files = version.find ("files");
  if (files == version.end () || !files->is_array () || files->empty ())
    return false;
  const json *chosen = &files->front ();
  for (const auto &file : *files)
    if (file.is_object () && file.value ("primary", false))
      chosen = &file;
  auto url = text_of (*chosen, "url");
  if (url.empty ())
    return false;
  out.origin.source = "modrinth";
  out.origin.project = text_of (version, "project_id");
  out.origin.version = text_of (version, "id");
  out.version_name = text_of (version, "version_number");
  out.urls = mirror.resolve (url);
  auto size = int_of (*chosen, "size");
  out.archive_size = size > 0 ? static_cast<std::uint64_t> (size) : 0;
  return !out.origin.project.empty () && !out.origin.version.empty ();
}

bool
sure (const pack_candidate &candidate)
{
  return candidate.exact && candidate.total > 0
         && candidate.matched * 10 >= candidate.total * 9;
}
std::string
family_of (std::string_view path)
{
  auto slash = path.rfind ('/');
  std::string directory (slash == std::string_view::npos
                             ? std::string_view ()
                             : path.substr (0, slash + 1));
  std::string name = to_lower (
      slash == std::string_view::npos ? path : path.substr (slash + 1));
  if (name.ends_with (".disabled"))
    name.resize (name.size () - 9);
  std::string extension;
  if (auto dot = name.rfind ('.'); dot != std::string::npos)
    {
      extension = name.substr (dot);
      name.resize (dot);
    }
  std::string key;
  std::string token;
  auto flush = [&] () {
    static constexpr std::string_view qualifiers[]
        = { "fabric",   "forge",   "neoforge",  "quilt", "mc",
            "mod",      "alpha",   "beta",      "rc",    "pre",
            "snapshot", "release", "universal", "client" };
    bool versioned
        = std::any_of (token.begin (), token.end (),
                       [] (unsigned char c) { return std::isdigit (c); });
    bool qualifier
        = std::find (std::begin (qualifiers), std::end (qualifiers), token)
          != std::end (qualifiers);
    if (!token.empty () && !versioned && !qualifier)
      key += token + "|";
    token.clear ();
  };
  for (char c : name)
    {
      if (c == '-' || c == '_' || c == '+' || c == '.' || c == ' ')
        flush ();
      else
        token += c;
    }
  flush ();
  return key.empty () ? std::string () : directory + key + extension;
}

void
pair_replacements (std::vector<update_item> &items)
{
  std::map<std::string, std::vector<std::size_t>> added;
  std::map<std::string, std::vector<std::size_t>> removed;
  for (std::size_t i = 0; i < items.size (); ++i)
    {
      const auto &item = items[i];
      if (item.action == update_action::add)
        {
          if (auto key = family_of (item.path); !key.empty ())
            added[key].push_back (i);
        }
      else if (item.action == update_action::remove && !item.modified)
        {
          if (auto key = family_of (item.path); !key.empty ())
            removed[key].push_back (i);
        }
    }
  std::vector<bool> gone (items.size (), false);
  for (const auto &[key, adds] : added)
    {
      auto it = removed.find (key);
      if (adds.size () != 1 || it == removed.end () || it->second.size () != 1)
        continue;
      auto &item = items[adds.front ()];
      const auto &old = items[it->second.front ()];
      item.action = update_action::change;
      item.replaces = old.path;
      item.disabled = old.path.ends_with (".disabled");
      gone[it->second.front ()] = true;
    }
  std::vector<update_item> kept;
  kept.reserve (items.size ());
  for (std::size_t i = 0; i < items.size (); ++i)
    if (!gone[i])
      kept.push_back (std::move (items[i]));
  items = std::move (kept);
}
}

result<void>
detail::write_pack_info (const std::filesystem::path &directory,
                         const pack_manifest &manifest,
                         const pack_origin &origin,
                         const std::map<std::string, owned_file> &owned)
{
  json info = { { "name", manifest.name },
                { "version", manifest.version },
                { "author", manifest.author },
                { "format", format_name (manifest.format) },
                { "minecraft", manifest.minecraft } };
  if (!origin.project.empty ())
    info["origin"] = { { "source", origin.source },
                       { "project", origin.project },
                       { "version", origin.version } };
  json files = json::object ();
  for (const auto &[path, file] : owned)
    {
      json entry = json::object ();
      if (!file.sha1.empty ())
        entry["sha1"] = file.sha1;
      if (file.crc)
        entry["crc"] = *file.crc;
      if (file.size >= 0)
        entry["size"] = file.size;
      files[path] = std::move (entry);
    }
  if (!owned.empty ())
    info["files"] = std::move (files);
  return fs::write_file_atomic (directory / info_file, info.dump (2));
}

result<pack_info>
read_pack_info (const std::filesystem::path &directory)
{
  auto file = directory / info_file;
  auto st = fs::stat (file);
  if (!st.exists || st.is_directory)
    return make_error (errc::not_found, "no modpack information");
  if (st.size > info_limit)
    return make_error (errc::size_mismatch,
                       "modpack information is too large");
  BLB_TRY_ASSIGN (auto text, fs::read_file (file));
  auto doc = json::parse (text, nullptr, false);
  if (!doc.is_object ())
    return make_error (errc::parse, "invalid modpack information");
  pack_info info;
  info.name = text_of (doc, "name");
  info.version = text_of (doc, "version");
  info.format = text_of (doc, "format");
  info.minecraft = text_of (doc, "minecraft");
  if (auto origin = doc.find ("origin");
      origin != doc.end () && origin->is_object ())
    {
      info.origin.source = text_of (*origin, "source");
      info.origin.project = text_of (*origin, "project");
      info.origin.version = text_of (*origin, "version");
    }
  if (auto files = doc.find ("files");
      files != doc.end () && files->is_object ())
    {
      info.tracked = true;
      for (auto it = files->begin (); it != files->end (); ++it)
        {
          if (!it->is_object () || !fs::safe_relative (it.key ()))
            continue;
          owned_file owned;
          owned.sha1 = to_lower (text_of (*it, "sha1"));
          if (auto crc = it->find ("crc");
              crc != it->end () && crc->is_number_unsigned ())
            owned.crc = crc->get<std::uint32_t> ();
          if (auto size = it->find ("size");
              size != it->end () && size->is_number_integer ())
            owned.size = size->get<std::int64_t> ();
          info.files.emplace (it.key (), std::move (owned));
        }
    }
  return info;
}

result<update_plan>
plan_update (install::game_installer &installer, std::string_view id,
             std::vector<std::string> urls, std::uint64_t archive_size,
             const curseforge_settings &curseforge, progress_sink *sink,
             cancellation_token token)
{
  auto stage = [sink] (std::string_view name) {
    if (sink)
      sink->on_stage ("update", name);
  };
  const auto &layout = installer.layout ();
  instance::instance_manager manager (layout);
  BLB_TRY_ASSIGN (auto inst, manager.get (id));
  auto game_dir = manager.game_directory (inst);
  pack_info old;
  if (auto info = read_pack_info (layout.version_dir (id)))
    old = std::move (*info);

  update_plan plan;
  plan.id = std::string (id);
  plan.urls = urls;
  if (!old.tracked && !old.origin.project.empty ())
    {
      stage ("origin");
      auto installed
          = detail::describe_origin (installer, old.origin, curseforge, token);
      if (!installed && installed.err ().code == errc::cancelled)
        return std::move (installed).err ();
      if (installed)
        {
          auto before
              = load_remote_pack (installer, installed->urls,
                                  installed->archive_size, curseforge, token);
          if (!before && before.err ().code == errc::cancelled)
            return std::move (before).err ();
          if (before)
            {
              old.files = std::move (before->owned);
              old.tracked = true;
            }
        }
    }
  plan.tracked = old.tracked;
  stage ("index");
  BLB_TRY_ASSIGN (auto pack,
                  load_remote_pack (installer, std::move (urls), archive_size,
                                    curseforge, token));
  plan.archive_size = pack.archive_size;
  plan.manifest = std::move (pack.manifest);
  plan.owned = std::move (pack.owned);
  const auto &targets = pack.targets;

  stage ("compare");
  progress_tracker tracker (sink, "update:compare");
  std::uint64_t total = targets.size ();
  for (const auto &[relative, recorded] : old.files)
    total += targets.contains (relative) ? 0 : 1;
  tracker.add_total (0, total);
  tracker.report (true);
  for (const auto &[relative, t] : targets)
    {
      BLB_TRY (token.check ());
      tracker.item_done ();
      tracker.report ();
      const auto &want = plan.owned[relative];
      update_item item;
      item.path = relative;
      item.file = t.file;
      item.entry = t.entry;
      item.bytes = t.bytes;
      auto local = local_at (game_dir, relative);
      if (local.stat.is_directory || local.stat.is_link)
        {
          s_log.warn ("cannot update {}: it is not a regular file", relative);
          continue;
        }
      if (!present (local))
        {
          if (t.file)
            {
              auto disabled = local_at (game_dir, relative + ".disabled");
              if (present (disabled))
                {
                  BLB_TRY_ASSIGN (bool same, matches (disabled, want, token));
                  if (same)
                    continue;
                }
            }
          plan.items.push_back (std::move (item));
          continue;
        }
      BLB_TRY_ASSIGN (bool current, matches (local, want, token));
      if (current)
        continue;
      item.action = update_action::change;
      item.modified = true;
      if (auto it = old.files.find (relative); it != old.files.end ())
        {
          BLB_TRY_ASSIGN (bool untouched, matches (local, it->second, token));
          if (!untouched && same_owned (it->second, want))
            continue;
          item.modified = !untouched;
        }
      plan.items.push_back (std::move (item));
    }
  for (const auto &[relative, recorded] : old.files)
    {
      if (targets.contains (relative))
        continue;
      BLB_TRY (token.check ());
      tracker.item_done ();
      tracker.report ();
      for (const auto &name : { relative, relative + ".disabled" })
        {
          auto local = local_at (game_dir, name);
          if (!present (local))
            continue;
          BLB_TRY_ASSIGN (bool untouched, matches (local, recorded, token));
          update_item item;
          item.path = name;
          item.action = update_action::remove;
          item.modified = !untouched;
          plan.items.push_back (std::move (item));
        }
    }
  tracker.finish ();
  pair_replacements (plan.items);
  plan.loaders_changed = inst.minecraft_version != plan.manifest.minecraft
                         || primary_loaders (inst.loaders)
                                != primary_loaders (plan.manifest.loaders);
  s_log.info ("planned update of {} to {}: {} changes, {} of {} bytes",
              plan.id, plan.manifest.version, plan.items.size (), pack.fetched,
              plan.archive_size);
  return plan;
}

result<void>
apply_update (install::game_installer &installer, const update_plan &plan,
              const pack_origin &origin, const std::set<std::string> &keep,
              progress_sink *sink, cancellation_token token)
{
  const auto &layout = installer.layout ();
  instance::instance_manager manager (layout);
  BLB_TRY_ASSIGN (auto inst, manager.get (plan.id));
  auto game_dir = manager.game_directory (inst);
  const auto &manifest = plan.manifest;
  auto chosen = [&] (const update_item &item) {
    return !item.modified || !keep.contains (item.path);
  };
  auto wanted = [&] (const update_item &item) {
    return item.action != update_action::remove && chosen (item);
  };

  if (plan.loaders_changed)
    {
      install::loader_installer loaders (installer);
      install::install_request request;
      request.minecraft = manifest.minecraft;
      request.loaders = manifest.loaders;
      request.id = plan.id;
      request.mods_directory = game_dir / "mods";
      BLB_TRY (loaders.install (request, sink, token));
    }

  std::vector<install::planned_file> planned;
  for (const auto &item : plan.items)
    {
      if (!item.file || !wanted (item))
        continue;
      const auto &f = manifest.files[*item.file];
      install::planned_file p;
      p.kind = install::file_kind::other;
      auto placed = item.disabled ? f.path + ".disabled" : f.path;
      p.target = game_dir / path_from_utf8 (placed);
      p.relative = "versions/" + plan.id + "/" + placed;
      p.urls = installer.mirror ().resolve_all (f.urls);
      if (!f.sha1.empty ())
        p.sha1 = parse_sha1 (f.sha1);
      p.size = f.size;
      planned.push_back (std::move (p));
    }
  if (!planned.empty ())
    BLB_TRY (installer.ensure_files (std::move (planned), sink, token,
                                     "modpack-files"));

  std::vector<const update_item *> extracts;
  for (const auto &item : plan.items)
    if (!item.entry.empty () && wanted (item))
      extracts.push_back (&item);
  if (!extracts.empty ())
    {
      BLB_TRY_ASSIGN (auto zip,
                      remote_zip::open (plan.urls, plan.archive_size, token));
      progress_tracker progress (sink, "update:overrides");
      std::uint64_t total = 0;
      for (const auto *item : extracts)
        total += item->bytes;
      progress.add_total (total, extracts.size ());
      std::size_t start = 0;
      while (start < extracts.size ())
        {
          BLB_TRY (token.check ());
          std::size_t stop = start;
          std::uint64_t bytes = 0;
          std::vector<const remote_entry *> batch;
          while (stop < extracts.size () && stop - start < batch_items
                 && (batch.empty ()
                     || bytes + extracts[stop]->bytes <= batch_bytes))
            {
              bytes += extracts[stop]->bytes;
              batch.push_back (zip.find (extracts[stop]->entry));
              ++stop;
            }
          BLB_TRY (zip.prefetch (batch, token));
          for (std::size_t i = start; i < stop; ++i)
            {
              const auto &item = *extracts[i];
              auto placed
                  = item.disabled ? item.path + ".disabled" : item.path;
              BLB_TRY (zip.extract (
                  item.entry, game_dir / path_from_utf8 (placed), token));
              progress.add_done (item.bytes);
              progress.item_done ();
              progress.report ();
            }
          zip.forget ();
          start = stop;
        }
      progress.finish ();
      s_log.info ("fetched {} override files of {} with {} bytes in {} "
                  "requests",
                  extracts.size (), plan.id, zip.downloaded (),
                  zip.requests ());
    }

  for (const auto &item : plan.items)
    {
      std::string old;
      if (item.action == update_action::remove && chosen (item))
        old = item.path;
      else if (!item.replaces.empty () && wanted (item))
        old = item.replaces;
      if (old.empty ())
        continue;
      auto target = game_dir / path_from_utf8 (old);
      if (fs::exists (target))
        BLB_TRY (fs::remove_file (target));
    }
  BLB_TRY (token.check ());
  BLB_TRY (detail::write_pack_info (layout.version_dir (plan.id), manifest,
                                    origin, plan.owned));
  s_log.info ("updated {} to {} {}", plan.id, manifest.name, manifest.version);
  return ok ();
}

result<pack_candidate>
detail::describe_origin (install::game_installer &installer,
                         const pack_origin &origin,
                         const curseforge_settings &curseforge,
                         cancellation_token token)
{
  const auto &mirror = installer.mirror ();
  pack_candidate out;
  out.origin = origin;
  if (origin.source == "curseforge")
    {
      BLB_TRY_ASSIGN (auto doc, detail::curseforge_get (
                                    curseforge, mirror,
                                    sformat ("/v1/mods/{}/files/{}",
                                             url_encode (origin.project),
                                             url_encode (origin.version)),
                                    token));
      auto data = doc.find ("data");
      if (data == doc.end () || !data->is_object ())
        return make_error (errc::not_found, "curseforge file not found");
      auto file = detail::curseforge_remote_file (*data);
      out.version_name = text_of (*data, "displayName");
      out.urls = mirror.resolve_all (detail::curseforge_mirrors (file.url));
      out.archive_size
          = file.size > 0 ? static_cast<std::uint64_t> (file.size) : 0;
      return out;
    }
  BLB_TRY_ASSIGN (
      auto doc, detail::modrinth_get (
                    mirror, "/version/" + url_encode (origin.version), token));
  if (!doc.is_object () || !fill_modrinth (doc, mirror, out))
    return make_error (errc::not_found, "modrinth version not found");
  return out;
}

result<std::vector<pack_candidate>>
identify_pack (install::game_installer &installer, std::string_view id,
               const curseforge_settings &curseforge, cancellation_token token)
{
  const auto &layout = installer.layout ();
  instance::instance_manager manager (layout);
  BLB_TRY_ASSIGN (auto inst, manager.get (id));
  auto game_dir = manager.game_directory (inst);
  BLB_TRY_ASSIGN (auto info, read_pack_info (layout.version_dir (id)));
  bool curse = info.format == "curseforge";
  if (!curse && info.format != "modrinth")
    return make_error (errc::unsupported,
                       "only Modrinth and CurseForge modpacks can be "
                       "identified");
  if (info.name.empty () || info.version.empty ())
    return make_error (errc::parse,
                       "the modpack does not record its name and version");
  const auto &mirror = installer.mirror ();

  struct project
  {
    std::string id;
    std::string title;
    std::string icon;
  };
  std::vector<project> projects;
  auto search = [&] (bool by_version) -> result<void> {
    projects.clear ();
    bool versioned = by_version && !info.minecraft.empty ();
    if (curse)
      {
        std::string path = "/v1/mods/search?gameId=432&classId=4471"
                           "&pageSize=8&sortField=2&sortOrder=desc"
                           "&searchFilter="
                           + url_encode (info.name);
        if (versioned)
          path += "&gameVersion=" + url_encode (info.minecraft);
        BLB_TRY_ASSIGN (auto doc, detail::curseforge_get (curseforge, mirror,
                                                          path, token));
        if (auto data = doc.find ("data");
            data != doc.end () && data->is_array ())
          for (const auto &item : *data)
            if (item.is_object ())
              {
                std::string icon;
                if (auto logo = item.find ("logo");
                    logo != item.end () && logo->is_object ())
                  icon = text_of (*logo, "thumbnailUrl").empty ()
                             ? text_of (*logo, "url")
                             : text_of (*logo, "thumbnailUrl");
                projects.push_back ({ std::to_string (int_of (item, "id")),
                                      text_of (item, "name"), icon });
              }
        return {};
      }
    json facets = json::array ({ json::array ({ "project_type:modpack" }) });
    if (versioned)
      facets.push_back (json::array ({ "versions:" + info.minecraft }));
    BLB_TRY_ASSIGN (
        auto doc,
        detail::modrinth_get (mirror,
                              "/search?limit=8&query=" + url_encode (info.name)
                                  + "&facets=" + url_encode (facets.dump ()),
                              token));
    if (auto hits = doc.find ("hits"); hits != doc.end () && hits->is_array ())
      for (const auto &item : *hits)
        if (item.is_object ())
          projects.push_back ({ text_of (item, "project_id"),
                                text_of (item, "title"),
                                text_of (item, "icon_url") });
    return {};
  };
  BLB_TRY (search (true));
  if (projects.empty ())
    BLB_TRY (search (false));

  std::vector<pack_candidate> found;
  auto order = [&] () {
    std::stable_sort (found.begin (), found.end (),
                      [] (const pack_candidate &a, const pack_candidate &b) {
                        if (a.exact != b.exact)
                          return a.exact;
                        return a.matched * std::max<std::size_t> (b.total, 1)
                               > b.matched
                                     * std::max<std::size_t> (a.total, 1);
                      });
  };
  for (const auto &p : projects)
    {
      BLB_TRY (token.check ());
      if (p.id.empty ())
        continue;
      std::vector<pack_candidate> versions;
      if (curse)
        {
          for (int page = 0; page < 3 && versions.size () < 3; ++page)
            {
              std::string path
                  = sformat ("/v1/mods/{}/files?pageSize=50&index={}",
                             url_encode (p.id), page * 50);
              if (!info.minecraft.empty ())
                path += "&gameVersion=" + url_encode (info.minecraft);
              auto doc
                  = detail::curseforge_get (curseforge, mirror, path, token);
              if (!doc && doc.err ().code == errc::cancelled)
                return std::move (doc).err ();
              if (!doc)
                break;
              auto data = doc->find ("data");
              if (data == doc->end () || !data->is_array () || data->empty ())
                break;
              for (const auto &f : *data)
                {
                  if (!f.is_object ())
                    continue;
                  auto display = text_of (f, "displayName");
                  auto name = text_of (f, "fileName");
                  if (display.find (info.version) == std::string::npos
                      && name.find (info.version) == std::string::npos)
                    continue;
                  auto file = detail::curseforge_remote_file (f);
                  pack_candidate c;
                  c.origin = { "curseforge", p.id,
                               std::to_string (int_of (f, "id")) };
                  c.title = p.title;
                  c.icon_url = p.icon;
                  c.version_name = display.empty () ? name : display;
                  c.urls = mirror.resolve_all (
                      detail::curseforge_mirrors (file.url));
                  c.archive_size = file.size > 0
                                       ? static_cast<std::uint64_t> (file.size)
                                       : 0;
                  versions.push_back (std::move (c));
                  if (versions.size () >= 3)
                    break;
                }
              if (data->size () < 50)
                break;
            }
        }
      else
        {
          auto doc = detail::modrinth_get (
              mirror, "/project/" + url_encode (p.id) + "/version", token);
          if (!doc && doc.err ().code == errc::cancelled)
            return std::move (doc).err ();
          if (doc && doc->is_array ())
            for (const auto &v : *doc)
              {
                if (!v.is_object ()
                    || (text_of (v, "version_number") != info.version
                        && text_of (v, "name") != info.version))
                  continue;
                pack_candidate c;
                c.title = p.title;
                c.icon_url = p.icon;
                if (!fill_modrinth (v, mirror, c))
                  continue;
                versions.push_back (std::move (c));
                if (versions.size () >= 3)
                  break;
              }
        }
      for (auto &c : versions)
        {
          auto pack = load_remote_pack (installer, c.urls, c.archive_size,
                                        curseforge, token);
          if (!pack && pack.err ().code == errc::cancelled)
            return std::move (pack).err ();
          if (!pack)
            {
              s_log.warn ("cannot read candidate {} {}: {}", c.title,
                          c.version_name, pack.err ().message);
              continue;
            }
          BLB_TRY_ASSIGN (auto score, score_local (*pack, game_dir, token));
          c.matched = score.first;
          c.total = score.second;
          c.exact = pack->manifest.name == info.name
                    && pack->manifest.version == info.version;
          bool certain = sure (c);
          found.push_back (std::move (c));
          if (certain)
            {
              order ();
              return found;
            }
        }
    }
  order ();
  return found;
}

result<void>
bind_pack (install::game_installer &installer, std::string_view id,
           const pack_candidate &candidate,
           const curseforge_settings &curseforge, cancellation_token token)
{
  BLB_TRY_ASSIGN (auto pack, load_remote_pack (installer, candidate.urls,
                                               candidate.archive_size,
                                               curseforge, token));
  BLB_TRY (token.check ());
  BLB_TRY (detail::write_pack_info (installer.layout ().version_dir (id),
                                    pack.manifest, candidate.origin,
                                    pack.owned));
  s_log.info ("bound {} to {} {} {}", id, candidate.origin.source,
              candidate.origin.project, candidate.origin.version);
  return ok ();
}

result<bool>
record_pack_files (install::game_installer &installer, std::string_view id,
                   const curseforge_settings &curseforge,
                   cancellation_token token)
{
  BLB_TRY_ASSIGN (auto info,
                  read_pack_info (installer.layout ().version_dir (id)));
  if (info.tracked || info.origin.project.empty ()
      || info.origin.version.empty ())
    return false;
  BLB_TRY_ASSIGN (
      auto candidate,
      detail::describe_origin (installer, info.origin, curseforge, token));
  BLB_TRY (bind_pack (installer, id, candidate, curseforge, token));
  return true;
}
}
