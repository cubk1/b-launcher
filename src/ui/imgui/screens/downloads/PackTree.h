#pragma once

#include "ui/imgui/screens/downloads/DownloadsParts.h"

namespace PackTree {

    void draw(DownloadsParts::Route& route, const std::vector<const Addons::Version*>& versions, float top,
              float bottom, bool covered);
}
