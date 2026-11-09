#include "UiTheme.h"
#include "UiRender.h"
#include "UiCommon.h"
#include "../theme/ThemeStyle.h"

#include <algorithm>
#include <cmath>

namespace UiTheme {

    namespace {
        Palette current;
        unsigned revision = 0;

        const char* const TOKEN_NAMES[TOKEN_COUNT] = {
                "shell",        "sidebar",     "panel",         "surface",       "surfaceRaised", "surfaceSunken",
                "menu",         "toolbar",     "textPrimary",   "textSecondary", "textMuted",     "textFaint",
                "outline",      "outlineSoft", "panelOutline",  "groupOutline",  "divider",       "overlay",
                "onAccent",     "knob",        "knobShadow",    "knobOff",       "pickerRing",    "brandShadow",
                "rowHover",     "navHover",    "hover",         "hoverStrong",   "accent",        "accentSoft",
                "accentText",   "accentHover", "accent2",       "danger",        "dangerSoft",    "success",
                "warning",      "warningInk",  "caution",       "shadow",        "docSurface",    "selection",
                "logSelection", "scrollTrack", "scrollThumb",   "scrim",         "modalScrim",    "toastFill",
                "toastText",    "toastInfo",   "toastDone",     "toastFailed",   "titleText",     "titleMuted",
                "titleActive",  "primaryText", "buttonOutline", "shellOutline",
        };

        const char* const SCALAR_NAMES[SCALAR_COUNT] = {
                "overlayGain", "glassFill", "veil", "logoFaintImage", "logoFaintPlain", "sidebarShade", "panelShade",
        };
    }

    using UiRender::color;

    void apply(const Palette& palette) {
        current = palette;
        revision++;
    }

    const Palette& palette() {
        return current;
    }

    unsigned generation() {
        return revision;
    }

    uint32_t token(Token id) {
        return current.tokens[id];
    }

    float scalar(Scalar id) {
        return current.scalars[id];
    }

    const char* tokenName(Token id) {
        return TOKEN_NAMES[id];
    }

    const char* scalarName(Scalar id) {
        return SCALAR_NAMES[id];
    }

    bool tokenOf(std::string_view name, Token& out) {
        for (int i = 0; i < TOKEN_COUNT; i++) {
            if (name == TOKEN_NAMES[i]) {
                out = (Token) i;
                return true;
            }
        }
        return false;
    }

    bool scalarOf(std::string_view name, Scalar& out) {
        for (int i = 0; i < SCALAR_COUNT; i++) {
            if (name == SCALAR_NAMES[i]) {
                out = (Scalar) i;
                return true;
            }
        }
        return false;
    }

    bool isLight() {
        return current.light;
    }

    uint32_t shell() {
        return token(SHELL);
    }

    uint32_t sidebar() {
        return token(SIDEBAR);
    }

    uint32_t panel() {
        return token(PANEL);
    }

    uint32_t surface() {
        return token(SURFACE);
    }

    uint32_t surfaceRaised() {
        return token(SURFACE_RAISED);
    }

    uint32_t surfaceSunken() {
        return token(SURFACE_SUNKEN);
    }

    uint32_t menu() {
        return token(MENU);
    }

    uint32_t toolbar() {
        return token(TOOLBAR);
    }

    uint32_t tokenOr(Token id, uint32_t fallback) {
        uint32_t value = token(id);
        return (value >> 24) != 0 ? value : fallback;
    }

    uint32_t border(uint32_t color) {
        return ThemeStyle::outlines() ? color : color & 0x00FFFFFFu;
    }

    uint32_t rowDivider(uint32_t color) {
        return ThemeStyle::rowDividers() ? color : color & 0x00FFFFFFu;
    }

    uint32_t textPrimary() {
        return token(TEXT_PRIMARY);
    }

    uint32_t textSecondary() {
        return token(TEXT_SECONDARY);
    }

    uint32_t textMuted() {
        return token(TEXT_MUTED);
    }

    uint32_t textFaint() {
        return token(TEXT_FAINT);
    }

    uint32_t outline() {
        return token(OUTLINE);
    }

    uint32_t outlineSoft() {
        return token(OUTLINE_SOFT);
    }

    uint32_t panelOutline() {
        return token(PANEL_OUTLINE);
    }

    uint32_t groupOutline() {
        return token(GROUP_OUTLINE);
    }

    uint32_t divider() {
        return token(DIVIDER);
    }

    uint32_t overlay(int alpha) {
        float gain = scalar(OVERLAY_GAIN);
        int a = gain == 1.0f ? alpha : std::min(255, Ui::round(alpha * gain));
        return ((uint32_t) a << 24) | (token(OVERLAY) & 0x00FFFFFFu);
    }

    uint32_t inkOn(uint32_t argb) {
        auto linear = [](uint32_t channel) {
            float value = (float) channel / 255.0f;
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };
        float luminance = 0.2126f * linear((argb >> 16) & 0xFF) + 0.7152f * linear((argb >> 8) & 0xFF) +
                0.0722f * linear(argb & 0xFF);
        return luminance > 0.34f ? color(22, 24, 29) : color(255, 255, 255);
    }

    uint32_t onAccent() {
        return token(ON_ACCENT);
    }

    uint32_t knob() {
        return token(KNOB);
    }

    uint32_t knobShadow() {
        return token(KNOB_SHADOW);
    }

    uint32_t knobOff() {
        return token(KNOB_OFF);
    }

    uint32_t pickerRing() {
        return token(PICKER_RING);
    }

    uint32_t brandShadow() {
        return token(BRAND_SHADOW);
    }

    uint32_t clear() {
        return color(0, 0, 0, 0);
    }

    uint32_t rowHover() {
        return token(ROW_HOVER);
    }

    uint32_t navHover() {
        return token(NAV_HOVER);
    }

    uint32_t hover() {
        return token(HOVER);
    }

    uint32_t hoverStrong() {
        return token(HOVER_STRONG);
    }

    uint32_t accent() {
        return token(ACCENT);
    }

    uint32_t accentSoft(int alpha) {
        return (token(ACCENT_SOFT) & 0x00FFFFFFu) | ((uint32_t) std::max(0, std::min(255, alpha)) << 24);
    }

    uint32_t accentText() {
        return token(ACCENT_TEXT);
    }

    uint32_t accentHover() {
        return token(ACCENT_HOVER);
    }

    uint32_t accent2() {
        return token(ACCENT2);
    }

    uint32_t danger() {
        return token(DANGER);
    }

    uint32_t dangerSoft() {
        return token(DANGER_SOFT);
    }

    uint32_t success() {
        return token(SUCCESS);
    }

    uint32_t warning() {
        return token(WARNING);
    }

    uint32_t warningInk() {
        return token(WARNING_INK);
    }

    uint32_t caution() {
        return token(CAUTION);
    }

    uint32_t shadow() {
        return token(SHADOW);
    }

    uint32_t docSurface() {
        return token(DOC_SURFACE);
    }

    uint32_t selection() {
        return token(SELECTION);
    }

    uint32_t logSelection() {
        return token(LOG_SELECTION);
    }

    uint32_t scrollTrack() {
        return token(SCROLL_TRACK);
    }

    uint32_t scrollThumb() {
        return token(SCROLL_THUMB);
    }

    uint32_t scrim() {
        return token(SCRIM);
    }

    uint32_t modalScrim() {
        return token(MODAL_SCRIM);
    }

    uint32_t toastFill() {
        return token(TOAST_FILL);
    }

    uint32_t toastText() {
        return token(TOAST_TEXT);
    }
}
