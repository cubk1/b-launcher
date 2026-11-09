#include "ui/imgui/screens/theme/ContrastGuard.h"

#include "ui/imgui/theme/ColorMath.h"

#include <algorithm>

namespace ContrastGuard {

    namespace {
        const UiTheme::Token TEXTS[] = {UiTheme::TEXT_PRIMARY, UiTheme::TEXT_SECONDARY, UiTheme::TEXT_MUTED,
                                        UiTheme::ACCENT_TEXT,  UiTheme::DANGER,         UiTheme::SUCCESS,
                                        UiTheme::WARNING_INK};
        const UiTheme::Token SURFACES[] = {UiTheme::SIDEBAR, UiTheme::PANEL, UiTheme::SURFACE, UiTheme::MENU};

        bool excluded(UiTheme::Token token, const std::vector<std::string>& exclude) {
            const char* name = UiTheme::tokenName(token);
            return std::any_of(exclude.begin(), exclude.end(), [&](const std::string& item) { return item == name; });
        }

        uint32_t worst(uint32_t text, const std::vector<uint32_t>& grounds) {
            uint32_t chosen = grounds.front();
            float lowest = ColorMath::contrast(text, chosen);
            for (uint32_t ground : grounds) {
                float value = ColorMath::contrast(text, ground);
                if (value < lowest) {
                    lowest = value;
                    chosen = ground;
                }
            }
            return chosen;
        }
    }

    void apply(UiTheme::Palette& palette, float minimum, const std::vector<std::string>& exclude) {
        uint32_t backdrop = palette.light ? 0xFFFFFFFFu : 0xFF000000u;
        uint32_t shell = ColorMath::over(palette.tokens[UiTheme::SHELL], backdrop);
        std::vector<uint32_t> grounds;
        for (UiTheme::Token surface : SURFACES) {
            grounds.push_back(ColorMath::over(palette.tokens[surface], shell));
        }
        for (UiTheme::Token text : TEXTS) {
            if (excluded(text, exclude)) {
                continue;
            }
            uint32_t& value = palette.tokens[text];
            for (int round = 0; round < 3; round++) {
                uint32_t ground = worst(value, grounds);
                if (ColorMath::contrast(value, ground) >= minimum) {
                    break;
                }
                value = ColorMath::ensureContrast(value, ground, minimum);
            }
        }
        if (!excluded(UiTheme::ON_ACCENT, exclude)) {
            uint32_t accent = ColorMath::over(palette.tokens[UiTheme::ACCENT], shell);
            palette.tokens[UiTheme::ON_ACCENT] =
                    ColorMath::ensureContrast(palette.tokens[UiTheme::ON_ACCENT], accent, minimum);
        }
        if (!excluded(UiTheme::TOAST_TEXT, exclude)) {
            uint32_t fill = ColorMath::over(palette.tokens[UiTheme::TOAST_FILL], shell);
            palette.tokens[UiTheme::TOAST_TEXT] =
                    ColorMath::ensureContrast(palette.tokens[UiTheme::TOAST_TEXT], fill, minimum);
        }
    }
}
