#include "NavItemWidget.h"
#include "../panel/UiSound.h"
#include "../panel/NewUi.h"
#include "../panel/UiFont.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"
#include "../theme/ThemeStyle.h"
#include "../screens/theme/ThemeEffects.h"
#include "../screens/theme/ThemePaint.h"

#include <algorithm>

NavItemWidget::NavItemWidget(const std::string& id, const std::string& glyph, const std::string& label)
    : Widget(id),
      glyph(glyph),
      texture(0),
      textureSize(16.0f),
      label(label),
      active(false),
      child(false),
      pressed(false),
      fold(0.0f),
      openWidth(0.0f),
      trail(0.0f) {}

NavItemWidget& NavItemWidget::selected(bool value) {
    active = value;
    return *this;
}

NavItemWidget& NavItemWidget::nested(bool value) {
    child = value;
    return *this;
}

NavItemWidget& NavItemWidget::note(const std::string& text) {
    aside = text;
    return *this;
}

NavItemWidget& NavItemWidget::folded(float amount, float fullWidth) {
    fold = amount;
    openWidth = fullWidth;
    return *this;
}

NavItemWidget& NavItemWidget::reserve(float right) {
    trail = right;
    return *this;
}

NavItemWidget& NavItemWidget::image(uint32_t value, float size) {
    texture = value;
    textureSize = size;
    return *this;
}

void NavItemWidget::paint() {
    float indent = child ? 14.0f : 0.0f;
    hitArea(x + indent, y, w - indent, h);
    bool isHovered = hovered();
    pressed = clicked();
    if (pressed) {
        UiSound::request(UiSound::TICK);
    }
    float r = UiState::motion(id + "#r", active ? 1.0f : (isHovered ? 0.48f : 0.0f), 16.0f);
    ThemePaint::selectedFill(ThemePaintSpec::NAV_ACTIVE, id, x + indent, y, w - indent, h, 8.0f, active, isHovered, r);
    ThemeEffects::button(id, "nav", x + indent, y, w - indent, h, 8.0f, 0xF, UiTheme::textPrimary(), isHovered);
    bool bare = !ThemeStyle::icons() && fold < 0.5f;
    float labelX = bare ? 12.0f : 31.0f;
    if (texture != 0) {
        UiRender::texture(texture, x + indent + 8, y + (h - textureSize) * 0.5f, textureSize, textureSize);
        labelX = 31.0f;
    } else if (!bare) {
        UiWidgets::iconBeside(glyph, x + indent + 16, label, UiFont::MEDIUM, UiWidgets::TEXT_BODY, y, h,
                              UiRender::mix(UiTheme::textMuted(), UiTheme::accent(), r), UiWidgets::TEXT_ICON);
    }
    float labelW = fold > 0.0f ? std::max(w, openWidth) : w;
    float room = labelW - indent - labelX - 10 - trail;
    std::string shown = NewUi::fit(label, room);
    float savedAlpha = UiRender::alphaMul;
    UiRender::alphaMul = savedAlpha * std::max(0.0f, 1.0f - fold * 2.0f);
    UiRender::textY(shown, x + indent + labelX, y, h, UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), r),
                    UiFont::MEDIUM, UiWidgets::TEXT_BODY);
    bool cut = shown != label;
    if (!aside.empty() && !cut) {
        float noteX = x + indent + labelX + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        float parens = UiFont::width("\xEF\xBC\x88\xEF\xBC\x89", UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        float left = x + labelW - 10 - trail - noteX - parens;
        if (left > 12.0f) {
            std::string inner = NewUi::fit(aside, left);
            cut = inner != aside;
            uint32_t dim = UiRender::mix(UiTheme::textFaint(), UiTheme::textMuted(), r);
            UiRender::textY("\xEF\xBC\x88" + inner + "\xEF\xBC\x89", noteX, y, h, dim, UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
        } else {
            cut = true;
        }
    }
    UiRender::alphaMul = savedAlpha;
    if (isHovered && fold > 0.5f) {
        NewUi::tooltipText = aside.empty() ? label : label + "\xEF\xBC\x88" + aside + "\xEF\xBC\x89";
        NewUi::tooltipX = x + w + 8;
        NewUi::tooltipY = y + 3;
    } else if (isHovered && cut) {
        NewUi::tooltipText = aside.empty() ? label : label + "\xEF\xBC\x88" + aside + "\xEF\xBC\x89";
        NewUi::tooltipX = x + indent + 22;
        NewUi::tooltipY = y + h + 4;
    }
}
