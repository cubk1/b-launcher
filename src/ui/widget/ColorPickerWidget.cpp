#include "ColorPickerWidget.h"
#include "InputWidget.h"
#include "../menu/ContextMenu.h"
#include "../panel/NewUi.h"
#include "../panel/UiCommon.h"
#include "../panel/UiCursor.h"
#include "../panel/UiFont.h"
#include "../panel/UiRender.h"
#include "../panel/UiState.h"
#include "../panel/UiTheme.h"
#include "../panel/UiWidgets.h"

#include <algorithm>
#include <cmath>

ColorPickerWidget::ColorPickerWidget(std::shared_ptr<ColorValue> color, bool square, bool accepts)
    : Widget(color->owner),
      color(std::move(color)),
      square(square),
      accepts(accepts) {}

bool ColorPickerWidget::modeMenuShown() const {
    return id == NewUi::pickerModeOwner || UiState::getOrDefault("pickerMode#open" + id, 0.0f) > 0.002f;
}

bool ColorPickerWidget::dragging(float areaX, float areaY, float areaW, float areaH, float menuX, float menuY,
                                 float menuW, float menuH) const {
    return accepts && NewUi::hit(areaX, areaY, areaW, areaH) && NewUi::mouseDown &&
            !NewUi::insideRect(modeMenuShown(), menuX, menuY, menuW, menuH);
}

void ColorPickerWidget::wheel(float areaX, float areaY, float areaW, float areaH, const std::string& channel) {
    if (accepts && !modeMenuShown() && NewUi::hit(areaX, areaY, areaW, areaH)) {
        NewUi::hoveredColor = color;
        NewUi::hoveredColorChannel = channel;
    }
}

void ColorPickerWidget::paint() {
    NewUi::pickerModeMenu.fill(NewUi::pickerModes());
    float menuW = NewUi::pickerModeMenu.menuWidth();
    float menuH = NewUi::pickerModeMenu.menuHeight();
    float menuX = x + 13;
    float menuY = y + h - 13 - 24 - menuH - 6;
    bool menuShown = modeMenuShown();
    uint32_t argb = color->get();
    int alpha = (argb >> 24) & 0xFF;
    float hsb[3];
    Ui::rgbToHsb((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, hsb);

    float fieldX = x + 13;
    float fieldY = y + 13;
    float fieldW = w - 26;
    float fieldH = square ? fieldW : 176;
    UiRender::svField(fieldX, fieldY, fieldW, fieldH, 7.0f, hsb[0]);
    UiRender::outline(fieldX, fieldY, fieldW, fieldH, 7.0f, UiTheme::outlineSoft());
    if (dragging(fieldX, fieldY, fieldW, fieldH, menuX, menuY, menuW, menuH)) {
        float sat = UiRender::saturate((NewUi::mouseX - fieldX) / fieldW);
        float bri = 1.0f - UiRender::saturate((NewUi::mouseY - fieldY) / fieldH);
        color->set(((uint32_t) alpha << 24) | (Ui::hsbToRgb(hsb[0], sat, bri) & 0xFFFFFFu));
        NewUi::clickConsumed = true;
    }
    wheel(fieldX, fieldY, fieldW, fieldH, "bri");
    NewUi::pickerCursor(fieldX + fieldW * hsb[1], fieldY + fieldH * (1.0f - hsb[2]), argb);

    float barH = 9;
    float hueY = fieldY + fieldH + 12;
    UiRender::hueField(fieldX, hueY, fieldW, barH, barH * 0.5f);
    if (dragging(fieldX, hueY, fieldW, barH + 6, menuX, menuY, menuW, menuH)) {
        float hue = UiRender::saturate((NewUi::mouseX - fieldX) / fieldW);
        color->set(((uint32_t) alpha << 24) |
                   (Ui::hsbToRgb(hue, std::max(0.0001f, hsb[1]), std::max(0.0001f, hsb[2])) & 0xFFFFFFu));
        NewUi::clickConsumed = true;
    }
    wheel(fieldX, hueY, fieldW, barH + 6, "hue");
    NewUi::pickerHandle(fieldX + fieldW * hsb[0], hueY + barH * 0.5f,
                        0xFF000000u | (Ui::hsbToRgb(hsb[0], 1.0f, 1.0f) & 0xFFFFFFu));

    float alphaY = hueY + barH + 12;
    UiRender::alphaField(fieldX, alphaY, fieldW, barH, barH * 0.5f, hsb[0], hsb[1], hsb[2]);
    if (dragging(fieldX, alphaY, fieldW, barH + 6, menuX, menuY, menuW, menuH)) {
        int picked = Ui::round(UiRender::saturate((NewUi::mouseX - fieldX) / fieldW) * 255);
        color->set(((uint32_t) picked << 24) | (argb & 0xFFFFFFu));
        NewUi::clickConsumed = true;
    }
    wheel(fieldX, alphaY, fieldW, barH + 6, "alpha");
    NewUi::pickerHandle(fieldX + fieldW * (alpha / 255.0f), alphaY + barH * 0.5f, argb | 0xFF000000u);

    float footY = alphaY + barH + 14;
    UiRender::textY(Ui::glyph(0xF078), fieldX + 2, footY, 24, UiTheme::textMuted(), UiFont::ICON,
                    UiWidgets::TEXT_SMALL);
    UiRender::textY(NewUi::pickerModeName(), fieldX + 16, footY, 24, UiTheme::textSecondary(), UiFont::MEDIUM,
                    UiWidgets::TEXT_CONTROL);
    if (NewUi::swatchMenuOwner.empty() && NewUi::click(fieldX, footY, 54, 24)) {
        NewUi::pickerModeOwner = id == NewUi::pickerModeOwner ? std::string() : id;
        NewUi::pickerModeMenu.closeChild();
    }

    if (NewUi::pickerMode == 0) {
        paintHex(argb, fieldX + 52, footY, fieldW - 52 - 26, menuShown);
    } else {
        paintChannels(argb, alpha, hsb, fieldX, footY, fieldW, menuShown);
    }

    float gridX = fieldX + fieldW - 22;
    bool gridHover = NewUi::hit(gridX, footY, 22, 24);
    bool justCopied = id == NewUi::colorCopiedOwner && Ui::millis() - NewUi::colorCopiedAt < 1500;
    if (justCopied) {
        UiWidgets::check(gridX + 10, footY + 12, gridHover ? UiTheme::textPrimary() : UiTheme::textMuted(), 11.0f);
    } else {
        UiRender::textY(Ui::glyph(0xF0C5), gridX + 4, footY, 24,
                        gridHover ? UiTheme::textPrimary() : UiTheme::textMuted(), UiFont::ICON,
                        UiWidgets::TEXT_CONTROL);
    }
    if (gridHover) {
        NewUi::tooltipText = "Copy color";
        NewUi::tooltipX = gridX - 46;
        NewUi::tooltipY = footY + 26;
    }
    if (accepts && !menuShown && NewUi::click(gridX, footY, 22, 24)) {
        NewUi::colorClipboard = argb;
        NewUi::colorClipboardSet = true;
        NewUi::colorCopiedOwner = id;
        NewUi::colorCopiedAt = Ui::millis();
    }

    NewUi::pickerModeLayer(id, fieldX, footY);
}

void ColorPickerWidget::paintHex(uint32_t argb, float boxX, float footY, float boxW, bool menuShown) {
    bool hexEditing = id == NewUi::colorEditing && NewUi::colorEditingCell == 0;
    UiRender::roundRect(boxX, footY, boxW, 24, 7.0f, UiTheme::surfaceRaised());
    if (hexEditing) {
        UiRender::outline(boxX, footY, boxW, 24, 7.0f, UiTheme::accent());
    }
    UiWidgets::checkerBoard(boxX + 7, footY + 6, 12, 12, 3.0f, 4.0f);
    UiRender::roundRect(boxX + 7, footY + 6, 12, 12, 3.0f, argb);
    float hexRoom = boxW - 25 - 8;
    if (hexEditing) {
        InputWidget(NewUi::INPUT_COLOR)
                .area(boxX + 25, hexRoom, UiWidgets::TEXT_CONTROL, 14)
                .limit(8)
                .center()
                .bounds(boxX, footY, boxW, 24)
                .block(!accepts || menuShown)
                .paint();
        return;
    }
    std::string hexText = Ui::format("#%08X", argb);
    float hexW = UiFont::width(hexText, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
    UiRender::textClipped(hexText, boxX + 25 + std::max(0.0f, (hexRoom - hexW) * 0.5f), footY, 24, hexRoom,
                          UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, UiTheme::surfaceRaised());
    if (accepts && !menuShown && NewUi::hit(boxX, footY, boxW, 24)) {
        UiCursor::text();
    }
    if (accepts && !menuShown && NewUi::click(boxX, footY, boxW, 24)) {
        NewUi::beginColorEdit(color, 0, Ui::format("%08X", argb));
    }
}

void ColorPickerWidget::paintChannels(uint32_t argb, int alpha, const float* hsb, float fieldX, float footY,
                                      float fieldW, bool menuShown) {
    int r = (argb >> 16) & 0xFF;
    int g = (argb >> 8) & 0xFF;
    int b = argb & 0xFF;
    int parts[4];
    if (NewUi::pickerMode == 1) {
        parts[0] = r;
        parts[1] = g;
        parts[2] = b;
        parts[3] = alpha;
    } else {
        parts[0] = Ui::round(hsb[0] * 360);
        parts[1] = Ui::round(hsb[1] * 100);
        parts[2] = Ui::round(hsb[2] * 100);
        parts[3] = alpha;
    }
    float cellW = (fieldW - 52 - 26) / 4.0f - 3;
    for (int i = 0; i < 4; i++) {
        float cx = fieldX + 52 + i * (cellW + 3);
        bool cellEditing = id == NewUi::colorEditing && NewUi::colorEditingCell == i + 1;
        if (!cellEditing) {
            wheel(cx, footY, cellW, 24,
                  i == 3                           ? "alpha"
                          : NewUi::pickerMode == 1 ? (i == 0           ? "red"
                                                              : i == 1 ? "green"
                                                                       : "blue")
                                                   : (i == 0           ? "hue"
                                                              : i == 1 ? "sat"
                                                                       : "bri"));
        }
        UiRender::roundRect(cx, footY, cellW, 24, 7.0f, UiTheme::surfaceRaised());
        if (cellEditing) {
            UiRender::outline(cx, footY, cellW, 24, 7.0f, UiTheme::accent());
        }
        if (cellEditing) {
            InputWidget(NewUi::INPUT_COLOR)
                    .area(cx + 3, cellW - 6, UiWidgets::TEXT_CONTROL, 14)
                    .limit(3)
                    .center()
                    .bounds(cx, footY, cellW, 24)
                    .block(!accepts || menuShown)
                    .paint();
            continue;
        }
        std::string text = std::to_string(parts[i]);
        float tw = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        UiRender::textY(text, cx + (cellW - tw) * 0.5f, footY, 24, UiTheme::textPrimary(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
        if (accepts && !menuShown && NewUi::hit(cx, footY, cellW, 24)) {
            UiCursor::text();
        }
        if (accepts && !menuShown && NewUi::click(cx, footY, cellW, 24)) {
            NewUi::beginColorEdit(color, i + 1, std::to_string(parts[i]));
        }
    }
}
