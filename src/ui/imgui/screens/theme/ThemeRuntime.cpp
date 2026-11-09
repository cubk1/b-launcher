#include "ui/imgui/screens/theme/ThemeRuntime.h"

#include "app/system/Config.h"
#include "app/system/SystemColors.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/theme/ThemePaintSpec.h"
#include "ui/imgui/theme/ThemeStyle.h"

#include <core/base/json.hpp>

#include <algorithm>

#include <chrono>

namespace ThemeRuntime {

    namespace {
        const char* const FALLBACK = "plain";
        constexpr auto SYSTEM_INTERVAL = std::chrono::seconds(2);

        ThemeResolver resolver;
        ThemeInputs inputs;
        std::string active;
        bool loaded = false;
        bool dirty = false;
        bool systemRead = false;
        bool resolvedDark = false;
        std::chrono::steady_clock::time_point nextSystem;
        UiTheme::Palette pending;
        ThemeStyle::Style pendingStyle;
        ShellLayout::Spec pendingLayout;
        ThemePaintSpec::Spec pendingPaints;
        bool ticking = false;
        double clock = 0.0;
        double lastScript = -1.0;
        std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
        std::vector<std::string> notes;
        std::vector<std::string> combined;
        bool pressFailed = false;

        void readSystem() {
            inputs.system = SystemColors::read();
            systemRead = true;
            nextSystem = std::chrono::steady_clock::now() + SYSTEM_INTERVAL;
        }

        nlohmann::json storedSettings(const std::string& id) {
            const nlohmann::json& doc = Config::data();
            auto all = doc.find("themeSettings");
            if (all == doc.end() || !all->is_object()) {
                return nlohmann::json::object();
            }
            auto mine = all->find(id);
            return mine != all->end() && mine->is_object() ? *mine : nlohmann::json::object();
        }

        void resolveNow() {
            if (!loaded) {
                return;
            }
            pending = resolver.resolve(inputs);
            pendingStyle = resolver.style();
            pendingLayout = resolver.layout();
            pendingPaints = resolver.paints();
            dirty = true;
        }

        bool load(const std::string& id) {
            if (!systemRead) {
                readSystem();
            }
            resolvedDark = inputs.system.darkKnown && inputs.system.dark;
            if (!resolver.load(id, resolvedDark)) {
                return false;
            }
            active = id;
            inputs.settings = storedSettings(id);
            return true;
        }
    }

    void install(const std::string& id) {
        notes.clear();
        pressFailed = false;
        if (!load(id)) {
            notes.push_back("无法加载主题 " + id + "，已改用默认主题");
            notes.insert(notes.end(), resolver.loadIssues().begin(), resolver.loadIssues().end());
            if (!load(FALLBACK)) {
                return;
            }
        }
        loaded = true;
        resolveNow();
    }

    const std::string& current() {
        return active;
    }

    void setUserAccent(uint32_t argb) {
        if (inputs.accent == argb) {
            return;
        }
        inputs.accent = argb;
        resolveNow();
    }

    uint32_t userAccent() {
        return inputs.accent;
    }

    void setImageSwatches(const Swatches& swatches) {
        if (inputs.image == swatches) {
            return;
        }
        inputs.image = swatches;
        resolveNow();
    }

    const std::vector<ThemeSetting>& settings() {
        return resolver.settings();
    }

    nlohmann::json settingValues() {
        return resolver.settingValues(inputs.settings);
    }

    void setSetting(const std::string& id, const nlohmann::json& value) {
        if (active.empty()) {
            return;
        }
        inputs.settings[id] = value;
        nlohmann::json& all = Config::data()["themeSettings"];
        if (!all.is_object()) {
            all = nlohmann::json::object();
        }
        all[active] = inputs.settings;
        Config::save();
        resolveNow();
    }

    void commit() {
        auto now = std::chrono::steady_clock::now();
        double step = std::min(0.1, std::chrono::duration<double>(now - lastTick).count());
        lastTick = now;
        if (ticking) {
            clock += step;
            double rate = resolver.scriptRate();
            if (rate > 0.0 && clock - lastScript >= 1.0 / rate) {
                lastScript = clock;
                inputs.time = clock;
                resolveNow();
            }
        }
        if (loaded && now >= nextSystem) {
            SystemColors::State before = inputs.system;
            readSystem();
            if (!(before == inputs.system)) {
                bool dark = inputs.system.darkKnown && inputs.system.dark;
                if (resolver.followsSystem() && dark != resolvedDark) {
                    install(active);
                } else {
                    resolveNow();
                }
            }
        }
        if (!dirty) {
            return;
        }
        dirty = false;
        UiTheme::apply(pending);
        ThemeStyle::apply(pendingStyle);
        ShellLayout::setTheme(pendingLayout);
        ThemePaintSpec::apply(pendingPaints);
    }

    bool animated() {
        return loaded && (resolver.scriptRate() > 0.0 || resolver.paints().animated);
    }

    double time() {
        return clock;
    }

    const ThemeInputs& currentInputs() {
        return inputs;
    }

    ThemeFacts facts() {
        return resolver.facts();
    }

    bool accentPinned() {
        return resolver.accentPinned();
    }

    std::string accentPicker() {
        return resolver.accentPinned() ? std::string("none") : resolver.accentPicker();
    }

    bool effects() {
        return loaded && !pressFailed && resolver.hasPress();
    }

    std::optional<nlohmann::json> press(const nlohmann::json& event) {
        if (!effects()) {
            return std::nullopt;
        }
        std::string error;
        std::optional<nlohmann::json> output = resolver.press(event, error);
        if (!error.empty()) {
            pressFailed = true;
            notes.push_back("press()：" + error);
            return std::nullopt;
        }
        return output;
    }

    void reload() {
        if (!active.empty()) {
            install(active);
        }
    }

    double frameInterval(bool foreground, bool visible) {
        ticking = animated() && animationEnabled() && foreground && visible;
        return ticking ? 1000.0 / frameCap() : 0.0;
    }

    bool animationEnabled() {
        return b_launcher_base::json_bool(Config::data(), "themeAnimation", true);
    }

    void setAnimationEnabled(bool on) {
        Config::data()["themeAnimation"] = on;
        Config::save();
    }

    int frameCap() {
        const nlohmann::json& doc = Config::data();
        auto it = doc.find("themeFrameCap");
        int fps = it != doc.end() && it->is_number_integer() ? it->get<int>() : 30;
        return std::clamp(fps, 15, 60);
    }

    void setFrameCap(int fps) {
        Config::data()["themeFrameCap"] = std::clamp(fps, 15, 60);
        Config::save();
    }

    const std::vector<std::string>& problems() {
        combined = notes;
        combined.insert(combined.end(), resolver.problems().begin(), resolver.problems().end());
        return combined;
    }
}
