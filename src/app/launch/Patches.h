#pragma once

#include <core/base/cancellation.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Patches {

    bool all();
    void setAll(bool on);
    bool enabled(const std::optional<bool>& instance);

    std::string value(const std::string& key);
    void setValue(const std::string& key, const std::string& value);
    bool flag(const std::string& key);
    void setFlag(const std::string& key, bool on);

    struct Plan {
        std::vector<std::pair<std::string, std::string>> values;
        std::string player;
    };

    Plan plan();
    std::string resolve(Plan& plan, b_launcher_base::cancellation_token token);
    std::filesystem::path jarIn(const std::filesystem::path& directory);
    std::vector<std::string> arguments(const std::filesystem::path& jar, const Plan& plan);
    std::string extract(const std::filesystem::path& jar);
}
