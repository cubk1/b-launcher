#pragma once

#include <cstdint>
#include <string>

namespace SvgPath {

    bool valid(const std::string& data);
    bool bounds(const std::string& data, float& x, float& y, float& w, float& h);
    void draw(const std::string& data, float x, float y, float w, float h, uint32_t rgba, bool uniform = true);
}
