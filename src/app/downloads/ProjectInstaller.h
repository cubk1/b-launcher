#pragma once

#include "app/downloads/Addons.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ProjectInstaller {

    enum class State { Idle, Running, Done, Failed };

    struct Plan {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        std::vector<Addons::Version> dependencies;
    };

    // Supplying an instance selects a world task in that instance's saves directory.
    State state(const std::string& versionId, const std::string& instanceKey = {});
    bool busy();
    float progress(const std::string& versionId, const std::string& instanceKey = {});

    std::string folder(Addons::Kind kind);
    std::string loaderOf(const std::string& instanceKey);
    bool installed(const Addons::Version& version, Addons::Kind kind, const std::string& instanceKey);

    const Plan& plan(const Addons::Version& version, const std::string& gameVersion, const std::string& loader);

    void install(const Addons::Version& version, Addons::Kind kind, const std::string& title,
                 const std::string& instanceKey,
                 std::optional<std::vector<Addons::Version>> dependencies = std::nullopt);
    void saveTo(const Addons::Version& version, const std::filesystem::path& folder, const std::string& title);
    void saveAs(const Addons::Version& version, const std::string& title);
    void saveAllAs(const Addons::Version& version, const std::vector<Addons::Version>& dependencies,
                   const std::string& title);
    void installWorld(const Addons::Version& version, const std::string& title, const std::string& instanceKey);

    void poll();
    void shutdown();
}
