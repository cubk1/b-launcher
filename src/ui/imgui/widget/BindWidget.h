#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class BindWidget final : public Widget {
public:
    std::shared_ptr<KeyValue> value;
    float right;

    explicit BindWidget(std::shared_ptr<KeyValue> value);
    BindWidget& row(float x, float y, float w, float h, float right);
    bool listening() const;
    void paint() override;
};
