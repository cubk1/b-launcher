#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <memory>

class KeyFieldWidget final : public Widget {
public:
    std::shared_ptr<KeyValue> value;

    KeyFieldWidget(const std::string& id, std::shared_ptr<KeyValue> value);
    bool capturing() const;
    void paint() override;
};
