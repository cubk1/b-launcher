#pragma once

#include <string>
#include <vector>

namespace TerracottaGuest {

    enum class Stage { Idle, Preparing, Connecting, Ready, Closing, Failed };

    struct Member {
        std::string name;
        std::string kind;
    };

    struct State {
        Stage stage = Stage::Idle;
        std::string code;
        std::string address;
        std::string error;
        std::string detail;
        std::vector<Member> members;
    };

    bool active();
    const State& state();
    std::string stageText();
    void join(const std::string& code);
    void leave();
    void dismiss();
    void poll();
    void shutdown();
}
