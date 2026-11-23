#pragma once

#include "app/downloads/Addons.h"

#include <cstdint>
#include <string>
#include <vector>

namespace PackUpdates {

    struct Origin {
        Addons::Source source = Addons::Source::Modrinth;
        std::string project;
        std::string version;
        std::string versionName;
    };

    const Origin* origin(const std::string& instanceKey);

    enum class Phase { Idle, Planning, Planned, Applying, Failed };

    struct Summary {
        std::string version;
        std::string minecraft;
        std::vector<std::string> loaders;
        int added = 0;
        int changed = 0;
        int removed = 0;
        std::vector<std::string> modified;
        std::vector<std::string> kept;
        uint64_t bytes = 0;
        uint64_t modifiedBytes = 0;
        uint64_t archive = 0;
        bool loadersChanged = false;
        bool tracked = false;
    };

    struct State {
        Phase phase = Phase::Idle;
        std::string instanceKey;
        std::string versionId;
        Summary summary;
        std::string error;
    };

    const State& state();
    std::string progress();
    void plan(const std::string& instanceKey, const Addons::Version& version);
    void apply(bool keepModified);
    void cancel();
    void reset();
    bool running();
    void poll();
    void shutdown();
}
