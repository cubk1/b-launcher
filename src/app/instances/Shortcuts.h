#pragma once

#include "app/instances/Instances.h"

#include <filesystem>
#include <string>
#include <vector>

// Shortcuts that launch an instance, with the launcher's icon, the instance's or a picture's.
namespace Shortcuts {

    enum class Icon { Launcher, Instance, Custom };

    struct Draft {
        Icon icon = Icon::Instance;
        std::filesystem::path custom;
        bool headless = false;
    };

    // What was chosen for an instance, for the rest of the run.
    Draft& draft(const std::string& instanceKey);
    // Asks where to save the shortcut and makes it, telling how that went through the notices; true once it is made.
    bool create(const Instances::Entry& entry, const Draft& draft);
    // A picture for the custom icon, size × size in RGBA; empty when it cannot be read.
    std::vector<unsigned char> pixels(const std::filesystem::path& file, int size);
}
