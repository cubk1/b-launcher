#pragma once

#include "app/downloads/Addons.h"

#include <string>

namespace Dependencies {

    struct Node {
        Addons::Status status = Addons::Status::Loading;
        std::string error;
        Addons::Version version;
    };

    const Node& resolve(Addons::Source source, const Addons::Dependency& dependency, const std::string& gameVersion,
                        const std::string& loader);
    void poll();
    void shutdown();
}
