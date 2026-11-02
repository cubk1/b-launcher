#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/mods.hpp>

#include "core/modpack/json.hpp"
#include "core/modpack/toml.hpp"

#include <algorithm>
#include <unordered_set>

namespace b_launcher_base::modpack
{

namespace
{

using json = nlohmann::json;

constexpr log::logger s_log ("mods");

using detail::text_of;

bool
digit (char c)
{
  return c >= '0' && c <= '9';
}

std::string_view
trimmed (std::string_view text)
{
  while (!text.empty () && (text.front () == ' ' || text.front () == '\t'))
    text.remove_prefix (1);
  while (!text.empty () && (text.back () == ' ' || text.back () == '\t'))
    text.remove_suffix (1);
  return text;
}

std::string_view
bare_version (std::string_view version)
{
  version = trimmed (version);
  if (!version.empty ()
      && (version.front () == 'v' || version.front () == 'V'))
    version.remove_prefix (1);
  if (auto plus = version.find ('+'); plus != std::string_view::npos)
    version = version.substr (0, plus);
  return version;
}

std::vector<std::string_view>
version_parts (std::string_view version)
{
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= version.size ())
    {
      auto dot = version.find ('.', start);
      parts.push_back (version.substr (start, dot == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : dot - start));
      if (dot == std::string_view::npos)
        break;
      start = dot + 1;
    }
  return parts;
}

bool
wildcard_part (std::string_view part)
{
  return part == "x" || part == "X" || part == "*";
}

std::string
padded (std::string_view version)
{
  auto dash = version.find ('-');
  std::string head (version.substr (0, dash));
  std::string tail (dash == std::string_view::npos ? std::string_view ()
                                                   : version.substr (dash));
  while (head.size () > 2 && head.ends_with (".0"))
    head.resize (head.size () - 2);
  return head + tail;
}

int
compare_padded (std::string_view a, std::string_view b)
{
  return compare_versions (padded (a), padded (b));
}

long long
leading_number (std::string_view part)
{
  long long value = 0;
  for (char c : part)
    {
      if (!digit (c) || value > 100000000)
        break;
      value = value * 10 + (c - '0');
    }
  return value;
}

std::string
raised (std::string_view version, std::size_t index)
{
  auto parts = version_parts (version.substr (0, version.find ('-')));
  std::string out;
  for (std::size_t i = 0; i <= index; ++i)
    {
      long long value = i < parts.size () ? leading_number (parts[i]) : 0;
      if (i == index)
        ++value;
      out += (i == 0 ? "" : ".") + std::to_string (value);
    }
  return out;
}

bool
wildcard_holds (std::string_view version, std::string_view pattern)
{
  auto wanted = version_parts (pattern);
  auto have = version_parts (version.substr (0, version.find ('-')));
  for (std::size_t i = 0; i < wanted.size (); ++i)
    {
      if (wildcard_part (wanted[i]))
        return true;
      if (i >= have.size () || compare_versions (have[i], wanted[i]) != 0)
        return false;
    }
  return true;
}

bool
predicate_holds (std::string_view version, std::string_view predicate)
{
  predicate = trimmed (predicate);
  if (predicate.empty () || wildcard_part (predicate))
    return true;
  std::string_view op;
  for (std::string_view candidate : { ">=", "<=", ">", "<", "=", "~", "^" })
    if (predicate.starts_with (candidate))
      {
        op = candidate;
        break;
      }
  auto target = bare_version (predicate.substr (op.size ()));
  if (target.empty () || !digit (target.front ()))
    return true;
  auto parts = version_parts (target);
  for (auto part : parts)
    if (wildcard_part (part))
      return op.empty () || op == "=" ? wildcard_holds (version, target)
                                      : true;
  int c = compare_padded (version, target);
  if (op == ">=")
    return c >= 0;
  if (op == "<=")
    return c <= 0;
  if (op == ">")
    return c > 0;
  if (op == "<")
    return c < 0;
  if (op == "=")
    return c == 0;
  if (op == "~")
    return c >= 0
           && compare_padded (version,
                              raised (target, parts.size () > 1 ? 1 : 0))
                  < 0;
  if (op == "^")
    {
      std::size_t index = 0;
      while (index + 1 < parts.size () && leading_number (parts[index]) == 0)
        ++index;
      return c >= 0 && compare_padded (version, raised (target, index)) < 0;
    }
  return c >= 0;
}

bool
alternative_holds (std::string_view version, std::string_view alternative)
{
  std::vector<std::string> predicates;
  std::size_t i = 0;
  while (i < alternative.size ())
    {
      while (i < alternative.size () && alternative[i] == ' ')
        ++i;
      auto end = alternative.find (' ', i);
      auto token = alternative.substr (
          i, end == std::string_view::npos ? std::string_view::npos : end - i);
      if (!token.empty ())
        {
          if (!predicates.empty ()
              && predicates.back ().find_first_not_of ("<>=~^")
                     == std::string::npos)
            predicates.back () += std::string (token);
          else
            predicates.emplace_back (token);
        }
      if (end == std::string_view::npos)
        break;
      i = end + 1;
    }
  for (const auto &predicate : predicates)
    if (!predicate_holds (version, predicate))
      return false;
  return true;
}

bool
maven_holds (std::string_view version, std::string_view range)
{
  bool any = false;
  std::size_t i = 0;
  while (i < range.size ())
    {
      while (i < range.size () && (range[i] == ',' || range[i] == ' '))
        ++i;
      if (i >= range.size ())
        break;
      char open = range[i];
      if (open != '[' && open != '(')
        return true;
      auto close = range.find_first_of ("])", i + 1);
      if (close == std::string_view::npos)
        return true;
      auto body = range.substr (i + 1, close - i - 1);
      bool held = true;
      auto comma = body.find (',');
      if (comma == std::string_view::npos)
        {
          auto exact = bare_version (body);
          held = exact.empty () || compare_padded (version, exact) == 0;
        }
      else
        {
          auto low = bare_version (body.substr (0, comma));
          auto high = bare_version (body.substr (comma + 1));
          if (!low.empty () && digit (low.front ()))
            {
              int c = compare_padded (version, low);
              held = open == '[' ? c >= 0 : c > 0;
            }
          if (held && !high.empty () && digit (high.front ()))
            {
              int c = compare_padded (version, high);
              held = range[close] == ']' ? c <= 0 : c < 0;
            }
        }
      if (held)
        return true;
      any = true;
      i = close + 1;
    }
  return !any;
}

std::string
license_text (const json &node)
{
  if (node.is_string ())
    return node.get<std::string> ();
  if (node.is_object ())
    {
      auto name = text_of (node, "name");
      return name.empty () ? text_of (node, "id") : name;
    }
  std::string out;
  if (node.is_array ())
    for (const auto &item : node)
      {
        auto part = license_text (item);
        if (!part.empty ())
          out += (out.empty () ? "" : ", ") + part;
      }
  return out;
}

std::string
manifest_value (zip_reader &zip, std::string_view key)
{
  auto manifest = zip.read ("META-INF/MANIFEST.MF");
  if (!manifest)
    return {};
  for (auto line : split (*manifest, '\n'))
    {
      line = trim (line);
      if (line.starts_with (key) && line.size () > key.size ()
          && line[key.size ()] == ':')
        return std::string (trim (line.substr (key.size () + 1)));
    }
  return {};
}

void
read_fabric (mod_info &mod, const json &doc)
{
  mod.id = text_of (doc, "id");
  mod.version = text_of (doc, "version");
  mod.name = text_of (doc, "name");
  mod.description = text_of (doc, "description");
  if (auto it = doc.find ("authors"); it != doc.end () && it->is_array ())
    for (const auto &author : *it)
      {
        if (author.is_string ())
          mod.authors.push_back (author.get<std::string> ());
        else if (author.is_object ())
          mod.authors.push_back (text_of (author, "name"));
      }
  if (auto it = doc.find ("contact"); it != doc.end ())
    {
      mod.homepage = text_of (*it, "homepage");
      mod.sources = text_of (*it, "sources");
      mod.issues = text_of (*it, "issues");
    }
  if (auto it = doc.find ("license"); it != doc.end ())
    mod.license = license_text (*it);
  if (auto it = doc.find ("icon"); it != doc.end ())
    {
      if (it->is_string ())
        mod.icon = it->get<std::string> ();
      else if (it->is_object () && !it->empty ())
        mod.icon = it->rbegin ()->is_string ()
                       ? it->rbegin ()->get<std::string> ()
                       : std::string ();
    }
  auto deps = [&] (const char *key, bool required) {
    auto it = doc.find (key);
    if (it == doc.end () || !it->is_object ())
      return;
    for (auto entry = it->begin (); entry != it->end (); ++entry)
      {
        std::string range;
        if (entry->is_string ())
          range = entry->get<std::string> ();
        else if (entry->is_array () && !entry->empty ()
                 && entry->front ().is_string ())
          range = entry->front ().get<std::string> ();
        mod.dependencies.push_back ({ entry.key (), range, required });
      }
  };
  deps ("depends", true);
  deps ("recommends", false);
  if (auto it = doc.find ("provides"); it != doc.end () && it->is_array ())
    for (const auto &p : *it)
      if (p.is_string ())
        mod.provides.push_back (p.get<std::string> ());
}

void
read_quilt (mod_info &mod, const json &doc)
{
  auto loader = doc.find ("quilt_loader");
  if (loader == doc.end ())
    return;
  mod.id = text_of (*loader, "id");
  mod.version = text_of (*loader, "version");
  if (auto meta = loader->find ("metadata"); meta != loader->end ())
    {
      mod.name = text_of (*meta, "name");
      mod.description = text_of (*meta, "description");
      mod.icon = text_of (*meta, "icon");
      if (auto contact = meta->find ("contact"); contact != meta->end ())
        {
          mod.homepage = text_of (*contact, "homepage");
          mod.sources = text_of (*contact, "sources");
          mod.issues = text_of (*contact, "issues");
        }
      if (auto license = meta->find ("license"); license != meta->end ())
        mod.license = license_text (*license);
      if (auto people = meta->find ("contributors");
          people != meta->end () && people->is_object ())
        for (auto p = people->begin (); p != people->end (); ++p)
          mod.authors.push_back (p.key ());
    }
  if (auto deps = loader->find ("depends");
      deps != loader->end () && deps->is_array ())
    for (const auto &d : *deps)
      {
        if (d.is_string ())
          mod.dependencies.push_back ({ d.get<std::string> (), {}, true });
        else if (d.is_object ())
          mod.dependencies.push_back (
              { text_of (d, "id"), text_of (d, "versions"),
                !(d.contains ("optional") && d["optional"].is_boolean ()
                  && d["optional"].get<bool> ()) });
      }
  if (auto provides = loader->find ("provides");
      provides != loader->end () && provides->is_array ())
    for (const auto &p : *provides)
      {
        if (p.is_string ())
          mod.provides.push_back (p.get<std::string> ());
        else if (p.is_object ())
          mod.provides.push_back (text_of (p, "id"));
      }
}

void
read_forge_toml (mod_info &mod, const json &doc, zip_reader &zip)
{
  auto mods = doc.find ("mods");
  if (mods == doc.end () || !mods->is_array () || mods->empty ())
    return;
  const auto &first = mods->front ();
  mod.id = text_of (first, "modId");
  mod.version = text_of (first, "version");
  mod.name = text_of (first, "displayName");
  mod.description = std::string (trim (text_of (first, "description")));
  mod.homepage = text_of (first, "displayURL");
  mod.issues = text_of (doc, "issueTrackerURL");
  mod.license = text_of (doc, "license");
  mod.icon = text_of (first, "logoFile");
  if (mod.icon.empty ())
    mod.icon = text_of (doc, "logoFile");
  auto authors = text_of (first, "authors");
  if (authors.empty ())
    authors = text_of (doc, "authors");
  for (auto author : split (authors, ','))
    if (!trim (author).empty ())
      mod.authors.emplace_back (trim (author));
  if (mod.version.find ("${file.jarVersion}") != std::string::npos)
    {
      auto version = manifest_value (zip, "Implementation-Version");
      mod.version = replace_all (mod.version, "${file.jarVersion}", version);
    }
  for (std::size_t i = 1; i < mods->size (); ++i)
    mod.provides.push_back (text_of ((*mods)[i], "modId"));
  if (auto deps = doc.find ("dependencies");
      deps != doc.end () && deps->is_object ())
    for (const auto &entry : *mods)
      {
        auto id = text_of (entry, "modId");
        if (id.empty ())
          continue;
        if (auto list = deps->find (id);
            list != deps->end () && list->is_array ())
          for (const auto &d : *list)
            {
              bool required = true;
              if (auto m = d.find ("mandatory");
                  m != d.end () && m->is_boolean ())
                required = m->get<bool> ();
              if (auto t = d.find ("type");
                  t != d.end () && t->is_string ())
                required = to_lower (t->get<std::string> ()) == "required";
              mod.dependencies.push_back (
                  { text_of (d, "modId"), text_of (d, "versionRange"),
                    required, to_lower (text_of (d, "side")) != "server" });
            }
      }
}

void
read_mcmod (mod_info &mod, const json &doc)
{
  const json *entry = nullptr;
  if (doc.is_array () && !doc.empty ())
    entry = &doc.front ();
  else if (doc.is_object ())
    if (auto list = doc.find ("modList");
        list != doc.end () && list->is_array () && !list->empty ())
      entry = &list->front ();
  if (!entry)
    return;
  mod.id = text_of (*entry, "modid");
  mod.name = text_of (*entry, "name");
  mod.version = text_of (*entry, "version");
  mod.description = text_of (*entry, "description");
  mod.homepage = text_of (*entry, "url");
  mod.icon = text_of (*entry, "logoFile");
  for (const char *key : { "authorList", "authors" })
    if (auto it = entry->find (key); it != entry->end () && it->is_array ())
      for (const auto &a : *it)
        if (a.is_string ())
          mod.authors.push_back (a.get<std::string> ());
  if (auto it = entry->find ("requiredMods");
      it != entry->end () && it->is_array ())
    for (const auto &d : *it)
      if (d.is_string ())
        mod.dependencies.push_back ({ d.get<std::string> (), {}, true });
}

void
read_litemod (mod_info &mod, const json &doc)
{
  mod.name = text_of (doc, "name");
  mod.id = to_lower (mod.name);
  mod.version = text_of (doc, "version");
  if (mod.version.empty ())
    mod.version = text_of (doc, "revision");
  mod.description = text_of (doc, "description");
  auto author = text_of (doc, "author");
  if (!author.empty ())
    mod.authors.push_back (author);
}

void
nested_ids (zip_reader &zip, std::vector<std::string> &out)
{
  if (auto text = zip.read ("fabric.mod.json"))
    {
      auto doc = detail::parse_json (*text);
      auto id = text_of (doc, "id");
      if (!id.empty ())
        out.push_back (id);
      if (auto it = doc.find ("provides"); it != doc.end () && it->is_array ())
        for (const auto &p : *it)
          if (p.is_string ())
            out.push_back (p.get<std::string> ());
    }
  if (auto text = zip.read ("quilt.mod.json"))
    {
      auto doc = detail::parse_json (*text);
      if (auto loader = doc.find ("quilt_loader"); loader != doc.end ())
        {
          auto id = text_of (*loader, "id");
          if (!id.empty ())
            out.push_back (id);
        }
    }
  for (const char *name :
       { "META-INF/mods.toml", "META-INF/neoforge.mods.toml" })
    if (auto text = zip.read (name))
      if (auto doc = detail::parse_toml (*text))
        if (auto mods = doc->find ("mods");
            mods != doc->end () && mods->is_array ())
          for (const auto &m : *mods)
            {
              auto id = text_of (m, "modId");
              if (!id.empty ())
                out.push_back (id);
            }
}

void
collect_nested (zip_reader &zip, int depth, std::vector<std::string> &out)
{
  if (depth > 3)
    return;
  std::vector<std::string> paths;
  if (auto text = zip.read ("fabric.mod.json"))
    {
      auto doc = detail::parse_json (*text);
      if (auto jars = doc.find ("jars");
          jars != doc.end () && jars->is_array ())
        for (const auto &j : *jars)
          paths.push_back (text_of (j, "file"));
    }
  if (auto text = zip.read ("quilt.mod.json"))
    {
      auto doc = detail::parse_json (*text);
      if (auto loader = doc.find ("quilt_loader"); loader != doc.end ())
        if (auto jars = loader->find ("jars");
            jars != loader->end () && jars->is_array ())
          for (const auto &j : *jars)
            if (j.is_string ())
              paths.push_back (j.get<std::string> ());
    }
  if (auto text = zip.read ("META-INF/jarjar/metadata.json"))
    {
      auto doc = detail::parse_json (*text);
      if (auto jars = doc.find ("jars");
          jars != doc.end () && jars->is_array ())
        for (const auto &j : *jars)
          paths.push_back (text_of (j, "path"));
    }
  for (const auto &path : paths)
    {
      if (path.empty ())
        continue;
      auto data = zip.read (path);
      if (!data)
        continue;
      auto nested = zip_reader::open_memory (std::move (*data));
      if (!nested)
        continue;
      nested_ids (*nested, out);
      collect_nested (*nested, depth + 1, out);
    }
}

std::string
strip_disabled (std::string name)
{
  auto lower = to_lower (name);
  for (std::string_view suffix : { ".disabled", ".old" })
    if (lower.ends_with (suffix))
      return name.substr (0, name.size () - suffix.size ());
  return name;
}

}

std::string_view
platform_name (mod_platform platform) noexcept
{
  switch (platform)
    {
    case mod_platform::fabric:
      return "fabric";
    case mod_platform::quilt:
      return "quilt";
    case mod_platform::forge:
      return "forge";
    case mod_platform::neoforge:
      return "neoforge";
    case mod_platform::legacy_forge:
      return "legacy_forge";
    case mod_platform::liteloader:
      return "liteloader";
    }
  return "unknown";
}

bool
is_disabled_file (const std::filesystem::path &file) noexcept
{
  auto lower = to_lower (path_to_utf8 (file.filename ()));
  return lower.ends_with (".disabled") || lower.ends_with (".old");
}

bool
is_mod_file (const std::filesystem::path &file) noexcept
{
  auto lower = to_lower (strip_disabled (path_to_utf8 (file.filename ())));
  return lower.ends_with (".jar") || lower.ends_with (".zip")
         || lower.ends_with (".litemod");
}

result<mod_info>
read_mod (const std::filesystem::path &file)
{
  mod_info mod;
  mod.file = file;
  mod.file_name = path_to_utf8 (file.filename ());
  mod.enabled = !is_disabled_file (file);
  auto st = fs::stat (file);
  if (!st.exists)
    return make_error (errc::not_found,
                       sformat ("mod '{}' does not exist", mod.file_name));
  mod.size = st.size;
  mod.modified = st.mtime;
  BLB_TRY_ASSIGN (auto zip, zip_reader::open (file));
  std::unordered_set<std::string> names;
  BLB_TRY (zip.for_each ([&] (const zip_entry &entry, zip_reader &) {
    if (!entry.directory && entry.name.find ('/', 9) == std::string::npos)
      names.insert (entry.name);
    return true;
  }));
  bool primary = false;
  auto load
      = [&] (const char *name, mod_platform platform, auto &&reader) -> void {
    if (!names.contains (name))
      return;
    auto text = zip.read (name);
    if (!text)
      return;
    mod.platforms.push_back (platform);
    if (primary)
      return;
    reader (*text);
    primary = !mod.id.empty () || !mod.name.empty ();
  };
  load ("fabric.mod.json", mod_platform::fabric, [&] (const std::string &t) {
    auto doc = detail::parse_json (t);
    if (doc.is_object ())
      read_fabric (mod, doc);
  });
  load ("quilt.mod.json", mod_platform::quilt, [&] (const std::string &t) {
    auto doc = detail::parse_json (t);
    if (doc.is_object ())
      read_quilt (mod, doc);
  });
  load ("META-INF/neoforge.mods.toml", mod_platform::neoforge,
        [&] (const std::string &t) {
          if (auto doc = detail::parse_toml (t))
            read_forge_toml (mod, *doc, zip);
          else
            mod.problem = doc.err ().message;
        });
  load ("META-INF/mods.toml", mod_platform::forge, [&] (const std::string &t) {
    if (auto doc = detail::parse_toml (t))
      {
        read_forge_toml (mod, *doc, zip);
        for (const auto &d : mod.dependencies)
          if (d.id == "neoforge")
            {
              mod.platforms.back () = mod_platform::neoforge;
              break;
            }
      }
    else
      mod.problem = doc.err ().message;
  });
  load ("mcmod.info", mod_platform::legacy_forge, [&] (const std::string &t) {
    auto doc = detail::parse_json (t);
    if (!doc.is_discarded ())
      read_mcmod (mod, doc);
  });
  load ("litemod.json", mod_platform::liteloader, [&] (const std::string &t) {
    auto doc = detail::parse_json (t);
    if (doc.is_object ())
      read_litemod (mod, doc);
  });
  collect_nested (zip, 0, mod.provides);
  if (mod.name.empty ())
    mod.name = mod.id.empty () ? strip_disabled (mod.file_name) : mod.id;
  if (mod.platforms.empty () && mod.problem.empty ())
    mod.problem = "no mod metadata";
  return mod;
}

std::vector<mod_info>
scan_mods (const std::filesystem::path &directory, thread_pool *pool)
{
  std::vector<std::filesystem::path> files;
  for (const auto &[name, st] : fs::list_directory (directory))
    {
      auto file = directory / path_from_utf8 (name);
      if (!st.is_directory && is_mod_file (file))
        files.push_back (std::move (file));
    }
  std::vector<mod_info> mods (files.size ());
  parallel_for (pool ? *pool : thread_pool::shared (), files.size (),
                [&] (std::size_t i) {
                  auto mod = read_mod (files[i]);
                  if (mod)
                    mods[i] = std::move (*mod);
                  else
                    {
                      mods[i].file = files[i];
                      mods[i].file_name = path_to_utf8 (files[i].filename ());
                      mods[i].enabled = !is_disabled_file (files[i]);
                      mods[i].name = mods[i].file_name;
                      mods[i].problem = mod.err ().message;
                      s_log.debug ("cannot read {}: {}", mods[i].file_name,
                                   mods[i].problem);
                    }
                });
  std::sort (mods.begin (), mods.end (), [] (const auto &a, const auto &b) {
    return to_lower (a.name) < to_lower (b.name);
  });
  return mods;
}

result<std::filesystem::path>
set_mod_enabled (const mod_info &mod, bool enabled)
{
  auto name = path_to_utf8 (mod.file.filename ());
  auto base = strip_disabled (name);
  auto target_name = enabled ? base : base + ".disabled";
  if (target_name == name)
    return mod.file;
  auto target = mod.file.parent_path () / path_from_utf8 (target_name);
  if (fs::exists (target))
    return make_error (errc::already_exists,
                       sformat ("'{}' already exists", target_name));
  BLB_TRY (fs::rename_file (mod.file, target));
  return target;
}

result<std::string>
read_mod_icon (const mod_info &mod)
{
  if (mod.icon.empty ())
    return make_error (errc::not_found, "mod has no icon");
  BLB_TRY_ASSIGN (auto zip, zip_reader::open (mod.file));
  return zip.read (mod.icon);
}

const mod_info *
find_mod (std::span<const mod_info> mods, std::string_view id)
{
  for (const auto &mod : mods)
    {
      if (mod.id == id)
        return &mod;
      for (const auto &p : mod.provides)
        if (p == id)
          return &mod;
    }
  return nullptr;
}

std::vector<mod_issue>
check_dependencies (std::span<const mod_info> mods,
                    std::span<const std::string> builtin_ids)
{
  std::unordered_set<std::string> available (builtin_ids.begin (),
                                             builtin_ids.end ());
  for (auto id :
       { "minecraft", "java", "fabricloader", "fabric-loader", "quilt_loader",
         "forge", "neoforge", "javafml", "lowcodefml", "mixinextras" })
    available.insert (id);
  for (const auto &mod : mods)
    if (mod.enabled)
      {
        available.insert (mod.id);
        for (const auto &p : mod.provides)
          available.insert (p);
      }
  std::vector<mod_issue> issues;
  for (const auto &mod : mods)
    {
      if (!mod.enabled)
        continue;
      for (const auto &d : mod.dependencies)
        if (d.required && d.client && !d.id.empty ()
            && !available.contains (d.id))
          issues.push_back (
              { mod.id, d.id,
                sformat ("{} requires {} {}", mod.name, d.id, d.version) });
    }
  return issues;
}

bool
version_in_range (std::string_view version, std::string_view range)
{
  range = trimmed (range);
  auto current = bare_version (version);
  if (range.empty () || current.empty () || !digit (current.front ())
      || range.find ("${") != std::string_view::npos
      || current.find ("${") != std::string_view::npos)
    return true;
  if (range.front () == '[' || range.front () == '(')
    return maven_holds (current, range);
  std::size_t start = 0;
  while (true)
    {
      auto bar = range.find ("||", start);
      auto alternative = range.substr (start, bar == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : bar - start);
      if (alternative_holds (current, alternative))
        return true;
      if (bar == std::string_view::npos)
        return false;
      start = bar + 2;
    }
}
}
