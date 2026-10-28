#include <core/network/mirror_provider.hpp>

#include <algorithm>

namespace b_launcher_base::net
{

mirror_provider::~mirror_provider () = default;

rewrite_mirror_provider::rewrite_mirror_provider (
    std::string name, std::vector<rewrite_rule> rules, bool keep_original,
    bool original_first)
    : m_name (std::move (name))
    , m_rules (std::move (rules))
    , m_keep_original (keep_original)
    , m_original_first (original_first)
{
  std::stable_sort (m_rules.begin (), m_rules.end (),
                    [] (const rewrite_rule &a, const rewrite_rule &b) {
                      return a.from.size () > b.from.size ();
                    });
}

std::string_view
rewrite_mirror_provider::name () const
{
  return m_name;
}

std::string
rewrite_mirror_provider::rewrite (std::string_view url) const
{
  for (const auto &rule : m_rules)
    if (url.starts_with (rule.from))
      return rule.to + std::string (url.substr (rule.from.size ()));
  return std::string (url);
}

std::vector<std::string>
rewrite_mirror_provider::resolve (std::string_view url) const
{
  auto mirrored = rewrite (url);
  std::vector<std::string> out;
  if (mirrored == url)
    {
      out.emplace_back (url);
      return out;
    }
  if (m_original_first)
    {
      out.emplace_back (url);
      out.push_back (std::move (mirrored));
      return out;
    }
  out.push_back (std::move (mirrored));
  if (m_keep_original)
    out.emplace_back (url);
  return out;
}

std::string_view
official_mirror_provider::name () const
{
  return "official";
}

std::vector<std::string>
official_mirror_provider::resolve (std::string_view url) const
{
  return { std::string (url) };
}

std::vector<rewrite_rule>
bmclapi_rules (std::string_view root)
{
  std::string r (root);
  return {
    { "https://launchermeta.mojang.com", r },
    { "https://piston-meta.mojang.com", r },
    { "https://piston-data.mojang.com", r },
    { "https://launcher.mojang.com", r },
    { "https://libraries.minecraft.net", r + "/maven" },
    { "https://resources.download.minecraft.net", r + "/assets" },
    { "http://resources.download.minecraft.net", r + "/assets" },
    { "https://files.minecraftforge.net/maven", r + "/maven" },
    { "http://files.minecraftforge.net/maven", r + "/maven" },
    { "https://maven.minecraftforge.net", r + "/maven" },
    { "https://maven.neoforged.net/releases", r + "/maven" },
    { "https://meta.fabricmc.net", r + "/fabric-meta" },
    { "https://maven.fabricmc.net", r + "/maven" },
    { "https://maven.quiltmc.org/repository/release", r + "/maven" },
    { "http://dl.liteloader.com/versions/versions.json",
      r + "/maven/com/mumfrey/liteloader/versions.json" },
    { "https://dl.liteloader.com/versions/versions.json",
      r + "/maven/com/mumfrey/liteloader/versions.json" },
    { "http://dl.liteloader.com/versions", r + "/maven" },
    { "https://authlib-injector.yushi.moe", r + "/mirrors/authlib-injector" },
  };
}

std::shared_ptr<mirror_provider>
make_official_provider ()
{
  return std::make_shared<official_mirror_provider> ();
}

std::shared_ptr<mirror_provider>
make_bmclapi_provider (bool fallback_official)
{
  return std::make_shared<rewrite_mirror_provider> (
      "bmclapi", bmclapi_rules (), fallback_official, false);
}

std::shared_ptr<mirror_provider>
make_official_first_provider ()
{
  return std::make_shared<rewrite_mirror_provider> (
      "official+bmclapi", bmclapi_rules (), true, true);
}

}
