#pragma once

#include <core/base/error.hpp>
#include <core/meta/game_layout.hpp>
#include <core/meta/maven.hpp>
#include <core/meta/rules.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b_launcher_base::meta
{

struct download_artifact
{
  std::string path;
  std::string url;
  std::string sha1;
  std::int64_t size = -1;
};

struct library
{
  std::string name;
  maven_artifact artifact;
  std::optional<download_artifact> download;
  std::map<std::string, download_artifact> classifiers;
  std::map<std::string, std::string> natives;
  std::vector<std::string> extract_exclude;
  std::vector<rule> rules;
  std::string url;
  std::string sha1;
  std::int64_t size = -1;
  std::vector<std::string> checksums;
  std::optional<bool> client_required;

  bool is_legacy_native () const noexcept;
  bool is_modern_native () const noexcept;
  std::optional<std::string>
  native_classifier (const rule_context &context) const;
  std::string relative_path () const;
  std::string default_url () const;
};

struct argument
{
  std::vector<std::string> values;
  std::vector<rule> rules;
};

struct asset_index_ref
{
  std::string id;
  std::string sha1;
  std::string url;
  std::int64_t size = -1;
  std::int64_t total_size = -1;
};

struct java_version_ref
{
  std::string component;
  int major_version = 0;
};

struct logging_ref
{
  std::string argument;
  std::string type;
  std::string file_id;
  download_artifact file;
};

struct version
{
  std::string id;
  std::string inherits_from;
  std::string type;
  std::string main_class;
  std::string minecraft_arguments;
  std::string assets;
  std::string jar;
  std::string release_time;
  std::string time;
  std::string client_version;
  bool has_arguments = false;
  std::vector<argument> game_arguments;
  std::vector<argument> jvm_arguments;
  std::vector<library> libraries;
  std::optional<asset_index_ref> asset_index;
  std::map<std::string, download_artifact> downloads;
  std::optional<java_version_ref> java_version;
  std::optional<logging_ref> logging;
  int compliance_level = 0;
  int minimum_launcher_version = 0;
  std::vector<std::string> chain;

  std::string jar_id () const;
  std::string asset_id () const;
  std::string minecraft () const;
  const download_artifact *client_download () const;
};

result<version> parse_version (std::string_view json);
result<library> parse_library (std::string_view json);
std::string serialize_version (const version &value);
version merge_versions (const version &parent, const version &child);
result<version> load_version (const game_layout &layout, std::string_view id);
result<version> resolve_version (const game_layout &layout,
                                 std::string_view id);
result<void> save_version (const game_layout &layout, const version &value);
std::vector<std::string> split_legacy_arguments (std::string_view text);

}
