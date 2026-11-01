#pragma once

#include "app/downloads/Addons.h"

namespace ProjectBrowser {

    // A missing instance leaves the current search unchanged.
    void followInstance(Addons::Query& query, const std::string& instanceKey);

    // Returned pointers remain valid while the input list is unchanged.
    std::vector<const Addons::Version*> matching(const std::vector<Addons::Version>& versions,
                                                const std::string& game, const std::string& loader);
    std::string sharedPrefix(const std::vector<Addons::Version>& versions);
    std::string shortName(const Addons::Version& version, const std::string& prefix, const std::string& title);
}
