#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class SwatchWidget final : public Widget {
public:
    std::shared_ptr<ColorValue> color;
    float pickX;
    float pickY;
    bool ring;

    explicit SwatchWidget(std::shared_ptr<ColorValue> color);
    SwatchWidget& swatchAt(float x, float y);
    SwatchWidget& picker(float x, float y);
    SwatchWidget& hoverRing();
    void paint() override;
};
