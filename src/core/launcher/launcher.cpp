#include <core/auth/yggdrasil.hpp>
#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/installer/task_graph.hpp>
#include <core/java/java_requirement.hpp>
#include <core/java/java_runtime.hpp>
#include <core/launcher/launcher.hpp>
#include <core/launcher/natives.hpp>

#include "core/base/win_util.hpp"

#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace b_launcher_base::launch
{

namespace
{

constexpr log::logger s_log ("launcher");

std::string
argument_file_text (const std::string &utf8)
{
#if defined(_WIN32)
  if (GetACP () == CP_UTF8 || utf8.empty ())
    return utf8;
  std::wstring wide = detail::utf8_to_wide (utf8);
  BOOL lossy = FALSE;
  int size = WideCharToMultiByte (CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data (),
                                  static_cast<int> (wide.size ()), nullptr, 0,
                                  nullptr, &lossy);
  std::string out (static_cast<std::size_t> (size), '\0');
  WideCharToMultiByte (CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data (),
                       static_cast<int> (wide.size ()), out.data (), size,
                       nullptr, &lossy);
  if (lossy)
    s_log.warn ("the launch arguments have characters code page {} lacks; "
                "Java will not find those paths",
                GetACP ());
  return out;
#else
  return utf8;
#endif
}

using clock = std::chrono::steady_clock;

std::chrono::milliseconds
since (clock::time_point start)
{
  return std::chrono::duration_cast<std::chrono::milliseconds> (clock::now ()
                                                                - start);
}

std::vector<install::planned_file>
split_natives (std::vector<install::planned_file> &files)
{
  std::vector<install::planned_file> natives;
  std::erase_if (files, [&] (install::planned_file &f) {
    if (f.kind != install::file_kind::native)
      return false;
    natives.push_back (std::move (f));
    return true;
  });
  return natives;
}

}

struct game_launcher::impl
{
  std::shared_ptr<install::game_installer> installer;
  std::shared_ptr<auth::microsoft_authenticator> authenticator;
  std::mutex java_mutex;
  std::optional<std::vector<java::java_installation>> java_cache;
};

game_launcher::game_launcher (
    std::shared_ptr<install::game_installer> installer)
    : m_impl (std::make_unique<impl> ())
{
  m_impl->installer = std::move (installer);
}

game_launcher::~game_launcher () = default;

void
game_launcher::set_authenticator (
    std::shared_ptr<auth::microsoft_authenticator> authenticator)
{
  m_impl->authenticator = std::move (authenticator);
}

void
game_launcher::set_java_candidates (
    std::vector<java::java_installation> candidates)
{
  std::lock_guard lock (m_impl->java_mutex);
  m_impl->java_cache = std::move (candidates);
}

std::vector<java::java_installation>
game_launcher::java_candidates (bool rescan)
{
  std::lock_guard lock (m_impl->java_mutex);
  if (!m_impl->java_cache || rescan)
    {
      java::scan_options options;
      options.runtime_roots.push_back (
          m_impl->installer->layout ().runtime ());
      m_impl->java_cache = java::scan_java (options);
    }
  return *m_impl->java_cache;
}

install::game_installer &
game_launcher::installer () noexcept
{
  return *m_impl->installer;
}

result<java::java_installation>
game_launcher::resolve_java (const meta::version &resolved,
                             const launch_settings &settings,
                             progress_sink *progress, cancellation_token token)
{
  if (settings.java)
    {
      BLB_TRY_ASSIGN (auto install, java::probe_java (*settings.java));
      install.source = java::java_source::user;
      return install;
    }
  auto requirement = java::requirement_for (resolved);
  auto candidates = java_candidates ();
  if (auto chosen = java::select_java (candidates, requirement))
    return *chosen;
  if (!settings.auto_download_java)
    return make_error (errc::java_not_found,
                       sformat ("no java {}+ found", requirement.min_major));
  s_log.info ("no suitable java, installing runtime {}",
              requirement.component);
  BLB_TRY_ASSIGN (auto installed, java::install_runtime (*m_impl->installer,
                                                         requirement.component,
                                                         progress, token));
  {
    std::lock_guard lock (m_impl->java_mutex);
    if (m_impl->java_cache)
      m_impl->java_cache->push_back (installed);
  }
  if (!java::satisfies (installed, requirement))
    return make_error (errc::java_not_found,
                       sformat ("installed runtime {} does not satisfy "
                                "java {}",
                                installed.version, requirement.min_major));
  return installed;
}

result<prepared_launch>
game_launcher::prepare (const launch_settings &settings,
                        const auth::account &account,
                        const launch_callbacks &callbacks,
                        cancellation_token token)
{
  auto started = clock::now ();
  auto &installer = *m_impl->installer;
  const auto &layout = installer.layout ();
  prepared_launch out;
  out.account = account;

  auto step = clock::now ();
  BLB_TRY_ASSIGN (out.version,
                  meta::resolve_version (layout, settings.version_id));
  out.timings.resolve = since (step);

  auto game_dir = settings.game_directory.empty () ? layout.root ()
                                                   : settings.game_directory;
  auto natives_dir = layout.natives_dir (settings.version_id);
  auto host_rules = meta::rule_context::current ();
  BLB_TRY (fs::create_directories (game_dir));

  auto files = install::plan_version_files (out.version, layout,
                                            installer.mirror (), host_rules);
  auto native_files = split_natives (files);

  std::vector<std::string> agent;
  install::task_graph graph;
  graph.add ("auth", [&] (cancellation_token t) -> result<void> {
    auto begin = clock::now ();
    if (auth::is_yggdrasil (out.account.type))
      {
        BLB_TRY_ASSIGN (auto fresh, auth::yggdrasil_ensure (out.account, t));
        bool changed = fresh.access_token != out.account.access_token
                       || fresh.name != out.account.name
                       || fresh.skin_url != out.account.skin_url;
        out.account = std::move (fresh);
        if (changed && callbacks.on_account_refreshed)
          callbacks.on_account_refreshed (out.account);
        BLB_TRY_ASSIGN (
            agent, auth::yggdrasil_agent (out.account, layout.cache (), t));
      }
    else if (out.account.needs_refresh ())
      {
        if (!m_impl->authenticator)
          return make_error (errc::auth_expired,
                             "account token expired and no authenticator");
        BLB_TRY_ASSIGN (out.account,
                        m_impl->authenticator->refresh (out.account, t));
        if (callbacks.on_account_refreshed)
          callbacks.on_account_refreshed (out.account);
      }
    out.timings.auth = since (begin);
    return {};
  });
  graph.add ("integrity", [&] (cancellation_token t) -> result<void> {
    auto begin = clock::now ();
    if (settings.check_integrity)
      {
        BLB_TRY (installer.complete_version (out.version, host_rules,
                                             callbacks.progress, t, false));
        BLB_TRY (installer.prepare_legacy_assets (out.version, game_dir));
      }
    out.timings.integrity = since (begin);
    return {};
  });
  graph.add ("java", [&] (cancellation_token t) -> result<void> {
    auto begin = clock::now ();
    BLB_TRY_ASSIGN (
        out.java, resolve_java (out.version, settings, callbacks.progress, t));
    out.timings.java = since (begin);
    return {};
  });
  graph.add ("natives", [&] (cancellation_token t) -> result<void> {
    auto begin = clock::now ();
    if (!native_files.empty ())
      BLB_TRY (installer.ensure_files (native_files, callbacks.progress, t,
                                       "natives"));
    BLB_TRY (prepare_natives (out.version, layout, host_rules, natives_dir));
    out.timings.natives = since (begin);
    return {};
  });
  BLB_TRY (graph.run (token, callbacks.progress));

  auto java_rules = host_rules;
  java_rules.arch = out.java.arch;
  if (java_rules.arch != host_rules.arch)
    {
      auto planned = install::plan_version_files (
          out.version, layout, installer.mirror (), java_rules);
      auto extra = split_natives (planned);
      if (!extra.empty ())
        BLB_TRY (installer.ensure_files (std::move (extra), callbacks.progress,
                                         token, "natives"));
      BLB_TRY (prepare_natives (out.version, layout, java_rules, natives_dir));
    }

  launch_context ctx;
  ctx.version = &out.version;
  ctx.layout = &layout;
  ctx.account = out.account;
  ctx.java = out.java;
  ctx.game_directory = game_dir;
  ctx.natives_directory = natives_dir;
  ctx.rules = java_rules;
  ctx.settings = settings;
  BLB_TRY_ASSIGN (out.command, build_launch_command (ctx));
  out.command.jvm_args.insert (out.command.jvm_args.begin (), agent.begin (),
                               agent.end ());

  if (settings.use_argument_file && out.java.major >= 9)
    {
      std::string content;
      for (const auto &arg : out.command.jvm_args)
        content += quote_argument_file_entry (arg) + "\n";
      content += quote_argument_file_entry (out.command.main_class) + "\n";
      for (const auto &arg : out.command.game_args)
        content += quote_argument_file_entry (arg) + "\n";
      auto file = layout.version_dir (settings.version_id) / "launch.args";
      BLB_TRY (fs::write_file_atomic (file, argument_file_text (content)));
      out.command.argument_file = file;
    }
  else if (out.java.major < 9)
    {
      std::size_t length = 0;
      for (const auto &arg : out.command.command_line ())
        length += arg.size () + 3;
      if (length > 30000)
        {
          auto &jvm = out.command.jvm_args;
          for (std::size_t i = 0; i + 1 < jvm.size (); ++i)
            if (jvm[i] == "-cp" || jvm[i] == "-classpath")
              {
                out.command.environment.emplace_back ("CLASSPATH", jvm[i + 1]);
                jvm.erase (jvm.begin () + static_cast<std::ptrdiff_t> (i),
                           jvm.begin () + static_cast<std::ptrdiff_t> (i + 2));
                break;
              }
        }
    }
  out.timings.total = since (started);
  s_log.info ("prepared {} in {} ms (resolve {}, auth {}, integrity {}, "
              "java {}, natives {})",
              settings.version_id, out.timings.total.count (),
              out.timings.resolve.count (), out.timings.auth.count (),
              out.timings.integrity.count (), out.timings.java.count (),
              out.timings.natives.count ());
  s_log.debug ("command: {}", out.command.redacted (out.account.access_token));
  return out;
}

result<process>
game_launcher::start (const prepared_launch &prepared,
                      const launch_callbacks &callbacks)
{
  process_options options;
  options.args = prepared.command.command_line ();
  options.working_directory = prepared.command.working_directory;
  options.environment = prepared.command.environment;
  options.capture_output = static_cast<bool> (callbacks.on_output);
  s_log.info ("starting {} with java {} ({})", prepared.version.id,
              prepared.java.version,
              path_to_native (prepared.java.executable));
  return process::start (options, callbacks.on_output);
}

result<process>
game_launcher::launch (const launch_settings &settings,
                       const auth::account &account,
                       const launch_callbacks &callbacks,
                       cancellation_token token)
{
  BLB_TRY_ASSIGN (auto prepared,
                  prepare (settings, account, callbacks, token));
  return start (prepared, callbacks);
}

}
