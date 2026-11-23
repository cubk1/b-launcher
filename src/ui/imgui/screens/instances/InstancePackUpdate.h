#pragma once

#include "app/instances/Instances.h"

#include <string>

namespace InstancePackUpdate {

    std::string rowValue(const std::string& instanceKey);
    void reset();
    bool draw(const Instances::Entry& entry);
}
