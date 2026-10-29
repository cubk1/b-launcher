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
    // The interface of this run: the style --ui names, even one settings do not offer yet, or else the
    // saved one.
    const Style& choose(const std::string& requested);
    void set(const std::string& key);
}
