#pragma once

namespace ThemeStyle {

    struct Style {
        float radiusScale = 1.0f;
        float radiusAbsolute = -1.0f;
        bool squarePills = false;
        float windowRadius = 8.0f;
        bool icons = true;
        bool sounds = true;
        bool animations = true;
        bool outlines = true;
        bool rowDividers = true;

        bool operator==(const Style& other) const = default;
    };

    void apply(const Style& style);
    const Style& current();
    bool icons();
    bool sounds();
    bool animations();
    bool outlines();
    bool rowDividers();
    float radius(float radius, float width, float height, float unit);
}
