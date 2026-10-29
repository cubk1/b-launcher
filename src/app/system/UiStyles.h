#pragma once

#include <string>
#include <vector>

namespace UiStyles {

    struct Style {
        const char* key;
        const char* label;
        bool available;
    };

    const std::vector<Style>& all();
    const Style& current();
    // Picks the interface of this run: the style --ui names, even one settings do not offer yet, or else
    // the saved one.
    void start(const std::string& requested);
    // The interface of this run; a style chosen in settings takes over after a restart.
    const Style& running();
    void set(const std::string& key);
}
