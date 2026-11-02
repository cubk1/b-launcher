#pragma once

#include <string>

namespace PclThemes {

    enum class State { Idle, Done, Failed };

    struct Status {
        State state = State::Idle;
        std::string id;
        std::string error;
    };

    bool installed();
    const Status& status();
    void unlock();
}
