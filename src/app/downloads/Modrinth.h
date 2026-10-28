#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace Modrinth {

    const char* projectType(Addons::Kind kind);
    const Addons::Results& search(const Addons::Query& query);
    void refresh(const Addons::Query& query);
    const std::vector<Addons::Category>& categories(Addons::Kind kind);
    void preload();
    const Addons::Versions& versions(const std::string& projectId);
    void refreshVersions(const std::string& projectId);
    std::string label(const std::string& tag);
    Addons::Fetched fetchVersion(const std::string& versionId);
    Addons::Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                         const std::string& loader);
    void poll();
    void shutdown();
}
