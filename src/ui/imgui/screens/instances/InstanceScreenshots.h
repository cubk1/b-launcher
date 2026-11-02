#pragma once

#include "app/instances/Instances.h"

namespace InstanceScreenshots {

    bool draw(const Instances::Entry& entry);
    void openAt(const std::string& key, int index);
}
