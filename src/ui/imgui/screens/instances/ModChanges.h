#pragma once

#include <string>

namespace ModChanges {

    float height(const std::string& key);
    void draw(const std::string& key, float x, float y, float w, bool blocked);
}
