#pragma once

#include "app/instances/Instances.h"

namespace InstanceScreenshots {

    bool draw(const Instances::Entry& entry);
    void open(const std::string& key, const std::filesystem::path& path);
}
