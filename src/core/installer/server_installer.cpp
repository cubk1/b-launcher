#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>
#include <core/installer/server_installer.hpp>

#include "core/installer/loader_impl.hpp"

#include <optional>

namespace b_launcher_base::install
{

namespace
{

constexpr log::logger s_log ("server");

bool
server_loader (loader_type type)
{
  switch (type)
    {
    case loader_type::forge:
    case loader_type::neoforge:
    case loader_type::cleanroom:
    case loader_type::fabric:
    case loader_type::legacy_fabric:
    case loader_type::quilt:
      return true;
    default:
      return false;
    }
}

std::optional<loader_info>
primary_loader (const meta::version &resolved,
                std::vector<std::string> *warnings)
{
  std::optional<loader_info> found;
  for (const auto &loader : detect_loaders (resolved))
    {
      if (server_loader (loader.type))
        {
          if (!found)
            found = loader;
        }
      else if (warnings != nullptr
               && (loader.type == loader_type::liteloader
                   || loader.type == loader_type::optifine))
        warnings->push_back (
            sformat ("{} only runs on the client", loader_name (loader.type)));
    }
  return found;
}

std::string
manifest_entry (std::string_view text)
{
  std::string out;
  std::size_t pos = 0;
  bool first = true;
  while (pos < text.size ())
    {
      std::size_t take = first ? 72 : 71;
      if (!first)
        out += ' ';
      out += text.substr (pos, take);
      out += "\r\n";
      pos += take;
      first = false;
    }
  return out;
}

result<void>
write_launch_jar (const std::filesystem::path &file, std::string_view main,
                  std::string_view properties, std::string_view launch,
                  const std::vector<std::string> &class_path)
{
  std::string manifest = "Manifest-Version: 1.0\r\n";
  manifest += manifest_entry (sformat ("Main-Class: {}", main));
  manifest
      += manifest_entry (sformat ("Class-Path: {}", join (class_path, " ")));
  manifest += "\r\n";
  BLB_TRY_ASSIGN (auto zip, zip_writer::create (file));
  BLB_TRY (zip.add_buffer ("META-INF/MANIFEST.MF", manifest));
  BLB_TRY (
      zip.add_buffer (properties, sformat ("launch.mainClass={}\n", launch)));
  return zip.close ();
}

result<void>
download_vanilla (detail::loader_context &ctx,
                  const std::filesystem::path &target)
{
  auto it = ctx.vanilla->downloads.find ("server");
  if (it == ctx.vanilla->downloads.end () || it->second.url.empty ())
    return make_error (errc::not_found,
                       sformat ("{} has no server", ctx.minecraft));
  return detail::download_file (ctx, it->second.url, target,
                                "minecraft-server", it->second.sha1,
                                it->second.size);
}

result<void>
install_fabric (detail::loader_context &ctx, const loader_info &loader,
                server_result &out)
{
  bool quilt = loader.type == loader_type::quilt;
  auto url
      = sformat ("{}/{}/versions/loader/{}/{}/server/json",
                 detail::fabric_meta_root (loader.type), quilt ? "v3" : "v2",
                 url_encode (ctx.minecraft), url_encode (loader.version));
  BLB_TRY_ASSIGN (auto body, detail::fetch_text (ctx, url));
  BLB_TRY_ASSIGN (auto profile, meta::parse_version (body));
  if (profile.main_class.empty ())
    return make_error (errc::parse, "server profile has no main class");
  BLB_TRY (
      detail::download_libraries (ctx, profile.libraries, "server-libraries"));
  BLB_TRY (download_vanilla (ctx, ctx.layout ().root () / "server.jar"));
  std::vector<std::string> class_path;
  std::filesystem::path loader_jar;
  for (const auto &lib : profile.libraries)
    {
      auto relative = lib.relative_path ();
      if (relative.empty () || !fs::safe_relative (relative))
        continue;
      class_path.push_back ("libraries/" + relative);
      if (lib.artifact.name == "fabric-loader"
          || lib.artifact.name == "quilt-loader")
        loader_jar = ctx.layout ().library (relative);
    }
  std::string launcher
      = quilt ? "org.quiltmc.loader.impl.launch.server.QuiltServerLauncher"
              : "net.fabricmc.loader.impl.launch.server.FabricServerLauncher";
  if (!quilt)
    if (auto zip = zip_reader::open (loader_jar);
        zip
        && !zip->contains ("net/fabricmc/loader/impl/launch/server/"
                           "FabricServerLauncher.class"))
      launcher = "net.fabricmc.loader.launch.server.FabricServerLauncher";
  std::string prefix = quilt ? "quilt" : "fabric";
  std::string jar = prefix + "-server-launch.jar";
  BLB_TRY (write_launch_jar (ctx.layout ().root () / jar, launcher,
                             prefix + "-server-launch.properties",
                             profile.main_class, class_path));
  BLB_TRY (fs::write_file (ctx.layout ().root ()
                               / (prefix + "-server-launcher.properties"),
                           "serverJar=server.jar\n"));
  out.windows = { "-jar", jar };
  out.unix = out.windows;
  return ok ();
}

}

result<void>
server_supported (const meta::version &resolved)
{
  auto loader = primary_loader (resolved, nullptr);
  if (loader && loader->type == loader_type::cleanroom)
    return make_error (errc::unsupported, "cleanroom has no server install");
  return ok ();
}

result<server_result>
install_server (const meta::version &resolved,
                const std::filesystem::path &target,
                const server_options &options, progress_sink *sink,
                cancellation_token token)
{
  BLB_TRY (server_supported (resolved));
  server_result out;
  auto loader = primary_loader (resolved, &out.warnings);
  BLB_TRY (fs::create_directories (target));
  auto install = options.install;
  if (!options.client_root.empty ())
    install.import_sources.push_back (options.client_root);
  install.assets = false;
  game_installer installer (meta::game_layout (target), install);
  auto temp = std::filesystem::temp_directory_path ()
              / ("blb-server-" + random_bytes_hex (6));
  BLB_TRY (fs::create_directories (temp));
  struct cleanup
  {
    std::filesystem::path temp;
    std::filesystem::path cache;
    ~cleanup ()
    {
      std::error_code ec;
      std::filesystem::remove_all (temp, ec);
      std::filesystem::remove_all (cache, ec);
    }
  } remove { temp, meta::game_layout (target).cache () };

  meta::version vanilla = resolved;
  if (!vanilla.downloads.contains ("server"))
    {
      game_installer fetcher (meta::game_layout (temp, temp / "versions"),
                              install);
      BLB_TRY_ASSIGN (
          vanilla, fetcher.fetch_version_json (resolved.minecraft (), token));
    }
  detail::loader_context ctx;
  ctx.installer = &installer;
  ctx.minecraft = resolved.minecraft ();
  ctx.target_id = "server";
  ctx.temp_dir = temp;
  ctx.sink = sink;
  ctx.token = token;
  ctx.java = options.java;
  ctx.vanilla = &vanilla;

  if (!loader)
    {
      BLB_TRY (download_vanilla (ctx, target / "server.jar"));
      out.windows = { "-jar", "server.jar" };
      out.unix = out.windows;
    }
  else if (loader->type == loader_type::forge
           || loader->type == loader_type::neoforge)
    {
      if (!ctx.java)
        return make_error (errc::invalid_argument, "no java resolver");
      auto job
          = detail::make_forge_job ({ loader->type, loader->version }, true);
      BLB_TRY (job->prepare (ctx));
      BLB_TRY (job->finalize (ctx));
      auto launch = job->server ();
      if (!launch || launch->windows.empty ())
        return make_error (errc::parse, "forge server has no launch command");
      out.windows = launch->windows;
      out.unix = launch->unix;
      out.user_jvm_args = launch->user_jvm_args;
    }
  else
    BLB_TRY (install_fabric (ctx, *loader, out));
  BLB_TRY (token.check ());
  s_log.info ("installed {} server in {}", ctx.minecraft,
              path_to_utf8 (target));
  return out;
}

}
