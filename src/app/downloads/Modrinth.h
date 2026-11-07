#pragma once

#include "app/downloads/Addons.h"

#include <core/base/error.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Modrinth {

    const char* projectType(Addons::Kind kind);
    Addons::Kind kindOf(const std::string& projectType);
    b_launcher_base::result<nlohmann::json> fetchJson(const std::string& url,
                                                      b_launcher_base::cancellation_token token);
    b_launcher_base::result<nlohmann::json> api(const std::string& path, b_launcher_base::cancellation_token token);
    b_launcher_base::result<nlohmann::json> post(const std::string& path, const nlohmann::json& body,
                                                 b_launcher_base::cancellation_token token);
    const Addons::Results& search(const Addons::Query& query);
    void refresh(const Addons::Query& query);
    const std::vector<Addons::Category>& categories(Addons::Kind kind);
    void preload();
    const Addons::Versions& versions(const std::string& projectId);
    void refreshVersions(const std::string& projectId);
    std::string label(const std::string& tag);
    Addons::Version versionFrom(const nlohmann::json& item);
    Addons::Fetched fetchVersion(const std::string& versionId, b_launcher_base::cancellation_token token);
    Addons::Fetched fetchProjectVersions(const std::string& projectId, const std::string& gameVersion,
                                         const std::string& loader, b_launcher_base::cancellation_token token);
    void poll();
    void shutdown();
}
