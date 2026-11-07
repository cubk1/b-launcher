#pragma once

#include "app/downloads/Mcmod.h"
#include "app/downloads/PackContents.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"

#include <string>

namespace RelationGraph {

    bool viewSwitch(const std::string& id, bool graph, float x, float y, bool blocked);
    void pack(const DownloadsParts::Route& route, const PackContents::Contents& contents, const std::string& versionId,
              const std::string& query, float x, float y, float w, float h, bool covered);
    void mod(const DownloadsParts::Route& route, const Addons::Version& version, const Mcmod::Ref* mcmod,
             const std::string& gameVersion, const std::string& loader, float x, float y, float w, float h,
             bool covered);
}
