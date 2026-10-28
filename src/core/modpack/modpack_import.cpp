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

std::string
sanitize_id (std::string_view name)
{
  std::string out;
  for (char c : name)
    {
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
          || c == '<' || c == '>' || c == '|'
          || static_cast<unsigned char> (c) < 32)
        out += '_';
      else
        out += c;
    }
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
  BLB_TRY (zip.for_each ([&] (const zip_entry &entry, zip_reader &reader) {
    if (token.is_cancelled ())
      {
        failure = make_error (errc::cancelled, "cancelled");
        return false;
      }
    auto name = replace_all (entry.name, "\\", "/");
    if (entry.directory || name.ends_with ('/'))
      return true;
    for (const auto &prefix : manifest.overrides)
      {
        if (!name.starts_with (prefix))
          continue;
        auto relative = name.substr (prefix.size ());
        if (relative.empty () || !detail::safe_relative (relative))
          return true;
        auto target = game_dir / path_from_utf8 (relative);
        if (auto r = reader.extract_current (target); !r)
          {
            failure = std::move (r).err ();
            return false;
          }
        ++count;
        return true;
      }
    return true;
  }));
  if (failure)
    return *failure;
  s_log.info ("extracted {} override files", count);
  return ok ();
}

}

result<std::string>
import_modpack (install::game_installer &installer,
                const std::filesystem::path &archive,
                const import_options &options, progress_sink *sink,
                cancellation_token token)
{
  auto started = std::chrono::steady_clock::now ();
  BLB_TRY_ASSIGN (auto manifest, read_modpack (archive));
  const auto &layout = installer.layout ();
  auto id = options.id.empty () ? sanitize_id (manifest.name) : options.id;
  if (fs::exists (layout.version_json (id)) && !options.overwrite)
    return make_error (errc::already_exists,
                       sformat ("instance '{}' already exists", id));
  auto game_dir = layout.version_dir (id);
  BLB_TRY (fs::create_directories (game_dir));
  s_log.info ("importing {} pack '{}' {} for minecraft {} as {}",
              format_name (manifest.format), manifest.name, manifest.version,
              manifest.minecraft, id);

  std::vector<pack_file> files;
  for (auto &f : manifest.files)
    if (options.include_optional || !f.optional)
      files.push_back (f);

  install::task_graph graph;
  graph.add ("modpack:version", [&] (cancellation_token t) -> result<void> {
    install::loader_installer loaders (installer);
    install::install_request request;
    request.minecraft = manifest.minecraft;
    request.loaders = manifest.loaders;
    request.id = id;
    request.mods_directory = game_dir / "mods";
    BLB_TRY (loaders.install (request, sink, t));
    return {};
  });
  auto resolve = graph.add (
      "modpack:resolve", [&] (cancellation_token t) -> result<void> {
        return detail::resolve_curseforge (files, options.curseforge, t);
      });
  auto download = graph.add (
      "modpack:files",
      [&] (cancellation_token t) -> result<void> {
        std::vector<install::planned_file> planned;
        for (const auto &f : files)
          {
            if (f.urls.empty () || !detail::safe_relative (f.path))
              continue;
            install::planned_file p;
            p.kind = install::file_kind::other;
            p.target = game_dir / path_from_utf8 (f.path);
            p.relative = "versions/" + id + "/" + f.path;
            p.urls = f.urls;
            for (const auto &url : f.urls)
              if (auto mirror = detail::modrinth_mirror (url);
                  !mirror.empty ())
                p.urls.push_back (mirror);
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
               return extract_overrides (archive, manifest, game_dir, t);
             },
             { download });
  BLB_TRY (graph.run (token, sink));

  instance::instance_manager manager (layout);
  instance::instance_settings settings;
  if (auto existing = manager.load_settings (id))
    settings = std::move (*existing);
  settings.isolated = true;
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
  BLB_TRY (
      fs::write_file_atomic (game_dir / "blb-modpack.json", info.dump (2)));
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now () - started)
                .count ();
  s_log.info ("imported '{}' as {} in {} ms ({} files)", manifest.name, id, ms,
              files.size ());
  return id;
}

}
