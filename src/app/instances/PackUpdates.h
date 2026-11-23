#pragma once

#include "app/downloads/Addons.h"

#include <cstdint>
#include <filesystem>
#include <set>
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
    bool identifiable(const std::string& instanceKey);
    bool addedFile(const std::string& instanceKey, const std::filesystem::path& file, uint64_t size);
    void recognize(const std::string& instanceKey);
    void adoptIcon(const std::string& instanceKey, const std::string& url);

    struct Candidate {
        std::string title;
        std::string version;
        Addons::Source source = Addons::Source::Modrinth;
        size_t matched = 0;
        size_t total = 0;
        bool exact = false;
    };

    enum class Search { Idle, Running, Binding, Done, Failed };

    struct Identification {
        Search phase = Search::Idle;
        std::string instanceKey;
        std::vector<Candidate> candidates;
        std::string error;
    };

    const Identification& identification();
    void identify(const std::string& instanceKey);
    void choose(size_t index);

    enum class Phase { Idle, Planning, Planned, Applying, Failed };

    enum class Change { Add, Update, Remove };

    struct Item {
        std::string path;
        Change change = Change::Add;
        bool modified = false;
        uint64_t bytes = 0;
        std::string from;
        bool extra = false;
    };

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
        std::vector<Item> items;
    };

    struct State {
        Phase phase = Phase::Idle;
        std::string instanceKey;
        std::string versionId;
        Summary summary;
        std::string error;
        uint64_t serial = 0;
        bool restore = false;
    };

    const State& state();
    std::string progress();

    enum class StepState { Pending, Active, Done };

    struct Step {
        std::string label;
        StepState state = StepState::Pending;
        uint64_t done = 0;
        uint64_t total = 0;
    };

    std::vector<Step> steps();
    void plan(const std::string& instanceKey, const Addons::Version& version);
    void restore(const std::string& instanceKey);
    void apply(const std::set<std::string>& keep);
    void cancel();
    void reset();
    bool running();
    void poll();
    void shutdown();
}
