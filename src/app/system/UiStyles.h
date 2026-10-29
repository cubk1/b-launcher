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
    void set(const std::string& key);
}
