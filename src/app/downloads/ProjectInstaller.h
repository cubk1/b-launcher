#pragma once

#include "app/downloads/Addons.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ProjectInstaller {

    enum class State { Idle, Running, Done, Failed };

    struct Plan {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        std::vector<Addons::Version> dependencies;
    };

    struct Pick {
        std::vector<std::pair<Addons::Source, std::string>> projects;
        std::string title;
        std::string icon;
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
                 std::optional<std::vector<Addons::Version>> dependencies = std::nullopt, const std::string& icon = {});
    void installAll(const std::vector<Pick>& picks, Addons::Kind kind, const std::string& instanceKey);
    void saveTo(const Addons::Version& version, const std::filesystem::path& folder, const std::string& title,
                const std::string& icon = {});
    void saveAs(const Addons::Version& version, const std::string& title, const std::string& icon = {});
    void saveAllAs(const Addons::Version& version, const std::vector<Addons::Version>& dependencies,
                   const std::string& title, const std::string& icon = {});
    void installWorld(const Addons::Version& version, const std::string& title, const std::string& instanceKey,
                      const std::string& icon = {});

    void poll();
    void shutdown();
}
