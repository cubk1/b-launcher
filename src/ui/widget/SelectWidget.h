#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class SelectWidget final : public Widget {
public:
    std::shared_ptr<Choice> choice;
    bool compact;
    float right;
    float room;

    SelectWidget(const std::string& id, std::shared_ptr<Choice> choice);
    SelectWidget& compactRow(float rowX, float rowY, float rowW, float rowH, float right, float room);
    bool showing() const;
    void open();
    void paint() override;
};
