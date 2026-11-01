#pragma once

#include "app/downloads/Addons.h"

namespace ProjectBrowser {

    bool usesInstance(Addons::Kind kind);
    bool hasLoaders(Addons::Source source, Addons::Kind kind);
    const std::vector<std::string>& loaderTags(Addons::Source source, Addons::Kind kind);

    struct SortOption {
        Addons::Sort sort;
        const char* label;
    };
    const std::vector<SortOption>& sorts(Addons::Source source);

    // A missing instance leaves the current search unchanged.
    void followInstance(Addons::Query& query, const std::string& instanceKey);

    // Returned pointers remain valid while the input list is unchanged.
    std::vector<const Addons::Version*> matching(const std::vector<Addons::Version>& versions,
                                                const std::string& game, const std::string& loader);
    std::string sharedPrefix(const std::vector<Addons::Version>& versions);
    std::string shortName(const Addons::Version& version, const std::string& prefix, const std::string& title);
}
