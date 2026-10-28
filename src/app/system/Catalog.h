#pragma once

#include <core/installer/loader.hpp>
#include <core/java/java_detect.hpp>
#include <core/meta/version_manifest.hpp>

#include <filesystem>
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

    const Versions& minecraft();
    void refreshMinecraft();
    void preload();
    const LoaderVersions& loader(blb::install::loader_type type, const std::string& minecraft);
    void refreshLoader(blb::install::loader_type type, const std::string& minecraft);

    struct JavaList {
        Status status = Status::Loading;
        std::vector<blb::java::java_installation> list;
    };

    const JavaList& java();
    void refreshJava();
    void addJava(const std::filesystem::path& path);
    bool addedByHand(const std::filesystem::path& executable);
    void removeJava(const std::filesystem::path& executable);

    void poll();
    void shutdown();
}
