#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>
#include <core/installer/download_plan.hpp>
#include <core/installer/integrity.hpp>
#include <core/meta/game_layout.hpp>
#include <core/meta/rules.hpp>
#include <core/meta/version.hpp>
#include <core/meta/version_manifest.hpp>
#include <core/network/download_client.hpp>
#include <core/network/mirror_provider.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace b_launcher_base::install
{

struct install_options
{
  std::shared_ptr<net::mirror_provider> mirror;
  net::download_options download;
  verify_mode verify = verify_mode::cached_hash;
  std::vector<std::filesystem::path> import_sources;
  bool discover_import_sources = true;
  bool verify_imported = true;
  bool assets = true;
};

enum class completion_scope : std::uint8_t
{
  all,
  core,
  assets,
};

struct completion_report
{
  std::size_t planned = 0;
  std::size_t invalid = 0;
  std::size_t imported = 0;
  std::size_t downloaded = 0;
  std::uint64_t downloaded_bytes = 0;
  std::chrono::milliseconds verify_time { 0 };
  std::chrono::milliseconds download_time { 0 };
};

class game_installer
{
public:
  explicit game_installer (meta::game_layout layout,
                           install_options options = {});
  game_installer (const game_installer &) = delete;
  game_installer &operator= (const game_installer &) = delete;
  ~game_installer ();

  const meta::game_layout &layout () const noexcept;
  const install_options &options () const noexcept;
  net::mirror_provider &mirror () const noexcept;
  std::shared_ptr<net::mirror_provider> mirror_ptr () const noexcept;
  net::download_client &downloader () noexcept;
  integrity_cache &cache () noexcept;
  std::vector<std::filesystem::path> import_sources () const;

  result<meta::version_manifest> manifest (cancellation_token token = {},
                                           bool refresh = false);
  result<meta::version> fetch_version_json (std::string_view id,
                                            cancellation_token token = {});
  result<completion_report> ensure_files (std::vector<planned_file> files,
                                          progress_sink *sink = nullptr,
                                          cancellation_token token = {},
                                          std::string_view task = "files");
  result<completion_report>
  complete_version (const meta::version &resolved,
                    const meta::rule_context &context,
                    progress_sink *sink = nullptr,
                    cancellation_token token = {}, bool include_natives = true,
                    completion_scope scope = completion_scope::all);
  result<meta::version> install_vanilla (std::string_view id,
                                         progress_sink *sink = nullptr,
                                         cancellation_token token = {});
  result<void> prepare_legacy_assets (const meta::version &resolved,
                                      const std::filesystem::path &game_dir);

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

}
