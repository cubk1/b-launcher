#pragma once

#include "app/downloads/Addons.h"
#include "app/instances/Mods.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ModManager {

    struct Remote {
        std::string projectId;
        std::string versionId;
        std::string number;
        std::vector<Addons::Dependency> dependencies;
    };

    struct Project {
        std::string id;
        std::string slug;
        std::string title;
        std::string iconUrl;
    };

    struct Change {
        std::filesystem::path file;
        std::string name;
        std::string projectId;
        std::string from;
        bool disabled = false;
        Addons::Version target;
    };

    enum class Severity { Error, Warning };

    struct Issue {
        Severity severity = Severity::Error;
        std::string text;
        std::string missingProject;
    };

    enum class Phase { Idle, Downloading, Verifying, Replacing, Failed };

    struct Progress {
        Phase phase = Phase::Idle;
        float fraction = -1.0f;
        std::string error;
        std::vector<std::string> problems;
    };

    void prepare(const std::string& key);
    bool identifying(const std::string& key);
    std::string identifyError(const std::string& key);
    const Remote* remote(const Mods::Mod& mod);
    const Project* project(const std::string& projectId);
    int metadataRevision();
    std::string dependencyProject(const Addons::Dependency& dependency);
    std::string title(const std::string& projectId);
    std::string gameVersion(const std::string& key);

    const Addons::Versions& versions(const std::string& key, const std::string& projectId);
    void refreshVersions(const std::string& key, const std::string& projectId);

    const std::vector<Change>& changes(const std::string& key);
    const Change* changeFor(const std::string& key, const std::filesystem::path& file);
    void stage(const std::string& key, const std::filesystem::path& file, const Addons::Version& target);
    void addProject(const std::string& key, const std::string& projectId);
    bool adding(const std::string& key, const std::string& projectId);
    void unstage(const std::string& key, const Change& change);
    void discard(const std::string& key);
    std::vector<Issue> check(const std::string& key);
    bool checking(const std::string& key);

    bool apply(const std::string& key);
    void cancel(const std::string& key);
    bool busy(const std::string& key);
    bool busy();
    const Progress& progress(const std::string& key);
    void dismiss(const std::string& key);

    void poll();
    void shutdown();
}
