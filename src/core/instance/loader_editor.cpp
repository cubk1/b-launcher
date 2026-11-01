#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/instance/loader_editor.hpp>
#include <core/modpack/mods.hpp>

#include <algorithm>
#include <exception>

namespace b_launcher_base::instance
{

namespace
{

constexpr log::logger s_log ("instance");

using install::loader_type;

bool
release_like (std::string_view minecraft)
{
  for (char c : minecraft)
    if (!((c >= '0' && c <= '9') || c == '.'))
      return false;
  return !minecraft.empty ();
}

bool
at_least (std::string_view minecraft, std::string_view version)
{
  return compare_versions (minecraft, version) >= 0;
}

bool
at_most (std::string_view minecraft, std::string_view version)
{
  return compare_versions (minecraft, version) <= 0;
}

std::optional<loader_type>
addon_base (loader_type type)
{
  switch (type)
    {
    case loader_type::fabric_api:
      return loader_type::fabric;
    case loader_type::legacy_fabric_api:
      return loader_type::legacy_fabric;
    case loader_type::quilt_api:
      return loader_type::quilt;
    default:
      return std::nullopt;
    }
}

std::vector<std::string_view>
addon_mod_ids (loader_type type)
{
  switch (type)
    {
    case loader_type::fabric_api:
      return { "fabric-api", "fabric" };
    case loader_type::legacy_fabric_api:
      return { "legacy-fabric-api", "legacyfabric-api" };
    case loader_type::quilt_api:
      return { "qsl", "quilted_fabric_api", "quilt_standard_libraries" };
    default:
      return {};
    }
}

const modpack::mod_info *
find_addon (std::span<const modpack::mod_info> mods, loader_type type)
{
  for (auto id : addon_mod_ids (type))
    if (auto *mod = modpack::find_mod (mods, id))
      return mod;
  return nullptr;
}

const modpack::mod_info *
find_optifine_mod (std::span<const modpack::mod_info> mods)
{
  for (const auto &mod : mods)
    if (to_lower (mod.file_name).starts_with ("optifine"))
      return &mod;
  return nullptr;
}

std::string
optifine_version_from_file (std::string_view file_name)
{
  auto name = std::string (file_name);
  auto dot = name.rfind (".jar");
  if (dot != std::string::npos)
    name.resize (dot);
  auto pos = name.find ("HD_U");
  return pos == std::string::npos ? name : name.substr (pos);
}

std::vector<install::loader_info>
loaders_with_mods (std::vector<install::loader_info> loaders,
                   std::span<const modpack::mod_info> enabled)
{
  for (auto type : { loader_type::fabric_api, loader_type::legacy_fabric_api,
                     loader_type::quilt_api })
    if (auto *mod = find_addon (enabled, type))
      loaders.push_back ({ type, mod->version });
  bool optifine
      = std::any_of (loaders.begin (), loaders.end (), [] (const auto &l) {
          return l.type == loader_type::optifine;
        });
  if (!optifine)
    if (auto *mod = find_optifine_mod (enabled))
      loaders.push_back ({ loader_type::optifine,
                           optifine_version_from_file (mod->file_name) });
  return loaders;
}

// Version metadata may be replaced by the installer before its final check.
// Keep copies until the new mods and settings have also been committed.
class file_changes
{
public:
  explicit file_changes (std::filesystem::path directory)
      : directory (std::move (directory)) {}

  ~file_changes ()
  {
    try
      {
        if (active)
          (void)rollback ();
        if (!active)
          (void)fs::remove_all (directory);
      }
    catch (...) {}
  }

  result<void> remember (const std::filesystem::path &file)
  {
    for (const auto &entry : entries)
      if (entry.file == file)
        return ok ();
    auto backup = directory / (std::to_string (entries.size ()) + ".bak");
    auto st = fs::stat (file);
    if (st.is_directory)
      return make_error (errc::io, "expected a file: " + path_to_utf8 (file));
    if (st.exists)
      BLB_TRY (fs::copy_file (file, backup));
    entries.push_back ({ file, backup, st.exists });
    return ok ();
  }

  result<void> rollback ()
  {
    std::string failures;
    for (auto it = entries.rbegin (); it != entries.rend (); ++it)
      {
        auto restored = it->existed ? fs::copy_file (it->backup, it->file)
                                    : fs::remove_file (it->file);
        if (!restored)
          failures += restored.err ().message + "\n";
      }
    if (!failures.empty ())
      return make_error (errc::io, failures + "backups: " + path_to_utf8 (directory));
    active = false;
    return ok ();
  }

  std::filesystem::path directory;
  bool active = false;

private:
  struct entry
  {
    std::filesystem::path file, backup;
    bool existed;
  };
  std::vector<entry> entries;
};

}

std::string_view
choice_state_name (choice_state state) noexcept
{
  switch (state)
    {
    case choice_state::installed:
      return "installed";
    case choice_state::available:
      return "available";
    case choice_state::incompatible:
      return "incompatible";
    case choice_state::requires_loader:
      return "requires_loader";
    }
  return "unknown";
}

std::vector<loader_type>
supported_loaders (std::string_view minecraft)
{
  std::vector<loader_type> out;
  bool release = release_like (minecraft);
  if (release)
    out.push_back (loader_type::forge);
  if (release && at_least (minecraft, "1.20.1"))
    out.push_back (loader_type::neoforge);
  if (minecraft == "1.12.2")
    out.push_back (loader_type::cleanroom);
  if (!release || at_least (minecraft, "1.14"))
    {
      out.push_back (loader_type::fabric);
      out.push_back (loader_type::fabric_api);
      out.push_back (loader_type::quilt);
      out.push_back (loader_type::quilt_api);
    }
  if (release && at_least (minecraft, "1.3") && at_most (minecraft, "1.13.2"))
    {
      out.push_back (loader_type::legacy_fabric);
      out.push_back (loader_type::legacy_fabric_api);
    }
  if (release && at_least (minecraft, "1.5.2")
      && at_most (minecraft, "1.12.2"))
    out.push_back (loader_type::liteloader);
  if (release && at_least (minecraft, "1.7.2"))
    out.push_back (loader_type::optifine);
  return out;
}

std::vector<loader_choice>
loader_choices (std::string_view minecraft,
                std::span<const install::loader_info> current)
{
  std::vector<loader_choice> out;
  for (auto type : supported_loaders (minecraft))
    {
      loader_choice choice;
      choice.type = type;
      auto installed
          = std::find_if (current.begin (), current.end (),
                          [&] (const auto &l) { return l.type == type; });
      if (installed != current.end ())
        {
          choice.state = choice_state::installed;
          choice.installed_version = installed->version;
          out.push_back (std::move (choice));
          continue;
        }
      if (auto base = addon_base (type))
        {
          bool has_base
              = std::any_of (current.begin (), current.end (),
                             [&] (const auto &l) { return l.type == *base; });
          if (!has_base
              && !(type == loader_type::fabric_api
                   && std::any_of (current.begin (), current.end (),
                                   [] (const auto &l) {
                                     return l.type == loader_type::quilt;
                                   })))
            {
              choice.state = choice_state::requires_loader;
              choice.related = std::string (install::loader_name (*base));
              out.push_back (std::move (choice));
              continue;
            }
        }
      std::vector<install::loader_request> trial;
      for (const auto &l : current)
        trial.push_back ({ l.type, l.version });
      trial.push_back ({ type, "" });
      if (auto check = install::check_loader_combination (trial, minecraft);
          !check)
        {
          choice.state = choice_state::incompatible;
          for (const auto &l : current)
            {
              std::vector<install::loader_request> pair {
                { l.type, l.version }, { type, "" }
              };
              if (!install::check_loader_combination (pair, minecraft))
                {
                  choice.related = std::string (install::loader_name (l.type));
                  break;
                }
            }
          if (choice.related.empty ())
            choice.related = check.err ().message;
        }
      out.push_back (std::move (choice));
    }
  return out;
}

std::vector<std::string>
loader_warnings (std::span<const install::loader_info> current)
{
  std::vector<std::string> out;
  auto has = [&] (loader_type type) {
    return std::any_of (current.begin (), current.end (),
                        [&] (const auto &l) { return l.type == type; });
  };
  if (has (loader_type::fabric) && !has (loader_type::fabric_api))
    out.emplace_back ("Fabric API is not installed, most Fabric mods will not "
                      "work");
  if (has (loader_type::quilt) && !has (loader_type::quilt_api)
      && !has (loader_type::fabric_api))
    out.emplace_back ("QSL is not installed, most Quilt mods will not work");
  if (has (loader_type::legacy_fabric)
      && !has (loader_type::legacy_fabric_api))
    out.emplace_back ("Legacy Fabric API is not installed, most Legacy Fabric "
                      "mods will not work");
  return out;
}

result<std::vector<install::loader_info>>
installed_loaders (const instance_manager &manager, std::string_view id)
{
  BLB_TRY_ASSIGN (auto inst, manager.get (id));
  auto mods = modpack::scan_mods (manager.game_directory (inst) / "mods");
  std::erase_if (mods, [] (const auto &mod) { return !mod.enabled; });
  return loaders_with_mods (std::move (inst.loaders), mods);
}

result<std::string>
modify_loaders (install::game_installer &installer,
                const instance_manager &manager, std::string_view id,
                std::span<const install::loader_request> desired,
                progress_sink *sink, cancellation_token token)
{
  BLB_TRY (token.check ());
  BLB_TRY_ASSIGN (auto inst, manager.get (id));
  if (inst.minecraft_version.empty ())
    return make_error (
        errc::parse,
        sformat ("cannot determine minecraft version of '{}'", id));
  BLB_TRY (
      install::check_loader_combination (desired, inst.minecraft_version));
  auto game_dir = manager.game_directory (inst);
  auto mods_dir = game_dir / "mods";
  auto mods = modpack::scan_mods (mods_dir);
  std::erase_if (mods, [] (const auto &mod) { return !mod.enabled; });
  auto current = loaders_with_mods (inst.loaders, mods);

  auto wanted = [&] (loader_type type) -> const install::loader_request * {
    for (const auto &d : desired)
      if (d.type == type)
        return &d;
    return nullptr;
  };
  std::vector<install::loader_request> request;
  for (const auto &d : desired)
    {
      bool addon = install::is_addon_mod (d.type);
      auto existing
          = std::find_if (current.begin (), current.end (),
                          [&] (const auto &l) { return l.type == d.type; });
      bool same = existing != current.end ()
                  && (d.version.empty () || d.version == existing->version);
      if (addon && same)
        continue;
      request.push_back (d);
    }
  std::vector<std::filesystem::path> removed;
  for (const auto &l : current)
    {
      auto *target = wanted (l.type);
      bool removing = !target;
      bool replacing = target && !target->version.empty ()
                       && target->version != l.version;
      if (!removing && !replacing)
        continue;
      const modpack::mod_info *file = nullptr;
      if (install::is_addon_mod (l.type))
        file = find_addon (mods, l.type);
      else if (l.type == loader_type::optifine)
        file = find_optifine_mod (mods);
      if (file)
        removed.push_back (file->file);
    }

  file_changes changes (manager.layout ().cache () / "loader-edit" / random_bytes_hex (8));
  BLB_TRY (fs::create_directories (changes.directory));
  BLB_TRY (changes.remember (manager.layout ().version_json (id)));
  BLB_TRY (changes.remember (manager.layout ().version_jar (id)));
  BLB_TRY (changes.remember (manager.layout ().version_dir (id) / settings_file));
  auto staged_mods = changes.directory / "mods";
  install::loader_installer loaders (installer);
  install::install_request install_request;
  install_request.minecraft = inst.minecraft_version;
  install_request.loaders = request;
  install_request.id = std::string (id);
  install_request.mods_directory = staged_mods;
  changes.active = true;
  auto install_and_commit = [&] () -> result<std::string> {
    BLB_TRY_ASSIGN (auto installed, loaders.install (install_request, sink, token));
    BLB_TRY (token.check ());
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> added;
    for (const auto &[name, st] : fs::list_directory (staged_mods))
      {
        if (st.is_directory || st.is_link || !fs::safe_relative (name))
          return make_error (errc::io, "invalid staged mod: " + name);
        auto target = mods_dir / path_from_utf8 (name);
        BLB_TRY (changes.remember (target));
        added.emplace_back (staged_mods / path_from_utf8 (name), target);
      }
    for (const auto &file : removed)
      BLB_TRY (changes.remember (file));
    for (const auto &file : removed)
      BLB_TRY (fs::remove_file (file));
    for (const auto &[from, to] : added)
      BLB_TRY (fs::copy_file (from, to));
    // Adding/removing a loader must not switch the instance's game directory.
    inst.settings.isolated = manager.is_isolated (inst);
    BLB_TRY (manager.save_settings (id, inst.settings));
    changes.active = false;
    return installed;
  };
  result<std::string> installed;
  try
    {
      installed = install_and_commit ();
    }
  catch (const std::exception &e)
    {
      installed = make_error (errc::unknown, e.what ());
    }
  if (!installed)
    {
      if (auto restored = changes.rollback (); !restored)
        return make_error (errc::io, installed.err ().message + "\n" + restored.err ().message);
      return std::move (installed).err ();
    }
  s_log.info ("modified loaders of {}", *installed);
  return *installed;
}

}
