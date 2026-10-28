#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/log.hpp>
#include <core/base/process.hpp>
#include <core/base/strings.hpp>
#include <core/base/thread_pool.hpp>
#include <core/java/java_detect.hpp>

#include "core/base/win_util.hpp"

#include <algorithm>
#include <map>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace b_launcher_base::java
{

namespace
{

constexpr log::logger s_log ("java");

struct candidate
{
  std::filesystem::path home;
  java_source source;
};

std::filesystem::path
java_binary (const std::filesystem::path &home, bool gui)
{
#if defined(_WIN32)
  return home / "bin" / (gui ? "javaw.exe" : "java.exe");
#else
  (void)gui;
  return home / "bin" / "java";
#endif
}

std::string
unquote (std::string_view value)
{
  value = trim (value);
  if (value.size () >= 2 && value.front () == '"' && value.back () == '"')
    value = value.substr (1, value.size () - 2);
  return std::string (value);
}

std::map<std::string, std::string>
read_release (const std::filesystem::path &home)
{
  std::map<std::string, std::string> values;
  auto text = fs::read_file (home / "release");
  if (!text)
    return values;
  for (auto line : split (*text, '\n'))
    {
      auto eq = line.find ('=');
      if (eq == std::string_view::npos)
        continue;
      values.emplace (std::string (trim (line.substr (0, eq))),
                      unquote (line.substr (eq + 1)));
    }
  return values;
}

void
add_home (std::vector<candidate> &out, const std::filesystem::path &home,
          java_source source)
{
  if (!home.empty () && fs::exists (java_binary (home, false)))
    out.push_back ({ home, source });
}

void
scan_tree (std::vector<candidate> &out, const std::filesystem::path &root,
           java_source source, int depth)
{
  if (!fs::stat (root).is_directory)
    return;
  if (fs::exists (java_binary (root, false)))
    {
      out.push_back ({ root, source });
      return;
    }
#if defined(__APPLE__)
  if (fs::exists (java_binary (root / "Contents" / "Home", false)))
    {
      out.push_back ({ root / "Contents" / "Home", source });
      return;
    }
#endif
  if (depth <= 0)
    return;
  for (const auto &[name, st] : fs::list_directory (root))
    if (st.is_directory && name != "bin" && name != "lib" && name != "legal"
        && name != "include" && name != "jmods" && name != "conf")
      scan_tree (out, root / path_from_utf8 (name), source, depth - 1);
}

#if defined(_WIN32)

void
scan_registry_key (std::vector<candidate> &out, HKEY root, const wchar_t *path,
                   REGSAM view, const wchar_t *value_name,
                   const wchar_t *subkey_suffix)
{
  HKEY key;
  if (RegOpenKeyExW (root, path, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
    return;
  wchar_t name[256];
  for (DWORD i = 0;; ++i)
    {
      DWORD length = 256;
      if (RegEnumKeyExW (key, i, name, &length, nullptr, nullptr, nullptr,
                         nullptr)
          != ERROR_SUCCESS)
        break;
      std::wstring sub = name;
      if (subkey_suffix)
        sub += subkey_suffix;
      HKEY child;
      if (RegOpenKeyExW (key, sub.c_str (), 0, KEY_READ | view, &child)
          != ERROR_SUCCESS)
        continue;
      wchar_t buffer[1024];
      DWORD bytes = sizeof (buffer);
      DWORD type = 0;
      if (RegQueryValueExW (child, value_name, nullptr, &type,
                            reinterpret_cast<LPBYTE> (buffer), &bytes)
              == ERROR_SUCCESS
          && (type == REG_SZ || type == REG_EXPAND_SZ))
        {
          std::wstring value (buffer, bytes / sizeof (wchar_t));
          while (!value.empty () && value.back () == L'\0')
            value.pop_back ();
          add_home (out, std::filesystem::path (value), java_source::registry);
        }
      RegCloseKey (child);
    }
  RegCloseKey (key);
}

void
scan_registry (std::vector<candidate> &out)
{
  for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY })
    {
      for (auto *path :
           { L"SOFTWARE\\JavaSoft\\Java Runtime Environment",
             L"SOFTWARE\\JavaSoft\\Java Development Kit",
             L"SOFTWARE\\JavaSoft\\JRE", L"SOFTWARE\\JavaSoft\\JDK" })
        scan_registry_key (out, HKEY_LOCAL_MACHINE, path, view, L"JavaHome",
                           nullptr);
      for (auto *path : { L"SOFTWARE\\Eclipse Adoptium\\JRE",
                          L"SOFTWARE\\Eclipse Adoptium\\JDK",
                          L"SOFTWARE\\Eclipse Foundation\\JDK",
                          L"SOFTWARE\\Microsoft\\JDK" })
        scan_registry_key (out, HKEY_LOCAL_MACHINE, path, view, L"Path",
                           L"\\hotspot\\MSI");
      scan_registry_key (out, HKEY_LOCAL_MACHINE,
                         L"SOFTWARE\\Azul Systems\\Zulu", view,
                         L"InstallationPath", nullptr);
    }
}

#endif

std::vector<candidate>
collect_candidates (const scan_options &options)
{
  std::vector<candidate> out;
  for (const auto &home : options.extra_homes)
    {
      auto st = fs::stat (home);
      if (st.exists && !st.is_directory)
        add_home (out, java_home_from_executable (home), java_source::user);
      else
        add_home (out, home, java_source::user);
    }
  for (const auto &root : options.extra_roots)
    scan_tree (out, root, java_source::user, 3);
  for (const auto &root : options.runtime_roots)
    scan_tree (out, root, java_source::managed, 4);
  if (!options.system_locations)
    return out;

  if (auto home = environment ("JAVA_HOME"))
    add_home (out, path_from_utf8 (*home), java_source::java_home);
  if (auto path = environment ("PATH"))
    for (auto entry : split (*path, path_list_separator ()))
      {
        auto dir = trim (entry);
        if (dir.empty ())
          continue;
        auto bin = path_from_utf8 (dir);
        if (bin.filename () == "bin")
          add_home (out, bin.parent_path (), java_source::path);
      }

  auto home = home_directory ();
  scan_tree (out, default_minecraft_directory () / "runtime",
             java_source::minecraft_runtime, 4);
#if defined(_WIN32)
  scan_registry (out);
  std::vector<std::filesystem::path> program_dirs;
  for (auto name : { "ProgramFiles", "ProgramW6432", "ProgramFiles(x86)" })
    if (auto dir = environment (name))
      program_dirs.push_back (path_from_utf8 (*dir));
  for (const auto &dir : program_dirs)
    for (auto vendor :
         { "Java", "Eclipse Adoptium", "Eclipse Foundation", "AdoptOpenJDK",
           "Zulu", "Microsoft", "BellSoft", "Amazon Corretto", "Semeru",
           "Huawei", "Alibaba", "Tencent", "Liberica", "GraalVM" })
      scan_tree (out, dir / vendor, java_source::system, 2);
  if (auto local = environment ("LOCALAPPDATA"))
    scan_tree (out,
               path_from_utf8 (*local)
                   / "Packages/Microsoft.4297127D64EC6_8wekyb3d8bbwe/"
                     "LocalCache/Local/runtime",
               java_source::minecraft_runtime, 4);
  scan_tree (out, app_data_directory () / ".hmcl" / "java",
             java_source::system, 3);
  if (fs::stat (home / "scoop" / "apps").is_directory)
    for (const auto &[name, st] : fs::list_directory (home / "scoop" / "apps"))
      if (st.is_directory)
        add_home (out,
                  home / "scoop" / "apps" / path_from_utf8 (name) / "current",
                  java_source::system);
#elif defined(__APPLE__)
  scan_tree (out, "/Library/Java/JavaVirtualMachines", java_source::system, 2);
  scan_tree (out, home / "Library/Java/JavaVirtualMachines",
             java_source::system, 2);
#else
  for (auto dir : { "/usr/lib/jvm", "/usr/lib64/jvm", "/usr/lib32/jvm",
                    "/usr/java", "/opt/java", "/opt/jdk", "/opt/jdks" })
    scan_tree (out, dir, java_source::system, 2);
  scan_tree (out, home / ".sdkman/candidates/java", java_source::system, 2);
  scan_tree (out, home / ".local/share/hmcl/java", java_source::system, 3);
#endif
  scan_tree (out, home / ".jdks", java_source::system, 2);
  return out;
}

result<java_installation>
probe_with_process (java_installation install)
{
  process_options options;
  options.args = { path_to_utf8 (install.executable),
                   "-XshowSettings:properties", "-version" };
  BLB_TRY_ASSIGN (
      auto output,
      run_process (options, {}, std::chrono::milliseconds (15000)));
  auto text = output.err + output.out;
  for (auto line : split (text, '\n'))
    {
      auto eq = line.find (" = ");
      if (eq == std::string_view::npos)
        continue;
      auto key = trim (line.substr (0, eq));
      auto value = std::string (trim (line.substr (eq + 3)));
      if (key == "java.version")
        install.version = value;
      else if (key == "os.arch")
        {
          if (auto arch = parse_arch (value))
            install.arch = *arch;
        }
      else if (key == "java.vendor")
        install.vendor = value;
    }
  if (install.version.empty ())
    return make_error (errc::java_not_found,
                       sformat ("cannot read version from {}",
                                path_to_utf8 (install.executable)));
  install.major = parse_java_major (install.version);
  return install;
}

}

int
parse_java_major (std::string_view version) noexcept
{
  version = trim (version);
  if (version.starts_with ("1."))
    version.remove_prefix (2);
  int major = 0;
  for (char c : version)
    {
      if (c < '0' || c > '9')
        break;
      major = major * 10 + (c - '0');
    }
  return major;
}

std::string_view
java_source_name (java_source source) noexcept
{
  switch (source)
    {
    case java_source::user:
      return "user";
    case java_source::java_home:
      return "java_home";
    case java_source::path:
      return "path";
    case java_source::registry:
      return "registry";
    case java_source::system:
      return "system";
    case java_source::minecraft_runtime:
      return "minecraft_runtime";
    case java_source::managed:
      return "managed";
    }
  return "unknown";
}

std::filesystem::path
java_home_from_executable (const std::filesystem::path &executable)
{
  auto bin = executable.parent_path ();
  if (bin.filename () == "bin")
    return bin.parent_path ();
  return bin;
}

result<java_installation>
probe_java (const std::filesystem::path &home_or_executable,
            bool allow_process)
{
  auto st = fs::stat (home_or_executable);
  if (!st.exists)
    return make_error (
        errc::java_not_found,
        sformat ("'{}' does not exist", path_to_utf8 (home_or_executable)));
  java_installation install;
  install.home = st.is_directory
                     ? home_or_executable
                     : java_home_from_executable (home_or_executable);
  install.executable = java_binary (install.home, false);
  install.gui_executable = java_binary (install.home, true);
  if (!fs::exists (install.executable))
    return make_error (
        errc::java_not_found,
        sformat ("no java executable in '{}'", path_to_utf8 (install.home)));
  if (!fs::exists (install.gui_executable))
    install.gui_executable = install.executable;
#if defined(_WIN32)
  install.jdk = fs::exists (install.home / "bin" / "javac.exe");
#else
  install.jdk = fs::exists (install.home / "bin" / "javac");
#endif
  auto release = read_release (install.home);
  if (auto it = release.find ("JAVA_VERSION"); it != release.end ())
    {
      install.version = it->second;
      install.major = parse_java_major (install.version);
      if (auto arch = release.find ("OS_ARCH"); arch != release.end ())
        if (auto parsed = parse_arch (arch->second))
          install.arch = *parsed;
      if (auto vendor = release.find ("IMPLEMENTOR"); vendor != release.end ())
        install.vendor = vendor->second;
      if (install.major > 0 && install.arch != cpu_arch::unknown)
        return install;
    }
  if (!allow_process)
    {
      if (install.major > 0)
        {
          install.arch = current_platform ().arch;
          return install;
        }
      return make_error (errc::java_not_found, "no release file");
    }
  return probe_with_process (std::move (install));
}

std::vector<java_installation>
scan_java (const scan_options &options, cancellation_token token)
{
  auto candidates = collect_candidates (options);
  std::vector<candidate> unique;
  std::vector<std::string> keys;
  for (auto &c : candidates)
    {
      auto canonical = fs::real_path (c.home);
      auto key = to_lower (path_to_utf8 (canonical));
      if (std::find (keys.begin (), keys.end (), key) != keys.end ())
        continue;
      keys.push_back (key);
      unique.push_back ({ canonical, c.source });
    }
  std::vector<std::optional<java_installation>> found (unique.size ());
  parallel_for (thread_pool::shared (), unique.size (), [&] (std::size_t i) {
    if (token.is_cancelled ())
      return;
    auto probed = probe_java (unique[i].home, options.probe_process);
    if (!probed)
      {
        s_log.debug ("skip {}: {}", path_to_utf8 (unique[i].home),
                     probed.err ().message);
        return;
      }
    probed->source = unique[i].source;
    found[i] = std::move (*probed);
  });
  std::vector<java_installation> out;
  for (auto &f : found)
    if (f)
      out.push_back (std::move (*f));
  s_log.info ("found {} java installations", out.size ());
  return out;
}

}
