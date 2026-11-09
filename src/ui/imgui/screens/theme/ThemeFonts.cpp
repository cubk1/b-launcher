#include "ui/imgui/screens/theme/ThemeFonts.h"

#include "app/system/Config.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/theme/ThemePaintSpec.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>

namespace ThemeFonts {

    namespace {
        ThemePaintSpec::FontSpec last[ThemePaintSpec::FONT_ROLES];
        float lastScale = -1.0f;
        bool synced = false;
    }

    float userScale() {
        const nlohmann::json& doc = Config::data();
        auto it = doc.find("themeFontScale");
        float scale = it != doc.end() && it->is_number() ? it->get<float>() : 1.0f;
        return std::clamp(scale, 0.9f, 1.15f);
    }

    void setUserScale(float scale) {
        Config::data()["themeFontScale"] = std::clamp(scale, 0.9f, 1.15f);
        Config::save();
    }

    void sync() {
        const ThemePaintSpec::Spec& spec = ThemePaintSpec::current();
        float scale = userScale();
        if (!synced || scale != lastScale || !std::equal(std::begin(last), std::end(last), std::begin(spec.fonts))) {
            synced = true;
            lastScale = scale;
            std::copy(std::begin(spec.fonts), std::end(spec.fonts), std::begin(last));
            UiFont::FontSet set;
            set.scale = scale;
            std::map<std::string, std::shared_ptr<const std::string>> files;
            for (int role = 0; role < ThemePaintSpec::FONT_ROLES; role++) {
                const ThemePaintSpec::FontSpec& font = spec.fonts[role];
                UiFont::FontChoice& choice = set.roles[role];
                if (!font.source.empty()) {
                    auto cached = files.find(font.source);
                    if (cached == files.end()) {
                        std::string owner;
                        std::string path;
                        std::shared_ptr<const std::string> data;
                        if (ThemePaintSpec::splitSource(font.source, owner, path)) {
                            if (std::optional<std::string> bytes = ThemeStore::read(owner, path)) {
                                data = std::make_shared<const std::string>(std::move(*bytes));
                            }
                        }
                        cached = files.emplace(font.source, data).first;
                    }
                    choice.data = cached->second;
                    choice.face = font.face;
                    choice.weight = font.weight;
                    choice.key = "theme:" + font.source + "#" + std::to_string(font.face) + "@" +
                            std::to_string(font.weight);
                    if (choice.data || font.system.empty()) {
                        continue;
                    }
                    choice = UiFont::FontChoice();
                }
                for (const std::string& name : font.system) {
                    std::string found = UiFont::systemFontSpec(name);
                    if (!found.empty()) {
                        choice.spec = found;
                        choice.weight = font.weight;
                        choice.key = "system:" + found + "@" + std::to_string(font.weight);
                        break;
                    }
                }
            }
            UiFont::request(set);
        }
        UiFont::applyPending();
    }
}
