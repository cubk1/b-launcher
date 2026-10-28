#pragma once

#include "app/downloads/Modrinth.h"

#include <filesystem>
#include <string>

namespace ProjectInstaller {

    enum class State { Idle, Running, Done, Failed };

    State state(const std::string& versionId);
    float progress(const std::string& versionId);

    std::string folder(Modrinth::Kind kind);
    std::string loaderOf(const std::string& instanceKey);
    bool installed(const Modrinth::Version& version, Modrinth::Kind kind, const std::string& instanceKey);

    void install(const Modrinth::Version& version, Modrinth::Kind kind, const std::string& title,
                 const std::string& instanceKey);
    void saveTo(const Modrinth::Version& version, const std::filesystem::path& folder, const std::string& title);

    void poll();
    void shutdown();
}
