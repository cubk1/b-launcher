#pragma once

#include <core/installer/game_installer.hpp>
#include <core/installer/loader.hpp>
#include <core/meta/version.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::install::detail
{

using json = nlohmann::json;

struct loader_context
{
  game_installer *installer = nullptr;
  std::string minecraft;
  std::string target_id;
  std::filesystem::path temp_dir;
  std::filesystem::path mods_dir;
  progress_sink *sink = nullptr;
  cancellation_token token;
  java_resolver java;
  const meta::version *vanilla = nullptr;
  bool with_forge = false;
  bool modern_forge = false;

  const meta::game_layout &
  layout () const
  {
    return installer->layout ();
  }

  net::mirror_provider &
  mirror () const
  {
    return installer->mirror ();
  }
};

struct loader_patch
{
  loader_type type = loader_type::fabric;
  std::string version;
  meta::version json;
  std::vector<std::string> tweakers;
  bool primary = false;
};

class loader_job
{
public:
  explicit loader_job (loader_request request)
      : m_request (std::move (request))
  {
  }

  virtual ~loader_job () = default;
  virtual result<void> prepare (loader_context &ctx) = 0;
  virtual result<void> finalize (loader_context &ctx) = 0;
  virtual std::optional<loader_patch> patch () const = 0;

  const loader_request &
  request () const noexcept
  {
    return m_request;
  }

  result<void> resolve_version (loader_context &ctx);

protected:
  loader_request m_request;
  std::optional<loader_version> m_resolved;
};

std::unique_ptr<loader_job> make_fabric_job (loader_request request);
std::unique_ptr<loader_job> make_forge_job (loader_request request);
std::unique_ptr<loader_job> make_liteloader_job (loader_request request);
std::unique_ptr<loader_job> make_optifine_job (loader_request request);
std::unique_ptr<loader_job> make_mod_job (loader_request request);

result<std::string> fetch_text (const loader_context &ctx,
                                std::string_view url);
result<json> fetch_json (const loader_context &ctx, std::string_view url);
std::vector<std::string> parse_maven_versions (std::string_view xml);
std::string join_url (std::string_view base, std::string_view path);
result<void> download_libraries (loader_context &ctx,
                                 const std::vector<meta::library> &libraries,
                                 std::string_view task);
result<void> download_file (loader_context &ctx, std::string_view url,
                            const std::filesystem::path &target,
                            std::string_view task, std::string_view sha1 = {},
                            std::int64_t size = -1);
std::string fabric_meta_root (loader_type type);
std::string neoforge_minecraft (std::string_view version);

}
