#pragma once

#include "ui/imgui/screens/theme/ImageSample.h"
#include "ui/imgui/screens/theme/ThemeResolver.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ThemeRuntime {

    void install(const std::string& id);
    const std::string& current();
    void setUserAccent(uint32_t argb);
    uint32_t userAccent();
    void setImageSample(const ImageSample& sample);
    const std::vector<ThemeSetting>& settings();
    nlohmann::json settingValues();
    void setSetting(const std::string& id, const nlohmann::json& value);
    void commit();
    const std::vector<std::string>& problems();
    bool animated();
    double time();
    const ThemeInputs& currentInputs();
    ThemeFacts facts();
    bool accentPinned();
    std::string accentPicker();
    bool effects();
    std::optional<nlohmann::json> press(const nlohmann::json& event);
    void reload();
    double frameInterval(bool foreground, bool visible);
    bool animationEnabled();
    void setAnimationEnabled(bool on);
    int frameCap();
    void setFrameCap(int fps);
}
