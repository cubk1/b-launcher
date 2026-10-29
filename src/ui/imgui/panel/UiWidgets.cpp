#include "UiWidgets.h"
#include "UiRender.h"
#include "UiTheme.h"
#include "UiFont.h"
#include "UiState.h"
#include "UiCommon.h"
#include "NewUi.h"
#include "../widget/InputWidget.h"

#include <algorithm>
#include <cmath>

namespace UiWidgets {

    void dots(float x, float y, uint32_t color) {
        for (int i = 0; i < 3; i++) {
            UiRender::circle(x + i * 3.5f, y, 1.3f, color);
        }
    }

    void icon(const std::string& glyph, float cx, float cy, uint32_t color, float size) {
        float w = UiFont::width(glyph, UiFont::ICON, size);
        UiRender::text(glyph, cx - w * 0.5f, cy - size * 0.5f, color, UiFont::ICON, size);
    }

    void iconBeside(const std::string& glyph, float cx, const std::string& label, int family, float labelSize, float y,
                    float h, uint32_t color, float size) {
        icon(glyph, cx, UiFont::centreY(label, family, labelSize, y + (h - labelSize) * 0.5f), color, size);
    }

    void chevron(float x, float y, uint32_t color) {
        icon(Ui::glyph(0xF054), x + 1.5f, y + 3.0f, color, 10.0f);
    }

    void check(float cx, float cy, uint32_t color) {
        check(cx, cy, color, 11.0f);
    }

    void check(float cx, float cy, uint32_t color, float size) {
        UiRender::checkMark(cx, cy, size * 0.8f, std::max(1.1f, size * 0.12f), color);
    }

    void knobShadow(float cx, float cy, float radius) {
        UiRender::shadow(cx - radius, cy - radius, radius * 2, radius * 2, radius, 4.0f, 1.0f, UiTheme::knobShadow());
    }

    void card(float x, float y, float w, float h, const std::string& title) {
        UiRender::text(title, x + 12, y - 18, UiTheme::textFaint(), UiFont::MEDIUM, TEXT_CAPTION);
        UiRender::roundRect(x, y, w, h, 14.0f, UiTheme::surface());
        UiRender::outline(x, y, w, h, 14.0f, UiTheme::outlineSoft());
    }

    void toggle(const std::string& id, float x, float y, bool value, bool enabled, bool hovered) {
        float on = UiState::motion(id + "#on", value ? 1.0f : 0.0f, 19.0f);
        float hover = UiState::motion(id + "#hover", enabled && hovered ? 1.0f : 0.0f, 20.0f);
        (void) hover;
        UiRender::roundRect(x, y, 29, 18, 9.0f, UiRender::mix(UiTheme::surfaceRaised(), UiTheme::accent(), on));
        knobShadow(x + UiRender::lerp(9.0f, 20.0f, on), y + 9, 6.5f);
        UiRender::circle(x + UiRender::lerp(9.0f, 20.0f, on), y + 9, 6.5f,
                         UiRender::mix(UiTheme::knobOff(), UiTheme::onAccent(), on));
    }

    void select(const std::string& id, float x, float y, float w, const std::string& value, bool hovered, bool open) {
        float response = UiState::motion(id + "#sel", open ? 1.0f : (hovered ? 0.65f : 0.0f), 18.0f);
        uint32_t lift = UiTheme::overlay(Ui::round(22.0f * response));
        uint32_t fillColor = UiRender::over(lift, UiTheme::surfaceRaised());
        UiRender::roundRect(x, y, w, 23, 5.0f, fillColor);
        UiRender::outline(x, y, w, 23, 5.0f,
                          UiRender::over(UiTheme::overlay(Ui::round(22.0f * response) + 10), UiTheme::surfaceRaised()),
                          0xF, 0.5f);
        UiRender::textClipped(value.empty() ? std::string("请选择") : value, x + 7, y, 23, w - 26,
                              UiTheme::textSecondary(), UiFont::MEDIUM, TEXT_CONTROL, fillColor);
        uint32_t arrowColor = UiRender::mix(UiTheme::textMuted(), UiTheme::textSecondary(), response);
        icon(Ui::glyph(0xF078), x + w - 11.5f, y + 11.5f, arrowColor, 10.0f);
    }

    void slider(const std::string& id, float x, float y, float amount, const std::string& valueText, float cardRight,
                bool hovered, bool active, bool editing) {
        slider(id, x, y, 72.0f, amount, valueText, cardRight, hovered, active, editing);
    }

    void slider(const std::string& id, float x, float y, float trackWidth, float amount, const std::string& valueText,
                float cardRight, bool hovered, bool active, bool editing) {
        float shown = UiState::motionFrom(id + "#slider", amount, 12.0f, amount);
        float hover = UiState::motion(id + "#sliderHover", active ? 1.0f : (hovered ? 0.7f : 0.0f), 18.0f);
        float grab = UiState::motion(id + "#sliderGrab", active ? 1.0f : 0.0f, 14.0f);
        UiRender::roundRect(x, y, trackWidth, 3, 1.5f,
                            UiRender::mix(UiTheme::outline(), UiTheme::outlineSoft(), hover));
        float fillWidth = trackWidth * shown;
        if (fillWidth > 0.1f) {
            UiRender::roundRectGradient(x, y, fillWidth, 3, 1.5f, UiTheme::accent(), UiTheme::accent());
        }
        float thumbX = x + fillWidth;
        float thumbY = y + 1.5f;
        knobShadow(thumbX, thumbY, 5.0f + hover * 0.6f + grab * 1.2f);
        UiRender::circle(thumbX, thumbY, 5.0f + hover * 0.6f + grab * 1.2f, UiTheme::knob());
        std::string text = valueText.empty() ? std::string("0") : valueText;
        float pillW = pillWidth(text);
        float pillX = cardRight - 13 - pillW;
        float pillY = y - 10;
        UiRender::roundRect(pillX, pillY, pillW, 21, 5.0f, editing ? UiTheme::outline() : UiTheme::surfaceRaised());
        if (editing) {
            UiRender::outline(pillX, pillY, pillW, 21, 5.0f, UiTheme::accent());
        }
        if (editing) {
            InputWidget(NewUi::INPUT_SLIDER)
                    .area(pillX + 6, pillW - 12, TEXT_PILL, 12)
                    .limit(10)
                    .center()
                    .tint(UiTheme::textMuted())
                    .bounds(pillX, pillY, pillW, 21)
                    .paint();
            return;
        }
        float textWidth = UiFont::width(text, UiFont::MEDIUM, TEXT_PILL);
        UiRender::textY(text, pillX + (pillW - textWidth) * 0.5f, pillY, 21, UiTheme::textMuted(), UiFont::MEDIUM,
                        TEXT_PILL);
    }

    float pillWidth(const std::string& text) {
        return std::max(43.0f,
                        UiFont::width(text.empty() ? std::string("0") : text, UiFont::MEDIUM, TEXT_PILL) + 14.0f);
    }

    void chevronRow(const std::string& id, float cardX, float rowY, float width, bool hovered, bool firstRow,
                    bool lastRow) {
        (void) id;
        (void) hovered;
        (void) firstRow;
        (void) lastRow;
        chevron(cardX + width - 24, rowY + 15, UiTheme::textSecondary());
    }

    void anchorGlyph(const std::string& anchor, float cx, float cy, uint32_t color) {
        float w = 7.0f;
        float t = 1.5f;
        float x = cx - w * 0.5f;
        float y = cy - 3.25f;
        if (anchor == "Left" || anchor == "Center" || anchor == "Right") {
            float inset = anchor == "Left" ? 0.0f : (anchor == "Center" ? 1.0f : 2.0f);
            UiRender::rect(x, y, w, t, color);
            UiRender::rect(x + inset, y + 2.5f, 5.0f, t, color);
            UiRender::rect(x, y + 5.0f, w, t, color);
            return;
        }
        if (anchor == "Free") {
            UiRender::rect(x, y + 2.5f, w, t, color);
            UiRender::rect(x + 2.75f, y, t, 6.5f, color);
            return;
        }
        bool bottom = Ui::endsWith(anchor, "Bottom");
        float barY = bottom ? y + 5.0f : y;
        float stemY = bottom ? y : y + 1.5f;
        float stemX = Ui::startsWith(anchor, "Left") ? x : x + w - t;
        UiRender::rect(x, barY, w, t, color);
        UiRender::rect(stemX, stemY, t, 5.0f, color);
    }

    void checkerBoard(float x, float y, float w, float h, float radius, float cell) {
        UiRender::roundRect(x, y, w, h, radius, UiTheme::textSecondary());
        int cols = (int) std::ceil(w / cell);
        int rows = (int) std::ceil(h / cell);
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                if ((row + col) % 2 == 0) {
                    continue;
                }
                float cx = x + col * cell;
                float cy = y + row * cell;
                float cw = std::min(cell, x + w - cx);
                float ch = std::min(cell, y + h - cy);
                int mask = 0;
                if (row == 0 && col == 0) {
                    mask |= 4;
                }
                if (row == 0 && col == cols - 1) {
                    mask |= 2;
                }
                if (row == rows - 1 && col == 0) {
                    mask |= 1;
                }
                if (row == rows - 1 && col == cols - 1) {
                    mask |= 8;
                }
                if (mask == 0) {
                    UiRender::rect(cx, cy, cw, ch, UiTheme::textMuted());
                } else {
                    UiRender::roundRect(cx, cy, cw, ch, std::min(radius, std::min(cw, ch)), UiTheme::textMuted(), mask);
                }
            }
        }
    }

    void colorSwatch(float x, float y, uint32_t rgba) {
        if (((rgba >> 24) & 0xFF) < 255) {
            checkerBoard(x, y, 15, 15, 5.0f, 5.0f);
        }
        UiRender::roundRect(x, y, 15, 15, 5.0f, rgba);
    }

    void dotsButton(const std::string& id, float x, float y, bool open, bool hovered) {
        float response = UiState::motion(id + "#dots", open ? 1.0f : (hovered ? 1.0f : 0.0f), 18.0f);
        if (response > 0.001f) {
            UiRender::roundRect(x, y, 26, 22, 6.0f, UiTheme::overlay(Ui::round(20 * response)));
        }
        dots(x + 9.5f, y + 11, UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), response));
    }

    void menuPanel(float x, float y, float w, float h) {
        UiRender::shadow(x, y, w, h, 14.0f, 10.0f, 4.0f, UiTheme::shadow());
        overlayPanel(x, y, w, h, 14.0f, UiTheme::menu(), 0xF);
    }

    void overlayPanel(float x, float y, float w, float h, float radius, uint32_t fill, int cornerMask) {
        UiRender::bloom(x, y, w, h, radius, 12.0f, 1.0f, UiTheme::shadow());
        UiRender::blur(x, y, w, h, radius, 8, true, cornerMask);
        UiRender::roundRect(x, y, w, h, radius, fill, cornerMask);
        UiRender::outline(x, y, w, h, radius, UiTheme::panelOutline(), cornerMask, 0.5f);
    }

    void menuRowHover(const std::string& id, float x, float y, float w, float h, bool hovered) {
        float hover = UiState::motion(id + "#menurow", hovered ? 1.0f : 0.0f, 22.0f);
        if (hover > 0.001f) {
            UiRender::roundRect(x + 6, y + 2, w - 12, h - 4, 10.0f, UiTheme::overlay(Ui::round(14 * hover)));
        }
    }

    void subPanel(float x, float y, float w, float h) {
        UiRender::shadow(x, y, w, h, 12.0f, 14.0f, 5.0f, UiTheme::shadow());
        overlayPanel(x, y, w, h, 12.0f, UiTheme::surface(), 0xF);
    }

    void subGroup(float x, float y, float w, float h) {
        UiRender::outline(x, y, w, h, 12.0f, UiTheme::groupOutline());
    }

    void subRowHover(const std::string& id, float x, float y, float w, float h, bool hovered) {
        float hover = UiState::motion(id + "#subrow", hovered ? 1.0f : 0.0f, 20.0f);
        if (hover > 0.001f) {
            UiRender::roundRect(x + 4, y, w - 8, h, 8.0f, UiTheme::overlay(Ui::round(14 * hover)));
        }
    }

    void subMode(const std::string& id, float rightX, float y, float h, const std::string& value, bool hovered,
                 bool open, float maxWidth) {
        float response = UiState::motion(id + "#submode", open ? 1.0f : (hovered ? 0.7f : 0.0f), 18.0f);
        uint32_t tint = UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), response);
        float arrowW = 11.0f;
        std::string text = value.empty() ? std::string("关闭") : value;
        float textW = UiFont::width(text, UiFont::MEDIUM, TEXT_CONTROL);
        float room = std::max(20.0f, maxWidth - arrowW);
        if (textW > room) {
            UiRender::textClipped(text, rightX - arrowW - room, y, h, room, tint, UiFont::MEDIUM, TEXT_CONTROL);
        } else {
            UiRender::textY(text, rightX - arrowW - textW, y, h, tint, UiFont::MEDIUM, TEXT_CONTROL);
        }
        icon(Ui::glyph(0xF054), rightX - arrowW * 0.5f, y + h * 0.5f, tint, 8.5f);
    }

    float subModeWidth(const std::string& value) {
        return UiFont::width(value.empty() ? std::string("关闭") : value, UiFont::MEDIUM, TEXT_CONTROL) + 11.0f;
    }

    void subSlider(const std::string& id, float x, float y, float trackWidth, float amount, bool hovered, bool active,
                   const std::string& valueText) {
        float shown = UiState::motionFrom(id + "#subslider", amount, 12.0f, amount);
        float hover = UiState::motion(id + "#subsliderHover", active ? 1.0f : (hovered ? 0.7f : 0.0f), 18.0f);
        UiRender::roundRect(x, y, trackWidth, 4, 2.0f,
                            UiRender::mix(UiTheme::outline(), UiTheme::outlineSoft(), hover));
        float fillWidth = trackWidth * shown;
        if (fillWidth > 0.1f) {
            UiRender::roundRect(x, y, std::max(4.0f, fillWidth), 4, 2.0f, UiTheme::accent());
        }
        float bubble = UiState::motion(id + "#subsliderBubble", active || hovered ? 1.0f : 0.0f, 20.0f);
        if (bubble > 0.01f && !valueText.empty()) {
            float textWidth = UiFont::width(valueText, UiFont::MEDIUM, TEXT_PILL);
            float bubbleW = textWidth + 16;
            float bubbleH = 21;
            float bubbleX = UiRender::clamp(x + fillWidth - bubbleW * 0.5f, x - 10, x + trackWidth + 10 - bubbleW);
            float bubbleY = y - bubbleH - 7;
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * bubble;
            UiRender::shadow(bubbleX, bubbleY, bubbleW, bubbleH, 6.0f, 7.0f, 2.0f, UiTheme::shadow());
            UiRender::roundRect(bubbleX, bubbleY, bubbleW, bubbleH, 6.0f, UiTheme::outline());
            UiRender::textY(valueText, bubbleX + (bubbleW - textWidth) * 0.5f, bubbleY, bubbleH, UiTheme::textPrimary(),
                            UiFont::MEDIUM, TEXT_PILL);
            UiRender::alphaMul = savedAlpha;
        }
    }

    void subToggle(const std::string& id, float x, float y, bool value) {
        float on = UiState::motion(id + "#subon", value ? 1.0f : 0.0f, 19.0f);
        UiRender::roundRect(x, y, 30, 18, 9.0f, UiRender::mix(UiTheme::outlineSoft(), UiTheme::accent(), on));
        knobShadow(x + UiRender::lerp(9.0f, 21.0f, on), y + 9, 6.8f);
        UiRender::circle(x + UiRender::lerp(9.0f, 21.0f, on), y + 9, 6.8f, UiTheme::onAccent());
    }
}
