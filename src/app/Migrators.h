#pragma once

#include "MicrosoftClients.h"

#include <string>
#include <vector>

namespace Migrators {

    struct Found {
        MicrosoftClients::App app;
        std::string launcher;
        std::string name;
        std::string refreshToken;
    };

    std::vector<Found> scan();
}
