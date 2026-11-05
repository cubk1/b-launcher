#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/installer/task_graph.hpp>
#include <core/java/java_requirement.hpp>
#include <core/java/java_runtime.hpp>

#include "core/installer/loader_impl.hpp"

#include <mutex>
#include <unordered_set>

namespace b_launcher_base::install
{

namespace
{

constexpr log::logger s_log ("loader");
constexpr std::string_view launchwrapper_main
    = "net.minecraft.launchwrapper.Launch";

std::unique_ptr<detail::loader_job>
make_job (const loader_request &request)
{
  switch (request.type)
    {
    case loader_type::fabric:
    case loader_type::legacy_fabric:
    case loader_type::quilt:
      return detail::make_fabric_job (request);
    case loader_type::forge:
    case loader_type::neoforge:
    case loader_type::cleanroom:
      return detail::make_forge_job (request);
    case loader_type::liteloader:
      return detail::make_liteloader_job (request);
    case loader_type::optifine:
      return detail::make_optifine_job (request);
    case loader_type::fabric_api:
    case loader_type::legacy_fabric_api:
    case loader_type::quilt_api:
      return detail::make_mod_job (request);
    }
  return nullptr;
}

meta::version
merge_patches (const std::vector<detail::loader_patch> &patches,
               const meta::version &vanilla, const std::string &id,
               const std::string &minecraft)
{
  const detail::loader_patch *primary = nullptr;
  for (const auto &p : patches)
    if (p.primary)
      primary = &p;
  meta::version out;
  if (primary)
    out = primary->json;
  out.id = id;
  out.inherits_from = minecraft;
  out.chain.clear ();
  if (out.jar.empty ())
    out.jar = vanilla.jar_id ();
  std::vector<meta::library> addons;
  std::unordered_set<std::string> seen;
  for (const auto &lib : out.libraries)
    seen.insert (lib.merge_key ());
  std::vector<std::string> tweakers;
  for (const auto &p : patches)
    {
      if (&p == primary)
        continue;
      for (const auto &lib : p.json.libraries)
        if (seen.insert (lib.merge_key ()).second)
          addons.push_back (lib);
      tweakers.insert (tweakers.end (), p.tweakers.begin (),
                       p.tweakers.end ());
    }
  out.libraries.insert (out.libraries.end (), addons.begin (), addons.end ());
  if (!tweakers.empty ())
    {
      if (!primary)
        out.main_class = std::string (launchwrapper_main);
      bool modern = out.has_arguments || vanilla.has_arguments;
      if (modern && out.minecraft_arguments.empty ())
        {
          out.has_arguments = true;
          for (const auto &t : tweakers)
            out.game_arguments.push_back ({ { "--tweakClass", t }, {} });
        }
      else
        {
          auto args = out.minecraft_arguments.empty ()
                          ? vanilla.minecraft_arguments
                          : out.minecraft_arguments;
          for (const auto &t : tweakers)
            args += " --tweakClass " + t;
          out.minecraft_arguments = args;
        }
    }
  return out;
}

}

struct loader_installer::impl
{
  game_installer *installer = nullptr;
  java_resolver java;
  std::mutex java_mutex;
  std::optional<std::vector<java::java_installation>> java_cache;

  result<java::java_installation>
  default_java (const meta::version &vanilla, progress_sink *sink,
                cancellation_token token)
  {
    BLB_TRY (token.check ());
    auto requirement = java::requirement_for (vanilla);
    std::lock_guard lock (java_mutex);
    BLB_TRY (token.check ());
    if (!java_cache && installer->options ().java_candidates)
      {
        java_cache = *installer->options ().java_candidates;
        java::prune_missing (*java_cache);
      }
    if (java_cache)
      {
        if (auto chosen = java::select_java (*java_cache, requirement))
          return *chosen;
      }
    auto options = installer->options ().java_scan;
    options.runtime_roots.push_back (installer->layout ().runtime ());
    auto found = java::scan_java (options, token);
    BLB_TRY (token.check ());
    java_cache = std::move (found);
    if (auto chosen = java::select_java (*java_cache, requirement))
      return *chosen;
    if (auto blocked = java::runtime_blocked (requirement.min_major))
      return *blocked;
    BLB_TRY_ASSIGN (auto installed,
                    java::install_runtime (*installer, requirement.component, sink, token));
    java_cache->push_back (installed);
    return installed;
  }
};

loader_installer::loader_installer (game_installer &installer)
    : m_impl (std::make_unique<impl> ())
{
  m_impl->installer = &installer;
}

loader_installer::~loader_installer () = default;

void
loader_installer::set_java_resolver (java_resolver resolver)
{
  m_impl->java = std::move (resolver);
}

result<std::string>
loader_installer::install (const install_request &request, progress_sink *sink,
                           cancellation_token token)
{
  if (!fs::safe_component (request.minecraft))
    return make_error (errc::invalid_argument, "invalid minecraft version");
  if (!request.id.empty () && !fs::safe_component (request.id))
    return make_error (errc::invalid_argument, "invalid instance id");
  BLB_TRY (check_loader_combination (request.loaders, request.minecraft));
  auto &installer = *m_impl->installer;
  const auto &layout = installer.layout ();
  auto started = std::chrono::steady_clock::now ();
  // Official metadata and client jars are inputs, not user instances. Keep
  // them separate while sharing the libraries, assets and managed runtimes.
  meta::game_layout base_layout (layout.root (), layout.cache () / "vanilla");
  game_installer base_installer (base_layout, installer.options ());

  detail::loader_context ctx;
  ctx.installer = &base_installer;
  ctx.minecraft = request.minecraft;
  ctx.sink = sink;
  ctx.token = token;
  ctx.temp_dir = layout.cache () / "install" / random_bytes_hex (6);
  ctx.java = m_impl->java ? m_impl->java : [this, sink, token] (const meta::version &v) {
    return m_impl->default_java (v, sink, token);
  };
  for (const auto &l : request.loaders)
    if (l.type == loader_type::forge || l.type == loader_type::neoforge
        || l.type == loader_type::cleanroom)
      {
        ctx.with_forge = true;
        ctx.modern_forge = compare_versions (request.minecraft, "1.13") >= 0;
      }
  BLB_TRY (fs::create_directories (ctx.temp_dir));
  struct cleanup
  {
    std::filesystem::path directory;
    ~cleanup () { (void)fs::remove_all (directory); }
  } remove_temp { ctx.temp_dir };

  std::vector<std::unique_ptr<detail::loader_job>> jobs;
  for (const auto &l : request.loaders)
    jobs.push_back (make_job (l));

  std::string id;
  meta::version vanilla;
  ctx.vanilla = &vanilla;
  task_graph graph;
  auto vanilla_json
      = graph.add ("vanilla:json", [&] (cancellation_token t) -> result<void> {
          BLB_TRY_ASSIGN (auto manifest, base_installer.manifest (t));
          if (manifest.find (request.minecraft))
            BLB_TRY (base_installer.fetch_version_json (request.minecraft, t));
          else
            {
              // Preserve support for locally supplied base versions, but take
              // a fresh snapshot rather than preparing files in their folder.
              BLB_TRY_ASSIGN (auto local, meta::resolve_version (layout, request.minecraft));
              auto source = layout.version_jar (local.jar_id ());
              auto target = base_layout.version_jar (request.minecraft);
              if (fs::exists (source))
                BLB_TRY (fs::copy_file (source, target));
              else
                BLB_TRY (fs::remove_file (target));
              local.inherits_from.clear ();
              local.jar.clear ();
              BLB_TRY (meta::save_version (base_layout, local));
            }
          BLB_TRY_ASSIGN (vanilla,
                          meta::resolve_version (base_layout, request.minecraft));
          return {};
        });
  std::vector<std::size_t> resolves;
  for (auto &job : jobs)
    resolves.push_back (
        graph.add (sformat ("resolve:{}", loader_name (job->request ().type)),
                   [&ctx, j = job.get ()] (cancellation_token t) {
                     auto task = ctx;
                     task.token = t;
                     return j->resolve_version (task);
                   }));
  auto naming = graph.add (
      "naming",
      [&] (cancellation_token) -> result<void> {
        std::vector<loader_info> resolved;
        for (const auto &job : jobs)
          resolved.push_back (
              { job->request ().type, job->request ().version });
        id = default_version_id (request, resolved);
        if (!fs::safe_component (id))
          return make_error (errc::invalid_argument, "invalid instance id");
        ctx.target_id = id;
        ctx.mods_dir = request.mods_directory.empty ()
                           ? layout.version_dir (id) / "mods"
                           : request.mods_directory;
        return {};
      },
      resolves);
  auto vanilla_core = graph.add ("vanilla:core",
                                 [&] (cancellation_token t) -> result<void> {
                                   BLB_TRY (base_installer.complete_version (
                                       vanilla, meta::rule_context::current (),
                                       sink, t, true, completion_scope::core));
                                   return {};
                                 },
                                 { vanilla_json });
  for (auto &job : jobs)
    {
      auto name = std::string (loader_name (job->request ().type));
      auto prepare = graph.add ("prepare:" + name,
                                [&ctx, j = job.get ()] (cancellation_token t) {
                                  auto task = ctx;
                                  task.token = t;
                                  return j->prepare (task);
                                },
                                { naming });
      graph.add ("finalize:" + name,
                 [this, &ctx, j = job.get ()] (cancellation_token t) {
                   auto task = ctx;
                   task.token = t;
                   if (!m_impl->java)
                     task.java = [this, sink = ctx.sink, t] (const meta::version &v) {
                       return m_impl->default_java (v, sink, t);
                     };
                   return j->finalize (task);
                 },
                 { vanilla_core, prepare });
    }
  graph.add ("vanilla:assets",
             [&] (cancellation_token t) -> result<void> {
               BLB_TRY (base_installer.complete_version (
                   vanilla, meta::rule_context::current (), sink, t, true,
                   completion_scope::assets));
               return {};
             },
             { vanilla_json });
  BLB_TRY (graph.run (token, sink));
  BLB_TRY (token.check ());

  std::vector<detail::loader_patch> patches;
  for (const auto &job : jobs)
    if (auto p = job->patch ())
      patches.push_back (std::move (*p));
  auto merged = merge_patches (patches, vanilla, id, request.minecraft);
  if (merged.main_class.empty ())
    merged.main_class = vanilla.main_class;
  auto standalone = meta::merge_versions (vanilla, merged);
  auto vanilla_jar = base_layout.version_jar (vanilla.jar_id ());
  if (fs::exists (vanilla_jar))
    BLB_TRY (fs::copy_file (vanilla_jar, layout.version_jar (id)));
  standalone.inherits_from.clear ();
  standalone.chain.clear ();
  standalone.jar.clear ();
  standalone.client_version = request.minecraft;
  BLB_TRY (meta::save_version (layout, standalone));
  BLB_TRY_ASSIGN (auto final_version, meta::resolve_version (layout, id));
  BLB_TRY (installer.complete_version (final_version,
                                       meta::rule_context::current (), sink,
                                       token, true, completion_scope::core));
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now () - started)
                .count ();
  s_log.info ("installed {} in {} ms", id, ms);
  return id;
}

}
