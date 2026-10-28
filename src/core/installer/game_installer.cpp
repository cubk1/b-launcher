#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/installer/game_installer.hpp>
#include <core/installer/object_import.hpp>
#include <core/network/http_client.hpp>

#include <algorithm>
#include <iterator>
#include <mutex>
#include <optional>

namespace b_launcher_base::install
{

namespace
{
constexpr log::logger s_log ("installer");
}

struct game_installer::impl
{
  meta::game_layout layout;
  install_options options;
  integrity_cache cache;
  std::once_flag cache_loaded;
  std::mutex manifest_mutex;
  std::optional<meta::version_manifest> manifest;
  std::mutex clients_mutex;
  std::vector<std::unique_ptr<net::download_client>> clients;
  std::unique_ptr<net::download_client> primary;

  impl (meta::game_layout l, install_options o)
      : layout (std::move (l))
      , options (std::move (o))
      , cache (layout.cache () / "integrity.bin")
  {
    if (!options.mirror)
      options.mirror = net::make_official_provider ();
    primary = std::make_unique<net::download_client> (options.download);
  }

  void
  ensure_cache ()
  {
    std::call_once (cache_loaded, [this] {
      if (auto r = cache.load (); !r)
        s_log.warn ("cannot load integrity cache: {}", r.err ().message);
    });
  }

  std::unique_ptr<net::download_client>
  acquire_client ()
  {
    std::lock_guard lock (clients_mutex);
    if (!clients.empty ())
      {
        auto client = std::move (clients.back ());
        clients.pop_back ();
        return client;
      }
    return std::make_unique<net::download_client> (options.download);
  }

  void
  release_client (std::unique_ptr<net::download_client> client)
  {
    std::lock_guard lock (clients_mutex);
    clients.push_back (std::move (client));
  }
};

game_installer::game_installer (meta::game_layout layout,
                                install_options options)
    : m_impl (std::make_unique<impl> (std::move (layout), std::move (options)))
{
}

game_installer::~game_installer ()
{
  if (m_impl)
    (void)m_impl->cache.save ();
}

const meta::game_layout &
game_installer::layout () const noexcept
{
  return m_impl->layout;
}

const install_options &
game_installer::options () const noexcept
{
  return m_impl->options;
}

net::mirror_provider &
game_installer::mirror () const noexcept
{
  return *m_impl->options.mirror;
}

std::shared_ptr<net::mirror_provider>
game_installer::mirror_ptr () const noexcept
{
  return m_impl->options.mirror;
}

net::download_client &
game_installer::downloader () noexcept
{
  return *m_impl->primary;
}

integrity_cache &
game_installer::cache () noexcept
{
  m_impl->ensure_cache ();
  return m_impl->cache;
}

std::vector<std::filesystem::path>
game_installer::import_sources () const
{
  auto sources = m_impl->options.import_sources;
  if (m_impl->options.discover_import_sources)
    for (auto &root : discover_minecraft_roots (m_impl->layout.root ()))
      sources.push_back (std::move (root));
  return sources;
}

result<meta::version_manifest>
game_installer::manifest (cancellation_token token, bool refresh)
{
  std::lock_guard lock (m_impl->manifest_mutex);
  if (m_impl->manifest && !refresh)
    return *m_impl->manifest;
  BLB_TRY_ASSIGN (auto fetched,
                  meta::fetch_version_manifest (mirror (), token));
  m_impl->manifest = fetched;
  return fetched;
}

result<meta::version>
game_installer::fetch_version_json (std::string_view id,
                                    cancellation_token token)
{
  auto file = m_impl->layout.version_json (id);
  BLB_TRY_ASSIGN (auto list, manifest (token));
  const auto *entry = list.find (id);
  if (!entry)
    {
      if (fs::exists (file))
        return meta::load_version (m_impl->layout, id);
      return make_error (errc::version_not_found,
                         sformat ("unknown minecraft version '{}'", id));
    }
  auto expected = parse_sha1 (entry->sha1);
  if (fs::exists (file))
    {
      auto digest = sha1_file (file);
      if (digest && (!expected || *digest == *expected))
        return meta::load_version (m_impl->layout, id);
    }
  BLB_TRY_ASSIGN (auto body,
                  net::http_get (mirror ().resolve (entry->url), token));
  if (expected && sha1::digest (body) != *expected)
    return make_error (errc::checksum_mismatch,
                       sformat ("version json '{}' sha1 mismatch", id));
  BLB_TRY_ASSIGN (auto parsed, meta::parse_version (body));
  BLB_TRY (fs::write_file_atomic (file, body));
  return parsed;
}

result<completion_report>
game_installer::ensure_files (std::vector<planned_file> files,
                              progress_sink *sink, cancellation_token token,
                              std::string_view task)
{
  m_impl->ensure_cache ();
  completion_report report;
  report.planned = files.size ();
  verify_options verify { m_impl->options.verify, &m_impl->cache, nullptr };
  progress_tracker verify_progress (sink, sformat ("verify:{}", task));
  auto checked = verify_files (files, verify, &verify_progress, token);
  verify_progress.finish ();
  BLB_TRY (token.check ());
  report.verify_time = checked.elapsed;
  report.invalid = checked.invalid.size ();
  auto missing = std::move (checked.invalid);
  if (!missing.empty ())
    {
      auto sources = import_sources ();
      if (!sources.empty ())
        {
          auto imported = import_local_objects (
              files, missing, sources, m_impl->options.verify_imported,
              nullptr, token);
          report.imported = imported.linked + imported.copied;
          if (report.imported)
            {
              std::vector<std::size_t> linked;
              std::set_difference (missing.begin (), missing.end (),
                                   imported.remaining.begin (),
                                   imported.remaining.end (),
                                   std::back_inserter (linked));
              remember_files (files, linked, m_impl->cache);
            }
          missing = std::move (imported.remaining);
        }
    }
  BLB_TRY (token.check ());
  if (!missing.empty ())
    {
      std::vector<planned_file> pending;
      pending.reserve (missing.size ());
      for (auto i : missing)
        pending.push_back (files[i]);
      auto tasks = to_download_tasks (pending);
      progress_tracker download_progress (sink, sformat ("download:{}", task));
      auto client = m_impl->acquire_client ();
      auto downloaded = client->run (tasks, &download_progress, token);
      m_impl->release_client (std::move (client));
      download_progress.finish ();
      report.download_time = downloaded.elapsed;
      report.downloaded = downloaded.succeeded;
      report.downloaded_bytes = downloaded.bytes;
      std::vector<std::size_t> ok_indices;
      std::vector<char> failed (pending.size (), 0);
      for (const auto &f : downloaded.failures)
        failed[f.index] = 1;
      for (std::size_t i = 0; i < pending.size (); ++i)
        if (!failed[i])
          ok_indices.push_back (i);
      remember_files (pending, ok_indices, m_impl->cache);
      if (auto r = m_impl->cache.save (); !r)
        s_log.warn ("cannot save integrity cache: {}", r.err ().message);
      BLB_TRY (downloaded.to_result ());
    }
  else if (auto r = m_impl->cache.save (); !r)
    s_log.warn ("cannot save integrity cache: {}", r.err ().message);
  s_log.info ("{}: {} files, {} invalid, {} imported, {} downloaded "
              "(verify {} ms, download {} ms)",
              task, report.planned, report.invalid, report.imported,
              report.downloaded, report.verify_time.count (),
              report.download_time.count ());
  return report;
}

result<completion_report>
game_installer::complete_version (const meta::version &resolved,
                                  const meta::rule_context &context,
                                  progress_sink *sink,
                                  cancellation_token token,
                                  bool include_natives, completion_scope scope)
{
  auto files
      = plan_version_files (resolved, m_impl->layout, mirror (), context);
  if (!include_natives)
    std::erase_if (files, [] (const planned_file &f) {
      return f.kind == file_kind::native;
    });
  if (scope == completion_scope::assets)
    std::erase_if (files, [] (const planned_file &f) {
      return f.kind != file_kind::asset_index;
    });
  if (scope == completion_scope::core)
    std::erase_if (files, [] (const planned_file &f) {
      return f.kind == file_kind::asset_index;
    });
  std::optional<meta::asset_index> index;
  if (m_impl->options.assets && resolved.asset_index
      && scope != completion_scope::core)
    {
      auto it = std::find_if (files.begin (), files.end (),
                              [] (const planned_file &f) {
                                return f.kind == file_kind::asset_index;
                              });
      if (it != files.end ())
        {
          std::vector<planned_file> index_file { *it };
          files.erase (it);
          BLB_TRY (ensure_files (std::move (index_file), sink, token,
                                 "asset-index"));
        }
      BLB_TRY_ASSIGN (auto text, fs::read_file (m_impl->layout.asset_index (
                                     resolved.asset_index->id)));
      BLB_TRY_ASSIGN (index, meta::parse_asset_index (text));
      auto assets = plan_asset_files (*index, m_impl->layout, mirror ());
      files.insert (files.end (), std::make_move_iterator (assets.begin ()),
                    std::make_move_iterator (assets.end ()));
    }
  auto task = scope == completion_scope::core     ? "game-core"
              : scope == completion_scope::assets ? "game-assets"
                                                  : "game";
  BLB_TRY_ASSIGN (auto report,
                  ensure_files (std::move (files), sink, token, task));
  if (index && index->is_virtual)
    {
      auto target = m_impl->layout.virtual_assets (resolved.asset_id ());
      for (const auto &object : index->objects)
        {
          auto dest = target / path_from_utf8 (object.name);
          auto st = fs::stat (dest);
          if (st.exists && st.size == object.size)
            continue;
          auto source = m_impl->layout.asset_object (to_hex (object.hash));
          BLB_TRY (fs::link_or_copy (source, dest));
        }
    }
  return report;
}

result<meta::version>
game_installer::install_vanilla (std::string_view id, progress_sink *sink,
                                 cancellation_token token)
{
  BLB_TRY (fetch_version_json (id, token));
  BLB_TRY_ASSIGN (auto resolved, meta::resolve_version (m_impl->layout, id));
  BLB_TRY (complete_version (resolved, meta::rule_context::current (), sink,
                             token));
  return resolved;
}

result<void>
game_installer::prepare_legacy_assets (const meta::version &resolved,
                                       const std::filesystem::path &game_dir)
{
  if (!resolved.asset_index)
    return ok ();
  auto text
      = fs::read_file (m_impl->layout.asset_index (resolved.asset_index->id));
  if (!text)
    return ok ();
  BLB_TRY_ASSIGN (auto index, meta::parse_asset_index (*text));
  if (!index.is_virtual && !index.map_to_resources)
    return ok ();
  auto target = index.map_to_resources
                    ? game_dir / "resources"
                    : m_impl->layout.virtual_assets (resolved.asset_id ());
  for (const auto &object : index.objects)
    {
      auto dest = target / path_from_utf8 (object.name);
      auto st = fs::stat (dest);
      if (st.exists && st.size == object.size)
        continue;
      auto source = m_impl->layout.asset_object (to_hex (object.hash));
      if (!fs::exists (source))
        continue;
      BLB_TRY (fs::link_or_copy (source, dest));
    }
  return ok ();
}

}
