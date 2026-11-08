#pragma once

#include "app/downloads/Addons.h"

#include <string>

namespace DownloadsScreen {

    void draw();
    void openFor(const std::string& key);
    void open(Addons::Kind kind);
    void openJava();
    void showProject(const std::string& instanceKey, Addons::Source source, Addons::Kind kind, const Addons::Hit& hit);
}
