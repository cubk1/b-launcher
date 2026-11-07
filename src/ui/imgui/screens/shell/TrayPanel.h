#pragma once

#include "ui/imgui/screens/shell/BorderlessWindow.h"

#include <functional>
#include <string>

class TrayPanel {
public:
    explicit TrayPanel(BorderlessWindow& window);
    ~TrayPanel();
    TrayPanel(const TrayPanel&) = delete;
    TrayPanel& operator=(const TrayPanel&) = delete;

    std::function<void()> onOpenMain;
    std::function<void()> onQuit;

    bool active() const;
    void enter(float scale, bool reveal, bool maximized = false);
    void leave(bool reveal, int minWidth, int minHeight);
    void show();
    void hide();
    void fit(float scale);
    void presented();
    void setIcon(HICON icon);
    void setTip(const std::wstring& tip);
    void removeIcon();
    bool message(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
    void addIcon();
    void place();
    void menu();

    BorderlessWindow& window;
    bool on = false;
    bool iconShown = false;
    HICON icon = nullptr;
    std::wstring tip;
    float scale = 1.0f;
    WINDOWPLACEMENT saved = {};
    bool haveSaved = false;
    bool savedMaximized = false;
    ULONGLONG hiddenAt = 0;
    ULONGLONG shownAt = 0;
    int cloakedFrames = 0;
    UINT taskbarCreated = 0;
};
