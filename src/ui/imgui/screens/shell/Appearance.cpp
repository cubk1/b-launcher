#include "ui/imgui/screens/shell/Appearance.h"
#include "app/system/About.h"
#include "app/system/Config.h"
#include "app/system/Customization.h"
#include "ui/imgui/screens/shell/EmbeddedSounds.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/theme/ThemePaintSpec.h"

#include "ui/imgui/panel/UiSound.h"

#include <core/base/json.hpp>
#include <cstdio>
#include <charconv>

namespace Appearance {

    namespace {
        constexpr uint32_t DEFAULT_ACCENT = 0xFF4F7DFFu;

        const char* const DEFAULT_THEME = "plain";

        std::vector<Theme> listed;
        Theme shown;

        std::string text(const char* key, const std::string& fallback) {
            return b_launcher_base::json_text(Config::data(), key, fallback);
        }

        std::string hex(uint32_t argb) {
            char out[8];
            std::snprintf(out, sizeof(out), "#%06X", argb & 0x00FFFFFFu);
            return out;
        }

        bool parseHex(const std::string& value, uint32_t& argb) {
            if (value.size() != 7 || value[0] != '#') {
                return false;
            }
            uint32_t rgb = 0;
            const char* end = value.data() + value.size();
            auto parsed = std::from_chars(value.data() + 1, end, rgb, 16);
            if (parsed.ec != std::errc() || parsed.ptr != end) {
                return false;
            }
            argb = 0xFF000000u | rgb;
            return true;
        }

    }

    const std::vector<Theme>& themes() {
        ThemeStore::rescanIfStale();
        listed.clear();
        for (const ThemeStore::Entry& entry : ThemeStore::entries()) {
            listed.push_back({entry.id, entry.name, entry.builtIn});
        }
        return listed;
    }

    void load() {
        ThemeStore::rescan();
        uint32_t color = DEFAULT_ACCENT;
        parseHex(text("accent", ""), color);
        ThemeRuntime::setUserAccent(color);
        std::string saved = text("theme", DEFAULT_THEME);
        ThemeRuntime::install(ThemeStore::find(saved) != nullptr ? saved : std::string(DEFAULT_THEME));
        ThemeRuntime::commit();
        const float volume = 0.32f;
        UiSound::load(UiSound::PRESS, EmbeddedSounds::press, EmbeddedSounds::pressSize, volume);
        UiSound::load(UiSound::TICK, EmbeddedSounds::tick, EmbeddedSounds::tickSize, volume);
        UiSound::load(UiSound::RELEASE, EmbeddedSounds::release, EmbeddedSounds::releaseSize, volume);
        UiSound::load(UiSound::PAGE, EmbeddedSounds::page, EmbeddedSounds::pageSize, volume);
        UiSound::load(UiSound::PULSE, EmbeddedSounds::pulse, EmbeddedSounds::pulseSize, volume);
        UiSound::enabled = uiSounds();
    }

    const Theme& theme() {
        const ThemeStore::Entry* entry = ThemeStore::find(ThemeRuntime::current());
        shown = entry != nullptr ? Theme{entry->id, entry->name, entry->builtIn}
                                 : Theme{ThemeRuntime::current(), ThemeRuntime::current(), false};
        return shown;
    }

    void setTheme(const std::string& key) {
        if (Customization::themeLocked() || key == ThemeRuntime::current() || ThemeStore::find(key) == nullptr) {
            return;
        }
        ThemeRuntime::install(key);
        Config::data()["theme"] = key;
        Config::save();
    }

    uint32_t accent() {
        return ThemeRuntime::userAccent();
    }

    void setAccent(uint32_t argb) {
        ThemeRuntime::setUserAccent(0xFF000000u | argb);
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
        auto it = doc.find("backgroundImage");
        if (it != doc.end() && it->is_boolean()) {
            return it->get<bool>();
        }
        return ThemePaintSpec::current().background.enabled == 1;
    }

    void setBackgroundImage(bool on) {
        Config::data()["backgroundImage"] = on;
        Config::save();
    }

    bool uiSounds() {
        return b_launcher_base::json_bool(Config::data(), "uiSounds", true);
    }

    void setUiSounds(bool on) {
        UiSound::enabled = on;
        Config::data()["uiSounds"] = on;
        Config::save();
    }

    bool rememberWindow() {
        return b_launcher_base::json_bool(Config::data(), "rememberWindow", true);
    }

    void setRememberWindow(bool on) {
        Config::data()["rememberWindow"] = on;
        Config::save();
    }

    bool systemFrame() {
        return b_launcher_base::json_bool(Config::data(), "systemFrame");
    }

    void setSystemFrame(bool on) {
        Config::data()["systemFrame"] = on;
        Config::save();
    }

    bool systemFrameRunning() {
        static const bool running = systemFrame();
        return running;
    }

    const char* defaultWindowTitle() {
        return About::NAME;
    }

    std::string customWindowTitle() {
        return text("windowTitle", "");
    }

    std::string windowTitle() {
        std::string custom = customWindowTitle();
        if (custom.empty() && !Customization::windowTitle().empty()) {
            return Customization::windowTitle();
        }
        return custom.empty() ? std::string(defaultWindowTitle()) : custom;
    }

    void setWindowTitle(const std::string& title) {
        Config::data()["windowTitle"] = title;
        Config::save();
    }

    const char* defaultBrandTitle() {
        return About::BRAND;
    }

    std::string customBrandTitle() {
        return text("brandTitle", "");
    }

    std::string brandTitle() {
        std::string custom = customBrandTitle();
        if (custom.empty() && !Customization::brandTitle().empty()) {
            return Customization::brandTitle();
        }
        return custom.empty() ? std::string(defaultBrandTitle()) : custom;
    }

    void setBrandTitle(const std::string& title) {
        Config::data()["brandTitle"] = title;
        Config::save();
    }
}
