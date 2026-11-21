#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/json.hpp>
#include <core/base/strings.hpp>
#include <core/installer/task_graph.hpp>
#include <core/instance/instance_manager.hpp>
#include <core/network/http_client.hpp>

#include "core/base/win_util.hpp"
#include "core/meta/json_util.hpp"
#include "core/modpack/modpack_impl.hpp"

#include <algorithm>
#include <limits>
#include <system_error>

namespace b_launcher_base::modpack::detail
{

namespace
{

using meta::detail::json;
using meta::detail::member;
using meta::detail::string_of;
using std::filesystem::path;

constexpr std::size_t metadata_limit = 4u << 20;
constexpr std::string_view metadata_url = "https://meta.prismlauncher.org/v1/";

bool
safe_path (std::string_view name)
{
  for (auto part : split (name, '/'))
    if (!fs::safe_filename (part))
      return false;
  return true;
}

result<path>
source_file (const path &source, std::string_view name)
{
  if (!safe_path (name))
    return make_error (errc::parse, "invalid MMC source path");
  auto file = source;
  for (auto part : split (name, '/'))
    {
      file /= path_from_utf8 (part);
      auto stat = fs::stat (file);
      if (stat.is_link)
        return make_error (errc::unsupported,
                           sformat ("MMC source '{}' is a link", name));
      if (!stat.exists)
        return make_error (errc::not_found,
                           sformat ("MMC source '{}' is missing", name));
    }
  std::error_code ec;
  if (!std::filesystem::is_regular_file (file, ec))
    return make_error (errc::io,
                       sformat ("MMC source '{}' is not a regular file", name));
  return file;
}

result<void>
copy_source_file (const path &source, zip_reader *zip, std::string_view name,
                  const path &target)
{
  if (zip)
    return zip->extract (name, target);
  BLB_TRY_ASSIGN (auto file, source_file (source, name));
  return fs::copy_file (file, target);
}

bool
is_game_path (std::string_view name, std::string_view id)
{
  if (!safe_path (name))
    return false;
  auto top = to_lower (name.substr (0, name.find ('/')));
  return top != instance::settings_file && top != info_file
         && top != meta::instance_libraries && top != "natives"
         && !top.starts_with ("natives-")
         && top != "launch.args" && top != std::string (id) + ".json"
         && top != std::string (id) + ".jar";
}

result<void>
copy_game_folder (const path &source, const path &target,
                  std::string_view id, progress_tracker &progress,
                  cancellation_token token)
{
  if (fs::stat (source).is_link)
    return make_error (errc::unsupported, "MMC game directory is a link");
#if defined(_WIN32)
  const path root = b_launcher_base::detail::native_path (fs::absolute (source), true);
#else
  const auto &root = source;
#endif
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it (root, ec), end;
       !ec && it != end; it.increment (ec))
    {
      BLB_TRY (token.check ());
      const auto &file = it->path ();
      auto name = path_to_utf8 (file.lexically_relative (root));
      if (fs::stat (file).is_link || !is_game_path (name, id))
        {
          it.disable_recursion_pending ();
          continue;
        }
      if (it->is_directory (ec))
        BLB_TRY (fs::create_directories (target / path_from_utf8 (name)));
      else if (!ec && it->is_regular_file (ec))
        {
          BLB_TRY (fs::copy_file (file, target / path_from_utf8 (name)));
          progress.item_done ();
          progress.report ();
        }
      if (ec)
        break;
    }
  if (ec)
    return make_error (errc::io, sformat ("cannot read MMC game files: {}", ec.message ()));
  return ok ();
}

struct component_patch
{
  const multimc_component *component;
  json data;
  std::int64_t order;
};

result<std::vector<component_patch>>
load_patches (const path &source, zip_reader *zip,
              const multimc_profile &profile, cancellation_token token)
{
  std::vector<component_patch> patches;
  bool use_components = false;
  for (const auto &component : profile.components)
    {
      BLB_TRY (token.check ());
      BLB_TRY_ASSIGN (auto text, read_multimc_file (
          source, zip, profile.prefix + "patches/" + component.uid + ".json"));
      if (!text)
        {
          use_components |= !component.standard;
          patches.push_back ({ &component, nullptr, 0 });
          continue;
        }
      use_components = true;
      BLB_TRY_ASSIGN (auto data, meta::detail::parse_json (*text));
      if (!data.is_object ())
        return make_error (errc::parse,
                           sformat ("invalid MMC patch '{}'", component.uid));
      patches.push_back ({ &component, std::move (data), 0 });
    }
  if (!use_components)
    return std::vector<component_patch> {};
  for (auto &patch : patches)
    {
      BLB_TRY (token.check ());
      const auto &component = *patch.component;
      if (patch.data.is_null ())
        {
          if (component.version.empty ())
            return make_error (errc::not_found,
                               sformat ("missing MMC patch '{}'", component.uid));
          net::http_request request;
          request.url = std::string (metadata_url) + url_encode (component.uid)
                        + "/" + url_encode (component.version) + ".json";
          request.max_response_size = metadata_limit;
          BLB_TRY_ASSIGN (auto response, net::http_send (request, token));
          if (!response.ok ())
            return make_error (errc::not_found,
                               sformat ("MMC component '{} {}' is unavailable (HTTP {})",
                                        component.uid, component.version, response.status));
          BLB_TRY_ASSIGN (patch.data, meta::detail::parse_json (response.body));
        }
      if (string_of (patch.data, "uid") != component.uid
          || meta::detail::int_of (patch.data, "formatVersion", 1) != 1)
        return make_error (errc::parse,
                           sformat ("invalid MMC component '{}'", component.uid));
      patch.order = meta::detail::int_of (patch.data, "order");
    }
  std::stable_sort (patches.begin (), patches.end (),
                    [] (const auto &a, const auto &b) { return a.order < b.order; });
  return patches;
}

struct library_file
{
  meta::library library;
  std::string local_name;
};

void
normalize_rules (std::vector<meta::rule> &rules)
{
  std::vector<meta::rule> out;
  for (auto &rule : rules)
    {
      if (rule.os && rule.os->arch.empty ())
        {
          auto &name = rule.os->name;
          auto separator = name.find ('-');
          if (separator != std::string::npos)
            {
              if (auto arch = parse_arch (std::string_view (name).substr (separator + 1)))
                {
                  rule.os->arch = arch_name (*arch);
                  name.resize (separator);
                }
            }
          else if (!name.empty ())
            {
              rule.os->arch = "x86";
              out.push_back (rule);
              rule.os->arch = "x86_64";
            }
        }
      out.push_back (std::move (rule));
    }
  rules = std::move (out);
}

result<library_file>
parse_library (const json &data)
{
  auto lib = meta::detail::library_from_json (data);
  if (lib.artifact.name.empty () || !safe_path (lib.artifact.path ()))
    return make_error (errc::parse, "invalid MMC library coordinate");
  auto alt = string_of (data, "MMC-absoluteUrl");
  auto filename = string_of (data, "MMC-filename");
  bool local = string_of (data, "MMC-hint") == "local";
  if (local && lib.is_legacy_native ())
    return make_error (errc::unsupported, "local MMC native libraries are not supported");
  if (!alt.empty () || !filename.empty () || local)
    {
      if (!lib.download)
        lib.download.emplace ();
      if (!filename.empty ())
        {
          if (!fs::safe_filename (filename))
            return make_error (errc::parse, "invalid MMC library filename");
          auto relative = lib.relative_path ();
          lib.download->path = relative.substr (0, relative.rfind ('/') + 1) + filename;
        }
      if (local)
        {
          lib.url.clear ();
          lib.download->url.clear ();
          if (filename.empty ())
            filename = lib.artifact.file_name ();
        }
      else if (!alt.empty ())
        lib.download->url = std::move (alt);
      else if (lib.download->url.empty ())
        lib.download->url = lib.default_url ();
    }
  if (!safe_path (lib.relative_path ()))
    return make_error (errc::parse, "invalid MMC library path");
  for (const auto &[kind, artifact] : lib.classifiers)
    if (!artifact.path.empty () && !safe_path (artifact.path))
      return make_error (errc::parse, "invalid MMC native path");
  for (const auto &[os, classifier] : lib.natives)
    if (!safe_path (lib.artifact.with_classifier (classifier).path ()))
      return make_error (errc::parse, "invalid MMC native classifier");
  normalize_rules (lib.rules);
  return library_file { std::move (lib), local ? std::move (filename) : std::string () };
}

void
apply_library (std::vector<library_file> &libraries, library_file file)
{
  const auto key = file.library.merge_key ();
  auto existing = std::find_if (libraries.begin (), libraries.end (),
      [&] (const auto &value) { return value.library.merge_key () == key; });
  if (existing == libraries.end ())
    libraries.push_back (std::move (file));
  else if (compare_versions (file.library.artifact.version,
                             existing->library.artifact.version) >= 0)
    {
      if (file.library.rules.empty () && existing->library.rules.empty ())
        *existing = std::move (file);
      else
        libraries.insert (existing, std::move (file));
    }
  else if (!file.library.rules.empty () || !existing->library.rules.empty ())
    libraries.push_back (std::move (file));
}

result<void>
copy_local_library (const path &source, zip_reader *zip, std::string_view prefix,
                    const library_file &lib, const path &target)
{
  auto folder = std::string (prefix) + "libraries/";
  auto found = copy_source_file (source, zip, folder + lib.local_name, target);
  if (!found && found.err ().code == errc::not_found)
    return copy_source_file (source, zip, folder + lib.library.relative_path (), target);
  return found;
}

result<meta::version>
build_version (const path &source, zip_reader *zip, const pack_manifest &manifest,
               const std::vector<component_patch> &patches,
               const meta::version &vanilla, const meta::game_layout &layout,
               const std::string &id, cancellation_token token)
{
  meta::version out;
  out.id = id;
  out.client_version = manifest.minecraft;
  out.isolated_libraries = true;
  out.type = vanilla.type;
  out.release_time = vanilla.release_time;
  // 补丁不能改写共享资源索引。
  out.asset_index = vanilla.asset_index;
  out.assets = vanilla.assets;
  out.java_version = vanilla.java_version;

  std::optional<library_file> main_jar;
  std::vector<library_file> libraries;
  std::vector<std::string> tweakers;
  bool first_thread = false;
  std::vector<int> java_majors;
  std::string java_component;
  for (const auto &patch : patches)
    {
      BLB_TRY (token.check ());
      const auto &data = patch.data;
      for (auto key : { "jarMods", "+jarMods", "mavenFiles", "+agents", "mods",
                        "processors", "-libraries" })
        if (const auto *field = member (data, key); field && !field->empty ())
          return make_error (errc::unsupported,
                             sformat ("MMC component '{}' uses unsupported '{}'",
                                      patch.component->uid, key));

      auto part = meta::detail::version_from_json (data);
      if (!part.main_class.empty ())
        out.main_class = std::move (part.main_class);
      if (!part.minecraft_arguments.empty ())
        {
          out.minecraft_arguments = std::move (part.minecraft_arguments);
          out.game_arguments.clear ();
        }
      for (auto &arg : part.game_arguments)
        out.game_arguments.push_back (std::move (arg));
      for (auto &arg : part.jvm_arguments)
        out.jvm_arguments.push_back (std::move (arg));
      auto extra_jvm = json_strings (data, "+jvmArgs");
      if (!extra_jvm.empty ())
        out.jvm_arguments.push_back ({ std::move (extra_jvm), {} });
      for (auto &tweaker : json_strings (data, "+tweakers"))
        {
          std::erase (tweakers, tweaker);
          tweakers.push_back (std::move (tweaker));
        }
      auto traits = json_strings (data, "+traits");
      first_thread |= std::find (traits.begin (), traits.end (), "FirstThreadOnMacOS")
                      != traits.end ();
      if (const auto *majors = member (data, "compatibleJavaMajors");
          majors && majors->is_array ())
        for (const auto &major : *majors)
          if (major.is_number_integer () && major > 0
              && major <= std::numeric_limits<int>::max ())
            java_majors.push_back (major.get<int> ());
      if (auto component = string_of (data, "compatibleJavaName"); !component.empty ())
        java_component = std::move (component);
      if (part.java_version)
        out.java_version = std::move (part.java_version);
      if (patch.component->uid == "net.minecraft" && part.logging)
        out.logging = vanilla.logging;
      if (const auto *jar = member (data, "mainJar"))
        {
          BLB_TRY_ASSIGN (auto file, parse_library (*jar));
          main_jar = std::move (file);
        }
      for (auto key : { "libraries", "+libraries" })
        if (const auto *items = member (data, key); items && items->is_array ())
          for (const auto &entry : *items)
            {
              BLB_TRY_ASSIGN (auto file, parse_library (entry));
              apply_library (libraries, std::move (file));
            }
    }

  if (out.main_class.empty () || !main_jar)
    return make_error (errc::unsupported, "MMC components have no launch class or client jar");
  if (!java_majors.empty ())
    {
      int preferred = *std::min_element (java_majors.begin (), java_majors.end ());
      out.java_version = meta::java_version_ref {
        std::move (java_component), preferred, std::move (java_majors)
      };
    }
  if (!tweakers.empty ())
    {
      if (out.game_arguments.empty ())
        out.game_arguments.push_back ({ split_arguments (out.minecraft_arguments), {} });
      for (auto &tweaker : tweakers)
        out.game_arguments.push_back ({ { "--tweakClass", std::move (tweaker) }, {} });
    }
  if (first_thread)
    out.jvm_arguments.insert (out.jvm_arguments.begin (),
        { { "-XstartOnFirstThread" }, { { true, meta::os_rule { "osx", {}, {} }, {} } } });
  out.jvm_arguments.insert (out.jvm_arguments.begin (), {
    { "-Djava.library.path=${natives_directory}",
      "-Dminecraft.launcher.brand=${launcher_name}",
      "-Dminecraft.launcher.version=${launcher_version}" }, {}
  });

  const auto &prefix = manifest.multimc->prefix;
  if (!main_jar->local_name.empty ())
    BLB_TRY (copy_local_library (source, zip, prefix, *main_jar, layout.version_jar (id)));
  else
    {
      auto client = main_jar->library.download.value_or (meta::download_artifact {});
      if (client.url.empty ())
        client.url = main_jar->library.default_url ();
      client.path.clear ();
      out.downloads.emplace ("client", std::move (client));
    }
  const auto library_dir = out.library_directory (layout);
  for (auto &file : libraries)
    {
      BLB_TRY (token.check ());
      if (!file.local_name.empty ())
        BLB_TRY (copy_local_library (source, zip, prefix, file,
                                    library_dir / path_from_utf8 (file.library.relative_path ())));
      out.libraries.push_back (std::move (file.library));
    }
  return out;
}

}

result<std::optional<std::string>>
read_multimc_file (const path &source, zip_reader *zip, std::string_view name)
{
  if (zip)
    {
      auto data = zip->read (name, metadata_limit);
      if (!data && data.err ().code == errc::not_found)
        return std::nullopt;
      if (!data)
        return std::move (data).err ();
      return std::move (*data);
    }
  auto file = source_file (source, name);
  if (!file && file.err ().code == errc::not_found)
    return std::nullopt;
  if (!file)
    return std::move (file).err ();
  if (fs::stat (*file).size > metadata_limit)
    return make_error (errc::size_mismatch, "MMC metadata file is too large");
  BLB_TRY_ASSIGN (auto data, fs::read_file (*file));
  return std::move (data);
}

result<void>
copy_multimc_game (const path &source, const pack_manifest &manifest,
                   const meta::game_layout &layout, const std::string &id,
                   progress_sink *sink, cancellation_token token)
{
  if (manifest.overrides.empty ())
    return ok ();
  const auto &prefix = manifest.overrides.front ();
  const auto target = layout.version_dir (id);
  const auto lower_id = to_lower (id);
  progress_tracker progress (sink, "copy:instance-files");
  if (fs::stat (source).is_directory)
    BLB_TRY (copy_game_folder (source / path_from_utf8 (prefix.substr (0, prefix.size () - 1)),
                              target, lower_id, progress, token));
  else
    {
      BLB_TRY_ASSIGN (auto zip, zip_reader::open (source));
      std::optional<error> failure;
      BLB_TRY (zip.for_each ([&] (const zip_entry &entry, zip_reader &reader) {
        if (token.is_cancelled ())
          return false;
        auto name = replace_all (entry.name, "\\", "/");
        if (!name.starts_with (prefix) || entry.directory || name.ends_with ('/'))
          return true;
        auto relative = std::string_view (name).substr (prefix.size ());
        if (!is_game_path (relative, lower_id))
          return true;
        if (auto extracted = reader.extract_current (target / path_from_utf8 (relative));
            !extracted)
          {
            failure = std::move (extracted).err ();
            return false;
          }
        progress.item_done ();
        progress.report ();
        return true;
      }));
      if (failure)
        return *failure;
    }
  BLB_TRY (token.check ());
  progress.finish ();
  return ok ();
}

result<void>
install_multimc (install::game_installer &installer, const path &source,
                 const pack_manifest &manifest, const std::string &id,
                 progress_sink *sink, cancellation_token token)
{
  std::optional<zip_reader> archive;
  if (!fs::stat (source).is_directory)
    {
      BLB_TRY_ASSIGN (auto zip, zip_reader::open (source));
      archive.emplace (std::move (zip));
    }
  auto *zip = archive ? &*archive : nullptr;
  BLB_TRY_ASSIGN (auto patches, load_patches (source, zip, *manifest.multimc, token));
  const auto &layout = installer.layout ();
  if (patches.empty ())
    {
      install::loader_installer loaders (installer);
      BLB_TRY (loaders.install ({ manifest.minecraft, manifest.loaders,
                                 id, layout.version_dir (id) / "mods" }, sink, token));
      return ok ();
    }

  meta::game_layout base_layout (layout.root (), layout.cache () / "vanilla");
  install::game_installer base_installer (base_layout, installer.options ());
  BLB_TRY_ASSIGN (auto vanilla, base_installer.fetch_version_json (manifest.minecraft, token));
  BLB_TRY_ASSIGN (auto version, build_version (source, zip, manifest, patches,
                                              vanilla, layout, id, token));
  auto context = meta::rule_context::current ();
  install::task_graph graph;
  auto complete = [&] (install::completion_scope scope, cancellation_token t) -> result<void> {
    BLB_TRY (installer.complete_version (version, context, sink, t, true, scope));
    return ok ();
  };
  graph.add ("vanilla:core", [&] (cancellation_token t) {
    return complete (install::completion_scope::core, t);
  });
  graph.add ("vanilla:assets", [&] (cancellation_token t) {
    return complete (install::completion_scope::assets, t);
  });
  BLB_TRY (graph.run (token, sink));
  BLB_TRY (token.check ());
  return meta::save_version (layout, version);
}

}
