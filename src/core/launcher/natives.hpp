#pragma once

#include <core/base/error.hpp>
#include <core/base/thread_pool.hpp>
#include <core/meta/game_layout.hpp>
#include <core/meta/rules.hpp>
#include <core/meta/version.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace b_launcher_base::launch
{

struct native_archive
{
  std::filesystem::path jar;
  std::vector<std::string> exclude;
};

struct natives_report
{
  std::size_t archives = 0;
  std::size_t extracted = 0;
  bool reused = false;
};

std::vector<native_archive>
collect_native_archives (const meta::version &resolved,
                         const meta::game_layout &layout,
                         const meta::rule_context &context);
result<natives_report> prepare_natives (const meta::version &resolved,
                                        const meta::game_layout &layout,
                                        const meta::rule_context &context,
                                        const std::filesystem::path &directory,
                                        thread_pool *pool = nullptr);

}
