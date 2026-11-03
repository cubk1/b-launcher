#pragma once

#include "app/system/Pixels.h"

#include <memory>
#include <string>
#include <vector>

namespace ServerList {

    struct Server {
        std::string name;
        std::string address;
        std::shared_ptr<const Pixels> icon;
    };

    const std::vector<Server>& servers();
    bool loading();
    void poll();
    void shutdown();
}
