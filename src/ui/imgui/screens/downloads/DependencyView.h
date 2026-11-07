#pragma once

#include "app/downloads/Mcmod.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"

#include <vector>

namespace DependencyView {

    void draw(DownloadsParts::Route& route, const std::vector<const Addons::Version*>& versions,
              const Mcmod::Ref* mcmod, float top, float bottom, bool covered);
}
