#pragma once

#include "app/downloads/Addons.h"

#include <core/base/error.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace CurseForge {

    const std::string& apiKey();
    b_launcher_base::result<nlohmann::json> call(const std::string& path, const std::string& body,
                                                 b_launcher_base::cancellation_token token);
    Addons::Kind kindOf(long long classId);
    Addons::Hit hitFrom(const nlohmann::json& item);
    Addons::Version versionFrom(const nlohmann::json& item);
    const Addons::Results& search(const Addons::Query& query);
    void refresh(const Addons::Query& query);
    const std::vector<Addons::Category>& categories(Addons::Kind kind);
    void preload();
    const Addons::Versions& versions(const std::string& projectId);
    void refreshVersions(const std::string& projectId);
    Addons::Fetched fetchVersion(const std::string& fileId, b_launcher_base::cancellation_token token);
    Addons::Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                         const std::string& loader, b_launcher_base::cancellation_token token);
    b_launcher_base::result<std::map<std::uint32_t, Addons::Version>>
    matchFingerprints(const std::vector<std::uint32_t>& fingerprints, b_launcher_base::cancellation_token token);
    b_launcher_base::result<std::vector<Addons::Hit>> fetchProjects(const std::vector<std::string>& ids,
                                                                    b_launcher_base::cancellation_token token);
    void poll();
    void shutdown();
}
