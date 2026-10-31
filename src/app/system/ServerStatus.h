#pragma once

#include "app/system/Pixels.h"

#include <memory>
#include <string>

namespace ServerStatus {

    struct Result {
        bool ok = false;
        std::string error;
        std::string motd;
        std::string version;
        int online = -1;
        int max = -1;
        int latency = 0;
        std::shared_ptr<const Pixels> favicon;
    };

    struct State {
        std::string address;
        bool pinging = false;
        bool done = false;
        Result result;
    };

    const State& of(const std::string& address);
    void refresh();
    void poll();
    void shutdown();
}
