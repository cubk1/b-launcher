#pragma once

#include "ui/imgui/value/Values.h"

#include <memory>
#include <string>
#include <vector>

namespace HomeWidgets {

    constexpr const char* QUICK = "quickActions";
    constexpr const char* SERVER = "server";
    constexpr const char* ACCOUNT = "account";
    constexpr const char* LAUNCH = "launch";
    constexpr const char* SHOTS = "recentShots";

    std::shared_ptr<Choice> choice();
    bool enabled(const std::string& id);
    bool launchWidget();

    struct WidgetKind {
        const char* id;
        const char* label;
        int glyph;
    };

    const std::vector<WidgetKind>& widgetKinds();
    void setWidgetEnabled(const std::string& id, bool on);

    std::string serverAddress();
    void setServerAddress(const std::string& value);
    std::string serverName();
    void setServerName(const std::string& value);

    bool alignBottom();
    void setAlignBottom(bool bottom);

    void panel(float x, float y, float w, float h);
    float width(float left, float right);
    float height(float w);
    void draw(float x, float y, float w, bool blocked);
}
