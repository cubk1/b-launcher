#pragma once

#include <cstdint>
#include <string>

namespace UiTheme {

    constexpr int MODE_DARK = 0;
    constexpr int MODE_LIGHT = 1;
    constexpr int MODE_TRANSPARENT = 2;
    constexpr int MODE_PLAIN = 3;
    constexpr int MODE_SHADCN = 4;
    constexpr int MODE_SHADCN_LIGHT = 5;

    extern int mode;
    extern uint32_t accent_;

    void setMode(int value);
    void setMode(const std::string& name);
    std::string modeName();
    void setAccent(uint32_t argb);
    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent);
    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent, uint32_t plain);
    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent, uint32_t plain, uint32_t shadcn);
    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent, uint32_t plain, uint32_t shadcn,
                  uint32_t shadcnLight);
    bool isLight();
    uint32_t shell();
    uint32_t sidebar();
    uint32_t panel();
    uint32_t surface();
    uint32_t surfaceRaised();
    uint32_t surfaceSunken();
    uint32_t menu();
    uint32_t toolbar();
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
    uint32_t danger();
    uint32_t dangerSoft();
    uint32_t shadow();
    uint32_t docSurface();
    uint32_t selection();
    uint32_t scrollTrack();
    uint32_t scrollThumb();
}
