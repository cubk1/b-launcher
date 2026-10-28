#include "Page.h"
#include "MainWindow.h"

#include "ui/gl/ShaderUtil.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Page {

    namespace {
        constexpr float CARD_RADIUS = 12.0f;

        struct ScrollState {
            float target = 0.0f;
            float shown = 0.0f;
        };

        std::unordered_map<std::string, ScrollState> scrolls;

        bool clipping = false;
        float clipTop = 0.0f;
        float clipBottom = 0.0f;

        float currentLeft = LEFT;
    }

    float right() {
        return std::min(MainWindow::width() - RIGHT_MARGIN, LEFT + MAX_WIDTH);
    }

    float width() {
        return right() - LEFT;
    }

    float controlRight() {
        return right() - 12.0f;
    }

    Column::Column(float left) : saved(currentLeft) {
        currentLeft = left;
    }

    Column::~Column() {
        currentLeft = saved;
    }

    float columnLeft() {
        return currentLeft;
    }

    float columnWidth() {
        return right() - currentLeft;
    }

    bool shown(float y, float h) {
        return !clipping || (y + h >= clipTop && y <= clipBottom);
    }

    void title(const std::string& text, float x, float y) {
        UiRender::textY(text, x, y, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, TITLE_SIZE);
    }

    void caption(const std::string& text, float y) {
        UiRender::textY(text, currentLeft + 2.0f, y + 2.0f, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                        UiWidgets::TEXT_SMALL);
    }

    void card(float y, int rows) {
        float h = rows * ROW_H;
        UiRender::roundRect(currentLeft, y, columnWidth(), h, CARD_RADIUS, UiTheme::surface());
        UiRender::outline(currentLeft, y, columnWidth(), h, CARD_RADIUS, UiTheme::outlineSoft());
        for (int i = 1; i < rows; i++) {
            UiRender::hLine(currentLeft + 14.0f, right() - 14.0f, y + i * ROW_H, UiTheme::outlineSoft());
        }
    }

    void label(const std::string& text, float y) {
        UiRender::textY(text, currentLeft + 16.0f, y, ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
    }

    void spinner(float cx, float cy, float radius, uint32_t color) {
        const float tau = 6.28318531f;
        float x = UiRender::transformX(cx);
        float y = UiRender::transformY(cy);
        float r = UiRender::transformW(radius);
        float stroke = UiRender::transformW(std::max(1.5f, radius * 0.24f));
        ShaderUtil::arc(x, y, r, stroke, 0.0f, tau, UiRender::applyAlpha((color & 0x00FFFFFFu) | 0x40000000u));
        double time = ImGui::GetTime();
        float spin = (float) std::fmod(time * tau / 1.1, (double) tau);
        float sweep = (0.18f + 0.55f * (0.5f - 0.5f * (float) std::cos(time * tau / 1.5))) * tau;
        ShaderUtil::arc(x, y, r, stroke, spin, spin + sweep, UiRender::applyAlpha(color));
    }

    namespace {
        constexpr float BUTTON_ICON = 11.0f;
        constexpr float BUTTON_ICON_GAP = 6.0f;
    }

    float buttonWidth(const std::string& text, int glyph) {
        float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        return glyph != 0 ? 10.0f + BUTTON_ICON + BUTTON_ICON_GAP + textW + 12.0f : textW + 24.0f;
    }

    bool button(const std::string& id, const std::string& text, float x, float y, float w, bool primary, bool blocked,
                int glyph) {
        bool hovered = !blocked && NewUi::hit(x, y, w, FIELD_H);
        float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
        uint32_t ink;
        if (primary) {
            UiRender::roundRect(x, y, w, FIELD_H, 7.0f, UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.10f * hover));
            ink = UiTheme::onAccent();
        } else {
            UiRender::roundRect(x, y, w, FIELD_H, 7.0f, UiTheme::surfaceRaised());
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, FIELD_H, 7.0f, UiTheme::overlay(Ui::round(14 * hover)));
            }
            UiRender::outline(x, y, w, FIELD_H, 7.0f, UiTheme::outlineSoft());
            ink = UiTheme::textPrimary();
        }
        float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        if (glyph != 0) {
            float contentW = BUTTON_ICON + BUTTON_ICON_GAP + textW;
            float iconX = x + (w - contentW) * 0.5f;
            UiWidgets::iconBeside(Ui::glyph(glyph), iconX + BUTTON_ICON * 0.5f, text, UiFont::MEDIUM,
                                  UiWidgets::TEXT_CONTROL, y, FIELD_H, ink, 10.0f);
            UiRender::textY(text, iconX + BUTTON_ICON + BUTTON_ICON_GAP, y, FIELD_H, ink, UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        } else {
            UiRender::textY(text, x + (w - textW) * 0.5f, y, FIELD_H, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }
        if (!blocked && NewUi::click(x, y, w, FIELD_H)) {
            UiSound::request(UiSound::forLabel(text));
            return true;
        }
        return false;
    }

    void rowHover(const std::string& id, float y, bool hovered, int corners) {
        float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
        if (amount > 0.001f) {
            UiRender::roundRect(currentLeft, y, columnWidth(), ROW_H, CARD_RADIUS,
                                UiTheme::overlay(Ui::round(14 * amount)), corners);
        }
    }

    bool navRow(const std::string& id, const std::string& text, const std::string& value, float y, bool blocked,
                int corners, int glyph) {
        bool hovered = !blocked && NewUi::hit(currentLeft, y, columnWidth(), ROW_H);
        float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
        if (amount > 0.001f) {
            UiRender::roundRect(currentLeft, y, columnWidth(), ROW_H, CARD_RADIUS,
                                UiTheme::overlay(Ui::round(14 * amount)), corners);
        }
        float textX = currentLeft + 16.0f;
        if (glyph != 0) {
            UiWidgets::iconBeside(Ui::glyph(glyph), currentLeft + 24.0f, text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                  y, ROW_H, UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), amount), 12.0f);
            textX = currentLeft + 44.0f;
        }
        UiRender::textY(text, textX, y, ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        float arrowX = controlRight() - 6.0f;
        UiWidgets::icon(Ui::glyph(0xf054), arrowX, y + ROW_H * 0.5f,
                        UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), amount), 9.0f);
        if (!value.empty()) {
            float labelRight = textX + UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 24.0f;
            std::string shown = UiFont::fitEnd(value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                               std::max(0.0f, arrowX - 18.0f - labelRight));
            float w = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, arrowX - 18.0f - w, y, ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }
        if (!blocked && NewUi::click(currentLeft, y, columnWidth(), ROW_H)) {
            UiSound::request(UiSound::PAGE);
            return true;
        }
        return false;
    }

    Scroll::Scroll(const std::string& id, float top, float bottom, float contentHeight)
        : Scroll(id, top, bottom, contentHeight, currentLeft, columnWidth()) {}

    Scroll::Scroll(const std::string& id, float top, float bottom, float contentHeight, float left, float width)
        : top(top),
          bottom(bottom),
          offset(0.0f),
          id(id),
          left(left),
          width(width) {
        ScrollState& state = scrolls[id];
        maxScroll = std::max(0.0f, contentHeight - (bottom - top));
        if (NewUi::hit(left - 16.0f, top, width + 32.0f, bottom - top) && ImGui::GetIO().MouseWheel != 0.0f) {
            state.target -= ImGui::GetIO().MouseWheel * ROW_H * 1.5f;
        }
        state.target = UiRender::clamp(state.target, 0.0f, maxScroll);
        state.shown = UiRender::clamp(UiRender::approach(state.shown, state.target, 18.0f), 0.0f, maxScroll);
        offset = UiRender::snapLayout(state.shown);
    }

    bool Scroll::begin() {
        UiRender::scissor(left - 4.0f, top, width + 8.0f, bottom - top);
        clipping = true;
        clipTop = top;
        clipBottom = bottom;
        return !NewUi::rawHit(left - 4.0f, top, width + 8.0f, bottom - top);
    }

    void Scroll::end() {
        clipping = false;
        UiRender::unscissor();
        if (maxScroll < 0.5f) {
            return;
        }
        ScrollState& state = scrolls[id];
        float dragged = NewUi::listScrollbar("scroll#" + id, left + width + 6.0f, top + 2.0f, bottom - top - 4.0f,
                                             bottom - top, maxScroll, state.shown, state.target);
        if (dragged != state.target) {
            state.target = dragged;
            state.shown = dragged;
        }
    }
}
