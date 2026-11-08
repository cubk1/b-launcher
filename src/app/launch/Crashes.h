#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Crashes {

    struct Cause {
        std::string title;
        std::string detail;
    };

    struct Launch {
        std::string instance;
        std::string name;
        std::string minecraft;
        std::vector<std::string> loaders;
        std::filesystem::path gameDirectory;
        std::string java;
        std::string javaVersion;
        int javaMajor = 0;
        unsigned javaBits = 0;
        uint32_t memoryMb = 0;
        std::string token;
        int pid = -1;
        int log = 0;
        int64_t since = 0;
        double uptime = 0.0;
        int code = 0;
    };

    struct Report {
        Launch launch;
        std::chrono::system_clock::time_point at;
        std::filesystem::path crashReport;
        std::filesystem::path errorLog;
        std::vector<Cause> causes;
        std::string summary;
        std::string output;
    };

    std::shared_ptr<const Report> examine(Launch launch, std::string output);
    std::string exitText(int code);
    std::string fileName(const Report& report);
    void ask(std::shared_ptr<const Report> report, const std::string& key);
    bool asking();
    void save(std::shared_ptr<const Report> report, const std::filesystem::path& target);
    bool saving();

    void poll();
    void shutdown();
}
