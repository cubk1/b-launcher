#pragma once

#include <string>

namespace Updates {

    enum class State { Idle, Checking, Latest, Available, Failed };

    // False in builds from package managers (BLB_UPDATE_CHECK=OFF), which update the launcher themselves.
    bool enabled();
    void check(bool notifyFound, bool notifyFailure);
    State state();
    const std::string& latest();
    void openDownload();
    void poll();
    void shutdown();
}
