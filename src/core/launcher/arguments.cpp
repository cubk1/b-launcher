#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>
#include <core/installer/download_plan.hpp>
#include <core/launcher/arguments.hpp>
#include <core/meta/asset_index.hpp>

#include <algorithm>

namespace b_launcher_base::launch
{

namespace
{

using value_map = std::map<std::string, std::string, std::less<>>;

void
append_arguments (std::vector<std::string> &out,
                  const std::vector<meta::argument> &args,
                  const meta::rule_context &rules, const value_map &values)
{
  for (const auto &arg : args)
    {
      if (!meta::rules_allow (arg.rules, rules))
        continue;
      for (const auto &v : arg.values)
        out.push_back (substitute (v, values));
    }
}

bool
contains_prefix (const std::vector<std::string> &args, std::string_view prefix)
{
  return std::any_of (args.begin (), args.end (), [&] (const std::string &a) {
    return a.starts_with (prefix);
  });
}

bool
contains (const std::vector<std::string> &args, std::string_view value)
{
  return std::find (args.begin (), args.end (), value) != args.end ();
}

std::filesystem::path
game_assets_dir (const launch_context &ctx)
{
  const auto &version = *ctx.version;
  auto index_file = ctx.layout->asset_index (version.asset_id ());
  if (auto text = fs::read_file (index_file))
    if (auto index = meta::parse_asset_index (*text))
      {
        if (index->map_to_resources)
          return ctx.game_directory / "resources";
        if (index->is_virtual)
          return ctx.layout->virtual_assets (version.asset_id ());
      }
  return ctx.layout->assets ();
}

}

std::string
substitute (std::string_view text, const value_map &values)
{
  std::string out;
  out.reserve (text.size ());
  std::size_t pos = 0;
  while (pos < text.size ())
    {
      auto start = text.find ("${", pos);
      if (start == std::string_view::npos)
        {
          out += text.substr (pos);
          break;
        }
      auto end = text.find ('}', start + 2);
      if (end == std::string_view::npos)
        {
          out += text.substr (pos);
          break;
        }
      out += text.substr (pos, start - pos);
      auto key = text.substr (start + 2, end - start - 2);
      auto it = values.find (key);
      if (it != values.end ())
        out += it->second;
      else
        out += text.substr (start, end - start + 1);
      pos = end + 1;
    }
  return out;
}

std::vector<std::filesystem::path>
build_classpath (const meta::version &resolved,
                 const meta::game_layout &layout,
                 const meta::rule_context &context)
{
  std::vector<std::filesystem::path> out;
  for (const auto *lib : install::applicable_libraries (resolved, context))
    {
      if (lib->is_legacy_native () && !lib->download)
        continue;
      out.push_back (layout.library (lib->relative_path ()));
    }
  auto jar = layout.version_jar (resolved.jar_id ());
  if (std::find (out.begin (), out.end (), jar) == out.end ())
    out.push_back (jar);
  return out;
}

std::uint32_t
auto_memory_mb (const java::java_installation &java)
{
  if (arch_bits (java.arch) == 32)
    return 1024;
  auto total = current_platform ().total_memory >> 20;
  auto value = static_cast<std::uint32_t> (total / 4);
  return std::clamp<std::uint32_t> (value, 2048, 6144);
}

std::string
quote_argument_file_entry (std::string_view arg)
{
  std::string out = "\"";
  for (char c : arg)
    {
      if (c == '\\' || c == '"')
        out += '\\';
      out += c;
    }
  out += '"';
  return out;
}

std::vector<std::string>
launch_command::command_line () const
{
  std::vector<std::string> out = wrapper;
  out.push_back (path_to_native (executable));
  if (argument_file)
    {
      out.push_back ("@" + path_to_native (*argument_file));
      return out;
    }
  out.insert (out.end (), jvm_args.begin (), jvm_args.end ());
  out.push_back (main_class);
  out.insert (out.end (), game_args.begin (), game_args.end ());
  return out;
}

std::string
launch_command::redacted (std::string_view secret) const
{
  std::vector<std::string> parts = wrapper;
  parts.push_back (path_to_native (executable));
  parts.insert (parts.end (), jvm_args.begin (), jvm_args.end ());
  parts.push_back (main_class);
  parts.insert (parts.end (), game_args.begin (), game_args.end ());
  auto text = join (parts, " ");
  if (secret.size () > 4)
    text = replace_all (text, secret, "<access token>");
  return text;
}

result<launch_command>
build_launch_command (const launch_context &ctx)
{
  if (!ctx.version || !ctx.layout)
    return make_error (errc::invalid_argument, "incomplete launch context");
  const auto &version = *ctx.version;
  const auto &layout = *ctx.layout;
  const auto &settings = ctx.settings;
  const auto &account = ctx.account;
  if (version.main_class.empty ())
    return make_error (errc::parse, "version has no main class");

  auto rules = ctx.rules;
  rules.features["is_demo_user"] = settings.demo;
  rules.features["has_custom_resolution"]
      = settings.width.has_value () && settings.height.has_value ();
  bool quick_multiplayer = !settings.server.empty ();
  bool quick_singleplayer = !settings.quick_play_world.empty ();
  rules.features["has_quick_plays_support"] = false;
  rules.features["is_quick_play_multiplayer"] = quick_multiplayer;
  rules.features["is_quick_play_singleplayer"] = quick_singleplayer;
  rules.features["is_quick_play_realms"] = false;

  auto separator = std::string (1, path_list_separator ());
  auto classpath_entries = build_classpath (version, layout, rules);
  std::vector<std::string> classpath_strings;
  for (const auto &entry : classpath_entries)
    classpath_strings.push_back (path_to_native (entry));
  auto classpath = join (classpath_strings, separator);
  auto client_jar = layout.version_jar (version.jar_id ());

  value_map values;
  values["auth_player_name"] = account.name;
  values["version_name"] = version.id;
  values["game_directory"] = path_to_native (ctx.game_directory);
  values["assets_root"] = path_to_native (layout.assets ());
  values["game_assets"] = path_to_native (game_assets_dir (ctx));
  values["assets_index_name"] = version.asset_id ();
  values["auth_uuid"] = account.uuid;
  values["auth_access_token"] = account.access_token;
  values["auth_session"]
      = sformat ("token:{}:{}", account.access_token, account.uuid);
  values["user_type"] = account.user_type ();
  values["version_type"]
      = version.type.empty () ? settings.launcher_name : version.type;
  values["user_properties"] = "{}";
  values["clientid"] = account.client_id;
  values["auth_xuid"] = account.xuid;
  values["natives_directory"] = path_to_native (ctx.natives_directory);
  values["launcher_name"] = settings.launcher_name;
  values["launcher_version"] = settings.launcher_version;
  values["classpath"] = classpath;
  values["classpath_separator"] = separator;
  values["library_directory"] = path_to_native (layout.libraries ());
  values["primary_jar"] = path_to_native (client_jar);
  values["primary_jar_name"] = path_to_native (client_jar.filename ());
  values["resolution_width"]
      = settings.width ? std::to_string (*settings.width) : "854";
  values["resolution_height"]
      = settings.height ? std::to_string (*settings.height) : "480";
  values["quickPlayMultiplayer"] = settings.server;
  values["quickPlaySingleplayer"] = settings.quick_play_world;
  values["quickPlayPath"]
      = path_to_native (ctx.game_directory / "quickPlay" / "log.json");
  values["quickPlayRealms"] = "";

  launch_command cmd;
  cmd.executable = ctx.java.executable;
#if defined(_WIN32)
  if (!ctx.java.gui_executable.empty ())
    cmd.executable = ctx.java.gui_executable;
#endif
  cmd.working_directory = ctx.game_directory;
  cmd.wrapper = settings.wrapper;
  cmd.environment = settings.environment;
  cmd.main_class = version.main_class;

  auto &jvm = cmd.jvm_args;
  auto max_memory = settings.max_memory_mb ? settings.max_memory_mb
                                           : auto_memory_mb (ctx.java);
  jvm.push_back (sformat ("-Xmx{}m", max_memory));
  if (settings.min_memory_mb)
    jvm.push_back (
        sformat ("-Xms{}m", std::min (settings.min_memory_mb, max_memory)));
  if (settings.default_jvm_tuning)
    {
      auto gc = settings.gc;
      if (gc == gc_mode::generational_zgc && ctx.java.major < 21)
        gc = ctx.java.major >= 15 ? gc_mode::zgc : gc_mode::g1;
      if (gc == gc_mode::zgc && ctx.java.major < 15)
        gc = gc_mode::g1;
      switch (gc)
        {
        case gc_mode::tuned_g1:
          for (auto arg :
               { "-XX:+UnlockExperimentalVMOptions", "-XX:+UseG1GC",
                 "-XX:G1NewSizePercent=20", "-XX:G1ReservePercent=20",
                 "-XX:MaxGCPauseMillis=50", "-XX:G1HeapRegionSize=32M",
                 "-XX:-UseAdaptiveSizePolicy" })
            jvm.emplace_back (arg);
          break;
        case gc_mode::g1:
          jvm.emplace_back ("-XX:+UseG1GC");
          break;
        case gc_mode::zgc:
          jvm.emplace_back ("-XX:+UseZGC");
          break;
        case gc_mode::generational_zgc:
          jvm.emplace_back ("-XX:+UseZGC");
          if (ctx.java.major < 23)
            jvm.emplace_back ("-XX:+ZGenerational");
          break;
        case gc_mode::none:
          break;
        }
      for (auto arg :
           { "-XX:-OmitStackTraceInFastThrow",
             "-Dfml.ignoreInvalidMinecraftCertificates=true",
             "-Dfml.ignorePatchDiscrepancies=true",
             "-Dlog4j2.formatMsgNoLookups=true",
             "-Djava.rmi.server.useCodebaseOnly=true",
             "-Dcom.sun.jndi.rmi.object.trustURLCodebase=false",
             "-Dcom.sun.jndi.cosnaming.object.trustURLCodebase=false" })
        jvm.emplace_back (arg);
      if (ctx.java.major >= 18)
        {
          jvm.emplace_back ("-Dstdout.encoding=UTF-8");
          jvm.emplace_back ("-Dstderr.encoding=UTF-8");
        }
      else
        {
          jvm.emplace_back ("-Dsun.stdout.encoding=UTF-8");
          jvm.emplace_back ("-Dsun.stderr.encoding=UTF-8");
        }
      if (current_platform ().os == os_type::windows)
        jvm.emplace_back ("-Djdk.lang.Process.allowAmbiguousCommands=true");
    }
  for (const auto &arg : settings.jvm_args)
    jvm.push_back (substitute (arg, values));

  if (version.logging && !version.logging->argument.empty ()
      && !version.logging->file_id.empty ())
    {
      auto config = layout.logging_config (version.logging->file_id);
      if (fs::exists (config))
        {
          value_map log_values { { "path", path_to_native (config) } };
          jvm.push_back (substitute (version.logging->argument, log_values));
        }
    }

  if (!version.jvm_arguments.empty ())
    append_arguments (jvm, version.jvm_arguments, rules, values);
  else
    {
      if (rules.os == os_type::windows)
        jvm.emplace_back (
            "-XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_"
            "javaw.exe_minecraft.exe.heapdump");
      if (rules.os == os_type::osx)
        jvm.emplace_back ("-XstartOnFirstThread");
      if (arch_bits (ctx.java.arch) == 32)
        jvm.emplace_back ("-Xss1M");
      jvm.push_back ("-Djava.library.path=" + values["natives_directory"]);
      jvm.push_back ("-Dminecraft.launcher.brand=" + settings.launcher_name);
      jvm.push_back ("-Dminecraft.launcher.version="
                     + settings.launcher_version);
      jvm.emplace_back ("-cp");
      jvm.push_back (classpath);
    }
  for (auto &arg : jvm)
    if (arg.starts_with ("-DignoreList=")
        && arg.find (values["primary_jar_name"]) == std::string::npos)
      arg += "," + values["primary_jar_name"];
  if (!contains (jvm, "-cp") && !contains (jvm, "-classpath")
      && !contains_prefix (jvm, "--class-path"))
    {
      jvm.emplace_back ("-cp");
      jvm.push_back (classpath);
    }

  auto &game = cmd.game_args;
  if (!version.game_arguments.empty ())
    append_arguments (game, version.game_arguments, rules, values);
  else
    for (const auto &arg :
         meta::split_legacy_arguments (version.minecraft_arguments))
      game.push_back (substitute (arg, values));

  if (rules.feature ("has_custom_resolution") && !contains (game, "--width"))
    {
      game.emplace_back ("--width");
      game.push_back (values["resolution_width"]);
      game.emplace_back ("--height");
      game.push_back (values["resolution_height"]);
    }
  if (settings.fullscreen && !contains (game, "--fullscreen"))
    game.emplace_back ("--fullscreen");
  if (settings.demo && !contains (game, "--demo"))
    game.emplace_back ("--demo");
  if (quick_multiplayer && !contains (game, "--quickPlayMultiplayer"))
    {
      std::string host = settings.server;
      std::string port = "25565";
      auto colon = settings.server.rfind (':');
      bool bracketed = settings.server.starts_with ('[');
      bool has_port = colon != std::string::npos
                      && (bracketed ? settings.server.find (']') < colon
                                    : settings.server.find (':') == colon);
      if (has_port)
        {
          host = settings.server.substr (0, colon);
          port = settings.server.substr (colon + 1);
        }
      game.emplace_back ("--server");
      game.push_back (host);
      game.emplace_back ("--port");
      game.push_back (port);
    }
  for (const auto &arg : settings.game_args)
    game.push_back (substitute (arg, values));
  return cmd;
}

}
