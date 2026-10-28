#pragma once

#include "Widget.h"
#include "../value/Values.h"

#include <cstdint>
#include <memory>

class ColorPickerWidget final : public Widget {
public:
    std::shared_ptr<ColorValue> color;
    bool square;
    bool accepts;

    ColorPickerWidget(std::shared_ptr<ColorValue> color, bool square, bool accepts);
    bool modeMenuShown() const;
    bool dragging(float areaX, float areaY, float areaW, float areaH, float menuX, float menuY, float menuW,
                  float menuH) const;
    void wheel(float areaX, float areaY, float areaW, float areaH, const std::string& channel);
    void paint() override;
    void paintHex(uint32_t argb, float boxX, float footY, float boxW, bool menuShown);
    void paintChannels(uint32_t argb, int alpha, const float* hsb, float fieldX, float footY, float fieldW,
                       bool menuShown);
};
