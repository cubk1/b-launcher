#pragma once

#include <core/base/cancellation.hpp>

#include <string>
#include <vector>

namespace LanWorlds {

    struct World {
        int port = 0;
        std::string name;
        std::string version;
    };

    std::vector<World> scan(const b_launcher_base::cancellation_token& token);

    void announce(const std::string& motd, int port);
    void silence();
}
