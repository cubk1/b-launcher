#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/instance/instance_manager.hpp>
#include <core/meta/version.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace b_launcher_base::instance
{

namespace
{

using json = nlohmann::json;
constexpr log::logger s_log ("instance");

bool
valid_id (std::string_view id)
{
  if (id.empty () || id.size () > 128 || id == "." || id == "..")
    return false;
  for (char c : id)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
        || c == '<' || c == '>' || c == '|')
      return false;
  return id.back () != '.' && id.back () != ' ';
}

json
settings_to_json (const instance_settings &s)
{
  json out = json::object ();
  if (s.isolated)
    out["isolated"] = *s.isolated;
  if (s.java)
    out["java"] = path_to_utf8 (*s.java);
  if (s.max_memory_mb)
    out["maxMemory"] = s.max_memory_mb;
  if (s.min_memory_mb)
    out["minMemory"] = s.min_memory_mb;
  if (!s.jvm_args.empty ())
    out["jvmArgs"] = s.jvm_args;
  if (!s.game_args.empty ())
    out["gameArgs"] = s.game_args;
  if (s.width)
    out["width"] = *s.width;
  if (s.height)
    out["height"] = *s.height;
  if (s.fullscreen)
    out["fullscreen"] = true;
  if (!s.server.empty ())
    out["server"] = s.server;
  if (!s.login.empty ())
    out["login"] = s.login;
  if (!s.auth_server.empty ())
    out["authServer"] = s.auth_server;
  if (!s.notes.empty ())
    out["notes"] = s.notes;
  if (!s.wrapper.empty ())
    out["wrapper"] = s.wrapper;
  if (!s.pre_launch.empty ())
    out["preLaunch"] = s.pre_launch;
  if (!s.post_exit.empty ())
    out["postExit"] = s.post_exit;
  if (!s.environment.empty ())
    {
      json env = json::object ();
      for (const auto &[key, value] : s.environment)
        env[key] = value;
      out["environment"] = std::move (env);
    }
  if (!s.default_jvm_args)
    out["noDefaultJvmArgs"] = true;
  if (!s.check_integrity)
    out["skipIntegrity"] = true;
  if (!s.launcher_visibility.empty ())
    out["launcherVisibility"] = s.launcher_visibility;
  if (!s.process_priority.empty ())
    out["processPriority"] = s.process_priority;
  if (s.show_logs)
    out["showLogs"] = *s.show_logs;
  if (s.patch)
    out["patch"] = *s.patch;
  return out;
}

instance_settings
settings_from_json (const json &doc)
{
  instance_settings s;
  if (!doc.is_object ())
    return s;
  if (auto it = doc.find ("isolated"); it != doc.end () && it->is_boolean ())
    s.isolated = it->get<bool> ();
  if (auto it = doc.find ("java"); it != doc.end () && it->is_string ())
    s.java = path_from_utf8 (it->get<std::string> ());
  s.max_memory_mb = doc.value ("maxMemory", 0u);
  s.min_memory_mb = doc.value ("minMemory", 0u);
  if (auto it = doc.find ("jvmArgs"); it != doc.end () && it->is_array ())
    for (const auto &v : *it)
      if (v.is_string ())
        s.jvm_args.push_back (v.get<std::string> ());
  if (auto it = doc.find ("gameArgs"); it != doc.end () && it->is_array ())
    for (const auto &v : *it)
      if (v.is_string ())
        s.game_args.push_back (v.get<std::string> ());
  if (auto it = doc.find ("width"); it != doc.end () && it->is_number ())
    s.width = it->get<int> ();
  if (auto it = doc.find ("height"); it != doc.end () && it->is_number ())
    s.height = it->get<int> ();
  s.fullscreen = doc.value ("fullscreen", false);
  s.server = doc.value ("server", std::string ());
  s.login = doc.value ("login", std::string ());
  s.auth_server = doc.value ("authServer", std::string ());
  s.notes = doc.value ("notes", std::string ());
  s.wrapper = doc.value ("wrapper", std::string ());
  s.pre_launch = doc.value ("preLaunch", std::string ());
  s.post_exit = doc.value ("postExit", std::string ());
  if (auto it = doc.find ("environment"); it != doc.end () && it->is_object ())
    for (const auto &[key, value] : it->items ())
      if (value.is_string ())
        s.environment.emplace_back (key, value.get<std::string> ());
  s.default_jvm_args = !doc.value ("noDefaultJvmArgs", false);
  s.check_integrity = !doc.value ("skipIntegrity", false);
  s.launcher_visibility = doc.value ("launcherVisibility", std::string ());
  s.process_priority = doc.value ("processPriority", std::string ());
  if (auto it = doc.find ("showLogs"); it != doc.end () && it->is_boolean ())
    s.show_logs = it->get<bool> ();
  if (auto it = doc.find ("patch"); it != doc.end () && it->is_boolean ())
    s.patch = it->get<bool> ();
  return s;
}

std::vector<std::string>
split_command (std::string_view text)
{
  std::vector<std::string> out;
  std::string current;
  bool quoted = false;
  bool any = false;
  for (char c : text)
    {
      if (c == '"')
        {
          quoted = !quoted;
          any = true;
        }
      else if ((c == ' ' || c == '\t') && !quoted)
        {
          if (any)
            out.push_back (std::move (current));
          current.clear ();
          any = false;
        }
      else
        {
          current.push_back (c);
          any = true;
        }
    }
  if (any)
    out.push_back (std::move (current));
  return out;
}

}

std::string_view
isolation_name (isolation_mode mode) noexcept
{
  switch (mode)
    {
    case isolation_mode::none:
      return "none";
    case isolation_mode::all:
      return "all";
    case isolation_mode::modded:
      return "modded";
    }
  return "modded";
}

std::optional<isolation_mode>
parse_isolation (std::string_view text) noexcept
{
  for (auto mode :
       { isolation_mode::none, isolation_mode::all, isolation_mode::modded })
    if (text == isolation_name (mode))
      return mode;
  return std::nullopt;
}

instance_manager::instance_manager (meta::game_layout layout,
                                    isolation_mode mode)
    : m_layout (std::move (layout))
    , m_mode (mode)
{
}

const meta::game_layout &
instance_manager::layout () const noexcept
{
  return m_layout;
}

isolation_mode
instance_manager::isolation () const noexcept
{
  return m_mode;
}

void
instance_manager::set_isolation (isolation_mode mode) noexcept
{
  m_mode = mode;
}

bool
instance_manager::exists (std::string_view id) const
{
  return fs::exists (m_layout.version_json (id));
}

result<instance>
instance_manager::get (std::string_view id) const
{
  if (!valid_id (id))
    return make_error (errc::invalid_argument,
                       sformat ("invalid instance id '{}'", id));
  if (!exists (id))
    return make_error (errc::version_not_found,
                       sformat ("instance '{}' does not exist", id));
  instance out;
  out.id = std::string (id);
  out.directory = m_layout.version_dir (id);
  auto resolved = meta::resolve_version (m_layout, id);
  if (!resolved)
    {
      out.valid = false;
      out.problem = resolved.err ().message;
      out.chain = { std::string (id) };
      if (auto raw = meta::load_version (m_layout, id);
          raw && raw->inherits_from == id)
        out.minecraft_version = std::string (id);
    }
  else
    {
      out.minecraft_version = resolved->minecraft ();
      out.type = resolved->type;
      out.release_time = resolved->release_time;
      out.loaders = install::detect_loaders (*resolved);
      out.chain = resolved->chain;
    }
  if (auto settings = load_settings (id))
    out.settings = std::move (*settings);
  return out;
}

std::vector<instance>
instance_manager::list () const
{
  std::vector<instance> out;
  for (const auto &[name, st] : fs::list_directory (m_layout.versions ()))
    {
      if (!st.is_directory || !exists (name))
        continue;
      if (auto value = get (name))
        out.push_back (std::move (*value));
    }
  std::sort (out.begin (), out.end (),
             [] (const auto &a, const auto &b) { return a.id < b.id; });
  return out;
}

bool
instance_manager::is_isolated (const instance &value) const noexcept
{
  if (value.settings.isolated)
    return *value.settings.isolated;
  switch (m_mode)
    {
    case isolation_mode::none:
      return false;
    case isolation_mode::all:
      return true;
    case isolation_mode::modded:
      return value.modded ();
    }
  return false;
}

std::filesystem::path
instance_manager::game_directory (const instance &value) const
{
  return is_isolated (value) ? value.directory : m_layout.root ();
}

result<instance_settings>
instance_manager::load_settings (std::string_view id) const
{
  auto file = m_layout.version_dir (id) / settings_file;
  auto text = fs::read_file (file);
  if (!text)
    {
      if (text.err ().code == errc::not_found)
        return instance_settings {};
      return text.err ();
    }
  auto doc = json::parse (*text, nullptr, false);
  if (doc.is_discarded ())
    return make_error (errc::parse, "invalid instance settings");
  return settings_from_json (doc);
}

result<void>
instance_manager::save_settings (std::string_view id,
                                 const instance_settings &settings) const
{
  if (!exists (id))
    return make_error (errc::version_not_found,
                       sformat ("instance '{}' does not exist", id));
  return fs::write_file_atomic (m_layout.version_dir (id) / settings_file,
                                settings_to_json (settings).dump (2));
}

result<void>
instance_manager::rename (std::string_view from, std::string_view to) const
{
  if (!valid_id (to))
    return make_error (errc::invalid_argument,
                       sformat ("invalid instance id '{}'", to));
  if (from == to)
    return ok ();
  if (!exists (from))
    return make_error (errc::version_not_found,
                       sformat ("instance '{}' does not exist", from));
  if (fs::exists (m_layout.version_dir (to)))
    return make_error (errc::already_exists,
                       sformat ("instance '{}' already exists", to));
  BLB_TRY_ASSIGN (auto version, meta::load_version (m_layout, from));
  BLB_TRY (fs::rename_file (m_layout.version_dir (from),
                            m_layout.version_dir (to)));
  auto dir = m_layout.version_dir (to);
  auto old_json = dir / path_from_utf8 (std::string (from) + ".json");
  auto old_jar = dir / path_from_utf8 (std::string (from) + ".jar");
  if (fs::exists (old_jar))
    BLB_TRY (fs::rename_file (old_jar, m_layout.version_jar (to)));
  version.id = std::string (to);
  if (version.jar == from)
    version.jar = std::string (to);
  BLB_TRY (meta::save_version (m_layout, version));
  (void)fs::remove_file (old_json);
  for (const auto &[name, st] : fs::list_directory (m_layout.versions ()))
    {
      if (!st.is_directory || name == to)
        continue;
      auto child = meta::load_version (m_layout, name);
      if (!child)
        continue;
      bool changed = false;
      if (child->inherits_from == from)
        {
          child->inherits_from = std::string (to);
          changed = true;
        }
      if (child->jar == from)
        {
          child->jar = std::string (to);
          changed = true;
        }
      if (changed)
        BLB_TRY (meta::save_version (m_layout, *child));
    }
  s_log.info ("renamed instance {} to {}", from, to);
  return ok ();
}

result<void>
instance_manager::remove (std::string_view id) const
{
  if (!valid_id (id) || !exists (id))
    return make_error (errc::version_not_found,
                       sformat ("instance '{}' does not exist", id));
  return fs::remove_all (m_layout.version_dir (id));
}

result<void>
instance_manager::duplicate (std::string_view from, std::string_view to,
                             bool copy_game_files) const
{
  if (!valid_id (to))
    return make_error (errc::invalid_argument,
                       sformat ("invalid instance id '{}'", to));
  if (fs::exists (m_layout.version_dir (to)))
    return make_error (errc::already_exists,
                       sformat ("instance '{}' already exists", to));
  BLB_TRY_ASSIGN (auto source, get (from));
  BLB_TRY_ASSIGN (auto version, meta::load_version (m_layout, from));
  auto target_dir = m_layout.version_dir (to);
  BLB_TRY (fs::create_directories (target_dir));
  if (copy_game_files && is_isolated (source))
    {
      for (const auto &[text, entry] : fs::list_directory (source.directory))
        {
          if (text == std::string (from) + ".json"
              || text == std::string (from) + ".jar" || text == "natives"
              || text == "launch.args" || entry.is_link)
            continue;
          auto name = path_from_utf8 (text);
          if (entry.is_directory)
            BLB_TRY (
                fs::copy_tree (source.directory / name, target_dir / name));
          else
            BLB_TRY (
                fs::copy_file (source.directory / name, target_dir / name));
        }
    }
  auto jar = m_layout.version_jar (from);
  if (fs::exists (jar))
    BLB_TRY (fs::copy_file (jar, m_layout.version_jar (to)));
  version.id = std::string (to);
  if (version.jar == from)
    version.jar = std::string (to);
  BLB_TRY (meta::save_version (m_layout, version));
  return save_settings (to, source.settings);
}

launch::launch_settings
instance_manager::launch_settings_for (const instance &value) const
{
  launch::launch_settings out;
  out.version_id = value.id;
  out.game_directory = game_directory (value);
  out.java = value.settings.java;
  out.max_memory_mb = value.settings.max_memory_mb;
  out.min_memory_mb = value.settings.min_memory_mb;
  out.jvm_args = value.settings.jvm_args;
  out.game_args = value.settings.game_args;
  out.width = value.settings.width;
  out.height = value.settings.height;
  out.fullscreen = value.settings.fullscreen;
  out.server = value.settings.server;
  out.wrapper = split_command (value.settings.wrapper);
  out.environment = value.settings.environment;
  out.default_jvm_tuning = value.settings.default_jvm_args;
  out.check_integrity = value.settings.check_integrity;
  return out;
}

}
