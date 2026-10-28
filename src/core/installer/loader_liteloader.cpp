#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>

#include "core/installer/loader_impl.hpp"
#include "core/meta/json_util.hpp"

namespace b_launcher_base::install::detail
{

namespace
{

constexpr log::logger s_log ("liteloader");

class liteloader_job final : public loader_job
{
public:
  using loader_job::loader_job;

  result<void>
  prepare (loader_context &ctx) override
  {
    BLB_TRY_ASSIGN (auto versions, list_loader_versions (
                                       loader_type::liteloader, ctx.minecraft,
                                       ctx.mirror (), ctx.token));
    const loader_version *chosen = nullptr;
    for (const auto &v : versions)
      if ((m_request.version.empty () && (!chosen || v.stable))
          || v.version == m_request.version)
        {
          chosen = &v;
          if (!m_request.version.empty () || v.stable)
            break;
        }
    if (!chosen)
      return make_error (errc::loader_not_found,
                         sformat ("liteloader {} not found for {}",
                                  m_request.version, ctx.minecraft));
    m_request.version = chosen->version;
    auto entry = json::parse (chosen->extra, nullptr, false);
    m_tweaker = meta::detail::string_of (entry, "tweakClass",
                                         "com.mumfrey.liteloader.launch."
                                         "LiteLoaderTweaker");
    meta::library main;
    main.name = "com.mumfrey:liteloader:" + chosen->version;
    main.artifact = *meta::maven_artifact::parse (main.name);
    meta::download_artifact download;
    download.path = main.artifact.path ();
    if (chosen->stable)
      download.url
          = join_url ("http://dl.liteloader.com/versions", download.path);
    else
      download.url = sformat ("https://bmclapi2.bangbang93.com/liteloader/"
                              "download?version={}",
                              url_encode (chosen->version));
    main.download = download;
    m_libraries.push_back (main);
    if (auto *libs = meta::detail::member (entry, "libraries");
        libs && libs->is_array ())
      for (const auto &lib : *libs)
        m_libraries.push_back (meta::detail::library_from_json (lib));
    s_log.info ("liteloader {} with {} libraries", chosen->version,
                m_libraries.size ());
    return download_libraries (ctx, m_libraries, "liteloader");
  }

  result<void>
  finalize (loader_context &) override
  {
    return ok ();
  }

  std::optional<loader_patch>
  patch () const override
  {
    loader_patch p;
    p.type = loader_type::liteloader;
    p.version = m_request.version;
    p.json.libraries = m_libraries;
    p.tweakers.push_back (m_tweaker);
    return p;
  }

private:
  std::vector<meta::library> m_libraries;
  std::string m_tweaker;
};

}

std::unique_ptr<loader_job>
make_liteloader_job (loader_request request)
{
  return std::make_unique<liteloader_job> (std::move (request));
}

}
