#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <core/meta/game_layout.hpp>

namespace b_launcher_base::meta
{

game_layout::game_layout (std::filesystem::path root)
{
  m_root = fs::absolute (root);
  if (m_root.has_filename () == false && m_root.has_parent_path ()
      && m_root != m_root.root_path ())
    m_root = m_root.parent_path ();
}

const std::filesystem::path &
game_layout::root () const noexcept
{
  return m_root;
}

std::filesystem::path
game_layout::versions () const
{
  return m_root / "versions";
}

std::filesystem::path
game_layout::version_dir (std::string_view id) const
{
  return versions () / path_from_utf8 (id);
}

std::filesystem::path
game_layout::version_json (std::string_view id) const
{
  return version_dir (id) / path_from_utf8 (std::string (id) + ".json");
}

std::filesystem::path
game_layout::version_jar (std::string_view id) const
{
  return version_dir (id) / path_from_utf8 (std::string (id) + ".jar");
}

std::filesystem::path
game_layout::natives_dir (std::string_view id) const
{
  return version_dir (id) / "natives";
}

std::filesystem::path
game_layout::libraries () const
{
  return m_root / "libraries";
}

std::filesystem::path
game_layout::library (std::string_view relative) const
{
  return libraries () / path_from_utf8 (relative);
}

std::filesystem::path
game_layout::assets () const
{
  return m_root / "assets";
}

std::filesystem::path
game_layout::asset_index (std::string_view id) const
{
  return assets () / "indexes" / path_from_utf8 (std::string (id) + ".json");
}

std::filesystem::path
game_layout::asset_objects () const
{
  return assets () / "objects";
}

std::filesystem::path
game_layout::asset_object (std::string_view hash) const
{
  return asset_objects () / path_from_utf8 (hash.substr (0, 2))
         / path_from_utf8 (hash);
}

std::filesystem::path
game_layout::virtual_assets (std::string_view id) const
{
  return assets () / "virtual" / path_from_utf8 (id);
}

std::filesystem::path
game_layout::logging_config (std::string_view id) const
{
  return assets () / "log_configs" / path_from_utf8 (id);
}

std::filesystem::path
game_layout::runtime () const
{
  return m_root / "runtime";
}

std::filesystem::path
game_layout::cache () const
{
  return m_root / ".blb-cache";
}

}
