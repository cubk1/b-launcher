#pragma once

#include "app/instances/Instances.h"

#include <filesystem>
#include <optional>
#include <string>

namespace GameOptions {

    std::filesystem::path fileOf(const Instances::Entry& entry);
    bool ownFile(const Instances::Entry& entry);
    std::filesystem::path sharedFile(const std::string& source);
    bool inUse(const std::filesystem::path& file);

    std::optional<std::string> get(const std::filesystem::path& file, const std::string& key);
    std::string set(const std::filesystem::path& file, const std::string& key, const std::string& value);
}
