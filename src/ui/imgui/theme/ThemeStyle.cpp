#include "ThemeStyle.h"

#include <algorithm>

namespace ThemeStyle {

    namespace {
        Style active;
    }

    void apply(const Style& style) {
        active = style;
    }

    const Style& current() {
        return active;
    }

    bool icons() {
        return active.icons;
    }

    bool sounds() {
        return active.sounds;
    }

    bool animations() {
        return active.animations;
    }

    bool outlines() {
        return active.outlines;
    }

    bool rowDividers() {
        return active.rowDividers;
    }

    float radius(float radius, float width, float height, float unit) {
        if ((active.radiusScale == 1.0f && active.radiusAbsolute < 0.0f) || radius <= 0.0f) {
            return radius;
        }
        float half = std::max(0.0f, std::min(width, height) * 0.5f);
        if (!active.squarePills && radius >= half - 0.5f * unit) {
            return radius;
        }
        float styled = active.radiusAbsolute >= 0.0f ? active.radiusAbsolute * unit : radius * active.radiusScale;
        return std::clamp(styled, 0.0f, half);
    }
}
