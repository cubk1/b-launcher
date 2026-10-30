#pragma once

#include "app/instances/Instances.h"

#include <core/java/java_detect.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The parts of an instance's settings that are typed or chosen, shared by both interfaces.
namespace InstanceSettings {

    // Arguments in one line: spaces between them, double quotes around the ones that hold a space.
    std::string joinArgs(const std::vector<std::string>& args);
    std::vector<std::string> splitArgs(const std::string& text);
    // NAME=value items in one line, quoted like arguments; items without a name are dropped.
    std::string joinEnvironment(const std::vector<std::pair<std::string, std::string>>& variables);
    std::vector<std::pair<std::string, std::string>> splitEnvironment(const std::string& text);

    // A positive number of at most five digits, such as a window size.
    std::optional<int> parseSize(const std::string& text);
    // Megabytes of memory, at least 256; 0 when the text is not a size.
    uint32_t parseMegabytes(const std::string& text);
    // The memory an instance gets when it sets none: a quarter of the computer's, from 2 GB to 6 GB.
    uint32_t automaticMemory(uint32_t totalMb);

    // Whether the default isolation gives the instance a game directory of its own.
    bool isolatedByDefault(const Instances::Entry& entry);

    // The Java installations found, with names that tell them apart.
    struct JavaOption {
        std::string label;
        std::filesystem::path path;
    };
    std::vector<JavaOption> javaOptions();
    // Whether a chosen path is that installation: its java, its javaw or its home.
    bool isJava(const std::filesystem::path& chosen, const b_launcher_base::java::java_installation& java);
}
