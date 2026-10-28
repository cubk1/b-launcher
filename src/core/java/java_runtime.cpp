#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/strings.hpp>
#include <core/java/java_runtime.hpp>
#include <core/network/http_client.hpp>

#include <simdjson.h>

namespace b_launcher_base::java
{

namespace
{

constexpr log::logger s_log ("runtime");

std::string
as_string (simdjson::ondemand::value value)
{
  std::string_view text = value.get_string ();
  return std::string (text);
}

}

std::string
mojang_platform (os_type os, cpu_arch arch)
{
  switch (os)
    {
    case os_type::windows:
      if (arch == cpu_arch::x86)
        return "windows-x86";
      if (arch == cpu_arch::arm64)
        return "windows-arm64";
      return "windows-x64";
    case os_type::osx:
      return arch == cpu_arch::arm64 ? "mac-os-arm64" : "mac-os";
    case os_type::linux:
      return arch == cpu_arch::x86 ? "linux-i386" : "linux";
    case os_type::unknown:
      break;
    }
  return "unknown";
}

result<std::vector<runtime_entry>>
parse_runtime_index (std::string_view json)
{
  try
    {
      simdjson::ondemand::parser parser;
      simdjson::padded_string padded (json);
      auto doc = parser.iterate (padded);
      std::vector<runtime_entry> out;
      for (auto platform : doc.get_object ())
        {
          std::string platform_name (
              std::string_view (platform.unescaped_key ()));
          for (auto component : platform.value ().get_object ())
            {
              std::string component_name (
                  std::string_view (component.unescaped_key ()));
              for (auto item : component.value ().get_array ())
                {
                  runtime_entry entry;
                  entry.platform = platform_name;
                  entry.component = component_name;
                  for (auto field : item.get_object ())
                    {
                      std::string_view key = field.unescaped_key ();
                      if (key == "manifest")
                        for (auto m : field.value ().get_object ())
                          {
                            std::string_view mk = m.unescaped_key ();
                            if (mk == "url")
                              entry.manifest_url = as_string (m.value ());
                            else if (mk == "sha1")
                              entry.manifest_sha1 = as_string (m.value ());
                            else if (mk == "size")
                              entry.manifest_size = m.value ().get_int64 ();
                          }
                      else if (key == "version")
                        for (auto v : field.value ().get_object ())
                          {
                            std::string_view vk = v.unescaped_key ();
                            if (vk == "name")
                              entry.version = as_string (v.value ());
                            else if (vk == "released")
                              entry.released = as_string (v.value ());
                          }
                    }
                  out.push_back (std::move (entry));
                }
            }
        }
      return out;
    }
  catch (const simdjson::simdjson_error &e)
    {
      return make_error (errc::parse,
                         sformat ("invalid runtime index: {}", e.what ()));
    }
}

result<std::vector<runtime_file>>
parse_runtime_manifest (std::string_view json)
{
  try
    {
      simdjson::ondemand::parser parser;
      simdjson::padded_string padded (json);
      auto doc = parser.iterate (padded);
      std::vector<runtime_file> out;
      simdjson::ondemand::object files = doc["files"].get_object ();
      for (auto entry : files)
        {
          runtime_file file;
          file.path = std::string (std::string_view (entry.unescaped_key ()));
          for (auto field : entry.value ().get_object ())
            {
              std::string_view key = field.unescaped_key ();
              if (key == "type")
                {
                  std::string_view type = field.value ().get_string ();
                  file.type = type == "directory"
                                  ? runtime_file_type::directory
                              : type == "link" ? runtime_file_type::link
                                               : runtime_file_type::file;
                }
              else if (key == "executable")
                file.executable = field.value ().get_bool ();
              else if (key == "target")
                file.target = as_string (field.value ());
              else if (key == "downloads")
                for (auto kind : field.value ().get_object ())
                  {
                    std::string_view kind_name = kind.unescaped_key ();
                    if (kind_name != "raw")
                      continue;
                    for (auto raw : kind.value ().get_object ())
                      {
                        std::string_view rk = raw.unescaped_key ();
                        if (rk == "url")
                          file.url = as_string (raw.value ());
                        else if (rk == "sha1")
                          file.sha1 = as_string (raw.value ());
                        else if (rk == "size")
                          file.size = raw.value ().get_int64 ();
                      }
                  }
            }
          out.push_back (std::move (file));
        }
      return out;
    }
  catch (const simdjson::simdjson_error &e)
    {
      return make_error (errc::parse,
                         sformat ("invalid runtime manifest: {}", e.what ()));
    }
}

result<std::vector<runtime_entry>>
fetch_runtime_index (const net::mirror_provider &mirror,
                     cancellation_token token)
{
  BLB_TRY_ASSIGN (auto body,
                  net::http_get (mirror.resolve (runtime_index_url), token));
  return parse_runtime_index (body);
}

std::filesystem::path
runtime_home (const meta::game_layout &layout, std::string_view component,
              std::string_view platform)
{
  auto base = layout.runtime () / path_from_utf8 (component)
              / path_from_utf8 (platform) / path_from_utf8 (component);
  if (platform.starts_with ("mac-os"))
    return base / "jre.bundle" / "Contents" / "Home";
  return base;
}

result<java_installation>
install_runtime (install::game_installer &installer,
                 std::string_view component, progress_sink *sink,
                 cancellation_token token, std::optional<cpu_arch> arch)
{
  const auto &platform_info = current_platform ();
  auto platform
      = mojang_platform (platform_info.os, arch.value_or (platform_info.arch));
  const auto &layout = installer.layout ();
  auto base = layout.runtime () / path_from_utf8 (component)
              / path_from_utf8 (platform) / path_from_utf8 (component);
  auto home = runtime_home (layout, component, platform);

  BLB_TRY_ASSIGN (auto index,
                  fetch_runtime_index (installer.mirror (), token));
  const runtime_entry *entry = nullptr;
  for (const auto &e : index)
    if (e.platform == platform && e.component == component)
      {
        entry = &e;
        break;
      }
  if (!entry || entry->manifest_url.empty ())
    return make_error (
        errc::java_not_found,
        sformat ("runtime '{}' is not available for {}", component, platform));

  BLB_TRY_ASSIGN (
      auto manifest_text,
      net::http_get (installer.mirror ().resolve (entry->manifest_url),
                     token));
  if (auto expected = parse_sha1 (entry->manifest_sha1);
      expected && sha1::digest (manifest_text) != *expected)
    return make_error (errc::checksum_mismatch, "runtime manifest mismatch");
  BLB_TRY_ASSIGN (auto files, parse_runtime_manifest (manifest_text));

  auto relative_base
      = sformat ("runtime/{}/{}/{}", component, platform, component);
  std::vector<install::planned_file> planned;
  for (const auto &file : files)
    {
      auto target = base / path_from_utf8 (file.path);
      if (file.type == runtime_file_type::directory)
        {
          BLB_TRY (fs::create_directories (target));
          continue;
        }
      if (file.type == runtime_file_type::link)
        continue;
      install::planned_file p;
      p.kind = install::file_kind::runtime;
      p.target = target;
      p.relative = relative_base + "/" + file.path;
      p.urls = installer.mirror ().resolve (file.url);
      p.sha1 = parse_sha1 (file.sha1);
      p.size = file.size;
      p.executable = file.executable;
      planned.push_back (std::move (p));
    }
  s_log.info ("installing {} {} ({} files)", component, entry->version,
              planned.size ());
  BLB_TRY (installer.ensure_files (planned, sink, token,
                                   sformat ("java:{}", component)));
#if !defined(_WIN32)
  for (const auto &file : files)
    {
      auto target = base / path_from_utf8 (file.path);
      if (file.type == runtime_file_type::link)
        {
          (void)fs::create_symlink (path_from_utf8 (file.target), target);
        }
      else if (file.type == runtime_file_type::file && file.executable)
        (void)fs::set_executable (target);
    }
#endif
  BLB_TRY (
      fs::write_file_atomic (layout.runtime () / path_from_utf8 (component)
                                 / path_from_utf8 (platform) / ".version",
                             entry->version));
  BLB_TRY_ASSIGN (auto install, probe_java (home, true));
  install.source = java_source::managed;
  return install;
}

}
