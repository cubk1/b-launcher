#pragma once

#include "app/downloads/Addons.h"

#include <cstdint>

namespace SourceMarks {

    void draw(Addons::Source source, float x, float y, float size, uint32_t color);
    void mcmod(float x, float y, float size, uint32_t color);
    void mcmodColored(float x, float y, float size, float alpha);
    uint32_t brand(Addons::Source source);
}
