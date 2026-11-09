#include "ui/imgui/screens/onboarding/Onboarding.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/theme/ThemePaint.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include "imgui.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Onboarding {

    namespace {
        constexpr int STEPS = 4;
        constexpr int STEP_THEME = 0;
        constexpr int STEP_ZOOM = 1;
        constexpr int STEP_BACKGROUND = 2;
        constexpr int STEP_HOME = 3;

        constexpr float LEAVE_SECONDS = 0.18f;
        constexpr float INTRO_HOLD = 1.0f;
        constexpr float INTRO_OUT = 0.18f;
        constexpr float ENTER_SECONDS = 0.40f;

        constexpr float CONTENT_MAX = 880.0f;
        constexpr float STEPPER_Y = MainWindow::TITLE_HEIGHT + 10.0f;
        constexpr float STEPPER_H = 28.0f;
        constexpr float STEP_DOT = 22.0f;
        constexpr float CONTROL_H = 32.0f;
        constexpr float FOOTER_MARGIN = 24.0f;
        constexpr float BODY_MARGIN = 28.0f;
        constexpr float COLUMN_GAP = 40.0f;
        constexpr float LEFT_MIN = 280.0f;
        constexpr float LEFT_MAX = 340.0f;
        constexpr float PREVIEW_RATIO = 1.6f;
        constexpr float TITLE_H = 30.0f;
        constexpr float HINT_H = 20.0f;
        constexpr float HINT_GAP = 6.0f;
        constexpr float CONTROLS_GAP = 24.0f;
        constexpr float ROW_GAP = 12.0f;
        constexpr float CHIP_GAP = 8.0f;
        constexpr float SWATCH_R = 13.0f;
        constexpr float SWATCH_CELL = 40.0f;
        constexpr int SWATCH_COLUMNS = 5;
        constexpr int ZOOM_COLUMNS = 3;
        constexpr float LIST_ROW = 36.0f;
        constexpr float TOGGLE_W = 34.0f;
        constexpr float TOGGLE_H = 18.0f;
        constexpr float ICON_BUTTON = 28.0f;

        const char* const PENDING_KEY = "setupPending";

        const char* const TITLES[STEPS] = {"主题色", "界面缩放", "背景", "主页组件"};
        const char* const HINTS[STEPS] = {
                "改主题颜色，让你的启动器丰富多彩",
                "字小就调大，窗口会按比例一起放大",
                "设置启动器背景图片",
                "你的启动器，你来定义",
        };

        const uint32_t ACCENTS[] = {
                0xFF4F7DFFu, 0xFF8E6BFFu, 0xFFE45C8Au, 0xFFE2683Cu, 0xFFE3B341u,
                0xFF43A96Bu, 0xFF2AA9A0u, 0xFF3B9AE1u, 0xFF8A8F98u,
        };

        bool shown = false;
        bool leaving = false;
        bool outro = false;
        double leaveAt = 0.0;
        double outroAt = 0.0;
        double shownAt = 0.0;
        int step = STEP_THEME;
        float direction = 1.0f;
        float pageMix = 1.0f;
        float pulse = 0.0f;

        float clamp01(float value) {
            return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
        }

        float easeOut(float value) {
            float inverse = 1.0f - value;
            return 1.0f - inverse * inverse * inverse;
        }

        // Staggered entrance measured from the end of the splash.
        float enter(double delay) {
            double since = ImGui::GetTime() - shownAt - (double) INTRO_HOLD - delay;
            return easeOut(clamp01((float) (since / (double) ENTER_SECONDS)));
        }

        void kick(float amount) {
            pulse = std::max(pulse, amount);
        }

        std::string hexOf(uint32_t argb) {
            char out[8];
            std::snprintf(out, sizeof(out), "#%06X", argb & 0x00FFFFFFu);
            return out;
        }

        void splash(float w, float h, float elapsed) {
            float rise = easeOut(clamp01(elapsed / 0.34f));
            float leave = 1.0f - easeOut(clamp01((elapsed - (INTRO_HOLD - INTRO_OUT)) / INTRO_OUT));
            float alpha = rise * leave;
            if (alpha <= 0.002f) {
                return;
            }
            float size = std::clamp(std::min(w, h) * 0.11f, 46.0f, 78.0f) * (0.94f + 0.06f * rise);
            float cx = w * 0.5f;
            float cy = h * 0.5f - size * 0.5f - 16.0f + (1.0f - rise) * 12.0f - (1.0f - leave) * 10.0f;
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * alpha;
            Logo::brand(std::round(cx - size * 0.5f), std::round(cy), size, 1.0f);
            std::string title = Appearance::windowTitle();
            float titleW = UiFont::width(title, UiFont::BOLD, 22.0f);
            UiRender::textY(title, cx - titleW * 0.5f, cy + size + 18.0f, 30.0f, UiTheme::textPrimary(), UiFont::BOLD,
                            22.0f);
            UiRender::alphaMul = saved;
        }

        uint32_t withAlpha(uint32_t argb, float alpha) {
            return (argb & 0x00FFFFFFu) | ((uint32_t) Ui::round(255.0f * clamp01(alpha)) << 24);
        }

        bool action(const std::string& id, const std::string& text, float x, float y, float w, bool primary,
                    int glyph) {
            bool hovered = NewUi::hit(x, y, w, CONTROL_H);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 16.0f);
            float press = NewUi::mouseDown && hovered ? 1.0f : 0.0f;
            float push = UiState::motion(id + "#press", press, 26.0f);
            float dy = push * 1.0f;
            uint32_t ink;
            if (primary) {
                ThemePaint::primaryFill(x, y + dy, w, CONTROL_H, 9.0f, 0.10f * lit + 0.05f * push);
                ink = UiTheme::tokenOr(UiTheme::PRIMARY_TEXT, UiTheme::onAccent());
            } else {
                UiRender::roundRect(x, y + dy, w, CONTROL_H, 9.0f, UiTheme::surfaceRaised());
                if (lit > 0.001f) {
                    UiRender::roundRect(x, y + dy, w, CONTROL_H, 9.0f, UiTheme::overlay(Ui::round(14.0f * lit)));
                }
                UiRender::outline(x, y + dy, w, CONTROL_H, 9.0f, UiTheme::border(UiTheme::outlineSoft()));
                ink = UiTheme::textPrimary();
            }
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (glyph != 0) {
                float start = x + (w - (16.0f + textW)) * 0.5f;
                UiWidgets::icon(Ui::glyph(glyph), start + 5.0f, y + dy + CONTROL_H * 0.5f, ink, 10.0f);
                UiRender::textY(text, start + 16.0f, y + dy, CONTROL_H, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            } else {
                UiRender::textY(text, x + (w - textW) * 0.5f, y + dy, CONTROL_H, ink, UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (NewUi::click(x, y, w, CONTROL_H)) {
                UiSound::request(primary ? UiSound::RELEASE : UiSound::TICK);
                return true;
            }
            return false;
        }

        float actionWidth(const std::string& text, int glyph) {
            return UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + (glyph != 0 ? 16.0f : 0.0f) + 40.0f;
        }

        void complete() {
            if (outro || leaving) {
                return;
            }
            outro = true;
            outroAt = ImGui::GetTime();
            UiSound::request(UiSound::PULSE);
        }

        void go(int next) {
            if (next < 0 || next >= STEPS || next == step) {
                return;
            }
            direction = next > step ? 1.0f : -1.0f;
            step = next;
            pageMix = 0.0f;
            kick(0.85f);
            UiSound::request(UiSound::PAGE);
        }

        std::shared_ptr<ColorBinding> customAccent() {
            static auto binding = std::make_shared<ColorBinding>(
                    "onboarding#accent", [] { return Appearance::accent(); },
                    [](uint32_t argb) { Appearance::setAccent(argb); });
            binding->opaque = true;
            return binding;
        }

        void stepper(float x, float y, float w) {
            float progress = UiState::motion("onboarding#progress", (float) step, 10.0f);
            float widths[STEPS];
            float total = 0.0f;
            for (int i = 0; i < STEPS; i++) {
                widths[i] = STEP_DOT + 8.0f + UiFont::width(TITLES[i], UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                total += widths[i];
            }
            float link = std::max(0.0f, w - total) / (float) (STEPS - 1);
            float cy = y + STEPPER_H * 0.5f;
            float cursor = x;
            for (int i = 0; i < STEPS; i++) {
                std::string id = "onboarding#step#" + std::to_string(i);
                bool hovered = i != step && NewUi::hit(cursor - 4.0f, y, widths[i] + 8.0f, STEPPER_H);
                float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 16.0f);
                float done = clamp01(progress - (float) i);
                float here = std::max(0.0f, 1.0f - std::fabs(progress - (float) i));
                float reached = std::max(done, here);
                float dotX = cursor + STEP_DOT * 0.5f;
                float r = STEP_DOT * 0.5f;
                UiRender::circle(dotX, cy, r, UiRender::mix(UiTheme::surfaceRaised(), UiTheme::accent(), reached));
                if (reached < 0.999f) {
                    UiRender::outline(dotX - r, cy - r, STEP_DOT, STEP_DOT, r,
                                      withAlpha(UiTheme::outline(), 1.0f - reached));
                }
                uint32_t mark = UiRender::mix(UiTheme::textMuted(), UiTheme::onAccent(), reached);
                if (i < step) {
                    UiWidgets::check(dotX, cy, mark, 9.0f);
                } else {
                    Page::centredText(std::to_string(i + 1), dotX, y, STEPPER_H, mark, UiWidgets::TEXT_SMALL);
                }
                uint32_t ink = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), std::max(here, 0.6f * lit));
                UiRender::textY(TITLES[i], cursor + STEP_DOT + 8.0f, y, STEPPER_H, ink, UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (i != step && NewUi::click(cursor - 4.0f, y, widths[i] + 8.0f, STEPPER_H)) {
                    go(i);
                }
                if (i < STEPS - 1) {
                    float from = cursor + widths[i] + 12.0f;
                    float to = cursor + widths[i] + link - 12.0f;
                    if (to > from) {
                        UiRender::roundRect(from, cy - 1.0f, to - from, 2.0f, 1.0f, UiTheme::overlay(18));
                        float filled = (to - from) * clamp01(progress - (float) i);
                        if (filled > 0.5f) {
                            UiRender::roundRect(from, cy - 1.0f, filled, 2.0f, 1.0f, UiTheme::accent());
                        }
                    }
                }
                cursor += widths[i] + link;
            }
        }

        bool presetAccent() {
            for (uint32_t preset : ACCENTS) {
                if ((Appearance::accent() & 0x00FFFFFFu) == (preset & 0x00FFFFFFu)) {
                    return true;
                }
            }
            return false;
        }

        void swatchFace(const std::string& id, float cx, float cy, uint32_t fill, bool selected, float& lit) {
            float hit = SWATCH_R + 5.0f;
            bool hovered = NewUi::hit(cx - hit, cy - hit, hit * 2.0f, hit * 2.0f);
            lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 16.0f);
            float sel = UiState::motion(id + "#sel", selected ? 1.0f : 0.0f, 16.0f);
            if (sel > 0.001f) {
                float ring = SWATCH_R + 4.0f;
                UiRender::outline(cx - ring, cy - ring, ring * 2.0f, ring * 2.0f, ring, withAlpha(fill, sel), 0xF,
                                  2.0f);
            }
            UiRender::circle(cx, cy, SWATCH_R + lit, fill);
        }

        bool swatchClicked(float cx, float cy) {
            float hit = SWATCH_R + 5.0f;
            return NewUi::click(cx - hit, cy - hit, hit * 2.0f, hit * 2.0f);
        }

        void swatch(float cx, float cy, uint32_t color) {
            std::string id = "onboarding#accent#" + hexOf(color);
            bool selected = (Appearance::accent() & 0x00FFFFFFu) == (color & 0x00FFFFFFu);
            float lit = 0.0f;
            swatchFace(id, cx, cy, 0xFF000000u | (color & 0x00FFFFFFu), selected, lit);
            if (selected) {
                UiWidgets::check(cx, cy, UiTheme::inkOn(color), 10.0f);
            }
            if (lit > 0.5f) {
                Page::tooltip(hexOf(color), cx - SWATCH_R, cy + SWATCH_R + 4.0f, SWATCH_R * 2.0f, 0.0f);
            }
            if (swatchClicked(cx, cy)) {
                UiSound::request(UiSound::TICK);
                Appearance::setAccent(color);
                kick(0.6f);
            }
        }

        void customSwatch(float cx, float cy) {
            bool custom = !presetAccent();
            uint32_t accent = 0xFF000000u | (Appearance::accent() & 0x00FFFFFFu);
            float lit = 0.0f;
            swatchFace("onboarding#accent#custom", cx, cy, custom ? accent : UiTheme::surfaceRaised(), custom, lit);
            if (custom) {
                UiWidgets::icon(Ui::glyph(0xf1fb), cx, cy, UiTheme::inkOn(accent), 10.0f);
            } else {
                UiRender::outline(cx - SWATCH_R, cy - SWATCH_R, SWATCH_R * 2.0f, SWATCH_R * 2.0f, SWATCH_R,
                                  UiTheme::outline());
                UiWidgets::icon(Ui::glyph(0xf1fb), cx, cy,
                                UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit), 10.0f);
            }
            if (lit > 0.5f) {
                Page::tooltip("自定义颜色", cx - SWATCH_R, cy + SWATCH_R + 4.0f, SWATCH_R * 2.0f, 0.0f);
            }
            if (swatchClicked(cx, cy)) {
                UiSound::request(UiSound::PAGE);
                NewUi::openPicker(customAccent(), cx + SWATCH_R + 12.0f,
                                  std::max(MainWindow::TITLE_HEIGHT + 8.0f, cy - 150.0f));
            }
        }

        void themeStep(float x, float y) {
            int presets = (int) (sizeof(ACCENTS) / sizeof(ACCENTS[0]));
            for (int i = 0; i <= presets; i++) {
                float cx = x + SWATCH_R + (float) (i % SWATCH_COLUMNS) * SWATCH_CELL;
                float cy = y + SWATCH_R + (float) (i / SWATCH_COLUMNS) * SWATCH_CELL;
                if (i < presets) {
                    swatch(cx, cy, ACCENTS[i]);
                } else {
                    customSwatch(cx, cy);
                }
            }
        }

        void zoomStep(float x, float y, float w) {
            const std::vector<int>& zooms = Appearance::zooms();
            float cellW = (w - CHIP_GAP * (float) (ZOOM_COLUMNS - 1)) / (float) ZOOM_COLUMNS;
            float r = CONTROL_H * 0.5f;
            for (size_t i = 0; i < zooms.size(); i++) {
                float cx = x + (float) (i % ZOOM_COLUMNS) * (cellW + CHIP_GAP);
                float cy = y + (float) (i / ZOOM_COLUMNS) * (CONTROL_H + CHIP_GAP);
                bool on = zooms[i] == Appearance::zoom();
                std::string id = "onboarding#zoom#" + std::to_string(zooms[i]);
                bool hovered = !on && NewUi::hit(cx, cy, cellW, CONTROL_H);
                float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 16.0f);
                float sel = UiState::motion(id + "#on", on ? 1.0f : 0.0f, 16.0f);
                UiRender::roundRect(cx, cy, cellW, CONTROL_H, r,
                                    UiRender::mix(UiTheme::surfaceRaised(), UiTheme::accent(), sel));
                if (lit > 0.001f) {
                    UiRender::roundRect(cx, cy, cellW, CONTROL_H, r, UiTheme::overlay(Ui::round(14.0f * lit)));
                }
                UiRender::outline(cx, cy, cellW, CONTROL_H, r,
                                  UiRender::mix(UiTheme::border(UiTheme::outlineSoft()), UiTheme::accent(), sel));
                uint32_t rest = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
                Page::centredText(std::to_string(zooms[i]) + "%", cx + cellW * 0.5f, cy, CONTROL_H,
                                  UiRender::mix(rest, UiTheme::onAccent(), sel), UiWidgets::TEXT_CONTROL);
                if (!on && NewUi::click(cx, cy, cellW, CONTROL_H)) {
                    UiSound::request(UiSound::TICK);
                    Appearance::setZoom(zooms[i]);
                    kick(0.5f);
                }
            }
        }

        void backgroundStep(float x, float y, float w) {
            auto flag = std::make_shared<FlagBinding>(
                    "onboarding#background", [] { return Appearance::backgroundImage(); },
                    [](bool on) { Appearance::setBackgroundImage(on); });
            UiRender::textY("显示背景图片", x, y, CONTROL_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            ToggleWidget toggle("onboarding#background", flag, true);
            toggle.bounds(x + w - TOGGLE_W, y + (CONTROL_H - TOGGLE_H) * 0.5f, TOGGLE_W, TOGGLE_H)
                    .hitArea(x, y, w, CONTROL_H);
            toggle.paint();
            if (!Appearance::backgroundImage()) {
                return;
            }

            float rowY = y + CONTROL_H + ROW_GAP;
            std::string path = Background::path();
            float buttonY = rowY + (CONTROL_H - ICON_BUTTON) * 0.5f;
            float pickX = x + w - ICON_BUTTON;
            float fieldRight = pickX - 8.0f;
            if (!path.empty()) {
                float resetX = pickX - 4.0f - ICON_BUTTON;
                if (Page::iconButton("onboarding#bgReset", 0xf2ea, "恢复默认图片", resetX, buttonY)) {
                    Background::setPath(std::string());
                    path.clear();
                    kick(0.4f);
                }
                fieldRight = resetX - 8.0f;
            }
            UiRender::roundRect(x, rowY, fieldRight - x, CONTROL_H, 9.0f, UiTheme::surfaceSunken());
            UiRender::outline(x, rowY, fieldRight - x, CONTROL_H, 9.0f, UiTheme::border(UiTheme::outlineSoft()));
            float textRight = fieldRight - 12.0f;
            if (Page::iconButton("onboarding#bgPick", 0xf07c, "选择图片", pickX, buttonY)) {
                std::filesystem::path start =
                        path.empty() ? Platform::userDirectory() : b_launcher_base::path_from_utf8(path);
                if (std::optional<std::filesystem::path> picked = Platform::pickImage(start)) {
                    Background::setPath(b_launcher_base::path_to_utf8(*picked));
                    path = Background::path();
                }
            }
            std::string name = path.empty() ? std::string(Background::themeImage() ? "主题图片" : "默认图片")
                                            : Platform::display(b_launcher_base::path_from_utf8(path));
            name = UiFont::fitStart(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                    std::max(0.0f, textRight - (x + 12.0f)));
            UiRender::textY(name, x + 12.0f, rowY, CONTROL_H,
                            path.empty() ? UiTheme::textFaint() : UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void widgetRow(const HomeWidgets::WidgetKind& kind, float x, float y, float w, bool last) {
            std::string id = "onboarding#home#" + std::string(kind.id);
            std::string widget = kind.id;
            bool hovered = NewUi::hit(x, y, w, LIST_ROW);
            float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 16.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(x + 4.0f, y + 2.0f, w - 8.0f, LIST_ROW - 4.0f, 7.0f,
                                    UiTheme::overlay(Ui::round(12.0f * lit)));
            }
            UiWidgets::icon(Ui::glyph(kind.glyph), x + 22.0f, y + LIST_ROW * 0.5f, UiTheme::textMuted(), 10.0f);
            UiRender::textY(kind.label, x + 38.0f, y, LIST_ROW, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            auto flag = std::make_shared<FlagBinding>(
                    id + "#on", [widget] { return HomeWidgets::enabled(widget); },
                    [widget](bool on) {
                        HomeWidgets::setWidgetEnabled(widget, on);
                        kick(0.45f);
                    });
            ToggleWidget toggle(id + "#on", flag, true);
            toggle.bounds(x + w - 12.0f - TOGGLE_W, y + (LIST_ROW - TOGGLE_H) * 0.5f, TOGGLE_W, TOGGLE_H)
                    .hitArea(x, y, w, LIST_ROW);
            toggle.paint();
            if (!last) {
                UiRender::hLine(x + 12.0f, x + w - 12.0f, y + LIST_ROW, UiTheme::rowDivider(UiTheme::divider()));
            }
        }

        void homeStep(float x, float y, float w) {
            if (Homepage::source() != Homepage::SOURCE_NONE) {
                std::string hint = "当前主页：" + Homepage::choice() + "。切换后可使用主页组件。";
                float lineY = y;
                for (const std::string& line : UiFont::wrap(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w)) {
                    UiRender::textY(line, x, lineY, HINT_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    lineY += HINT_H;
                }
                std::string use = "使用主页组件";
                if (action("onboarding#useWidgets", use, x, lineY + ROW_GAP, actionWidth(use, 0xf00a), true, 0xf00a)) {
                    Homepage::choose("无");
                    kick(0.45f);
                }
                return;
            }
            const std::vector<HomeWidgets::WidgetKind>& kinds = HomeWidgets::widgetKinds();
            float cardH = LIST_ROW * (float) kinds.size();
            UiRender::roundRect(x, y, w, cardH, 10.0f, UiTheme::surfaceRaised());
            UiRender::outline(x, y, w, cardH, 10.0f, UiTheme::border(UiTheme::outlineSoft()));
            for (size_t i = 0; i < kinds.size(); i++) {
                widgetRow(kinds[i], x, y + LIST_ROW * (float) i, w, i + 1 == kinds.size());
            }
        }

        void preview(float x, float y, float w, float h, float alpha, float glow) {
            if (alpha <= 0.002f) {
                return;
            }
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * alpha;
            float radius = MainWindow::miniatureRadius(w, h);
            UiRender::shadow(x, y, w, h, radius, 10.0f, 4.0f, UiTheme::shadow());
            MainWindow::miniature(x, y, w, h);
            UiRender::outline(x, y, w, h, radius, UiTheme::border(UiTheme::outlineSoft()));
            if (glow > 0.01f) {
                UiRender::outline(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, radius + 2.0f,
                                  withAlpha(UiTheme::accent(), 0.18f * glow));
            }
            UiRender::alphaMul = savedAlpha;
        }

        void footer(float x, float right, float y) {
            bool last = step == STEPS - 1;
            std::string next = last ? "完成" : "下一步";
            int nextGlyph = last ? 0xf00c : 0xf061;
            float nextW = std::max(108.0f, actionWidth(next, nextGlyph));
            if (action("onboarding#next", next, right - nextW, y, nextW, true, nextGlyph)) {
                if (last) {
                    complete();
                } else {
                    go(step + 1);
                }
            }
            std::string back = "上一步";
            float backW = std::max(96.0f, actionWidth(back, 0xf060));
            if (step > 0 && action("onboarding#back", back, right - nextW - 10.0f - backW, y, backW, false, 0xf060)) {
                go(step - 1);
            }
            std::string skip = "跳过";
            if (!last && action("onboarding#skip", skip, x, y, std::max(72.0f, actionWidth(skip, 0)), false, 0)) {
                complete();
            }
        }
    }

    bool drawing() {
        return shown;
    }

    bool suggested() {
        static bool checked = false;
        if (!checked) {
            checked = true;
            if (Config::fresh() && !b_launcher_base::json_bool(Config::data(), PENDING_KEY)) {
                Config::data()[PENDING_KEY] = true;
                Config::save();
            }
        }
        return b_launcher_base::json_bool(Config::data(), PENDING_KEY);
    }

    void dismiss() {
        if (b_launcher_base::json_bool(Config::data(), PENDING_KEY)) {
            Config::data()[PENDING_KEY] = false;
            Config::save();
        }
    }

    void start() {
        dismiss();
        shown = true;
        leaving = false;
        outro = false;
        shownAt = 0.0;
        outroAt = 0.0;
        step = STEP_THEME;
        direction = 1.0f;
        pageMix = 1.0f;
        pulse = 0.0f;
        UiState::remove("onboarding#progress");
    }

    bool covering() {
        return shown && !leaving;
    }

    void draw() {
        if (!drawing()) {
            return;
        }
        double now = ImGui::GetTime();
        if (shownAt <= 0.0) {
            shownAt = now;
        }
        float w = MainWindow::width();
        float h = MainWindow::height();
        float savedAlpha = UiRender::alphaMul;
        if (leaving) {
            float remain = 1.0f - (float) ((now - leaveAt) / (double) LEAVE_SECONDS);
            if (remain <= 0.001f) {
                shown = false;
                leaving = false;
                return;
            }
            UiRender::alphaMul = savedAlpha * remain;
            UiRender::roundRect(0.0f, 0.0f, w, h, 14.0f, UiTheme::shell());
            UiRender::alphaMul = savedAlpha;
            UiState::keepAnimating();
            return;
        }

        UiRender::roundRect(0.0f, 0.0f, w, h, 14.0f, UiTheme::shell());

        if ((float) (now - shownAt) < INTRO_HOLD) {
            splash(w, h, (float) (now - shownAt));
            UiState::keepAnimating();
            return;
        }
        if (outro) {
            float elapsed = (float) (now - outroAt);
            if (elapsed >= INTRO_HOLD) {
                outro = false;
                leaving = true;
                leaveAt = now;
                return;
            }
            splash(w, h, elapsed);
            UiState::keepAnimating();
            return;
        }
        // The staggered entrance reads its own clock, so keep frames coming until it lands.
        if (now - shownAt < (double) (INTRO_HOLD + 0.23f + ENTER_SECONDS)) {
            UiState::keepAnimating();
        }

        pulse = UiRender::approach(pulse, 0.0f, 5.0f);
        if (pulse > 0.01f) {
            UiState::keepAnimating();
        }

        float pad = std::clamp(w * 0.06f, 32.0f, 64.0f);
        float colW = std::min(w - 2.0f * pad, CONTENT_MAX);
        float colX = std::round((w - colW) * 0.5f);
        float right = colX + colW;
        float leftW = std::round(std::clamp(colW * 0.38f, LEFT_MIN, LEFT_MAX));
        float previewX = colX + leftW + COLUMN_GAP;
        float previewW = right - previewX;

        float footerY = h - FOOTER_MARGIN - CONTROL_H;
        float bodyTop = STEPPER_Y + STEPPER_H + BODY_MARGIN;
        float bodyBottom = footerY - BODY_MARGIN;
        float previewH = std::max(120.0f, std::min(previewW / PREVIEW_RATIO, bodyBottom - bodyTop));
        float top = std::round(bodyTop + std::max(0.0f, (bodyBottom - bodyTop - previewH) * 0.5f));
        float titleY = top - 4.0f;
        float hintY = titleY + TITLE_H + HINT_GAP;
        float controlsY = hintY + HINT_H + CONTROLS_GAP;

        pageMix = UiRender::approach(pageMix, 1.0f, 16.0f);
        if (pageMix < 0.999f) {
            UiState::keepAnimating();
        }
        float eased = easeOut(pageMix);
        float pageShift = direction * 24.0f * (1.0f - eased) * UiRender::uiScale;
        float pageRise = (1.0f - eased) * 7.0f;

        auto group = [&](float delay, float amount, float shift, float rise, auto&& body) {
            float a = enter(delay);
            float sa = UiRender::alphaMul;
            float sx = UiRender::offsetX;
            float sy = UiRender::offsetY;
            UiRender::alphaMul = sa * a * amount;
            UiRender::offsetX = sx + shift;
            UiRender::offsetY = sy + ((1.0f - a) * 7.0f + rise) * UiRender::uiScale;
            body();
            UiRender::alphaMul = sa;
            UiRender::offsetX = sx;
            UiRender::offsetY = sy;
        };

        group(0.00f, 1.0f, 0.0f, 0.0f, [&] { stepper(colX, STEPPER_Y, colW); });
        preview(previewX, top + (1.0f - enter(0.08f)) * 12.0f, previewW, previewH, enter(0.08f), pulse);

        group(0.06f, pageMix, pageShift, pageRise, [&] {
            UiRender::textY(TITLES[step], colX, titleY, TITLE_H, UiTheme::textPrimary(), UiFont::MEDIUM, 22.0f);
        });
        group(0.11f, pageMix, pageShift, pageRise, [&] {
            UiRender::textY(UiFont::fitEnd(HINTS[step], UiFont::MEDIUM, UiWidgets::TEXT_BODY, leftW), colX, hintY,
                            HINT_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        });
        group(0.17f, pageMix, pageShift, pageRise, [&] {
            switch (step) {
                case STEP_THEME: themeStep(colX, controlsY); break;
                case STEP_ZOOM: zoomStep(colX, controlsY, leftW); break;
                case STEP_BACKGROUND: backgroundStep(colX, controlsY, leftW); break;
                default: homeStep(colX, controlsY, leftW); break;
            }
        });
        group(0.23f, 1.0f, 0.0f, 0.0f, [&] { footer(colX, right, footerY); });

        UiRender::alphaMul = savedAlpha;

        if (NewUi::popupOpen || NewUi::pickerColor != nullptr || leaving) {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (step == STEPS - 1) {
                complete();
            } else {
                go(step + 1);
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            go(step - 1);
        }
    }
}
