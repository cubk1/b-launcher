#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/MainWindow.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/theme/ThemeStyle.h"
#include "ui/imgui/screens/theme/ThemeEffects.h"
#include "ui/imgui/screens/theme/ThemePaint.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/widget/IconButtonWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Page {

    namespace {
        constexpr float CARD_RADIUS = 12.0f;

        struct ScrollState {
            float target = 0.0f;
            float shown = 0.0f;
        };

        std::unordered_map<std::string, ScrollState> scrolls;
        std::unordered_set<std::string> touched;
        std::string scrollPrefix;

        std::string scrollKey(const std::string& id) {
            return scrollPrefix + id;
        }

        bool clipping = false;
        float clipTop = 0.0f;
        float clipBottom = 0.0f;

        constexpr double REVEAL_HOLD = 0.6;
        constexpr double REVEAL_FADE = 1.0;
        std::string revealText;
        double revealAt = 0.0;
        bool revealScrolled = false;
        bool scrollActive = false;
        std::string activeId;
        float activeTop = 0.0f;
        float activeBottom = 0.0f;
        float activeOffset = 0.0f;
        float activeMax = 0.0f;
        float cardTop = 0.0f;
        float cardBottom = 0.0f;
        std::vector<std::string>* collected = nullptr;

        float currentLeft = -1.0f;
    }

    float top() {
        return MainWindow::contentTop() + 10.0f;
    }

    float left() {
        return MainWindow::sidebarWidth() + GUTTER;
    }

    float right() {
        return MainWindow::width() - RIGHT_MARGIN;
    }

    float width() {
        return right() - left();
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
        return currentLeft >= 0.0f ? currentLeft : left();
    }

    float columnWidth() {
        return right() - columnLeft();
    }

    bool shown(float y, float h) {
        return collected != nullptr || !clipping || (y + h >= clipTop && y <= clipBottom);
    }

    void centredText(const std::string& text, float cx, float y, float h, uint32_t color, float size) {
        float w = UiFont::width(text, UiFont::MEDIUM, size);
        UiRender::textY(text, cx - w * 0.5f, y, h, color, UiFont::MEDIUM, size);
    }

    int rowCorners(int index, int count) {
        return (index == 0 ? 1 | 2 : 0) | (index == count - 1 ? 4 | 8 : 0);
    }

    void title(const std::string& text, float x, float y) {
        UiRender::textY(text, x, y, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, TITLE_SIZE);
    }

    void caption(const std::string& text, float y) {
        UiRender::textY(text, columnLeft() + 2.0f, y + 2.0f, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                        UiWidgets::TEXT_SMALL);
    }

    void card(float x, float y, float w, float h) {
        cardTop = y;
        cardBottom = y + h;
        ThemePaint::fill(ThemePaintSpec::CARD, x, y, w, h, CARD_RADIUS, 0xF, UiTheme::surface());
        UiRender::outline(x, y, w, h, CARD_RADIUS, UiTheme::border(UiTheme::outlineSoft()));
    }

    void listCard(float x, float y, float w, float rowHeight, int rows) {
        card(x, y, w, rows * rowHeight);
        for (int i = 1; i < rows; i++) {
            UiRender::hLine(x + 14.0f, x + w - 14.0f, y + i * rowHeight, UiTheme::rowDivider(UiTheme::outlineSoft()));
        }
    }

    void card(float y, int rows) {
        listCard(columnLeft(), y, columnWidth(), ROW_H, rows);
    }

    void reveal(const std::string& text) {
        revealText = text;
        revealAt = ImGui::GetTime();
        revealScrolled = false;
    }

    void collect(std::vector<std::string>* sink) {
        collected = sink;
    }

    void label(const std::string& text, float y) {
        if (collected != nullptr) {
            collected->push_back(text);
            return;
        }
        if (!revealText.empty() && text == revealText) {
            if (!revealScrolled && scrollActive) {
                revealScrolled = true;
                float view = activeBottom - activeTop;
                scrolls[activeId].target =
                        UiRender::clamp(activeOffset + y - activeTop - (view - ROW_H) * 0.4f, 0.0f, activeMax);
            }
            double elapsed = ImGui::GetTime() - revealAt;
            float amount = elapsed < REVEAL_HOLD ? 1.0f : 1.0f - (float) ((elapsed - REVEAL_HOLD) / REVEAL_FADE);
            if (amount <= 0.0f) {
                revealText.clear();
            } else {
                UiState::keepAnimating();
                uint32_t alpha = (uint32_t) Ui::round(56.0f * amount);
                int corners = (std::fabs(y - cardTop) < 0.5f ? 1 | 2 : 0) |
                        (std::fabs(y + ROW_H - cardBottom) < 0.5f ? 4 | 8 : 0);
                UiRender::roundRect(columnLeft(), y, columnWidth(), ROW_H, CARD_RADIUS,
                                    (UiTheme::accent() & 0x00FFFFFFu) | (alpha << 24), corners);
            }
        }
        UiRender::textY(text, columnLeft() + 16.0f, y, ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                        UiWidgets::TEXT_CONTROL);
    }

    float tag(const std::string& text, float x, float rowY, uint32_t fill, uint32_t ink, float padding) {
        constexpr float height = 18.0f;
        float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 2.0f * padding;
        float y = rowY + (ROW_H - height) * 0.5f;
        UiRender::roundRect(x, y, w, height, 6.0f, fill);
        UiRender::textY(text, x + padding, y, height, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        return w;
    }

    void progressRing(float cx, float cy, float fraction) {
        const float tau = 6.28318531f;
        float r = UiRender::transformW(7.0f);
        float stroke = UiRender::transformW(1.7f);
        float x = UiRender::transformX(cx);
        float y = UiRender::transformY(cy);
        ShaderUtil::arc(x, y, r, stroke, 0.0f, tau,
                        UiRender::applyAlpha((UiTheme::accent() & 0x00FFFFFFu) | 0x40000000u));
        if (fraction > 0.001f) {
            ShaderUtil::arc(x, y, r, stroke, -tau * 0.25f, -tau * 0.25f + tau * std::min(1.0f, fraction),
                            UiRender::applyAlpha(UiTheme::accent()));
        }
    }

    void tooltip(const std::string& text, float x, float y, float w, float h) {
        float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
        NewUi::tooltipText = text;
        NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
        NewUi::tooltipY = y + h + 6.0f;
    }

    bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y, bool blocked) {
        IconButtonWidget button(id, Ui::glyph(glyph), UiTheme::textSecondary(), UiTheme::textPrimary());
        button.bounds(x, y, 28, 28).block(blocked);
        button.paint();
        if (!blocked && button.hovered()) {
            tooltip(hint, x, y, 28, 28);
        }
        return button.pressed;
    }

    bool escape(bool blocked) {
        return NewUi::takeEscape() && !blocked;
    }

    bool back(const std::string& id, const std::string& hint, float x, float y, bool blocked) {
        bool pressed = iconButton(id, 0xf053, hint, x, y, blocked);
        return escape(blocked) || pressed;
    }

    void spinnerArc(float& from, float& to) {
        UiState::keepAnimating();
        const double minimum = 10.0;
        const double maximum = 280.0;
        const double phase = 0.75;
        const double degreesPerSecond = 2.0e9 / 7.5e6;
        double time = ImGui::GetTime();
        double index = std::floor(time / phase);
        double progress = (time - index * phase) / phase;
        double inverse = 1.0 - progress;
        double eased = 1.0 - inverse * inverse * inverse;
        bool growing = std::fmod(index, 2.0) < 0.5;
        double current = growing ? minimum + (maximum - minimum) * eased : maximum - (maximum - minimum) * eased;
        double offset = std::fmod(std::floor(index * 0.5) * (maximum - minimum), 360.0);
        double rotation = std::fmod(time * degreesPerSecond, 360.0) + offset;
        double start = growing ? rotation : rotation + maximum - current;
        double radians = 3.14159265358979 / 180.0;
        from = (float) (std::fmod(start, 360.0) * radians);
        to = from + (float) (current * radians);
    }

    void spinner(float cx, float cy, float radius, uint32_t color) {
        const float tau = 6.28318531f;
        float x = UiRender::transformX(cx);
        float y = UiRender::transformY(cy);
        float r = UiRender::transformW(radius);
        float stroke = UiRender::transformW(std::max(1.5f, radius * 0.24f));
        ShaderUtil::arc(x, y, r, stroke, 0.0f, tau, UiRender::applyAlpha((color & 0x00FFFFFFu) | 0x40000000u));
        float from = 0.0f;
        float to = 0.0f;
        spinnerArc(from, to);
        ShaderUtil::arc(x, y, r, stroke, from, to, UiRender::applyAlpha(color));
    }

    namespace {
        constexpr float BUTTON_ICON = 11.0f;
        constexpr float BUTTON_ICON_GAP = 6.0f;
    }

    float buttonWidth(const std::string& text, int glyph) {
        glyph = ThemeStyle::icons() ? glyph : 0;
        float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        return glyph != 0 ? 10.0f + BUTTON_ICON + BUTTON_ICON_GAP + textW + 12.0f : textW + 24.0f;
    }

    bool button(const std::string& id, const std::string& text, float x, float y, float w, bool primary, bool blocked,
                int glyph, float h) {
        bool hovered = !blocked && NewUi::hit(x, y, w, h);
        float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
        uint32_t ink;
        if (primary) {
            ThemePaint::primaryFill(x, y, w, h, 7.0f, 0.10f * hover);
            ink = UiTheme::tokenOr(UiTheme::PRIMARY_TEXT, UiTheme::onAccent());
        } else {
            UiRender::roundRect(x, y, w, h, 7.0f, UiTheme::surfaceRaised());
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, h, 7.0f, UiTheme::overlay(Ui::round(14 * hover)));
            }
            UiRender::outline(x, y, w, h, 7.0f,
                              UiTheme::border(UiTheme::tokenOr(UiTheme::BUTTON_OUTLINE, UiTheme::outlineSoft())));
            ink = UiTheme::textPrimary();
        }
        ThemeEffects::button(id, primary ? "primary" : "button", x, y, w, h, 7.0f, 0xF, ink, hovered);
        float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        if (glyph != 0 && ThemeStyle::icons()) {
            float contentW = BUTTON_ICON + BUTTON_ICON_GAP + textW;
            float iconX = x + (w - contentW) * 0.5f;
            UiWidgets::iconBeside(Ui::glyph(glyph), iconX + BUTTON_ICON * 0.5f, text, UiFont::MEDIUM,
                                  UiWidgets::TEXT_CONTROL, y, h, ink, 10.0f);
            UiRender::textY(text, iconX + BUTTON_ICON + BUTTON_ICON_GAP, y, h, ink, UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        } else {
            UiRender::textY(text, x + (w - textW) * 0.5f, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        }
        if (!blocked && NewUi::click(x, y, w, h)) {
            UiSound::request(UiSound::forLabel(text));
            return true;
        }
        return false;
    }

    void rowHover(const std::string& id, float y, bool hovered, int corners) {
        float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
        if (amount > 0.001f) {
            UiRender::roundRect(columnLeft(), y, columnWidth(), ROW_H, CARD_RADIUS,
                                UiTheme::overlay(Ui::round(14 * amount)), corners);
        }
    }

    bool navRow(const std::string& id, const std::string& text, const std::string& value, float y, bool blocked,
                int corners, int glyph) {
        bool hovered = !blocked && NewUi::hit(columnLeft(), y, columnWidth(), ROW_H);
        float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
        if (amount > 0.001f) {
            UiRender::roundRect(columnLeft(), y, columnWidth(), ROW_H, CARD_RADIUS,
                                UiTheme::overlay(Ui::round(14 * amount)), corners);
        }
        float textX = columnLeft() + 16.0f;
        if (glyph != 0) {
            UiWidgets::iconBeside(Ui::glyph(glyph), columnLeft() + 24.0f, text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                  y, ROW_H, UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), amount), 12.0f);
            textX = columnLeft() + 44.0f;
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
        if (!blocked && NewUi::click(columnLeft(), y, columnWidth(), ROW_H)) {
            UiSound::request(UiSound::PAGE);
            return true;
        }
        return false;
    }

    ScrollScope::ScrollScope(const std::string& name) : saved(scrollPrefix) {
        scrollPrefix += name + "#";
    }

    ScrollScope::~ScrollScope() {
        scrollPrefix = std::move(saved);
    }

    Scroll::Scroll(const std::string& id, float top, float bottom, float contentHeight)
        : Scroll(id, top, bottom, contentHeight, columnLeft(), columnWidth()) {}

    Scroll::Scroll(const std::string& id, float top, float bottom, float contentHeight, float left, float width)
        : top(top),
          bottom(bottom),
          offset(0.0f),
          id(scrollKey(id)),
          left(left),
          width(width) {
        ScrollState& state = scrolls[this->id];
        touched.insert(this->id);
        maxScroll = std::max(0.0f, contentHeight - (bottom - top));
        if (NewUi::hit(left - 16.0f, top, width + 32.0f, bottom - top) && NewUi::wheel() != 0.0f) {
            state.target -= NewUi::wheel() * ROW_H * 1.5f;
        }
        state.target = UiRender::clamp(state.target, 0.0f, maxScroll);
        state.shown = UiRender::clamp(UiRender::approach(state.shown, state.target, 18.0f), 0.0f, maxScroll);
        offset = UiRender::snapLayout(state.shown);
    }

    float scrollOffset(const std::string& id) {
        auto found = scrolls.find(scrollKey(id));
        return found != scrolls.end() ? UiRender::snapLayout(found->second.shown) : 0.0f;
    }

    void scrollTo(const std::string& id, float target) {
        std::string key = scrollKey(id);
        scrolls[key].target = target;
        touched.insert(key);
    }

    void forgetHidden() {
        for (auto it = scrolls.begin(); it != scrolls.end();) {
            if (touched.count(it->first) == 0 && it->first.find("downloads#") == std::string::npos) {
                it = scrolls.erase(it);
            } else {
                ++it;
            }
        }
        touched.clear();
    }

    bool Scroll::begin() {
        UiRender::scissor(left - 4.0f, top, width + 8.0f, bottom - top);
        scrollActive = true;
        activeId = id;
        activeTop = top;
        activeBottom = bottom;
        activeOffset = offset;
        activeMax = maxScroll;
        clipping = true;
        clipTop = top;
        clipBottom = bottom;
        return !NewUi::rawHit(left - 4.0f, top, width + 8.0f, bottom - top);
    }

    void Scroll::end() {
        scrollActive = false;
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
