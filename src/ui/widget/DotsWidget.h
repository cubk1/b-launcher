#pragma once

#include "Widget.h"
#include "../value/Setting.h"

class DotsWidget final : public Widget {
public:
    SettingsPagePtr page;
    float anchorX;
    float anchorY;

    DotsWidget(const std::string& id, SettingsPagePtr page);
    DotsWidget& dotsAt(float x, float y);
    DotsWidget& anchor(float x, float y);
    bool expanded() const;
    void paint() override;
};
