#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace b_launcher_base::meta
{

class game_layout
{
public:
  game_layout () = default;
  explicit game_layout (std::filesystem::path root);

  const std::filesystem::path &root () const noexcept;
  std::filesystem::path versions () const;
  std::filesystem::path version_dir (std::string_view id) const;
  std::filesystem::path version_json (std::string_view id) const;
  std::filesystem::path version_jar (std::string_view id) const;
  std::filesystem::path natives_dir (std::string_view id) const;
  std::filesystem::path libraries () const;
  std::filesystem::path library (std::string_view relative) const;
  std::filesystem::path assets () const;
  std::filesystem::path asset_index (std::string_view id) const;
  std::filesystem::path asset_objects () const;
  std::filesystem::path asset_object (std::string_view hash) const;
  std::filesystem::path virtual_assets (std::string_view id) const;
  std::filesystem::path logging_config (std::string_view id) const;
  std::filesystem::path runtime () const;
  std::filesystem::path cache () const;

private:
  std::filesystem::path m_root;
};

}
