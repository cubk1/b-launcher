#include <core/base/format.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>

#include "core/installer/loader_impl.hpp"

namespace b_launcher_base::install::detail
{

namespace
{

constexpr log::logger s_log ("fabric");

class fabric_job final : public loader_job
{
public:
  using loader_job::loader_job;

  result<void>
  prepare (loader_context &ctx) override
  {
    BLB_TRY (resolve_version (ctx));
    auto api = m_request.type == loader_type::quilt ? "v3" : "v2";
    auto url
        = sformat ("{}/{}/versions/loader/{}/{}/profile/json",
                   fabric_meta_root (m_request.type), api,
                   url_encode (ctx.minecraft), url_encode (m_request.version));
    BLB_TRY_ASSIGN (auto body, fetch_text (ctx, url));
    BLB_TRY_ASSIGN (m_profile, meta::parse_version (body));
    if (m_profile.main_class.empty ())
      return make_error (errc::parse, "loader profile has no main class");
    s_log.info ("{} {} for {}: {} libraries", loader_name (m_request.type),
                m_request.version, ctx.minecraft, m_profile.libraries.size ());
    return download_libraries (ctx, m_profile.libraries,
                               loader_name (m_request.type));
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
    p.type = m_request.type;
    p.version = m_request.version;
    p.json = m_profile;
    p.primary = true;
    return p;
  }

private:
  meta::version m_profile;
};

}

std::unique_ptr<loader_job>
make_fabric_job (loader_request request)
{
  return std::make_unique<fabric_job> (std::move (request));
}

}
