#pragma once

#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/shell/ShellLayout.h"
#include "ui/imgui/screens/theme/ThemeResolver.h"
#include "ui/imgui/theme/ThemePaintSpec.h"
#include "ui/imgui/theme/ThemeStyle.h"

#include <string>
#include <vector>

namespace ThemePreview {

    struct Data {
        bool ok = false;
        UiTheme::Palette palette;
        ThemeStyle::Style style;
        ShellLayout::Spec layout;
        ThemePaintSpec::Spec paints;
        ThemeFacts facts;
        std::vector<std::string> problems;
    };

    const Data& of(const std::string& id);
    void forget();

    class Scope {
    public:
        explicit Scope(const std::string& id);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        const ShellLayout::Spec& layout() const;

    private:
        bool swapped = false;
        UiTheme::Palette savedPalette;
        ThemeStyle::Style savedStyle;
        ShellLayout::Spec shown;
    };
}
