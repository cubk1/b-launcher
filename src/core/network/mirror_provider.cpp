#include <core/network/mirror_provider.hpp>

#include <algorithm>

namespace b_launcher_base::net
{

mirror_provider::~mirror_provider () = default;

std::vector<std::string>
mirror_provider::resolve_all (std::span<const std::string> urls) const
{
  std::vector<std::string> front;
  std::vector<std::string> middle;
  std::vector<std::string> back;
  for (const auto &url : urls)
    {
      auto resolved = resolve (url);
      auto self = std::find (resolved.begin (), resolved.end (), url);
      front.insert (front.end (), resolved.begin (), self);
      if (self == resolved.end ())
        continue;
      middle.push_back (*self);
      back.insert (back.end (), self + 1, resolved.end ());
    }
  std::vector<std::string> out;
  for (auto *part : { &front, &middle, &back })
    for (auto &url : *part)
      if (std::find (out.begin (), out.end (), url) == out.end ())
        out.push_back (std::move (url));
  return out;
}

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

routed_mirror_provider::routed_mirror_provider (std::vector<routed_rule> rules)
    : m_rules (std::move (rules))
{
  std::stable_sort (m_rules.begin (), m_rules.end (),
                    [] (const routed_rule &a, const routed_rule &b) {
                      return a.rule.from.size () > b.rule.from.size ();
                    });
}

std::string_view
routed_mirror_provider::name () const
{
  return "routed";
}

std::vector<std::string>
routed_mirror_provider::resolve (std::string_view url) const
{
  for (const auto &[rule, order] : m_rules)
    {
      if (!url.starts_with (rule.from))
        continue;
      if (order == source_order::official_only)
        break;
      auto mirrored = rule.to + std::string (url.substr (rule.from.size ()));
      if (order == source_order::official_first)
        return { std::string (url), std::move (mirrored) };
      return { std::move (mirrored), std::string (url) };
    }
  return { std::string (url) };
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

std::vector<rewrite_rule>
mcim_modrinth_rules (std::string_view root)
{
  std::string r (root);
  return {
    { "https://api.modrinth.com", r + "/modrinth" },
    { "https://cdn.modrinth.com", r },
  };
}

std::vector<rewrite_rule>
mcim_curseforge_rules (std::string_view root)
{
  std::string r (root);
  return {
    { "https://api.curseforge.com", r + "/curseforge" },
    { "https://edge.forgecdn.net", r },
    { "https://mediafilez.forgecdn.net", r },
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

std::shared_ptr<mirror_provider>
make_source_provider (const source_orders &orders)
{
  std::vector<routed_rule> rules;
  for (auto &rule : bmclapi_rules ())
    {
      bool asset = rule.from.find ("resources.download.minecraft.net")
                   != std::string::npos;
      rules.push_back (
          { std::move (rule), asset ? orders.assets : orders.game });
    }
  for (auto &rule : mcim_modrinth_rules ())
    rules.push_back ({ std::move (rule), orders.modrinth });
  for (auto &rule : mcim_curseforge_rules ())
    rules.push_back ({ std::move (rule), orders.curseforge });
  return std::make_shared<routed_mirror_provider> (std::move (rules));
}
}
