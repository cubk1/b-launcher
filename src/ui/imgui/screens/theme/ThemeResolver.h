#pragma once

#include "app/system/SystemColors.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/shell/ShellLayout.h"
#include "ui/imgui/screens/theme/Swatches.h"
#include "ui/imgui/screens/theme/ThemeScript.h"
#include "ui/imgui/theme/ThemePaintSpec.h"
#include "ui/imgui/theme/ThemeStyle.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ThemeInputs {
    uint32_t accent = 0xFF4F7DFFu;
    SystemColors::State system;
    Swatches image;
    nlohmann::json settings = nlohmann::json::object();
    double time = 0.0;
};

struct ThemeFacts {
    std::string author;
    std::string base;
    bool script = false;
    bool shader = false;
    bool animated = false;
};

struct ThemeSetting {
    std::string id;
    std::string type;
    std::string label;
    nlohmann::json fallback;
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;
    std::vector<std::string> options;
};

class ThemeResolver {
public:
    bool load(const std::string& id, bool systemDark);
    UiTheme::Palette resolve(const ThemeInputs& inputs);
    const std::string& id() const;
    bool followsSystem() const;
    const std::vector<ThemeSetting>& settings() const;
    nlohmann::json settingValues(const nlohmann::json& stored) const;
    const std::vector<std::string>& problems() const;
    const std::vector<std::string>& loadIssues() const;
    const std::map<std::string, uint32_t>& variables() const;
    const ThemeStyle::Style& style() const;
    const ShellLayout::Spec& layout() const;
    const ThemePaintSpec::Spec& paints() const;
    double scriptRate() const;
    ThemeFacts facts() const;
    bool accentPinned() const;
    std::string accentPicker() const;
    bool hasPress() const;
    std::optional<nlohmann::json> press(const nlohmann::json& event, std::string& error);

private:
    void problem(const std::string& text);
    void applyColors(UiTheme::Palette& palette, const nlohmann::json& colors);
    bool reference(const std::string& name, const UiTheme::Palette& palette, uint32_t& result) const;
    bool colorOf(const nlohmann::json& value, const UiTheme::Palette& palette, uint32_t& out, std::string& error) const;
    bool readLayer(const nlohmann::json& node, const UiTheme::Palette& palette, const std::string& where,
                   ThemePaintSpec::Layer& layer);
    void readPaints(const UiTheme::Palette& palette, const nlohmann::json& tweaks);
    void readDecorations(bool light, const nlohmann::json& tweaks);
    void readFonts();
    void readSounds();
    void applyOutput(UiTheme::Palette& palette, const nlohmann::json& output, const std::string& source);
    nlohmann::json environment(const UiTheme::Palette& palette, const nlohmann::json& options) const;
    std::optional<nlohmann::json> run(ThemeScript& script, const std::string& source, const UiTheme::Palette& palette,
                                      const nlohmann::json& options);

    struct LoadedScript {
        std::unique_ptr<ThemeScript> runner;
        std::string error;
    };

    std::string themeId;
    nlohmann::json merged;
    std::vector<ThemeSetting> settingList;
    std::vector<std::string> loadProblems;
    std::vector<std::string> problemList;
    std::map<std::string, uint32_t> vars;
    std::map<std::string, LoadedScript> scripts;
    ThemeScript* pressScript = nullptr;
    const ThemeInputs* current = nullptr;
    ThemeStyle::Style styleResult;
    ShellLayout::Spec layoutResult;
    ThemePaintSpec::Spec paintResult;
};
