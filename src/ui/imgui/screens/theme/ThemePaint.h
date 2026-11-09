#pragma once

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/theme/ThemePaintSpec.h"

#include <cstdint>
#include <string>

namespace ThemePaint {

    bool custom(ThemePaintSpec::Region region);
    void fill(ThemePaintSpec::Region region, float x, float y, float w, float h, float radius, int cornerMask,
              uint32_t fallback);
    void paintRect(ThemePaintSpec::Region region, float x, float y, float w, float h, float radius, int cornerMask,
                   float opacity);
    void layers(ThemePaintSpec::Region region, const ShaderUtil::Shape& shape, const ShaderUtil::Shape* clip,
                const ShaderUtil::Shape* hole, float opacity);
    float blur(ThemePaintSpec::Region region);
    void primaryFill(float x, float y, float w, float h, float radius, float lift);

    struct Frames {
        float rects[ThemePaintSpec::CONTENT + 1][4];
    };
    void decorations(bool above, const std::string& page, const Frames& frames, const ShaderUtil::Shape& windowClip,
                     const ShaderUtil::Shape& shellClip);
    void selectedFill(ThemePaintSpec::Region region, const std::string& id, float x, float y, float w, float h,
                      float radius, bool selected, bool hovered, float lit);
}
