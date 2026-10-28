#pragma once

#include "Modrinth.h"

#include <string>

namespace DownloadsScreen {

    void draw();
    void openFor(const std::string& key);
    void open(Modrinth::Kind kind);
}
