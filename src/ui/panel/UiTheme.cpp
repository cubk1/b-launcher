#include "UiTheme.h"
#include "UiRender.h"
#include "UiCommon.h"

#include <algorithm>

namespace UiTheme {

    int mode = 0;
    uint32_t accent_ = 0xFF4F7DFFu;

    using UiRender::color;

    void setMode(int value) {
        mode = value == MODE_LIGHT || value == MODE_TRANSPARENT || value == MODE_PLAIN || value == MODE_SHADCN ||
                        value == MODE_SHADCN_LIGHT
                ? value
                : MODE_DARK;
    }

    void setMode(const std::string& name) {
        setMode(name == "Light"
                        ? MODE_LIGHT
                        : (name == "Transparent"
                                   ? MODE_TRANSPARENT
                                   : (name == "Plain" ? MODE_PLAIN
                                                      : (name == "Shadcn" ? MODE_SHADCN
                                                                          : (name == "ShadcnLight" ? MODE_SHADCN_LIGHT
                                                                                                   : MODE_DARK)))));
    }

    std::string modeName() {
        return mode == MODE_LIGHT
                ? "Light"
                : (mode == MODE_TRANSPARENT
                           ? "Transparent"
                           : (mode == MODE_PLAIN
                                      ? "Plain"
                                      : (mode == MODE_SHADCN ? "Shadcn"
                                                             : (mode == MODE_SHADCN_LIGHT ? "ShadcnLight" : "Dark"))));
    }

    void setAccent(uint32_t argb) {
        accent_ = argb;
    }

    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent) {
        return pick(dark, light, transparent, dark);
    }

    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent, uint32_t plain) {
        return pick(dark, light, transparent, plain, plain);
    }

    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent, uint32_t plain, uint32_t shadcn) {
        return pick(dark, light, transparent, plain, shadcn, light);
    }

    uint32_t pick(uint32_t dark, uint32_t light, uint32_t transparent, uint32_t plain, uint32_t shadcn,
                  uint32_t shadcnLight) {
        switch (mode) {
            case MODE_LIGHT: return light;
            case MODE_TRANSPARENT: return transparent;
            case MODE_PLAIN: return plain;
            case MODE_SHADCN: return shadcn;
            case MODE_SHADCN_LIGHT: return shadcnLight;
            default: return dark;
        }
    }

    bool isLight() {
        return mode == MODE_LIGHT || mode == MODE_SHADCN_LIGHT;
    }

    uint32_t shell() {
        return pick(color(18, 21, 30, 232), color(223, 223, 223, 244), color(14, 16, 24, 150), color(20, 20, 20),
                    color(10, 10, 10), color(255, 255, 255));
    }

    uint32_t sidebar() {
        return pick(color(20, 22, 28, 216), color(190, 190, 190, 236), color(18, 20, 27, 135), color(23, 23, 23),
                    color(23, 23, 23), color(250, 250, 250));
    }

    uint32_t panel() {
        return pick(color(13, 15, 20, 228), color(223, 223, 223, 245), color(12, 14, 20, 165), color(17, 17, 17),
                    color(10, 10, 10), color(255, 255, 255));
    }

    uint32_t surface() {
        return pick(color(17, 19, 27, 224), color(229, 229, 229, 248), color(12, 14, 21, 140), color(22, 22, 22),
                    color(23, 23, 23), color(255, 255, 255));
    }

    uint32_t surfaceRaised() {
        return pick(color(25, 25, 33), color(210, 210, 210), color(24, 26, 35, 170), color(33, 33, 33),
                    color(38, 38, 38), color(245, 245, 245));
    }

    uint32_t surfaceSunken() {
        return pick(color(26, 27, 35), color(214, 214, 214), color(18, 20, 28, 165), color(29, 29, 29),
                    color(33, 33, 33), color(240, 240, 240));
    }

    uint32_t menu() {
        return pick(color(18, 19, 28, 228), color(233, 233, 233, 248), color(14, 16, 24, 178), color(26, 26, 26),
                    color(23, 23, 23), color(255, 255, 255));
    }

    uint32_t toolbar() {
        return pick(color(11, 13, 20, 150), color(223, 223, 223, 220), color(10, 12, 19, 120), color(21, 21, 21, 210),
                    color(10, 10, 10, 220), color(255, 255, 255, 220));
    }

    uint32_t textPrimary() {
        return pick(color(226, 229, 237), color(40, 40, 40), color(232, 235, 242), color(222, 222, 222),
                    color(250, 250, 250), color(10, 10, 10));
    }

    uint32_t textSecondary() {
        return pick(color(216, 219, 228), color(55, 55, 55), color(220, 223, 232), color(200, 200, 200),
                    color(229, 229, 229), color(38, 38, 38));
    }

    uint32_t textMuted() {
        return pick(color(150, 154, 166), color(100, 100, 100), color(158, 162, 174), color(140, 140, 140),
                    color(161, 161, 161), color(115, 115, 115));
    }

    uint32_t textFaint() {
        return pick(color(110, 114, 128), color(135, 135, 135), color(120, 124, 138), color(102, 102, 102),
                    color(115, 115, 115), color(163, 163, 163));
    }

    uint32_t outline() {
        return pick(color(35, 37, 46, 255), color(205, 205, 205), color(60, 64, 78, 140), color(40, 40, 40),
                    color(40, 40, 40), color(229, 229, 229));
    }

    uint32_t outlineSoft() {
        return pick(color(28, 31, 41), color(214, 214, 214), color(52, 56, 70, 120), color(34, 34, 34),
                    color(34, 34, 34), color(235, 235, 235));
    }

    uint32_t panelOutline() {
        return pick(0xFF1F1F24u, color(204, 204, 204), 0xFF1F1F24u, color(34, 34, 34), color(40, 40, 40),
                    color(229, 229, 229));
    }

    uint32_t groupOutline() {
        return pick(0xFF202127u, color(206, 206, 206), 0xFF202127u, color(37, 37, 37), color(40, 40, 40),
                    color(229, 229, 229));
    }

    uint32_t divider() {
        return pick(color(255, 255, 255, 16), color(0, 0, 0, 26), color(255, 255, 255, 22), color(255, 255, 255, 12),
                    color(255, 255, 255, 26), color(0, 0, 0, 20));
    }

    uint32_t overlay(int alpha) {
        return isLight() ? color(0, 0, 0, std::min(255, Ui::round(alpha * 1.15f))) : color(255, 255, 255, alpha);
    }

    uint32_t onAccent() {
        return color(255, 255, 255);
    }

    uint32_t knob() {
        return pick(color(240, 242, 248), color(255, 255, 255), color(240, 242, 248), color(230, 230, 230),
                    color(250, 250, 250), color(255, 255, 255));
    }

    uint32_t knobShadow() {
        return isLight() ? color(0, 0, 0, 46) : color(0, 0, 0, 0);
    }

    uint32_t knobOff() {
        return pick(color(150, 154, 166), color(255, 255, 255), color(158, 162, 174), color(140, 140, 140),
                    color(229, 229, 229), color(255, 255, 255));
    }

    uint32_t pickerRing() {
        return pick(color(240, 242, 248), color(32, 35, 44), color(240, 242, 248), color(232, 232, 232),
                    color(250, 250, 250), color(10, 10, 10));
    }

    uint32_t brandShadow() {
        return mode == MODE_LIGHT || mode == MODE_PLAIN || mode == MODE_SHADCN || mode == MODE_SHADCN_LIGHT
                ? color(0, 0, 0, 0)
                : color(3, 145, 212);
    }

    uint32_t clear() {
        return color(0, 0, 0, 0);
    }

    uint32_t rowHover() {
        return pick(color(33, 33, 44), color(216, 216, 216), color(34, 37, 48, 180), color(37, 37, 37),
                    color(38, 38, 38), color(245, 245, 245));
    }

    uint32_t navHover() {
        return pick(color(255, 255, 255, 22), color(0, 0, 0, 26), color(255, 255, 255, 30), color(255, 255, 255, 16),
                    color(255, 255, 255, 20), color(0, 0, 0, 12));
    }

    uint32_t hover() {
        return pick(color(255, 255, 255, 14), color(0, 0, 0, 16), color(255, 255, 255, 20), color(255, 255, 255, 10),
                    color(255, 255, 255, 13), color(0, 0, 0, 10));
    }

    uint32_t hoverStrong() {
        return pick(color(255, 255, 255, 20), color(0, 0, 0, 24), color(255, 255, 255, 28), color(255, 255, 255, 16),
                    color(255, 255, 255, 20), color(0, 0, 0, 16));
    }

    uint32_t accent() {
        return accent_;
    }

    uint32_t accentSoft(int alpha) {
        int r = (accent_ >> 16) & 0xFF;
        int g = (accent_ >> 8) & 0xFF;
        int b = accent_ & 0xFF;
        float tone = mode == MODE_LIGHT
                ? 0.18f
                : (mode == MODE_SHADCN_LIGHT ? 0.14f : (mode == MODE_PLAIN || mode == MODE_SHADCN ? 0.2f : 0.28f));
        int base = isLight() ? 255 : (mode == MODE_PLAIN ? 30 : (mode == MODE_SHADCN ? 23 : 0));
        int sr = Ui::round(base + (r - base) * tone);
        int sg = Ui::round(base + (g - base) * tone);
        int sb = Ui::round(base + (b - base) * tone);
        return color(sr, sg, sb, std::max(0, std::min(255, alpha)));
    }

    uint32_t accentText() {
        int r = (accent_ >> 16) & 0xFF;
        int g = (accent_ >> 8) & 0xFF;
        int b = accent_ & 0xFF;
        if (mode == MODE_PLAIN || mode == MODE_SHADCN) {
            return color(std::min(255, r + 30), std::min(255, g + 30), std::min(255, b + 30));
        }
        if (!isLight()) {
            return color(std::min(255, r + 45), std::min(255, g + 45), std::min(255, b + 45));
        }
        return color(Ui::round(r * 0.72f), Ui::round(g * 0.72f), Ui::round(b * 0.72f));
    }

    uint32_t danger() {
        return pick(color(232, 96, 96), color(206, 62, 62), color(238, 108, 108), color(214, 98, 98),
                    color(255, 100, 103), color(231, 0, 11));
    }

    uint32_t dangerSoft() {
        return pick(color(226, 84, 84), color(198, 56, 56), color(232, 96, 96), color(204, 86, 86), color(230, 80, 84),
                    color(239, 68, 68));
    }

    uint32_t shadow() {
        return pick(color(0, 0, 0, 75), color(0, 0, 0, 40), color(0, 0, 0, 60), color(0, 0, 0, 70), color(0, 0, 0, 90),
                    color(0, 0, 0, 28));
    }

    uint32_t docSurface() {
        return pick(color(17, 19, 27, 250), color(236, 236, 236, 252), color(14, 16, 24, 236), color(24, 24, 24),
                    color(23, 23, 23), color(255, 255, 255));
    }

    uint32_t selection() {
        return ((uint32_t) (isLight() ? 90 : 110) << 24) | (accent_ & 0xFFFFFFu);
    }

    uint32_t scrollTrack() {
        return pick(color(30, 30, 38, 160), color(0, 0, 0, 22), color(255, 255, 255, 18), color(28, 28, 28, 160),
                    color(23, 23, 23, 160), color(0, 0, 0, 10));
    }

    uint32_t scrollThumb() {
        return pick(color(49, 49, 58), color(178, 178, 178), color(255, 255, 255, 60), color(52, 52, 52),
                    color(64, 64, 64), color(212, 212, 212));
    }
}
