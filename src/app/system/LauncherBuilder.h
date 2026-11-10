#pragma once

#include <core/modpack/modpack.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace LauncherBuilder {

    struct Link {
        std::string label;
        std::string url;
    };

    struct Server {
        std::string name;
        std::string address;
    };

    struct Project {
        std::string by;
        std::string version = "1.0.0";
        std::string theme;
        bool themeLock = false;
        std::string windowTitle;
        std::string brandTitle;
        std::string logoFile;
        std::string iconFile;
        std::string homepage;
        std::string homepageUrl;
        std::string homepagePreset;
        std::string homepageFile;
        bool homepageLock = false;
        std::string announcement;
        std::vector<Link> links;
        std::vector<Server> servers;
        std::string autoJoin;
        bool microsoft = true;
        bool yggdrasil = true;
        bool offline = true;
        std::string yggdrasilServer;
        std::vector<std::string> hidden;
        bool featuresLock = false;
        bool copySettings = false;
        bool settingsLock = false;
        bool updates = true;
    };

    using ThemeFiles = std::vector<std::pair<std::string, std::string>>;

    struct Job {
        Project project;
        ThemeFiles themes;
        nlohmann::json settings;
        bool modpack = false;
        bool single = false;
        std::filesystem::path root;
        std::string instanceId;
        std::filesystem::path gameDirectory;
        b_launcher_base::modpack::export_options pack;
        std::string instanceName;
        std::filesystem::path output;
        bool zip = false;
        std::string folder;
    };

    Project& project();
    void save();
    std::string suggestedName();
    std::string problem();
    std::shared_ptr<Job> prepare(ThemeFiles themes);
    bool attachModpack(Job& job, const std::string& instanceKey, const std::string& instanceName);
    std::string run(const Job& job, const std::function<void(const std::string&)>& say,
                    b_launcher_base::cancellation_token token);
    void start(ThemeFiles themes);
    void cancel();
    bool running();
    std::string status();
    void poll();
    void shutdown();
}
