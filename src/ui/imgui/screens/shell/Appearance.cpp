#include "ui/imgui/screens/shell/Appearance.h"
#include "app/system/Config.h"
#include "ui/imgui/screens/shell/EmbeddedSounds.h"

#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiSound.h"

#include <cstdio>

namespace Appearance {

    namespace {
        constexpr uint32_t DEFAULT_ACCENT = 0xFF4F7DFFu;

        const std::vector<Theme> THEMES = {
                {"plain", "简约", UiTheme::MODE_PLAIN},
                {"dark", "深色", UiTheme::MODE_SHADCN},
                {"light", "浅色", UiTheme::MODE_SHADCN_LIGHT},
                {"iridium-dark", "Iridium 深色", UiTheme::MODE_DARK},
                {"iridium-light", "Iridium 浅色", UiTheme::MODE_LIGHT},
        };

        const Theme* current = &THEMES[0];

        const Theme* find(const std::string& key) {
            for (const Theme& theme : THEMES) {
                if (key == theme.key) {
                    return &theme;
                }
            }
            return nullptr;
        }

        std::string text(const char* key, const std::string& fallback) {
            const nlohmann::json& doc = Config::data();
            auto value = doc.find(key);
            return value != doc.end() && value->is_string() ? value->get<std::string>() : fallback;
        }

        std::string hex(uint32_t argb) {
            char out[8];
            std::snprintf(out, sizeof(out), "#%06X", argb & 0x00FFFFFFu);
            return out;
        }

        bool parseHex(const std::string& value, uint32_t& argb) {
            unsigned int rgb = 0;
            if (value.size() != 7 || value[0] != '#' || std::sscanf(value.c_str() + 1, "%6x", &rgb) != 1) {
                return false;
            }
            argb = 0xFF000000u | rgb;
            return true;
        }

        void apply(const Theme& theme) {
            current = &theme;
            UiTheme::setMode(theme.mode);
        }
    }

    const std::vector<Theme>& themes() {
        return THEMES;
    }

    void load() {
        const Theme* saved = find(text("theme", "plain"));
        apply(saved != nullptr ? *saved : THEMES[0]);
        uint32_t color = DEFAULT_ACCENT;
        parseHex(text("accent", ""), color);
        UiTheme::setAccent(color);
        const float volume = 0.32f;
        UiSound::load(UiSound::PRESS, EmbeddedSounds::press, EmbeddedSounds::pressSize, volume);
        UiSound::load(UiSound::TICK, EmbeddedSounds::tick, EmbeddedSounds::tickSize, volume);
        UiSound::load(UiSound::RELEASE, EmbeddedSounds::release, EmbeddedSounds::releaseSize, volume);
        UiSound::load(UiSound::PAGE, EmbeddedSounds::page, EmbeddedSounds::pageSize, volume);
        UiSound::load(UiSound::PULSE, EmbeddedSounds::pulse, EmbeddedSounds::pulseSize, volume);
        UiSound::enabled = uiSounds();
    }

    const Theme& theme() {
        return *current;
    }

    void setTheme(const std::string& key) {
        const Theme* next = find(key);
        if (next == nullptr || next == current) {
            return;
        }
        apply(*next);
        Config::data()["theme"] = next->key;
        Config::save();
    }

    void toggleLight() {
        if (UiTheme::isLight()) {
            setTheme(text("lastDarkTheme", "plain"));
            return;
        }
        Config::data()["lastDarkTheme"] = current->key;
        setTheme(std::string(current->key) == "iridium-dark" ? "iridium-light" : "light");
    }

    uint32_t accent() {
        return UiTheme::accent();
    }

    void setAccent(uint32_t argb) {
        UiTheme::setAccent(0xFF000000u | argb);
        Config::data()["accent"] = hex(argb);
        Config::save();
    }

    uint32_t defaultAccent() {
        return DEFAULT_ACCENT;
    }

    const std::vector<int>& zooms() {
        static const std::vector<int> options = {75, 100, 125, 150, 175, 200};
        return options;
    }

    int zoom() {
        const nlohmann::json& doc = Config::data();
        auto value = doc.find("zoom");
        int percent = value != doc.end() && value->is_number_integer() ? value->get<int>() : 100;
        return percent < 75 ? 75 : percent > 200 ? 200 : percent;
    }

    void setZoom(int percent) {
        percent = percent < 75 ? 75 : percent > 200 ? 200 : percent;
        if (percent == zoom()) {
            return;
        }
        Config::data()["zoom"] = percent;
        Config::save();
    }

    bool backgroundImage() {
        const nlohmann::json& doc = Config::data();
        auto value = doc.find("backgroundImage");
        return value != doc.end() && value->is_boolean() && value->get<bool>();
    }

    void setBackgroundImage(bool on) {
        Config::data()["backgroundImage"] = on;
        Config::save();
    }

    bool uiSounds() {
        const nlohmann::json& doc = Config::data();
        auto value = doc.find("uiSounds");
        return value == doc.end() || !value->is_boolean() || value->get<bool>();
    }

    void setUiSounds(bool on) {
        UiSound::enabled = on;
        Config::data()["uiSounds"] = on;
        Config::save();
    }

    bool rememberWindow() {
        const nlohmann::json& doc = Config::data();
        auto value = doc.find("rememberWindow");
        return value == doc.end() || !value->is_boolean() || value->get<bool>();
    }

    void setRememberWindow(bool on) {
        Config::data()["rememberWindow"] = on;
        Config::save();
    }

    const char* defaultWindowTitle() {
        return "B Launcher";
    }

    std::string customWindowTitle() {
        return Config::data().value("windowTitle", std::string());
    }

    std::string windowTitle() {
        std::string custom = customWindowTitle();
        return custom.empty() ? std::string(defaultWindowTitle()) : custom;
    }

    void setWindowTitle(const std::string& title) {
        Config::data()["windowTitle"] = title;
        Config::save();
    }

    const char* defaultBrandTitle() {
        return "B Launcher\xE2\x84\xA2";
    }

    std::string customBrandTitle() {
        return Config::data().value("brandTitle", std::string());
    }

    std::string brandTitle() {
        std::string custom = customBrandTitle();
        return custom.empty() ? std::string(defaultBrandTitle()) : custom;
    }

    void setBrandTitle(const std::string& title) {
        Config::data()["brandTitle"] = title;
        Config::save();
    }
}
