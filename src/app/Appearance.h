#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Appearance {

    struct Theme {
        const char* key;
        const char* label;
        int mode;
    };

    const std::vector<Theme>& themes();
    void load();
    const Theme& theme();
    void setTheme(const std::string& key);
    void toggleLight();

    uint32_t accent();
    void setAccent(uint32_t argb);
    uint32_t defaultAccent();

    const std::vector<int>& zooms();
    int zoom();
    void setZoom(int percent);

    bool backgroundImage();
    void setBackgroundImage(bool on);

    bool uiSounds();
    void setUiSounds(bool on);
    bool rememberWindow();
    void setRememberWindow(bool on);
}
