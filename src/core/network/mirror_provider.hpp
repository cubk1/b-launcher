#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b_launcher_base::net
{

enum class source_order
{
  mirror_first,
  official_first,
  official_only,
};

struct source_orders
{
  source_order game = source_order::mirror_first;
  source_order assets = source_order::mirror_first;
  source_order modrinth = source_order::official_first;
  source_order curseforge = source_order::official_first;
};

class mirror_provider
{
public:
  virtual ~mirror_provider ();
  virtual std::string_view name () const = 0;
  virtual std::vector<std::string> resolve (std::string_view url) const = 0;
  std::vector<std::string>
  resolve_all (std::span<const std::string> urls) const;
};

struct rewrite_rule
{
  std::string from;
  std::string to;
};

class rewrite_mirror_provider : public mirror_provider
{
public:
  rewrite_mirror_provider (std::string name, std::vector<rewrite_rule> rules,
                           bool keep_original = true,
                           bool original_first = false);

  std::string_view name () const override;
  std::vector<std::string> resolve (std::string_view url) const override;
  std::string rewrite (std::string_view url) const;

private:
  std::string m_name;
  std::vector<rewrite_rule> m_rules;
  bool m_keep_original;
  bool m_original_first;
};

struct routed_rule
{
  rewrite_rule rule;
  source_order order = source_order::mirror_first;
};

class routed_mirror_provider final : public mirror_provider
{
public:
  explicit routed_mirror_provider (std::vector<routed_rule> rules);

  std::string_view name () const override;
  std::vector<std::string> resolve (std::string_view url) const override;

private:
  std::vector<routed_rule> m_rules;
};

class official_mirror_provider final : public mirror_provider
{
public:
  std::string_view name () const override;
  std::vector<std::string> resolve (std::string_view url) const override;
};

std::vector<rewrite_rule> bmclapi_rules (std::string_view root
                                         = "https://bmclapi2.bangbang93.com");
std::vector<rewrite_rule> mcim_modrinth_rules (std::string_view root
                                               = "https://mod.mcimirror.top");
std::vector<rewrite_rule>
mcim_curseforge_rules (std::string_view root = "https://mod.mcimirror.top");
std::shared_ptr<mirror_provider> make_official_provider ();
std::shared_ptr<mirror_provider> make_bmclapi_provider (bool fallback_official
                                                        = true);
std::shared_ptr<mirror_provider> make_official_first_provider ();
std::shared_ptr<mirror_provider>
make_source_provider (const source_orders &orders);
}
