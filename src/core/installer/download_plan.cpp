#include <core/base/strings.hpp>
#include <core/installer/download_plan.hpp>

#include <unordered_set>

namespace b_launcher_base::install
{

namespace
{

std::optional<sha1_digest>
digest_of (std::string_view hex)
{
  if (hex.empty ())
    return std::nullopt;
  return parse_sha1 (hex);
}

void
add_file (std::vector<planned_file> &out, file_kind kind,
          const std::filesystem::path &target, std::string relative,
          std::string_view url, std::string_view sha1, std::int64_t size,
          const net::mirror_provider &mirror)
{
  planned_file file;
  file.kind = kind;
  file.target = target;
  file.relative = std::move (relative);
  if (!url.empty ())
    file.urls = mirror.resolve (url);
  file.sha1 = digest_of (sha1);
  file.size = size;
  out.push_back (std::move (file));
}

std::string
library_url (const meta::library &lib, const meta::maven_artifact &artifact)
{
  std::string base
      = lib.url.empty () ? "https://libraries.minecraft.net/" : lib.url;
  if (!base.ends_with ('/'))
    base += '/';
  return base + artifact.path ();
}

}

bool
native_matches_arch (const meta::library &lib,
                     const meta::rule_context &context)
{
  if (!lib.is_modern_native ())
    return true;
  const auto &classifier = lib.artifact.classifier;
  cpu_arch required = cpu_arch::x86_64;
  if (classifier.ends_with ("-x86"))
    required = cpu_arch::x86;
  else if (classifier.ends_with ("-arm64")
           || classifier.ends_with ("-aarch64"))
    required = cpu_arch::arm64;
  else if (classifier.ends_with ("-arm32") || classifier.ends_with ("-arm"))
    required = cpu_arch::arm32;
  return required == context.arch;
}

std::vector<const meta::library *>
applicable_libraries (const meta::version &resolved,
                      const meta::rule_context &context)
{
  std::vector<const meta::library *> out;
  std::unordered_set<std::string> seen;
  for (const auto &lib : resolved.libraries)
    {
      if (lib.name.empty () || lib.artifact.name.empty ())
        continue;
      if (!meta::rules_allow (lib.rules, context))
        continue;
      if (lib.client_required && !*lib.client_required)
        continue;
      if (!native_matches_arch (lib, context))
        continue;
      if (lib.is_legacy_native () && !lib.native_classifier (context))
        continue;
      auto key = lib.artifact.key ();
      if (lib.is_legacy_native ())
        key += ":natives";
      if (!seen.insert (key).second)
        continue;
      out.push_back (&lib);
    }
  return out;
}

std::vector<planned_file>
plan_version_files (const meta::version &resolved,
                    const meta::game_layout &layout,
                    const net::mirror_provider &mirror,
                    const meta::rule_context &context,
                    const plan_options &options)
{
  std::vector<planned_file> out;
  if (options.client)
    if (const auto *client = resolved.client_download ())
      {
        auto jar = resolved.jar_id ();
        add_file (out, file_kind::client, layout.version_jar (jar),
                  "versions/" + jar + "/" + jar + ".jar", client->url,
                  client->sha1, client->size, mirror);
      }
  std::unordered_set<std::string> planned_paths;
  for (const auto &entry : resolved.libraries)
    {
      const auto *lib = &entry;
      if (lib->name.empty () || lib->artifact.name.empty ()
          || !meta::rules_allow (lib->rules, context)
          || (lib->client_required && !*lib->client_required)
          || !native_matches_arch (*lib, context)
          || (lib->is_legacy_native () && !lib->native_classifier (context)))
        continue;
      if (!planned_paths
               .insert (lib->relative_path ()
                        + (lib->is_legacy_native () ? "#n" : ""))
               .second)
        continue;
      bool legacy_native = lib->is_legacy_native ();
      bool has_artifact = lib->download.has_value ();
      bool generated
          = has_artifact && lib->download->url.empty () && lib->url.empty ();
      bool natives_only = legacy_native && !has_artifact;
      if (options.libraries && !generated && !natives_only)
        {
          auto relative = lib->relative_path ();
          auto url = has_artifact && !lib->download->url.empty ()
                         ? lib->download->url
                         : library_url (*lib, lib->artifact);
          std::string sha1 = has_artifact ? lib->download->sha1 : lib->sha1;
          auto size = has_artifact ? lib->download->size : lib->size;
          add_file (out,
                    lib->is_modern_native () ? file_kind::native
                                             : file_kind::library,
                    layout.library (relative), "libraries/" + relative, url,
                    sha1, size, mirror);
        }
      if (options.natives && legacy_native)
        {
          auto classifier = *lib->native_classifier (context);
          auto it = lib->classifiers.find (classifier);
          if (it != lib->classifiers.end ())
            {
              auto relative
                  = it->second.path.empty ()
                        ? lib->artifact.with_classifier (classifier).path ()
                        : it->second.path;
              add_file (out, file_kind::native, layout.library (relative),
                        "libraries/" + relative, it->second.url,
                        it->second.sha1, it->second.size, mirror);
            }
          else
            {
              auto artifact = lib->artifact.with_classifier (classifier);
              auto relative = artifact.path ();
              add_file (out, file_kind::native, layout.library (relative),
                        "libraries/" + relative, library_url (*lib, artifact),
                        {}, -1, mirror);
            }
        }
    }
  if (options.asset_index && resolved.asset_index)
    {
      const auto &index = *resolved.asset_index;
      add_file (out, file_kind::asset_index, layout.asset_index (index.id),
                "assets/indexes/" + index.id + ".json", index.url, index.sha1,
                index.size, mirror);
    }
  if (options.logging && resolved.logging
      && !resolved.logging->file_id.empty ())
    {
      const auto &file = resolved.logging->file;
      add_file (out, file_kind::logging,
                layout.logging_config (resolved.logging->file_id),
                "assets/log_configs/" + resolved.logging->file_id, file.url,
                file.sha1, file.size, mirror);
    }
  std::erase_if (out, [] (const planned_file &f) { return f.urls.empty (); });
  return out;
}

std::vector<planned_file>
plan_asset_files (const meta::asset_index &index,
                  const meta::game_layout &layout,
                  const net::mirror_provider &mirror)
{
  std::vector<planned_file> out;
  out.reserve (index.objects.size ());
  std::unordered_set<std::string> seen;
  for (const auto &object : index.objects)
    {
      auto relative = meta::asset_object_path (object.hash);
      if (!seen.insert (relative).second)
        continue;
      planned_file file;
      file.kind = file_kind::asset;
      file.target = layout.asset_objects () / path_from_utf8 (relative);
      file.urls = mirror.resolve (meta::asset_object_url (object.hash));
      file.sha1 = object.hash;
      file.size = static_cast<std::int64_t> (object.size);
      file.relative = "assets/objects/" + relative;
      out.push_back (std::move (file));
    }
  return out;
}

std::vector<net::download_task>
to_download_tasks (std::span<const planned_file> files)
{
  std::vector<net::download_task> out;
  out.reserve (files.size ());
  for (const auto &file : files)
    out.push_back (
        { file.urls, file.target, file.sha1, file.size, file.executable });
  return out;
}

std::uint64_t
total_size (std::span<const planned_file> files)
{
  std::uint64_t sum = 0;
  for (const auto &file : files)
    if (file.size > 0)
      sum += static_cast<std::uint64_t> (file.size);
  return sum;
}

}
