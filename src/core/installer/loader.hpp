#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/java/java_detect.hpp>
#include <core/meta/version.hpp>
#include <core/network/mirror_provider.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::install
{

enum class loader_type : std::uint8_t
{
  forge,
  neoforge,
  cleanroom,
  fabric,
  legacy_fabric,
  quilt,
  liteloader,
  optifine,
  fabric_api,
  legacy_fabric_api,
  quilt_api,
};

struct loader_version
{
  loader_type type = loader_type::fabric;
  std::string version;
  std::string minecraft;
  bool stable = true;
  std::string url;
  std::string release_time;
  std::string extra;
};

struct loader_request
{
  loader_type type = loader_type::fabric;
  std::string version;
};

struct loader_info
{
  loader_type type = loader_type::fabric;
  std::string version;
};

struct install_request
{
  std::string minecraft;
  std::vector<loader_request> loaders;
  std::string id;
  std::filesystem::path mods_directory;
};

using java_resolver
    = std::function<result<java::java_installation> (const meta::version &)>;

std::string_view loader_name (loader_type type) noexcept;
std::optional<loader_type> parse_loader_type (std::string_view text);
bool is_addon_mod (loader_type type) noexcept;
bool is_primary_loader (loader_type type) noexcept;
result<void> check_loader_combination (std::span<const loader_request> loaders,
                                       std::string_view minecraft);
std::vector<loader_info> detect_loaders (const meta::version &resolved);
result<std::vector<loader_version>>
list_loader_versions (loader_type type, std::string_view minecraft,
                      const net::mirror_provider &mirror,
                      cancellation_token token = {});
std::string default_version_id (const install_request &request,
                                std::span<const loader_info> resolved);

class loader_installer
{
public:
  explicit loader_installer (game_installer &installer);
  ~loader_installer ();

  void set_java_resolver (java_resolver resolver);
  result<std::string> install (const install_request &request,
                               progress_sink *sink = nullptr,
                               cancellation_token token = {});

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

}
