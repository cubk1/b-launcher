#pragma once

#include "app/downloads/Addons.h"

#include <filesystem>
#include <string>

namespace ProjectInstaller {

    enum class State { Idle, Running, Done, Failed };

    State state(const std::string& versionId);
    float progress(const std::string& versionId);

    std::string folder(Addons::Kind kind);
    std::string loaderOf(const std::string& instanceKey);
    bool installed(const Addons::Version& version, Addons::Kind kind, const std::string& instanceKey);

    void install(const Addons::Version& version, Addons::Kind kind, const std::string& title,
                 const std::string& instanceKey);
    void saveTo(const Addons::Version& version, const std::filesystem::path& folder, const std::string& title);

    void poll();
    void shutdown();
}
