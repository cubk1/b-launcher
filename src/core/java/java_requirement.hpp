#pragma once

#include <core/base/platform.hpp>
#include <core/java/java_detect.hpp>
#include <core/meta/version.hpp>

#include <optional>
#include <span>
#include <string>

namespace b_launcher_base::java
{

struct java_requirement
{
  int min_major = 8;
  int max_major = 0;
  int preferred_major = 8;
  std::string component = "jre-legacy";
};

java_requirement requirement_for (const meta::version &resolved);
bool satisfies (const java_installation &install,
                const java_requirement &requirement) noexcept;
bool arch_runnable (cpu_arch java_arch) noexcept;
std::optional<java_installation>
select_java (std::span<const java_installation> installs,
             const java_requirement &requirement);

}
