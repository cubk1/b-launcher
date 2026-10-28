#pragma once

#include "app/instances/Instances.h"

#include <cstdint>
#include <string>

namespace InstancePage {

    void draw(const std::string& key);
    void openMenu(const std::string& key, float x, float y);
    void beginRename(const std::string& key);
    uint32_t icon(const Instances::Entry& entry);
    std::string loaderLabel(b_launcher_base::install::loader_type type);
}
