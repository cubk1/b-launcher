#pragma once

#include "app/instances/Instances.h"

#include <cstdint>
#include <string>

namespace InstanceScreenshots {

    bool draw(const Instances::Entry& entry);
    std::string dateText(std::int64_t mtime);
    std::string sizeText(std::uint64_t bytes);
}
