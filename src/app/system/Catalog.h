#pragma once

#include <core/installer/loader.hpp>
#include <core/java/java_detect.hpp>
#include <core/meta/version_manifest.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Catalog {

    namespace blb = b_launcher_base;

    enum class Status { Loading, Ready, Failed };

    struct Versions {
        Status status = Status::Loading;
        std::vector<blb::meta::manifest_version> list;
        std::string error;
    };

    struct LoaderVersions {
        Status status = Status::Loading;
        std::vector<blb::install::loader_version> list;
        std::string error;
    };

    enum class Kind { Release, Snapshot, AprilFools, Old };

    const Versions& minecraft();
    void refreshMinecraft();
    Kind kindOf(const blb::meta::manifest_version& version);
    void preload();
    const LoaderVersions& loader(blb::install::loader_type type, const std::string& minecraft);
    void refreshLoader(blb::install::loader_type type, const std::string& minecraft);

    struct FabricApiRequirements {
        Status status = Status::Loading;
        std::vector<std::string> required;
        std::vector<std::string> incompatible;
        std::string error;
    };

    // Fabric Loader constraints from the API and its embedded mods.
    const FabricApiRequirements& fabricApiRequirements(const blb::install::loader_version& version,
                                                       bool retryFailed = false);

    struct JavaList {
        Status status = Status::Loading;
        std::vector<blb::java::java_installation> list;
    };

    const JavaList& java();
    // Capture the configured search paths on the UI thread before starting work.
    blb::java::scan_options javaScanOptions();
    std::optional<std::vector<blb::java::java_installation>> javaCandidates();
    int addFound(const std::vector<blb::java::java_installation>& found);
    void seenJava(const std::filesystem::path& executable);
    void refreshJava();
    void addJava(const std::filesystem::path& path);
    bool addedByHand(const std::filesystem::path& executable);
    void removeJava(const std::filesystem::path& executable);

    void poll();
    void shutdown();
}
