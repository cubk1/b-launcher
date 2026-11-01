#pragma once

#include "app/instances/ModpackSetup.h"

#include <string>

namespace NewInstanceScreen {

    void draw();
    void reset();
    void openModpack(const ModpackSetup& modpack);
    void leaveModpack();
    void openLoaderEdit(const std::string& instanceKey);
}
