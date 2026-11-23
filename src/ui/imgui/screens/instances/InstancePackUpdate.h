#pragma once

#include "app/instances/Instances.h"

#include <string>

namespace InstancePackUpdate {

    std::string rowValue(const std::string& instanceKey);
    void reset(const std::string& instanceKey);
    bool draw(const Instances::Entry& entry);
}
