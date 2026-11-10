#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/installer/loader.hpp>
#include <core/meta/version.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace b_launcher_base::install
{

struct server_options
{
  install_options install;
  std::filesystem::path client_root;
  java_resolver java;
};

struct server_result
{
  std::vector<std::string> windows;
  std::vector<std::string> unix;
  bool user_jvm_args = false;
  std::vector<std::string> warnings;
};

result<void> server_supported (const meta::version &resolved);
result<server_result> install_server (const meta::version &resolved,
                                      const std::filesystem::path &target,
                                      const server_options &options,
                                      progress_sink *sink = nullptr,
                                      cancellation_token token = {});

}
