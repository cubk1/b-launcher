#pragma once

#include "ui/value/Values.h"

#include <memory>

namespace QuickActions {

    std::shared_ptr<Choice> choice();
    float height();
    void draw(float x, float y, float w, bool blocked);
}
