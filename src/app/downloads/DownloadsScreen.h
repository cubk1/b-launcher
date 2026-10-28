#pragma once

#include "app/downloads/Addons.h"

#include <string>

namespace DownloadsScreen {

    void draw();
    void openFor(const std::string& key);
    void open(Addons::Kind kind);
}
