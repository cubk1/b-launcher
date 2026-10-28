#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class TextFieldWidget final : public Widget {
public:
    std::shared_ptr<TextValue> value;
    float boxX;
    float boxY;
    float boxW;
    float boxH;
    float trail;

    explicit TextFieldWidget(std::shared_ptr<TextValue> value);
    TextFieldWidget& box(float x, float y, float w, float h);
    TextFieldWidget& room(float right);
    bool editing() const;
    void paint() override;
};
