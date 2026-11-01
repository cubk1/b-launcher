#pragma once

#include "app/downloads/Addons.h"

#include <string>
#include <vector>

namespace CurseForge {

    const std::string& apiKey();
    const Addons::Results& search(const Addons::Query& query);
    void refresh(const Addons::Query& query);
    const std::vector<Addons::Category>& categories(Addons::Kind kind);
    void preload();
    const Addons::Versions& versions(const std::string& projectId);
    void refreshVersions(const std::string& projectId);
    Addons::Fetched fetchVersion(const std::string& fileId, b_launcher_base::cancellation_token token);
    Addons::Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                         const std::string& loader, b_launcher_base::cancellation_token token);
    void poll();
    void shutdown();
}
