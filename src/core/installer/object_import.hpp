#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/thread_pool.hpp>
#include <core/installer/download_plan.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace b_launcher_base::install
{

struct import_report
{
  std::size_t linked = 0;
  std::size_t copied = 0;
  std::uint64_t bytes = 0;
  std::vector<std::size_t> remaining;
};

std::vector<std::filesystem::path>
discover_minecraft_roots (const std::filesystem::path &exclude = {});

import_report import_local_objects (
    std::span<const planned_file> files, std::span<const std::size_t> missing,
    std::span<const std::filesystem::path> sources, bool verify_hash = true,
    thread_pool *pool = nullptr, cancellation_token token = {});

}
