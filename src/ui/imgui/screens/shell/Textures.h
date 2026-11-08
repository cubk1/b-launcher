#pragma once

#include "app/system/Pixels.h"

#include <cstdint>

namespace Textures {

    uint32_t of(const std::shared_ptr<const Pixels>& pixels);
    void sharp(const std::shared_ptr<const Pixels>& pixels, float x, float y, float w, float h, float radius);
}
