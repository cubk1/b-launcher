#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace b_launcher_base::meta
{

struct maven_artifact
{
  std::string group;
  std::string name;
  std::string version;
  std::string classifier;
  std::string extension = "jar";

  static std::optional<maven_artifact> parse (std::string_view descriptor);

  std::string path () const;
  std::string file_name () const;
  std::string descriptor () const;
  std::string key () const;
  maven_artifact with_classifier (std::string_view value) const;

  bool operator== (const maven_artifact &) const = default;
};

}
