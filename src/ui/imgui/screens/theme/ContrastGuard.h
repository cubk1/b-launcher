#pragma once

#include "ui/imgui/panel/UiTheme.h"

#include <string>
#include <vector>

namespace ContrastGuard {

    void apply(UiTheme::Palette& palette, float minimum, const std::vector<std::string>& exclude);
}
