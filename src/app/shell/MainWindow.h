#pragma once

#include <string>

namespace MainWindow {

    constexpr float MIN_WIDTH = 820.0f;
    constexpr float MIN_HEIGHT = 510.0f;
    constexpr float TITLE_HEIGHT = 44.0f;
    constexpr float SIDEBAR_WIDTH = 188.0f;

    extern bool closeRequested;
    extern bool minimizeRequested;
    extern bool maximizeRequested;

    void init();
    void setSize(float width, float height, bool maximized);
    float width();
    float sidebarWidth();
    float height();
    bool maximized();
    void save();

    void draw();
    void showHome();
    void showAccounts();
    void showNewInstance();
    void showDownloads();
    void showLoaderEdit();
    void openVersionSettings(const std::string& key);
    std::string shownInstance();
    void renamed(const std::string& from, const std::string& to);
    bool isDragArea(float x, float y);
}
