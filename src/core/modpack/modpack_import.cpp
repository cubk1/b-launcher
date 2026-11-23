#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/installer/task_graph.hpp>
#include <core/instance/instance_manager.hpp>
#include <core/modpack/modpack.hpp>

#include "core/modpack/modpack_impl.hpp"

namespace b_launcher_base::modpack
{

namespace
{

using json = nlohmann::json;

constexpr log::logger s_log ("modpack");

struct uncommitted_instance
{
  std::filesystem::path directory;
  ~uncommitted_instance ()
  {
    if (!directory.empty ())
      (void)fs::remove_all (directory);
  }
};

std::string
sanitize_id (std::string_view name)
{
  std::string out = fs::filter_filename (name, '_');
  auto view = trim (out);
  out = std::string (view);
  while (!out.empty () && (out.back () == '.' || out.back () == ' '))
    out.pop_back ();
  return out.empty () ? std::string ("modpack") : out;
}

result<void>
extract_overrides (const std::filesystem::path &archive,
                   const pack_manifest &manifest,
                   const std::filesystem::path &game_dir,
                   cancellation_token token)
{
  BLB_TRY_ASSIGN (auto zip, zip_reader::open (archive));
  std::optional<error> failure;
  std::size_t count = 0;
  // Apply each layer completely before the next, regardless of ZIP order.
  for (const auto &prefix : manifest.overrides)
    {
      BLB_TRY (token.check ());
      BLB_TRY (zip.for_each ([&] (const zip_entry &entry, zip_reader &reader) {
        if (token.is_cancelled ())
          {
            failure = make_error (errc::cancelled, "cancelled");
            return false;
          }
        auto name = replace_all (entry.name, "\\", "/");
        if (entry.directory || name.ends_with ('/') || !name.starts_with (prefix))
          return true;
        auto relative = name.substr (prefix.size ());
        if (relative.empty () || !fs::safe_relative (relative))
          return true;
        auto target = game_dir / path_from_utf8 (relative);
        if (auto r = reader.extract_current (target); !r)
          {
            failure = std::move (r).err ();
            return false;
          }
        ++count;
        return true;
      }));
      if (failure)
        return *failure;
    }
  s_log.info ("extracted {} override files", count);
  return ok ();
}

}

result<std::string>
import_modpack (install::game_installer &installer,
                const std::filesystem::path &source,
                const import_options &options, progress_sink *sink,
                cancellation_token token)
{
  auto started = std::chrono::steady_clock::now ();
  BLB_TRY (token.check ());
  BLB_TRY_ASSIGN (auto manifest, read_modpack (source));
  const auto &layout = installer.layout ();
  auto id = options.id.empty () ? sanitize_id (manifest.name) : options.id;
  if (!fs::safe_component (id))
    return make_error (errc::invalid_argument, "invalid instance id");
  auto game_dir = layout.version_dir (id);
  uncommitted_instance cleanup;
  if (manifest.multimc)
    {
      if (!fs::safe_filename (id) || id.size () > 128)
        return make_error (errc::invalid_argument, "invalid MMC instance id");
      if (fs::stat (source).is_directory)
        {
          std::error_code ec;
          auto target = std::filesystem::weakly_canonical (game_dir, ec);
          if (ec)
            return make_error (errc::io,
                               "cannot resolve the MMC import destination");
          if (fs::is_inside (fs::path_key (fs::real_path (source)),
                             fs::path_key (target)))
            return make_error (
                errc::invalid_argument,
                "cannot import an MMC instance into its source folder");
        }
      BLB_TRY (fs::create_directories (game_dir.parent_path ()));
      std::error_code ec;
      if (!std::filesystem::create_directory (game_dir, ec))
        return make_error (ec && ec != std::errc::file_exists
                               ? errc::io
                               : errc::already_exists,
                           sformat ("cannot create MMC instance folder '{}'",
                                    path_to_utf8 (game_dir)));
      cleanup.directory = game_dir;
    }
  else if (fs::exists (layout.version_json (id)) && !options.overwrite)
    return make_error (errc::already_exists,
                       sformat ("instance '{}' already exists", id));
  else
    BLB_TRY (fs::create_directories (game_dir));
  s_log.info ("importing {} pack '{}' {} for minecraft {} as {}",
              format_name (manifest.format), manifest.name, manifest.version,
              manifest.minecraft, id);

  std::vector<pack_file> files;
  for (auto &f : manifest.files)
    if (options.include_optional || !f.optional)
      files.push_back (f);

  install::task_graph graph;
  auto version_task = graph.add (
      "modpack:version", [&] (cancellation_token t) -> result<void> {
        if (manifest.multimc)
          return detail::install_multimc (installer, source, manifest, id,
                                          sink, t);
        install::loader_installer loaders (installer);
        install::install_request request;
        request.minecraft = manifest.minecraft;
        request.loaders = manifest.loaders;
        request.id = id;
        request.mods_directory = game_dir / "mods";
        BLB_TRY (loaders.install (request, sink, t));
        return {};
      });
  if (manifest.multimc)
    graph.add ("modpack:instance-files",
               [&] (cancellation_token t) {
                 return detail::copy_multimc_game (source, manifest, layout,
                                                   id, sink, t);
               },
               { version_task });
  else
    {
      auto resolve = graph.add (
          "modpack:resolve", [&] (cancellation_token t) -> result<void> {
            return detail::resolve_curseforge (files, options.curseforge,
                                               installer.mirror (), t);
          });
      auto download = graph.add (
          "modpack:files",
          [&] (cancellation_token t) -> result<void> {
            std::vector<install::planned_file> planned;
            for (const auto &f : files)
              {
                if (f.urls.empty () || !fs::safe_relative (f.path))
                  continue;
                install::planned_file p;
                p.kind = install::file_kind::other;
                p.target = game_dir / path_from_utf8 (f.path);
                p.relative = "versions/" + id + "/" + f.path;
                p.urls = installer.mirror ().resolve_all (f.urls);
                if (!f.sha1.empty ())
                  p.sha1 = parse_sha1 (f.sha1);
                p.size = f.size;
                planned.push_back (std::move (p));
              }
            if (planned.empty ())
              return {};
            BLB_TRY (installer.ensure_files (std::move (planned), sink, t,
                                             "modpack-files"));
            return {};
          },
          { resolve });
      graph.add ("modpack:overrides",
                 [&] (cancellation_token t) {
                   return extract_overrides (source, manifest, game_dir, t);
                 },
                 { download });
    }
  BLB_TRY (graph.run (token, sink));
  BLB_TRY (token.check ());

  instance::instance_manager manager (layout);
  instance::instance_settings settings;
  if (auto existing = manager.load_settings (id))
    settings = std::move (*existing);
  settings.isolated = true;
  if (manifest.multimc)
    settings.notes = manifest.multimc->notes;
  if (manifest.max_memory_mb)
    settings.max_memory_mb = manifest.max_memory_mb;
  else if (manifest.min_memory_mb
           && settings.max_memory_mb < manifest.min_memory_mb)
    settings.max_memory_mb = manifest.min_memory_mb;
  if (manifest.min_memory_mb)
    settings.min_memory_mb = manifest.min_memory_mb;
  for (const auto &arg : manifest.jvm_args)
    settings.jvm_args.push_back (arg);
  for (const auto &arg : manifest.game_args)
    settings.game_args.push_back (arg);
  BLB_TRY (manager.save_settings (id, settings));
  json info = { { "name", manifest.name },
                { "version", manifest.version },
                { "author", manifest.author },
                { "format", format_name (manifest.format) },
                { "minecraft", manifest.minecraft } };
  BLB_TRY (fs::write_file_atomic (game_dir / info_file, info.dump (2)));
  cleanup.directory.clear ();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now () - started)
                .count ();
  s_log.info ("imported '{}' as {} in {} ms ({} files)", manifest.name, id, ms,
              files.size ());
  return id;
}

}
