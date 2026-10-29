#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class SliderWidget final : public Widget {
public:
    std::shared_ptr<NumberValue> value;
    float pillRight;
    bool compact;

    SliderWidget(const std::string& id, std::shared_ptr<NumberValue> value);
    SliderWidget& compactTrack(float trackX, float trackY, float trackW);
    SliderWidget& track(float trackX, float trackY, float trackW, float pillRight);
    std::string text();
    void paintCompact();
    void paint() override;
};
