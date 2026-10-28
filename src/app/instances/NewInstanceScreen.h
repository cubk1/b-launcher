#pragma once

#include "app/instances/Installer.h"

#include <string>
#include <vector>

namespace NewInstanceScreen {

    struct Modpack {
        std::string title;
        std::string version;
        std::string minecraft;
        std::vector<std::string> loaders;
        Installer::Modpack pack;
    };

    void draw();
    void reset();
    void openModpack(const Modpack& modpack);
    void leaveModpack();
    void openLoaderEdit(const std::string& instanceKey);
}
