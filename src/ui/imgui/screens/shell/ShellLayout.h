#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ShellLayout {

    enum class Preset { Sidebar, Topbar, Launch };

    struct Spec {
        Preset preset = Preset::Sidebar;
        float sidebarWidth = 188.0f;
        float sidebarTitle = 44.0f;
        float titleHeight = 48.0f;
        float columnWidth = 176.0f;
        float launchWidth = 268.0f;
        float launchAvatar = 0.0f;
        float launchAvatarRadius = -1.0f;
        float innerRadius = 14.0f;
        std::string brandText;
        bool brandIcon = true;
        float brandSize = 15.0f;
        bool maximizeButton = true;
        bool navCentered = true;
        bool operator==(const Spec&) const = default;
    };

    void read(const nlohmann::json& node, Spec& spec);
    void setTheme(const Spec& spec);
    const Spec& theme();
    Spec current();
    Spec effective(const Spec& theme);
    std::string choice();
    void setChoice(const std::string& label);
    std::vector<std::string> choices();
}
