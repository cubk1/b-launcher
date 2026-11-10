#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace b_launcher_base {
    class zip_writer;
}

namespace Customization {

    struct Server {
        std::string name;
        std::string address;
        std::string icon;
    };

    struct Login {
        std::string type;
        std::string server;
    };

    struct Link {
        std::string label;
        std::string url;
    };

    void load();
    void apply();
    std::optional<int> command();
    bool active();
    const std::string& by();
    const std::string& version();
    const std::filesystem::path& origin();
    std::optional<std::string> read(const std::string& path);
    bool extract(const std::string& path, const std::filesystem::path& target);
    std::vector<std::string> files(const std::string& prefix);
    const std::vector<std::string>& problems();

    std::string theme();
    bool themeLocked();

    std::string windowTitle();
    std::string brandTitle();
    std::string logo();
    std::string icon();
    std::string iconImage();

    std::string homepageType();
    std::string homepageValue();
    bool homepageLocked();
    std::string announcement();
    const std::vector<Link>& links();

    const std::vector<Server>& servers();
    std::string autoJoin();

    const std::vector<Login>& logins();
    bool loginAllowed(const std::string& type, const std::string& server);

    std::string modpack();
    std::string modpackInstance();
    bool singleInstance();

    std::vector<std::string> hiddenFeatures();
    bool featuresLocked();

    bool preset(const std::string& key, nlohmann::json& value);
    bool locked(const std::string& key);

    bool updatesDisabled();

    int makePack(const std::filesystem::path& source, const std::filesystem::path& output, std::string& error);
    const char* programExtension();
    bool copyProgram(const std::filesystem::path& output, const std::string& ico, std::string& error);
    bool writeProgram(const std::filesystem::path& pack, const std::filesystem::path& output, const std::string& ico,
                      std::string& error);
    bool addProgram(b_launcher_base::zip_writer& zip, const std::string& prefix, const std::string& name,
                    const std::filesystem::path& pack, const std::string& ico, std::string& error);
}
