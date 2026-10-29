#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class ShortcutWidget final : public Widget {
public:
    std::shared_ptr<ShortcutValue> value;

    ShortcutWidget(const std::string& id, std::shared_ptr<ShortcutValue> value);
    bool recording() const;
    void paint() override;
};
