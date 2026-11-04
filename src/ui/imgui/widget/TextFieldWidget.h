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
    float leading;
    float corner;

    explicit TextFieldWidget(std::shared_ptr<TextValue> value);
    TextFieldWidget& box(float x, float y, float w, float h);
    TextFieldWidget& room(float right);
    TextFieldWidget& lead(float left);
    TextFieldWidget& radius(float value);
    bool editing() const;
    void paint() override;
};
