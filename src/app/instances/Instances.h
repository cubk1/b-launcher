#pragma once

#include <core/instance/instance_manager.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Instances {

    namespace blb = b_launcher_base;

    struct Source {
        std::string key;
        std::string label;
        std::filesystem::path root;
        bool exists = false;
        bool custom = false;
    };

    struct Entry {
        std::string key;
        std::string source;
        blb::instance::instance info;
        bool isolated = false;
        std::filesystem::path gameDirectory;
    };

    void init();
    void poll();
    void shutdown();

    bool loading();
    const std::string& activity();
    // What is being done to an instance, such as 正在删除; empty when nothing is.
    std::string activityOf(const std::string& key);
    const std::vector<Source>& sources();
    const Source* source(const std::string& key);
    const std::vector<Entry>& list();
    const Entry* find(const std::string& key);
    std::string keyOf(const std::string& source, const std::string& id);

    const std::string& selected();
    void select(const std::string& key);

    blb::instance::isolation_mode isolation();
    void setIsolation(blb::instance::isolation_mode mode);
    void refresh();
    void refresh(std::function<void()> done);
    void createFolder(const std::string& source);
    std::string addFolder(const std::filesystem::path& folder);
    void removeFolder(const std::string& source);

    void editSettings(const std::string& key, const std::function<void(blb::instance::instance_settings&)>& edit);

    using Done = std::function<void(const std::string& error)>;
    void rename(const std::string& key, const std::string& to, Done done = nullptr);
    void duplicate(const std::string& key, const std::string& to, Done done = nullptr);
    void remove(const std::string& key, Done done = nullptr);

    std::string checkId(const std::string& source, const std::string& id);
    std::string checkInstallId(const std::string& source, const std::string& id);
    std::string freeId(const std::string& source, const std::string& base);

    void notify(const std::string& text, bool error);
}
