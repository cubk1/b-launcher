#pragma once

#include <optional>
#include <string>

namespace ShortcutLaunch {

    struct Outcome {
        bool launched = false;
        bool quiet = false;
    };

    void request(const std::string& key, bool quiet);
    void poll();
    std::optional<Outcome> take();
}
