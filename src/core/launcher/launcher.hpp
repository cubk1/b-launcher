#pragma once

#include <core/auth/account.hpp>
#include <core/auth/microsoft.hpp>
#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/process.hpp>
#include <core/base/progress.hpp>
#include <core/installer/game_installer.hpp>
#include <core/java/java_detect.hpp>
#include <core/launcher/arguments.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace b_launcher_base::launch
{

struct launch_callbacks
{
  output_callback on_output;
  std::function<void (const auth::account &)> on_account_refreshed;
  progress_sink *progress = nullptr;
};

struct launch_timings
{
  std::chrono::milliseconds resolve { 0 };
  std::chrono::milliseconds auth { 0 };
  std::chrono::milliseconds integrity { 0 };
  std::chrono::milliseconds java { 0 };
  std::chrono::milliseconds natives { 0 };
  std::chrono::milliseconds total { 0 };
};

struct prepared_launch
{
  launch_command command;
  meta::version version;
  auth::account account;
  java::java_installation java;
  launch_timings timings;
};

class game_launcher
{
public:
  explicit game_launcher (std::shared_ptr<install::game_installer> installer);
  game_launcher (const game_launcher &) = delete;
  game_launcher &operator= (const game_launcher &) = delete;
  ~game_launcher ();

  void set_authenticator (
      std::shared_ptr<auth::microsoft_authenticator> authenticator);
  void set_java_candidates (std::vector<java::java_installation> candidates);
  std::vector<java::java_installation> java_candidates (bool rescan = false);
  install::game_installer &installer () noexcept;

  result<java::java_installation>
  resolve_java (const meta::version &resolved, const launch_settings &settings,
                progress_sink *progress = nullptr,
                cancellation_token token = {});
  result<prepared_launch> prepare (const launch_settings &settings,
                                   const auth::account &account,
                                   const launch_callbacks &callbacks = {},
                                   cancellation_token token = {});
  result<process> start (const prepared_launch &prepared,
                         const launch_callbacks &callbacks = {});
  result<process> launch (const launch_settings &settings,
                          const auth::account &account,
                          const launch_callbacks &callbacks = {},
                          cancellation_token token = {});

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

}
