#pragma once

#include "app/downloads/Addons.h"

#include <string>

namespace Prerequisites {

    void context(const Addons::Version& version, const std::string& instanceKey, std::string& gameVersion,
                 std::string& loader);
    bool installed(const std::string& instanceKey, Addons::Source source, const std::string& projectId,
                   const Addons::Version* version);
    bool needed(const Addons::Version& version);
    void open(const Addons::Version& version, const std::string& title, bool saveAs, const std::string& instanceKey);
}
