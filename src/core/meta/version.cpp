#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/meta/maven.hpp>
#include <core/meta/version.hpp>

#include "core/meta/json_util.hpp"

#include <set>
#include <unordered_set>

namespace b_launcher_base::meta
{

namespace detail
{

const json *
member (const json &node, std::string_view key)
{
  if (!node.is_object ())
    return nullptr;
  auto it = node.find (key);
  if (it == node.end () || it->is_null ())
    return nullptr;
  return &*it;
}

std::string
string_of (const json &node, std::string_view key, std::string fallback)
{
  auto *value = member (node, key);
  if (!value)
    return fallback;
  if (value->is_string ())
    return value->get<std::string> ();
  if (value->is_number_integer ())
    return std::to_string (value->get<std::int64_t> ());
  return fallback;
}

std::int64_t
int_of (const json &node, std::string_view key, std::int64_t fallback)
{
  auto *value = member (node, key);
  if (!value)
    return fallback;
  if (value->is_number ())
    return value->get<std::int64_t> ();
  if (value->is_string ())
    if (auto parsed = parse_int (value->get<std::string> ()))
      return *parsed;
  return fallback;
}

bool
bool_of (const json &node, std::string_view key, bool fallback)
{
  auto *value = member (node, key);
  if (!value || !value->is_boolean ())
    return fallback;
  return value->get<bool> ();
}

result<json>
parse_json (std::string_view text)
{
  auto parsed = json::parse (text, nullptr, false, true);
  if (parsed.is_discarded ())
    return make_error (errc::parse, "invalid json");
  return parsed;
}

rule
rule_from_json (const json &node)
{
  rule out;
  out.allow = string_of (node, "action", "allow") == "allow";
  if (auto *os = member (node, "os"); os && os->is_object ())
    out.os = os_rule { string_of (*os, "name"), string_of (*os, "version"),
                       string_of (*os, "arch") };
  if (auto *features = member (node, "features");
      features && features->is_object ())
    for (auto it = features->begin (); it != features->end (); ++it)
      if (it->is_boolean ())
        out.features.emplace_back (it.key (), it->get<bool> ());
  return out;
}

json
rule_to_json (const rule &value)
{
  json out = json::object ();
  out["action"] = value.allow ? "allow" : "disallow";
  if (value.os)
    {
      json os = json::object ();
      if (!value.os->name.empty ())
        os["name"] = value.os->name;
      if (!value.os->version.empty ())
        os["version"] = value.os->version;
      if (!value.os->arch.empty ())
        os["arch"] = value.os->arch;
      out["os"] = std::move (os);
    }
  if (!value.features.empty ())
    {
      json features = json::object ();
      for (const auto &[name, enabled] : value.features)
        features[name] = enabled;
      out["features"] = std::move (features);
    }
  return out;
}

std::vector<rule>
rules_from_json (const json *node)
{
  std::vector<rule> out;
  if (node && node->is_array ())
    for (const auto &entry : *node)
      if (entry.is_object ())
        out.push_back (rule_from_json (entry));
  return out;
}

download_artifact
artifact_from_json (const json &node)
{
  download_artifact out;
  out.path = string_of (node, "path");
  out.url = string_of (node, "url");
  out.sha1 = string_of (node, "sha1");
  out.size = int_of (node, "size", -1);
  return out;
}

json
artifact_to_json (const download_artifact &value, bool with_path)
{
  json out = json::object ();
  if (with_path && !value.path.empty ())
    out["path"] = value.path;
  if (!value.sha1.empty ())
    out["sha1"] = value.sha1;
  if (value.size >= 0)
    out["size"] = value.size;
  out["url"] = value.url;
  return out;
}

library
library_from_json (const json &node)
{
  library out;
  out.name = string_of (node, "name");
  if (auto parsed = maven_artifact::parse (out.name))
    out.artifact = *parsed;
  out.url = string_of (node, "url");
  out.sha1 = string_of (node, "sha1");
  out.size = int_of (node, "size", -1);
  if (auto *downloads = member (node, "downloads"))
    {
      if (auto *artifact = member (*downloads, "artifact");
          artifact && artifact->is_object ())
        out.download = artifact_from_json (*artifact);
      if (auto *classifiers = member (*downloads, "classifiers");
          classifiers && classifiers->is_object ())
        for (auto it = classifiers->begin (); it != classifiers->end (); ++it)
          if (it->is_object ())
            out.classifiers.emplace (it.key (), artifact_from_json (*it));
    }
  if (auto *natives = member (node, "natives");
      natives && natives->is_object ())
    for (auto it = natives->begin (); it != natives->end (); ++it)
      if (it->is_string ())
        out.natives.emplace (it.key (), it->get<std::string> ());
  if (auto *extract = member (node, "extract"))
    if (auto *exclude = member (*extract, "exclude");
        exclude && exclude->is_array ())
      for (const auto &entry : *exclude)
        if (entry.is_string ())
          out.extract_exclude.push_back (entry.get<std::string> ());
  out.rules = rules_from_json (member (node, "rules"));
  if (auto *checksums = member (node, "checksums");
      checksums && checksums->is_array ())
    for (const auto &entry : *checksums)
      if (entry.is_string ())
        out.checksums.push_back (entry.get<std::string> ());
  if (auto *clientreq = member (node, "clientreq");
      clientreq && clientreq->is_boolean ())
    out.client_required = clientreq->get<bool> ();
  return out;
}

json
library_to_json (const library &value)
{
  json out = json::object ();
  out["name"] = value.name;
  if (value.download || !value.classifiers.empty ())
    {
      json downloads = json::object ();
      if (value.download)
        downloads["artifact"] = artifact_to_json (*value.download, true);
      if (!value.classifiers.empty ())
        {
          json classifiers = json::object ();
          for (const auto &[key, artifact] : value.classifiers)
            classifiers[key] = artifact_to_json (artifact, true);
          downloads["classifiers"] = std::move (classifiers);
        }
      out["downloads"] = std::move (downloads);
    }
  if (!value.url.empty ())
    out["url"] = value.url;
  if (!value.sha1.empty ())
    out["sha1"] = value.sha1;
  if (value.size >= 0)
    out["size"] = value.size;
  if (!value.natives.empty ())
    {
      json natives = json::object ();
      for (const auto &[os, classifier] : value.natives)
        natives[os] = classifier;
      out["natives"] = std::move (natives);
    }
  if (!value.extract_exclude.empty ())
    out["extract"] = json { { "exclude", value.extract_exclude } };
  if (!value.rules.empty ())
    {
      json rules = json::array ();
      for (const auto &r : value.rules)
        rules.push_back (rule_to_json (r));
      out["rules"] = std::move (rules);
    }
  if (!value.checksums.empty ())
    out["checksums"] = value.checksums;
  if (value.client_required)
    out["clientreq"] = *value.client_required;
  return out;
}

namespace
{

std::vector<argument>
arguments_from_json (const json *node)
{
  std::vector<argument> out;
  if (!node || !node->is_array ())
    return out;
  for (const auto &entry : *node)
    {
      if (entry.is_string ())
        {
          out.push_back ({ { entry.get<std::string> () }, {} });
          continue;
        }
      if (!entry.is_object ())
        continue;
      argument arg;
      arg.rules = rules_from_json (member (entry, "rules"));
      if (auto *value = member (entry, "value"))
        {
          if (value->is_string ())
            arg.values.push_back (value->get<std::string> ());
          else if (value->is_array ())
            for (const auto &v : *value)
              if (v.is_string ())
                arg.values.push_back (v.get<std::string> ());
        }
      out.push_back (std::move (arg));
    }
  return out;
}

json
arguments_to_json (const std::vector<argument> &args)
{
  json out = json::array ();
  for (const auto &arg : args)
    {
      if (arg.rules.empty () && arg.values.size () == 1)
        {
          out.push_back (arg.values.front ());
          continue;
        }
      json entry = json::object ();
      json rules = json::array ();
      for (const auto &r : arg.rules)
        rules.push_back (rule_to_json (r));
      entry["rules"] = std::move (rules);
      if (arg.values.size () == 1)
        entry["value"] = arg.values.front ();
      else
        entry["value"] = arg.values;
      out.push_back (std::move (entry));
    }
  return out;
}

}

version
version_from_json (const json &node)
{
  version out;
  out.id = string_of (node, "id");
  out.inherits_from = string_of (node, "inheritsFrom");
  out.type = string_of (node, "type");
  out.main_class = string_of (node, "mainClass");
  out.minecraft_arguments = string_of (node, "minecraftArguments");
  out.assets = string_of (node, "assets");
  out.jar = string_of (node, "jar");
  out.release_time = string_of (node, "releaseTime");
  out.time = string_of (node, "time");
  out.client_version = string_of (node, "clientVersion");
  if (out.client_version.empty ())
    if (auto *patches = member (node, "patches");
        patches && patches->is_array ())
      for (const auto &patch : *patches)
        if (patch.is_object () && string_of (patch, "id") == "game")
          out.client_version = string_of (patch, "version");
  out.compliance_level = static_cast<int> (int_of (node, "complianceLevel"));
  out.minimum_launcher_version
      = static_cast<int> (int_of (node, "minimumLauncherVersion"));
  if (auto *args = member (node, "arguments"); args && args->is_object ())
    {
      out.has_arguments = true;
      out.game_arguments = arguments_from_json (member (*args, "game"));
      out.jvm_arguments = arguments_from_json (member (*args, "jvm"));
    }
  if (auto *libs = member (node, "libraries"); libs && libs->is_array ())
    for (const auto &entry : *libs)
      if (entry.is_object ())
        out.libraries.push_back (library_from_json (entry));
  if (auto *index = member (node, "assetIndex"); index && index->is_object ())
    out.asset_index = asset_index_ref { string_of (*index, "id"),
                                        string_of (*index, "sha1"),
                                        string_of (*index, "url"),
                                        int_of (*index, "size", -1),
                                        int_of (*index, "totalSize", -1) };
  if (auto *downloads = member (node, "downloads");
      downloads && downloads->is_object ())
    for (auto it = downloads->begin (); it != downloads->end (); ++it)
      if (it->is_object ())
        out.downloads.emplace (it.key (), artifact_from_json (*it));
  if (auto *java = member (node, "javaVersion"); java && java->is_object ())
    out.java_version = java_version_ref { string_of (*java, "component"),
                                          static_cast<int> (int_of (
                                              *java, "majorVersion")) };
  if (auto *logging = member (node, "logging"))
    if (auto *client = member (*logging, "client");
        client && client->is_object ())
      {
        logging_ref ref;
        ref.argument = string_of (*client, "argument");
        ref.type = string_of (*client, "type");
        if (auto *file = member (*client, "file"); file && file->is_object ())
          {
            ref.file = artifact_from_json (*file);
            ref.file_id = string_of (*file, "id");
          }
        out.logging = std::move (ref);
      }
  return out;
}

json
version_to_json (const version &value)
{
  json out = json::object ();
  out["id"] = value.id;
  if (!value.inherits_from.empty ())
    out["inheritsFrom"] = value.inherits_from;
  if (!value.type.empty ())
    out["type"] = value.type;
  if (!value.time.empty ())
    out["time"] = value.time;
  if (!value.release_time.empty ())
    out["releaseTime"] = value.release_time;
  if (!value.main_class.empty ())
    out["mainClass"] = value.main_class;
  if (!value.minecraft_arguments.empty ())
    out["minecraftArguments"] = value.minecraft_arguments;
  if (value.has_arguments || !value.game_arguments.empty ()
      || !value.jvm_arguments.empty ())
    out["arguments"]
        = json { { "game", arguments_to_json (value.game_arguments) },
                 { "jvm", arguments_to_json (value.jvm_arguments) } };
  if (!value.assets.empty ())
    out["assets"] = value.assets;
  if (!value.jar.empty ())
    out["jar"] = value.jar;
  if (!value.client_version.empty ())
    out["clientVersion"] = value.client_version;
  if (value.asset_index)
    {
      json index = json::object ();
      index["id"] = value.asset_index->id;
      if (!value.asset_index->sha1.empty ())
        index["sha1"] = value.asset_index->sha1;
      if (value.asset_index->size >= 0)
        index["size"] = value.asset_index->size;
      if (value.asset_index->total_size >= 0)
        index["totalSize"] = value.asset_index->total_size;
      index["url"] = value.asset_index->url;
      out["assetIndex"] = std::move (index);
    }
  if (!value.downloads.empty ())
    {
      json downloads = json::object ();
      for (const auto &[key, artifact] : value.downloads)
        downloads[key] = artifact_to_json (artifact, false);
      out["downloads"] = std::move (downloads);
    }
  if (value.java_version)
    {
      json java = json::object ();
      if (!value.java_version->component.empty ())
        java["component"] = value.java_version->component;
      java["majorVersion"] = value.java_version->major_version;
      out["javaVersion"] = std::move (java);
    }
  if (value.logging)
    {
      json file = artifact_to_json (value.logging->file, false);
      file["id"] = value.logging->file_id;
      out["logging"] = json { { "client",
                                { { "argument", value.logging->argument },
                                  { "file", std::move (file) },
                                  { "type", value.logging->type } } } };
    }
  if (value.compliance_level)
    out["complianceLevel"] = value.compliance_level;
  if (value.minimum_launcher_version)
    out["minimumLauncherVersion"] = value.minimum_launcher_version;
  json libs = json::array ();
  for (const auto &lib : value.libraries)
    libs.push_back (library_to_json (lib));
  out["libraries"] = std::move (libs);
  return out;
}

}

bool
library::is_legacy_native () const noexcept
{
  return !natives.empty ();
}

bool
library::is_modern_native () const noexcept
{
  return artifact.classifier.starts_with ("natives-");
}

std::optional<std::string>
library::native_classifier (const rule_context &context) const
{
  auto it = natives.find (std::string (os_name (context.os)));
  if (it == natives.end () && context.os == os_type::osx)
    it = natives.find ("macos");
  if (it == natives.end ())
    return std::nullopt;
  return replace_all (it->second, "${arch}",
                      arch_bits (context.arch) == 32 ? "32" : "64");
}

std::string
library::relative_path () const
{
  if (download && !download->path.empty ())
    return download->path;
  return artifact.path ();
}

std::string
library::default_url () const
{
  if (download && !download->url.empty ())
    return download->url;
  std::string base = url.empty () ? "https://libraries.minecraft.net/" : url;
  if (!base.ends_with ('/'))
    base += '/';
  return base + artifact.path ();
}

std::string
version::jar_id () const
{
  return jar.empty () ? id : jar;
}

namespace
{

std::string
minecraft_from_loader (const version &value)
{
  std::vector<std::string> tokens;
  for (const auto &arg : value.game_arguments)
    tokens.insert (tokens.end (), arg.values.begin (), arg.values.end ());
  for (auto token : split (value.minecraft_arguments, ' '))
    if (!token.empty ())
      tokens.emplace_back (token);
  for (std::size_t i = 0; i + 1 < tokens.size (); ++i)
    if (tokens[i] == "--fml.mcVersion" && !tokens[i + 1].empty ())
      return tokens[i + 1];

  for (const auto &lib : value.libraries)
    {
      auto parsed = maven_artifact::parse (lib.name);
      if (!parsed)
        continue;
      const auto &group = parsed->group;
      const auto &name = parsed->name;
      std::string_view ver = parsed->version;
      if ((group == "net.fabricmc" || group == "net.legacyfabric")
          && name == "intermediary")
        return std::string (ver);
      if (group == "org.quiltmc" && name == "hashed")
        return std::string (ver);
      if (group == "net.minecraftforge"
          && (name == "forge" || name == "fmlloader"))
        return std::string (ver.substr (0, ver.find ('-')));
      if (group == "com.mumfrey" && name == "liteloader")
        return std::string (ver.substr (0, ver.find ("-SNAPSHOT")));
      if (group == "optifine" && name == "OptiFine")
        return std::string (ver.substr (0, ver.find ("_HD")));
    }
  return {};
}

}

std::string
version::minecraft () const
{
  if (!client_version.empty ())
    return client_version;
  if (chain.size () > 1)
    return chain.front ();
  if (auto found = minecraft_from_loader (*this); !found.empty ())
    return found;
  return chain.empty () ? id : chain.front ();
}

std::string
version::asset_id () const
{
  if (asset_index && !asset_index->id.empty ())
    return asset_index->id;
  return assets.empty () ? "legacy" : assets;
}

const download_artifact *
version::client_download () const
{
  auto it = downloads.find ("client");
  return it == downloads.end () ? nullptr : &it->second;
}

result<version>
parse_version (std::string_view json_text)
{
  BLB_TRY_ASSIGN (auto node, detail::parse_json (json_text));
  if (!node.is_object ())
    return make_error (errc::parse, "version json is not an object");
  auto out = detail::version_from_json (node);
  if (out.id.empty ())
    return make_error (errc::parse, "version json has no id");
  return out;
}

result<library>
parse_library (std::string_view json_text)
{
  BLB_TRY_ASSIGN (auto node, detail::parse_json (json_text));
  if (!node.is_object ())
    return make_error (errc::parse, "library json is not an object");
  return detail::library_from_json (node);
}

std::string
serialize_version (const version &value)
{
  return detail::version_to_json (value).dump (2);
}

version
merge_versions (const version &parent, const version &child)
{
  version out = child;
  auto inherit = [] (std::string &target, const std::string &source) {
    if (target.empty ())
      target = source;
  };
  inherit (out.type, parent.type);
  inherit (out.main_class, parent.main_class);
  inherit (out.minecraft_arguments, parent.minecraft_arguments);
  inherit (out.assets, parent.assets);
  inherit (out.release_time, parent.release_time);
  inherit (out.time, parent.time);
  inherit (out.client_version, parent.client_version);
  if (out.jar.empty ())
    out.jar = parent.jar_id ();
  out.has_arguments = parent.has_arguments || child.has_arguments;
  out.game_arguments = parent.game_arguments;
  out.game_arguments.insert (out.game_arguments.end (),
                             child.game_arguments.begin (),
                             child.game_arguments.end ());
  out.jvm_arguments = parent.jvm_arguments;
  out.jvm_arguments.insert (out.jvm_arguments.end (),
                            child.jvm_arguments.begin (),
                            child.jvm_arguments.end ());
  auto library_key = [] (const library &lib) {
    auto key = lib.artifact.key ();
    if (lib.is_legacy_native ())
      key += ":natives";
    return key;
  };
  std::unordered_set<std::string> overridden;
  for (const auto &lib : child.libraries)
    overridden.insert (library_key (lib));
  for (const auto &lib : parent.libraries)
    if (!overridden.contains (library_key (lib)))
      out.libraries.push_back (lib);
  if (!out.asset_index)
    out.asset_index = parent.asset_index;
  for (const auto &[key, artifact] : parent.downloads)
    out.downloads.emplace (key, artifact);
  if (!out.java_version)
    out.java_version = parent.java_version;
  if (!out.logging)
    out.logging = parent.logging;
  if (!out.compliance_level)
    out.compliance_level = parent.compliance_level;
  if (!out.minimum_launcher_version)
    out.minimum_launcher_version = parent.minimum_launcher_version;
  out.inherits_from.clear ();
  out.chain = parent.chain;
  out.chain.push_back (child.id);
  return out;
}

result<version>
load_version (const game_layout &layout, std::string_view id)
{
  auto file = layout.version_json (id);
  auto text = fs::read_file (file);
  if (!text)
    {
      if (text.err ().code == errc::not_found)
        return make_error (errc::version_not_found,
                           sformat ("version '{}' is not installed", id));
      return std::move (text).err ();
    }
  auto parsed = parse_version (*text);
  if (!parsed)
    return make_error (errc::parse, sformat ("cannot parse version '{}': {}",
                                             id, parsed.err ().message));
  if (parsed->id != id)
    parsed->id = std::string (id);
  return parsed;
}

result<version>
resolve_version (const game_layout &layout, std::string_view id)
{
  BLB_TRY_ASSIGN (auto current, load_version (layout, id));
  std::set<std::string> visited { current.id };
  std::vector<version> stack;
  while (!current.inherits_from.empty ())
    {
      auto parent_id = current.inherits_from;
      if (!visited.insert (parent_id).second)
        return make_error (errc::parse,
                           sformat ("inheritance cycle at '{}'", parent_id));
      stack.push_back (std::move (current));
      BLB_TRY_ASSIGN (current, load_version (layout, parent_id));
    }
  current.chain = { current.id };
  while (!stack.empty ())
    {
      current = merge_versions (current, stack.back ());
      stack.pop_back ();
    }
  return current;
}

result<void>
save_version (const game_layout &layout, const version &value)
{
  return fs::write_file_atomic (layout.version_json (value.id),
                                serialize_version (value));
}

std::vector<std::string>
split_legacy_arguments (std::string_view text)
{
  std::vector<std::string> out;
  std::string current;
  for (char c : text)
    {
      if (c == ' ' || c == '\t')
        {
          if (!current.empty ())
            out.push_back (std::move (current));
          current.clear ();
        }
      else
        current.push_back (c);
    }
  if (!current.empty ())
    out.push_back (std::move (current));
  return out;
}

}
