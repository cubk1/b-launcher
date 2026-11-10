#pragma once

#include "app/downloads/Addons.h"

#include <utility>
#include <vector>

namespace ProjectBrowser {

    bool usesInstance(Addons::Kind kind);
    bool hasLoaders(Addons::Source source, Addons::Kind kind);
    const std::vector<std::string>& loaderTags(Addons::Source source, Addons::Kind kind);
    // Every loader of the given sources, each once, in the order the sources offer them.
    std::vector<std::string> loaderTagsOf(const std::vector<Addons::Source>& sources, Addons::Kind kind);
    // Whether the source publishes projects of the kind: Modrinth has no worlds.
    bool supports(Addons::Source source, Addons::Kind kind);

    struct SortOption {
        Addons::Sort sort;
        const char* label;
    };
    const std::vector<SortOption>& sorts(Addons::Source source);
    // The sorts both platforms understand, for searches across them.
    const std::vector<SortOption>& sorts();

    struct Found {
        Addons::Source source = Addons::Source::Modrinth;
        Addons::Hit hit;
    };

    struct MergedHit {
        // The project as shown: that of the platform with the most downloads.
        Addons::Hit hit;
        Addons::Source primary = Addons::Source::Modrinth;
        // One entry per platform the project was found on.
        std::vector<Found> found;
    };

    struct Merged {
        Addons::Status status = Addons::Status::Loading;
        std::vector<MergedHit> hits;
        int total = 0;
        int pages = 1;
        std::string error;
        std::vector<Addons::Source> failed;
    };

    // The searches of the sources, one page each, laid side by side: a project found on more than one platform keeps a
    // single row, with the platforms it was found on. Read in the frame it is asked for.
    Merged merge(const std::vector<std::pair<Addons::Source, const Addons::Results*>>& lists);

    struct MergedCategory {
        std::string label;
        std::string header;
        // What the platforms call the category, "mr:" for a Modrinth tag and "cf:" for a CurseForge id.
        std::vector<std::string> keys;
        std::string iconUrl;
    };

    // The categories of the sources under one name each, holding what every platform calls it.
    const std::vector<MergedCategory>& categories(const std::vector<Addons::Source>& sources, Addons::Kind kind);

    // A missing instance leaves the current search unchanged.
    void followInstance(Addons::Query& query, const std::string& instanceKey);

    // Returned pointers remain valid while the input list is unchanged.
    std::vector<const Addons::Version*> matching(const std::vector<Addons::Version>& versions,
                                                const std::string& game, const std::string& loader);
    std::string sharedPrefix(const std::vector<Addons::Version>& versions);
    std::string shortName(const Addons::Version& version, const std::string& prefix, const std::string& title);
}
