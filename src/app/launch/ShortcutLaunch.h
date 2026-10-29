#pragma once

#include <optional>
#include <string>

namespace ShortcutLaunch {

    struct Outcome {
        bool launched = false;
        bool quiet = false;
    };

    void request(const std::string& key, bool quiet);
    // A launch another b-launcher process forwarded with WM_COPYDATA; false when the message is not one.
    bool receive(unsigned long long kind, const std::wstring& text);
    void poll();
    std::optional<Outcome> take();
}
