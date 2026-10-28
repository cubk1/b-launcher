#pragma once

#include "Instances.h"

namespace InstanceExport {

    bool draw(const Instances::Entry& entry);
    void poll();
    void shutdown();
}
