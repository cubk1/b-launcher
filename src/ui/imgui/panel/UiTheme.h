#pragma once

#include <cstdint>
#include <string_view>

namespace UiTheme {

    enum Token {
        SHELL,
        SIDEBAR,
        PANEL,
        SURFACE,
        SURFACE_RAISED,
        SURFACE_SUNKEN,
        MENU,
        TOOLBAR,
        TEXT_PRIMARY,
        TEXT_SECONDARY,
        TEXT_MUTED,
        TEXT_FAINT,
        OUTLINE,
        OUTLINE_SOFT,
        PANEL_OUTLINE,
        GROUP_OUTLINE,
        DIVIDER,
        OVERLAY,
        ON_ACCENT,
        KNOB,
        KNOB_SHADOW,
        KNOB_OFF,
        PICKER_RING,
        BRAND_SHADOW,
        ROW_HOVER,
        NAV_HOVER,
        HOVER,
        HOVER_STRONG,
        ACCENT,
        ACCENT_SOFT,
        ACCENT_TEXT,
        ACCENT_HOVER,
        ACCENT2,
        DANGER,
        DANGER_SOFT,
        SUCCESS,
        WARNING,
        WARNING_INK,
        CAUTION,
        SHADOW,
        DOC_SURFACE,
        SELECTION,
        LOG_SELECTION,
        SCROLL_TRACK,
        SCROLL_THUMB,
        SCRIM,
        MODAL_SCRIM,
        TOAST_FILL,
        TOAST_TEXT,
        TOAST_INFO,
        TOAST_DONE,
        TOAST_FAILED,
        TITLE_TEXT,
        TITLE_MUTED,
        TITLE_ACTIVE,
        PRIMARY_TEXT,
        BUTTON_OUTLINE,
        SHELL_OUTLINE,
        TOKEN_COUNT
    };

    enum Scalar {
        OVERLAY_GAIN,
        GLASS_FILL,
        VEIL,
        LOGO_FAINT_IMAGE,
        LOGO_FAINT_PLAIN,
        SIDEBAR_SHADE,
        PANEL_SHADE,
        SCALAR_COUNT
    };

    struct Palette {
        uint32_t tokens[TOKEN_COUNT] = {};
        float scalars[SCALAR_COUNT] = {};
        bool light = false;
    };

    void apply(const Palette& palette);
    const Palette& palette();
    unsigned generation();
    uint32_t token(Token id);
    float scalar(Scalar id);
    const char* tokenName(Token id);
    const char* scalarName(Scalar id);
    bool tokenOf(std::string_view name, Token& out);
    bool scalarOf(std::string_view name, Scalar& out);

    bool isLight();
    uint32_t shell();
    uint32_t sidebar();
    uint32_t panel();
    uint32_t surface();
    uint32_t surfaceRaised();
    uint32_t surfaceSunken();
    uint32_t menu();
    uint32_t toolbar();
    uint32_t tokenOr(Token id, uint32_t fallback);
    uint32_t border(uint32_t color);
    uint32_t rowDivider(uint32_t color);
    uint32_t textPrimary();
    uint32_t textSecondary();
    uint32_t textMuted();
    uint32_t textFaint();
    uint32_t outline();
    uint32_t outlineSoft();
    uint32_t panelOutline();
    uint32_t groupOutline();
    uint32_t divider();
    uint32_t overlay(int alpha);
    uint32_t onAccent();
    uint32_t inkOn(uint32_t argb);
    uint32_t knob();
    uint32_t knobShadow();
    uint32_t knobOff();
    uint32_t pickerRing();
    uint32_t brandShadow();
    uint32_t clear();
    uint32_t rowHover();
    uint32_t navHover();
    uint32_t hover();
    uint32_t hoverStrong();
    uint32_t accent();
    uint32_t accentSoft(int alpha);
    uint32_t accentText();
    uint32_t accentHover();
    uint32_t accent2();
    uint32_t danger();
    uint32_t dangerSoft();
    uint32_t success();
    uint32_t warning();
    uint32_t warningInk();
    uint32_t caution();
    uint32_t shadow();
    uint32_t docSurface();
    uint32_t selection();
    uint32_t logSelection();
    uint32_t scrollTrack();
    uint32_t scrollThumb();
    uint32_t scrim();
    uint32_t modalScrim();
    uint32_t toastFill();
    uint32_t toastText();
}
