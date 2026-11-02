#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace CustomDownload {

    enum class State { Idle, Probing, Running, Done, Failed };

    struct Status {
        State state = State::Idle;
        std::string url;
        std::filesystem::path target;
        uint64_t done = 0;
        uint64_t total = 0;
        double speed = 0.0;
        std::string error;
    };

    const Status& status();
    bool busy();
    std::string nameOf(const std::string& url);
    void start(const std::string& url, const std::filesystem::path& target);
    void cancel();

    void poll();
    void shutdown();
}
