#pragma once

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/theme/ThemePaintSpec.h"

#include <string>
#include <vector>

namespace ThemeShaders {

    struct LayerState {
        std::string source;
        std::string status;
        std::string log;
        float gpuMs = 0.0f;
        bool halfRes = false;
    };

    void startup();
    void shutdown();
    void frame();
    bool enabled();
    void setEnabled(bool on);
    bool blocked();
    void unblock();
    void suppress(bool on);
    void reset();
    bool draw(const ThemePaintSpec::Layer& layer, const ShaderUtil::Shape& shape, const ShaderUtil::Shape* clip,
              float opacity);
    std::vector<LayerState> states();
    std::vector<std::string> problems();
}
