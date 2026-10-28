#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>

#include "core/installer/loader_impl.hpp"
#include "core/meta/json_util.hpp"

namespace b_launcher_base::install::detail
{

namespace
{

constexpr log::logger s_log ("mods");

class mod_job final : public loader_job
{
public:
  using loader_job::loader_job;

  result<void>
  prepare (loader_context &ctx) override
  {
    BLB_TRY_ASSIGN (auto versions,
                    list_loader_versions (m_request.type, ctx.minecraft,
                                          ctx.mirror (), ctx.token));
    const loader_version *chosen = nullptr;
    for (const auto &v : versions)
      {
        if (!m_request.version.empty ())
          {
            if (v.version == m_request.version)
              chosen = &v;
          }
        else if (v.stable)
          chosen = chosen ? chosen : &v;
      }
    if (!chosen && m_request.version.empty () && !versions.empty ())
      chosen = &versions.front ();
    if (!chosen)
      return make_error (errc::loader_not_found,
                         sformat ("{} {} not found for {}",
                                  loader_name (m_request.type),
                                  m_request.version, ctx.minecraft));
    m_request.version = chosen->version;
    auto file = json::parse (chosen->extra, nullptr, false);
    auto name = meta::detail::string_of (file, "filename");
    if (name.empty ())
      name = sformat ("{}-{}.jar", loader_name (m_request.type),
                      chosen->version);
    std::string sha1;
    if (auto *hashes = meta::detail::member (file, "hashes"))
      sha1 = meta::detail::string_of (*hashes, "sha1");
    auto size = meta::detail::int_of (file, "size", -1);
    s_log.info ("installing {} {} into {}", loader_name (m_request.type),
                chosen->version, path_to_utf8 (ctx.mods_dir));
    return download_file (ctx, chosen->url,
                          ctx.mods_dir / path_from_utf8 (name),
                          loader_name (m_request.type), sha1, size);
  }

  result<void>
  finalize (loader_context &) override
  {
    return ok ();
  }

  std::optional<loader_patch>
  patch () const override
  {
    return std::nullopt;
  }
};

}

std::unique_ptr<loader_job>
make_mod_job (loader_request request)
{
  return std::make_unique<mod_job> (std::move (request));
}

}
