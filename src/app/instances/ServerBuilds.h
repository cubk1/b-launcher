#pragma once

#include "app/instances/FileTree.h"
#include "app/instances/Instances.h"

#include <set>
#include <string>
#include <vector>

namespace ServerBuilds {

    struct Options {
        std::string java = "instance";
        std::string javaPath;
        int memoryMb = 0;
        std::string jvmArgs;
        int port = 25565;
        bool onlineMode = true;
        int maxPlayers = 20;
        std::string motd;
        std::string world;
        bool picked = false;
        FileTree::Rules picks;
        std::vector<std::string> seen;
    };

    Options& options(const std::string& instanceKey);
    void save(const std::string& instanceKey);
    FileTree::Tree& tree(const std::string& instanceKey);
    void open(const std::string& instanceKey);
    bool detecting(const std::string& instanceKey);
    bool clientOnly(const std::string& instanceKey, const std::string& path);
    void picked(const std::string& instanceKey, const std::string& path);
    std::vector<std::string> worlds(const Instances::Entry& entry);
    std::string loaderText(const Instances::Entry& entry);
    bool supported(const Instances::Entry& entry);
    int defaultMemory(const Instances::Entry& entry);
    void start(const std::string& instanceKey);
    void cancel();
    bool running();
    const std::string& runningKey();
    std::string status();
    void poll();
    void shutdown();
}
