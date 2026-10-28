#pragma once

#include <string>

namespace Updates {

    enum class State { Idle, Checking, Latest, Available, Failed };

    void check(bool notifyFound, bool notifyFailure);
    State state();
    const std::string& latest();
    void openDownload();
    void poll();
    void shutdown();
}
