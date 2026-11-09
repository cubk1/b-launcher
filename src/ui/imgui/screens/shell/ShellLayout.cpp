#include "ui/imgui/screens/shell/ShellLayout.h"

#include "app/system/Config.h"

#include <algorithm>
#include <array>

namespace ShellLayout {

    namespace {
        struct Choice {
            const char* key;
            const char* label;
        };

        constexpr std::array<Choice, 4> CHOICES = {{
                {"theme", "跟随主题"},
                {"sidebar", "侧栏"},
                {"topbar", "顶栏"},
                {"launch", "启动栏"},
        }};

        Spec themeSpec;

        std::string stored() {
            const nlohmann::json& doc = Config::data();
            auto it = doc.find("themeLayout");
            return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string("theme");
        }

        bool presetOf(const std::string& key, Preset& out) {
            if (key == "sidebar") {
                out = Preset::Sidebar;
            } else if (key == "topbar") {
                out = Preset::Topbar;
            } else if (key == "launch") {
                out = Preset::Launch;
            } else {
                return false;
            }
            return true;
        }

        void number(const nlohmann::json& node, const char* key, float low, float high, float& out) {
            auto it = node.find(key);
            if (it != node.end() && it->is_number()) {
                out = std::clamp(it->get<float>(), low, high);
            }
        }
    }

    void read(const nlohmann::json& node, Spec& spec) {
        if (!node.is_object()) {
            return;
        }
        auto preset = node.find("preset");
        if (preset != node.end() && preset->is_string()) {
            presetOf(preset->get<std::string>(), spec.preset);
        }
        number(node, "innerRadius", 0.0f, 24.0f, spec.innerRadius);
        auto brand = node.find("brand");
        if (brand != node.end() && brand->is_object()) {
            auto text = brand->find("text");
            if (text != brand->end() && text->is_string()) {
                spec.brandText = text->get<std::string>().substr(0, 48);
            }
            auto icon = brand->find("icon");
            if (icon != brand->end() && icon->is_boolean()) {
                spec.brandIcon = icon->get<bool>();
            }
            number(*brand, "size", 12.0f, 28.0f, spec.brandSize);
        }
        auto maximize = node.find("maximizeButton");
        if (maximize != node.end() && maximize->is_boolean()) {
            spec.maximizeButton = maximize->get<bool>();
        }
        auto sidebar = node.find("sidebar");
        if (sidebar != node.end() && sidebar->is_object()) {
            number(*sidebar, "width", 160.0f, 260.0f, spec.sidebarWidth);
            number(*sidebar, "titleHeight", 40.0f, 96.0f, spec.sidebarTitle);
        }
        auto topbar = node.find("topbar");
        if (topbar != node.end() && topbar->is_object()) {
            number(*topbar, "titleHeight", 40.0f, 64.0f, spec.titleHeight);
            number(*topbar, "columnWidth", 150.0f, 240.0f, spec.columnWidth);
            auto align = topbar->find("navAlign");
            if (align != topbar->end() && align->is_string()) {
                spec.navCentered = align->get<std::string>() != "left";
            }
        }
        auto launch = node.find("launch");
        if (launch != node.end() && launch->is_object()) {
            number(*launch, "columnWidth", 220.0f, 320.0f, spec.launchWidth);
            number(*launch, "avatarSize", 32.0f, 128.0f, spec.launchAvatar);
            number(*launch, "avatarRadius", 0.0f, 64.0f, spec.launchAvatarRadius);
        }
    }

    void setTheme(const Spec& spec) {
        themeSpec = spec;
    }

    const Spec& theme() {
        return themeSpec;
    }

    Spec current() {
        return effective(themeSpec);
    }

    Spec effective(const Spec& theme) {
        Spec spec = theme;
        presetOf(stored(), spec.preset);
        return spec;
    }

    std::string choice() {
        std::string key = stored();
        for (const Choice& entry : CHOICES) {
            if (key == entry.key) {
                return entry.label;
            }
        }
        return CHOICES[0].label;
    }

    void setChoice(const std::string& label) {
        for (const Choice& entry : CHOICES) {
            if (label == entry.label) {
                Config::data()["themeLayout"] = entry.key;
                Config::save();
                return;
            }
        }
    }

    std::vector<std::string> choices() {
        std::vector<std::string> labels;
        for (const Choice& entry : CHOICES) {
            labels.push_back(entry.label);
        }
        return labels;
    }
}
