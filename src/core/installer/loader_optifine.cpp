#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/process.hpp>
#include <core/base/strings.hpp>
#include <core/base/zip.hpp>

#include "core/installer/loader_impl.hpp"

namespace b_launcher_base::install::detail
{

namespace
{

constexpr log::logger s_log ("optifine");

meta::library
make_library (std::string_view name)
{
  meta::library lib;
  lib.name = std::string (name);
  lib.artifact = *meta::maven_artifact::parse (name);
  return lib;
}

class optifine_job final : public loader_job
{
public:
  using loader_job::loader_job;

  result<void>
  prepare (loader_context &ctx) override
  {
    BLB_TRY (resolve_version (ctx));
    m_forge_tweaker = ctx.with_forge && !ctx.modern_forge;
    auto underscore = m_request.version.rfind ('_');
    if (underscore == std::string::npos)
      return make_error (
          errc::invalid_argument,
          sformat ("invalid optifine version '{}'", m_request.version));
    auto type = m_request.version.substr (0, underscore);
    auto patch = m_request.version.substr (underscore + 1);
    m_full = ctx.minecraft + "_" + m_request.version;
    auto url = sformat ("https://bmclapi2.bangbang93.com/optifine/{}/{}/{}",
                        ctx.minecraft, type, patch);
    m_installer = ctx.layout ().library (
        sformat ("optifine/OptiFine/{0}/OptiFine-{0}-installer.jar", m_full));
    BLB_TRY (download_file (ctx, url, m_installer, "optifine"));
    if (ctx.modern_forge)
      {
        m_as_mod = true;
        auto target = ctx.mods_dir
                      / path_from_utf8 (sformat ("OptiFine_{}.jar", m_full));
        BLB_TRY (fs::copy_file (m_installer, target));
        s_log.info ("installed optifine {} as mod", m_full);
        return ok ();
      }
    BLB_TRY_ASSIGN (auto zip, zip_reader::open (m_installer));
    m_patcher = zip.contains ("optifine/Patcher.class");
    m_library = make_library ("optifine:OptiFine:" + m_full);
    m_libraries.push_back (m_library);
    if (auto text = zip.read ("launchwrapper-of.txt"))
      {
        auto version = std::string (trim (*text));
        auto lib = make_library ("optifine:launchwrapper-of:" + version);
        BLB_TRY (zip.extract (sformat ("launchwrapper-of-{}.jar", version),
                              ctx.layout ().library (lib.artifact.path ())));
        m_libraries.push_back (lib);
      }
    else if (zip.contains ("launchwrapper-2.0.jar"))
      {
        auto lib = make_library ("optifine:launchwrapper:2.0");
        BLB_TRY (zip.extract ("launchwrapper-2.0.jar",
                              ctx.layout ().library (lib.artifact.path ())));
        m_libraries.push_back (lib);
      }
    else
      {
        auto lib = make_library ("net.minecraft:launchwrapper:1.12");
        m_libraries.push_back (lib);
        BLB_TRY (download_libraries (ctx, { lib }, "optifine"));
      }
    return ok ();
  }

  result<void>
  finalize (loader_context &ctx) override
  {
    if (m_as_mod)
      return ok ();
    auto target = ctx.layout ().library (m_library.artifact.path ());
    if (!m_patcher)
      return fs::copy_file (m_installer, target);
    BLB_TRY_ASSIGN (auto java, ctx.java (*ctx.vanilla));
    BLB_TRY (fs::create_directories (target.parent_path ()));
    process_options options;
    options.args
        = { path_to_utf8 (java.executable),
            "-cp",
            path_to_utf8 (m_installer),
            "optifine.Patcher",
            path_to_utf8 (ctx.layout ().version_jar (ctx.vanilla->jar_id ())),
            path_to_utf8 (m_installer),
            path_to_utf8 (target) };
    options.working_directory = ctx.temp_dir;
    BLB_TRY_ASSIGN (auto output, run_process (options, ctx.token));
    if (output.exit_code != 0 || !fs::exists (target))
      return make_error (errc::process,
                         sformat ("optifine patcher failed ({}): {}",
                                  output.exit_code, output.err));
    s_log.info ("patched optifine {}", m_full);
    return ok ();
  }

  std::optional<loader_patch>
  patch () const override
  {
    if (m_as_mod)
      return std::nullopt;
    loader_patch p;
    p.type = loader_type::optifine;
    p.version = m_request.version;
    p.json.libraries = m_libraries;
    p.tweakers.push_back (m_forge_tweaker ? "optifine.OptiFineForgeTweaker"
                                          : "optifine.OptiFineTweaker");
    return p;
  }

private:
  std::string m_full;
  std::filesystem::path m_installer;
  bool m_patcher = false;
  bool m_as_mod = false;
  bool m_forge_tweaker = false;
  meta::library m_library;
  std::vector<meta::library> m_libraries;
};

}

std::unique_ptr<loader_job>
make_optifine_job (loader_request request)
{
  return std::make_unique<optifine_job> (std::move (request));
}

}
