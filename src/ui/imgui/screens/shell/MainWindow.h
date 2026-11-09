#pragma once

#include "ui/imgui/screens/shell/ShellLayout.h"

#include <string>

namespace MainWindow {

    constexpr float MIN_WIDTH = 820.0f;
    constexpr float MIN_HEIGHT = 510.0f;
    constexpr float TITLE_HEIGHT = 44.0f;
    constexpr float SIDEBAR_WIDTH = 188.0f;

    extern bool closeRequested;
    extern bool minimizeRequested;
    extern float cornerRadius;
    extern bool maximizeRequested;

    void init();
    void setSize(float width, float height, bool maximized);
    float width();
    float sidebarWidth();
    float height();
    float titleHeight();
    float contentTop();
    bool maximized();

    struct Rect {
        float x;
        float y;
        float w;
        float h;
    };
    Rect panel();
    void save();

    void draw();
    float miniatureRadius(float width, float height);
    void miniature(float x, float y, float width, float height);
    void miniature(float x, float y, float width, float height, const ShellLayout::Spec* layout, bool placeholder);
    void showHome();
    std::string pageName();
    bool launchColumnShown();
    void showAccounts();
    void showSettings();
    void showNewInstance();
    void startNewInstance();
    void showDownloads();
    void showTransfers();
    void showVersions();
    void showLoaderEdit();
    void openVersionSettings(const std::string& key);
    std::string shownInstance();
    void renamed(const std::string& from, const std::string& to);
    bool isDragArea(float x, float y);
    bool isCollapsed(const std::string& source);
    void toggleGroup(const std::string& source);
    void claimSearch();
}
