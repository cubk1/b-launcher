#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/platform.hpp>
#include <core/base/strings.hpp>

#include "core/base/win_util.hpp"

#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

namespace b_launcher_base
{

namespace
{

#if defined(_WIN32)

std::string
windows_version ()
{
  using rtl_get_version_fn = LONG (WINAPI *) (PRTL_OSVERSIONINFOW);
  auto module = GetModuleHandleW (L"ntdll.dll");
  if (!module)
    return "10.0";
  auto fn = reinterpret_cast<rtl_get_version_fn> (
      reinterpret_cast<void *> (GetProcAddress (module, "RtlGetVersion")));
  if (!fn)
    return "10.0";
  RTL_OSVERSIONINFOW info {};
  info.dwOSVersionInfoSize = sizeof (info);
  if (fn (&info) != 0)
    return "10.0";
  return sformat ("{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion,
                  info.dwBuildNumber);
}

cpu_arch
windows_arch ()
{
  using is_wow64_process2_fn = BOOL (WINAPI *) (HANDLE, USHORT *, USHORT *);
  auto kernel = GetModuleHandleW (L"kernel32.dll");
  if (kernel)
    {
      auto fn
          = reinterpret_cast<is_wow64_process2_fn> (reinterpret_cast<void *> (
              GetProcAddress (kernel, "IsWow64Process2")));
      USHORT process = 0;
      USHORT native = 0;
      if (fn && fn (GetCurrentProcess (), &process, &native))
        {
          switch (native)
            {
            case IMAGE_FILE_MACHINE_AMD64:
              return cpu_arch::x86_64;
            case IMAGE_FILE_MACHINE_ARM64:
              return cpu_arch::arm64;
            case IMAGE_FILE_MACHINE_I386:
              return cpu_arch::x86;
            case IMAGE_FILE_MACHINE_ARMNT:
              return cpu_arch::arm32;
            default:
              break;
            }
        }
    }
  SYSTEM_INFO info;
  GetNativeSystemInfo (&info);
  switch (info.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return cpu_arch::x86_64;
    case PROCESSOR_ARCHITECTURE_ARM64:
      return cpu_arch::arm64;
    case PROCESSOR_ARCHITECTURE_INTEL:
      return cpu_arch::x86;
    case PROCESSOR_ARCHITECTURE_ARM:
      return cpu_arch::arm32;
    default:
      return cpu_arch::unknown;
    }
}

#endif

platform_info
detect_platform ()
{
  platform_info info;
  info.cpu_count = std::max (1u, std::thread::hardware_concurrency ());
#if defined(_WIN32)
  info.os = os_type::windows;
  info.os_version = windows_version ();
  info.arch = windows_arch ();
  MEMORYSTATUSEX memory {};
  memory.dwLength = sizeof (memory);
  if (GlobalMemoryStatusEx (&memory))
    info.total_memory = memory.ullTotalPhys;
#else
#if defined(__APPLE__)
  info.os = os_type::osx;
#elif defined(__linux__)
  info.os = os_type::linux;
#endif
  struct utsname name;
  if (uname (&name) == 0)
    {
      info.os_version = name.release;
      if (auto arch = parse_arch (name.machine))
        info.arch = *arch;
    }
#if defined(__APPLE__)
  std::uint64_t memsize = 0;
  std::size_t length = sizeof (memsize);
  if (sysctlbyname ("hw.memsize", &memsize, &length, nullptr, 0) == 0)
    info.total_memory = memsize;
  char version[64] = {};
  length = sizeof (version);
  if (sysctlbyname ("kern.osproductversion", version, &length, nullptr, 0)
      == 0)
    info.os_version = version;
  int arm64 = 0;
  length = sizeof (arm64);
  if (sysctlbyname ("hw.optional.arm64", &arm64, &length, nullptr, 0) == 0
      && arm64)
    info.arch = cpu_arch::arm64;
#else
  auto pages = sysconf (_SC_PHYS_PAGES);
  auto page_size = sysconf (_SC_PAGESIZE);
  if (pages > 0 && page_size > 0)
    info.total_memory = static_cast<std::uint64_t> (pages)
                        * static_cast<std::uint64_t> (page_size);
#endif
#endif
  return info;
}

}

const platform_info &
current_platform ()
{
  static const platform_info info = detect_platform ();
  return info;
}

std::uint64_t
available_memory ()
{
#if defined(_WIN32)
  MEMORYSTATUSEX memory {};
  memory.dwLength = sizeof (memory);
  if (GlobalMemoryStatusEx (&memory))
    return memory.ullAvailPhys;
  return 0;
#elif defined(_SC_AVPHYS_PAGES)
  auto pages = sysconf (_SC_AVPHYS_PAGES);
  auto page_size = sysconf (_SC_PAGESIZE);
  if (pages > 0 && page_size > 0)
    return static_cast<std::uint64_t> (pages)
           * static_cast<std::uint64_t> (page_size);
  return 0;
#else
  return current_platform ().total_memory / 2;
#endif
}

std::string_view
os_name (os_type os) noexcept
{
  switch (os)
    {
    case os_type::windows:
      return "windows";
    case os_type::linux:
      return "linux";
    case os_type::osx:
      return "osx";
    case os_type::unknown:
      break;
    }
  return "unknown";
}

std::string_view
arch_name (cpu_arch arch) noexcept
{
  switch (arch)
    {
    case cpu_arch::x86:
      return "x86";
    case cpu_arch::x86_64:
      return "x86_64";
    case cpu_arch::arm32:
      return "arm32";
    case cpu_arch::arm64:
      return "arm64";
    case cpu_arch::unknown:
      break;
    }
  return "unknown";
}

std::optional<cpu_arch>
parse_arch (std::string_view text) noexcept
{
  auto lower = to_lower (text);
  if (lower == "x86_64" || lower == "amd64" || lower == "x64"
      || lower == "x86-64")
    return cpu_arch::x86_64;
  if (lower == "x86" || lower == "i386" || lower == "i486" || lower == "i586"
      || lower == "i686" || lower == "ia32")
    return cpu_arch::x86;
  if (lower == "aarch64" || lower == "arm64")
    return cpu_arch::arm64;
  if (lower == "arm" || lower == "arm32" || lower == "aarch32"
      || lower.starts_with ("armv7"))
    return cpu_arch::arm32;
  return std::nullopt;
}

unsigned
arch_bits (cpu_arch arch) noexcept
{
  return arch == cpu_arch::x86 || arch == cpu_arch::arm32 ? 32 : 64;
}

std::optional<std::string>
environment (std::string_view name)
{
#if defined(_WIN32)
  auto wide = detail::utf8_to_wide (name);
  auto size = GetEnvironmentVariableW (wide.c_str (), nullptr, 0);
  if (size == 0)
    return std::nullopt;
  std::wstring value (size, L'\0');
  size = GetEnvironmentVariableW (wide.c_str (), value.data (), size);
  value.resize (size);
  return detail::wide_to_utf8 (value);
#else
  std::string key (name);
  if (const char *value = std::getenv (key.c_str ()))
    return std::string (value);
  return std::nullopt;
#endif
}

std::filesystem::path
home_directory ()
{
#if defined(_WIN32)
  if (auto home = environment ("USERPROFILE"))
    return path_from_utf8 (*home);
#else
  if (auto home = environment ("HOME"))
    return path_from_utf8 (*home);
#endif
  return fs::current_directory ();
}

std::filesystem::path
app_data_directory ()
{
#if defined(_WIN32)
  if (auto appdata = environment ("APPDATA"))
    return path_from_utf8 (*appdata);
  return home_directory () / "AppData" / "Roaming";
#elif defined(__APPLE__)
  return home_directory () / "Library" / "Application Support";
#else
  if (auto xdg = environment ("XDG_DATA_HOME"))
    return path_from_utf8 (*xdg);
  return home_directory () / ".local" / "share";
#endif
}

std::filesystem::path
default_minecraft_directory ()
{
#if defined(_WIN32)
  return app_data_directory () / ".minecraft";
#elif defined(__APPLE__)
  return app_data_directory () / "minecraft";
#else
  return home_directory () / ".minecraft";
#endif
}

std::string_view
executable_suffix () noexcept
{
#if defined(_WIN32)
  return ".exe";
#else
  return "";
#endif
}

char
path_list_separator () noexcept
{
#if defined(_WIN32)
  return ';';
#else
  return ':';
#endif
}

}
