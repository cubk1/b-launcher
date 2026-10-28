#include <core/base/strings.hpp>
#include <core/java/java_requirement.hpp>

#include <algorithm>
#include <tuple>

namespace b_launcher_base::java
{

namespace
{

bool
has_library (const meta::version &v, std::string_view group,
             std::string_view name)
{
  return std::any_of (v.libraries.begin (), v.libraries.end (),
                      [&] (const meta::library &lib) {
                        return lib.artifact.group == group
                               && lib.artifact.name == name;
                      });
}

std::string
component_for (int major)
{
  if (major <= 8)
    return "jre-legacy";
  if (major <= 16)
    return "java-runtime-alpha";
  if (major <= 17)
    return "java-runtime-gamma";
  if (major <= 21)
    return "java-runtime-delta";
  return "java-runtime-epsilon";
}

}

java_requirement
requirement_for (const meta::version &resolved)
{
  java_requirement req;
  if (resolved.java_version && resolved.java_version->major_version > 0)
    {
      req.min_major = resolved.java_version->major_version;
      req.preferred_major = req.min_major;
      req.component = resolved.java_version->component.empty ()
                          ? component_for (req.min_major)
                          : resolved.java_version->component;
    }
  else
    {
      const auto &time = resolved.release_time;
      if (!time.empty () && time >= "2024-04-23")
        req.min_major = 21;
      else if (!time.empty () && time >= "2021-11-30")
        req.min_major = 17;
      else if (!time.empty () && time >= "2021-06-08")
        req.min_major = 16;
      req.preferred_major = req.min_major;
      req.component = component_for (req.min_major);
    }

  bool launchwrapper
      = resolved.main_class == "net.minecraft.launchwrapper.Launch"
        || has_library (resolved, "net.minecraft", "launchwrapper");
  bool launchwrapper_of
      = has_library (resolved, "optifine", "launchwrapper-of");
  bool modlauncher
      = has_library (resolved, "cpw.mods", "modlauncher")
        || has_library (resolved, "net.minecraftforge", "fmlloader")
        || has_library (resolved, "net.neoforged.fancymodloader", "loader");
  bool cleanroom = has_library (resolved, "com.cleanroommc", "cleanroom");
  if (launchwrapper && !launchwrapper_of && !cleanroom)
    req.max_major = 8;
  else if (modlauncher && req.min_major <= 8)
    req.preferred_major = 8;
  if (cleanroom && req.min_major < 21)
    {
      req.min_major = 21;
      req.preferred_major = 21;
      req.component = component_for (21);
    }
  if (req.max_major && req.max_major < req.min_major)
    req.max_major = req.min_major;
  return req;
}

bool
arch_runnable (cpu_arch java_arch) noexcept
{
  auto host = current_platform ().arch;
  if (java_arch == host)
    return true;
  if (host == cpu_arch::x86_64 && java_arch == cpu_arch::x86)
    return true;
  if (host == cpu_arch::arm64
      && (java_arch == cpu_arch::x86_64 || java_arch == cpu_arch::x86))
    return current_platform ().os != os_type::linux;
  return false;
}

bool
satisfies (const java_installation &install,
           const java_requirement &requirement) noexcept
{
  if (install.major < requirement.min_major)
    return false;
  if (requirement.max_major && install.major > requirement.max_major)
    return false;
  return arch_runnable (install.arch);
}

std::optional<java_installation>
select_java (std::span<const java_installation> installs,
             const java_requirement &requirement)
{
  const java_installation *best = nullptr;
  auto host = current_platform ().arch;
  auto score = [&] (const java_installation &j) {
    int exact = j.major == requirement.preferred_major ? 0 : 1;
    int native = j.arch == host ? 0 : 1;
    int distance = std::abs (j.major - requirement.preferred_major);
    int below = j.major < requirement.preferred_major ? 1 : 0;
    int source = j.source == java_source::managed
                         || j.source == java_source::minecraft_runtime
                     ? 0
                     : 1;
    return std::make_tuple (exact, native, below, distance, source);
  };
  for (const auto &j : installs)
    {
      if (!satisfies (j, requirement))
        continue;
      if (!best || score (j) < score (*best)
          || (score (j) == score (*best)
              && compare_versions (j.version, best->version) > 0))
        best = &j;
    }
  if (!best)
    return std::nullopt;
  return *best;
}

}
