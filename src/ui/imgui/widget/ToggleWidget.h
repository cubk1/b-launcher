#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class ToggleWidget final : public Widget {
public:
    std::shared_ptr<Flag> flag;
    bool compact;

    ToggleWidget(const std::string& id, std::shared_ptr<Flag> flag, bool compact);
    void paint() override;
};
