#pragma once

#include <string>
#include <vector>

namespace Game {

    enum class State { Idle, Preparing, Running };

    struct Step {
        enum class Status { Waiting, Running, Done, Failed };

        std::string label;
        Status status = Status::Waiting;
        std::string detail;
    };

    State state();
    float progress();
    std::vector<Step> steps();
    const std::string& instance();

    void launch(const std::string& key, bool test = false);
    void setQuiet(bool on);
    void cancel();
    void stop();
    void poll();
    void shutdown();
}
