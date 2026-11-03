#include "ui/imgui/screens/onboarding/Onboarding.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
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

        constexpr const char* DONE_KEY = "onboardingDone";
        constexpr float LEAVE_SECONDS = 0.18f;
        constexpr float INTRO_HOLD = 1.0f;
        constexpr float INTRO_OUT = 0.18f;
        constexpr float ENTER_SECONDS = 0.40f;

        constexpr float CONTENT_MAX = 720.0f;
        constexpr float PROGRESS_Y = 52.0f;
        constexpr float TITLE_Y = 70.0f;
        constexpr float HINT_Y = 106.0f;
        constexpr float PREVIEW_Y = 138.0f;
        constexpr float BLOCK_H[STEPS] = {34.0f, 36.0f, 80.0f, 78.0f};

        const char* const TITLES[STEPS] = {"主题色", "界面缩放", "背景", "主页组件"};
        const char* const HINTS[STEPS] = {
                "改主题颜色，让你的启动器丰富多彩。",
                "字小就调大，窗口会按比例一起放大。",
                "设置启动器背景图片",
                "你的启动器，你来定义",
        };

        const uint32_t ACCENTS[] = {
                0xFF4F7DFFu, 0xFF8E6BFFu, 0xFFE45C8Au, 0xFFE2683Cu, 0xFFE3B341u,
                0xFF43A96Bu, 0xFF2AA9A0u, 0xFF3B9AE1u, 0xFF8A8F98u,
        };

        bool checked = false;
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

        uint32_t inkOn(uint32_t argb) {
            float r = (float) ((argb >> 16) & 0xFF) / 255.0f;
            float g = (float) ((argb >> 8) & 0xFF) / 255.0f;
            float b = (float) (argb & 0xFF) / 255.0f;
            return 0.2126f * r + 0.7152f * g + 0.0722f * b > 0.62f ? 0xFF16181Du : 0xFFFFFFFFu;
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

        bool action(const std::string& id, const std::string& text, float x, float y, float w, bool primary,
                    int glyph) {
            const float h = 32.0f;
            bool hovered = NewUi::hit(x, y, w, h);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 16.0f);
            float press = NewUi::mouseDown && hovered ? 1.0f : 0.0f;
            float push = UiState::motion(id + "#press", press, 26.0f);
            float dy = push * 1.0f;
            uint32_t ink;
            if (primary) {
                UiRender::roundRect(x, y + dy, w, h, 9.0f,
                                    UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.10f * lit + 0.05f * push));
                ink = UiTheme::onAccent();
            } else {
                UiRender::roundRect(x, y + dy, w, h, 9.0f, UiTheme::surfaceRaised());
                if (lit > 0.001f) {
                    UiRender::roundRect(x, y + dy, w, h, 9.0f, UiTheme::overlay(Ui::round(14.0f * lit)));
                }
                UiRender::outline(x, y + dy, w, h, 9.0f, UiTheme::outlineSoft());
                ink = UiTheme::textPrimary();
            }
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (glyph != 0) {
                float contentW = 10.0f + 6.0f + textW;
                float start = x + (w - contentW) * 0.5f + lit * 1.5f;
                UiWidgets::icon(Ui::glyph(glyph), start + 5.0f, y + dy + h * 0.5f, ink, 10.0f);
                UiRender::textY(text, start + 16.0f, y + dy, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            } else {
                UiRender::textY(text, x + (w - textW) * 0.5f, y + dy, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            if (NewUi::click(x, y, w, h)) {
                UiSound::request(primary ? UiSound::RELEASE : UiSound::TICK);
                return true;
            }
            return false;
        }

        void complete() {
            if (outro || leaving) {
                return;
            }
            Config::data()[DONE_KEY] = true;
            Config::save();
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
            return binding;
        }

        void progressBar(float x, float y, float w) {
            float amount = UiState::motion("onboarding#progress", (float) (step + 1) / (float) STEPS, 9.0f);
            UiRender::roundRect(x, y, w, 3.0f, 1.5f, UiTheme::overlay(16));
            if (amount <= 0.001f) {
                return;
            }
            float fill = w * amount;
            UiRender::roundRect(x, y, fill, 3.0f, 1.5f, UiTheme::accent());
            float headX = x + fill - 1.5f;
            UiRender::circle(headX, y + 1.5f, 6.0f, (UiTheme::accent() & 0x00FFFFFFu) | 0x33000000u);
            UiRender::circle(headX, y + 1.5f, 2.4f, UiTheme::accent());
        }

        void swatch(float cx, float cy, uint32_t color) {
            std::string id = "onboarding#accent#" + hexOf(color);
            bool hovered = NewUi::hit(cx - 15.0f, cy - 15.0f, 30.0f, 30.0f);
            float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 16.0f);
            float sel = UiState::motion(
                    id + "#sel",
                    (Appearance::accent() & 0x00FFFFFFu) == (color & 0x00FFFFFFu) ? 1.0f : 0.0f, 16.0f);
            if (lit > 0.001f) {
                UiRender::circle(cx, cy, 14.0f + 2.0f * lit, UiTheme::overlay(Ui::round(14.0f * lit)));
            }
            if (sel > 0.001f) {
                UiRender::circle(cx, cy, 12.0f + 2.5f * sel, (color & 0x00FFFFFFu) | (Ui::round(56.0f * sel) << 24));
            }
            UiRender::circle(cx, cy, 10.0f, 0xFF000000u | (color & 0x00FFFFFFu));
            if (sel > 0.5f) {
                UiWidgets::check(cx, cy, inkOn(color), 9.0f);
            }
            if (hovered) {
                Page::tooltip(hexOf(color), cx - 15.0f, cy + 16.0f, 30.0f, 0.0f);
            }
            if (NewUi::click(cx - 15.0f, cy - 15.0f, 30.0f, 30.0f)) {
                UiSound::request(UiSound::TICK);
                Appearance::setAccent(color);
                kick(0.6f);
            }
        }

        void customSwatch(float cx, float cy) {
            bool hovered = NewUi::hit(cx - 15.0f, cy - 15.0f, 30.0f, 30.0f);
            float lit = UiState::motion("onboarding#accent#custom", hovered ? 1.0f : 0.0f, 16.0f);
            if (lit > 0.001f) {
                UiRender::circle(cx, cy, 14.0f + 2.0f * lit, UiTheme::overlay(Ui::round(14.0f * lit)));
            }
            UiRender::circle(cx, cy, 10.0f, UiTheme::surfaceRaised());
            UiRender::outline(cx - 10.0f, cy - 10.0f, 20.0f, 20.0f, 10.0f, UiTheme::outline());
            UiWidgets::icon(Ui::glyph(0xf1fb), cx, cy, UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit),
                            9.0f);
            if (hovered) {
                Page::tooltip("自定义颜色", cx - 15.0f, cy + 16.0f, 30.0f, 0.0f);
            }
            if (NewUi::click(cx - 15.0f, cy - 15.0f, 30.0f, 30.0f)) {
                UiSound::request(UiSound::PAGE);
                NewUi::openPicker(customAccent(), cx + 22.0f, cy - 190.0f);
            }
        }

        void themeStep(float x, float y, float w) {
            float cy = y + 16.0f;
            float cx = x + 16.0f;
            for (uint32_t preset : ACCENTS) {
                swatch(cx, cy, preset);
                cx += 38.0f;
            }
            if (cx < x + w - 20.0f) {
                customSwatch(cx, cy);
            }
        }

        void zoomStep(float x, float y) {
            const std::vector<int>& zooms = Appearance::zooms();
            float segY = y;
            const float segH = 32.0f;
            float segW = 56.0f;
            for (int percent : zooms) {
                segW = std::max(segW, UiFont::width(std::to_string(percent) + "%", UiFont::MEDIUM,
                                                   UiWidgets::TEXT_CONTROL) +
                                              30.0f);
            }
            segW = std::min(segW, 84.0f);
            float total = segW * (float) zooms.size();
            int selected = 0;
            for (size_t i = 0; i < zooms.size(); i++) {
                if (zooms[i] == Appearance::zoom()) {
                    selected = (int) i;
                }
            }
            UiRender::roundRect(x, segY, total, segH, segH * 0.5f, UiTheme::surfaceSunken());
            UiRender::outline(x, segY, total, segH, segH * 0.5f, UiTheme::outlineSoft());
            float thumb = UiState::motion("onboarding#zoom#thumb", (float) selected, 14.0f);
            UiRender::roundRect(x + 3.0f + thumb * segW, segY + 3.0f, segW - 6.0f, segH - 6.0f, (segH - 6.0f) * 0.5f,
                                UiTheme::accent());
            for (size_t i = 0; i < zooms.size(); i++) {
                float sx = x + segW * (float) i;
                std::string text = std::to_string(zooms[i]) + "%";
                float near = std::max(0.0f, 1.0f - std::fabs(thumb - (float) i));
                uint32_t ink = UiRender::mix(UiTheme::textMuted(), UiTheme::onAccent(), near);
                Page::centredText(text, sx + segW * 0.5f, segY, segH, ink, UiWidgets::TEXT_CONTROL);
                if (NewUi::click(sx, segY, segW, segH)) {
                    UiSound::request(UiSound::TICK);
                    Appearance::setZoom(zooms[i]);
                    kick(0.5f);
                }
            }
        }

        void backgroundStep(float x, float y, float w) {
            auto flag = std::make_shared<FlagBinding>("onboarding#background",
                                                      [] { return Appearance::backgroundImage(); },
                                                      [](bool on) { Appearance::setBackgroundImage(on); });
            UiRender::textY("显示背景图片", x + 2.0f, y, 24.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            ToggleWidget toggle("onboarding#background", flag, true);
            toggle.bounds(x + w - 34.0f, y + 3.0f, 34.0f, 18.0f).hitArea(x, y, w, 26.0f);
            toggle.paint();
            if (!Appearance::backgroundImage()) {
                return;
            }

            float rowY = y + 42.0f;
            std::string path = Background::path();
            UiRender::textY("背景图片", x + 2.0f, rowY, 28.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float pickX = x + w - 28.0f;
            float right = pickX;
            if (!path.empty()) {
                if (Page::iconButton("onboarding#bgReset", 0xf2ea, "恢复默认图片", pickX - 32.0f, rowY)) {
                    Background::setPath(std::string());
                    path.clear();
                    kick(0.4f);
                }
                right = pickX - 34.0f;
            }
            if (Page::iconButton("onboarding#bgPick", 0xf07c, "选择图片", pickX, rowY)) {
                std::filesystem::path start =
                        path.empty() ? Platform::exeDirectory() : b_launcher_base::path_from_utf8(path);
                if (std::optional<std::filesystem::path> picked = Platform::pickImage(start)) {
                    Background::setPath(b_launcher_base::path_to_utf8(*picked));
                    path = Background::path();
                }
            }
            std::string name = path.empty() ? std::string("默认图片")
                                            : Platform::display(b_launcher_base::path_from_utf8(path));
            name = UiFont::fitStart(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                    std::max(0.0f, right - 8.0f - (x + 80.0f)));
            float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(name, right - 8.0f - nameW, rowY, 28.0f,
                            path.empty() ? UiTheme::textFaint() : UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void chip(const std::string& widgetId, int glyph, const std::string& text, float x, float y, float w,
                  float h, bool on) {
            std::string id = "onboarding#home#" + widgetId;
            bool hovered = NewUi::hit(x, y, w, h);
            float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 16.0f);
            float sel = UiState::motion(id + "#on", on ? 1.0f : 0.0f, 16.0f);
            UiRender::roundRect(x, y, w, h, h * 0.5f,
                                UiRender::mix(UiTheme::surfaceRaised(), UiTheme::accentSoft(34), sel));
            uint32_t ring = UiRender::mix(UiTheme::outlineSoft(), UiTheme::accent(), sel);
            if (lit > 0.001f) {
                ring = UiRender::mix(ring, UiTheme::textSecondary(), 0.3f * lit);
            }
            UiRender::outline(x, y, w, h, h * 0.5f, ring);
            uint32_t ink = UiRender::mix(UiTheme::textMuted(), UiTheme::accentText(), sel);
            UiWidgets::icon(Ui::glyph(glyph), x + 15.0f, y + h * 0.5f, ink, 10.0f);
            UiRender::textY(text, x + 25.0f, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (NewUi::click(x, y, w, h)) {
                UiSound::request(on ? UiSound::TICK : UiSound::RELEASE);
                HomeWidgets::setWidgetEnabled(widgetId, !on);
                kick(0.45f);
            }
        }

        void homeStep(float x, float y, float w) {
            const float h = 30.0f;
            float cursor = x;
            float rowY = y;
            for (const HomeWidgets::WidgetKind& kind : HomeWidgets::widgetKinds()) {
                std::string text = kind.label;
                float cw = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 45.0f;
                if (cursor > x && cursor + cw > x + w) {
                    cursor = x;
                    rowY += h + 8.0f;
                }
                chip(kind.id, kind.glyph, text, cursor, rowY, cw, h, HomeWidgets::enabled(kind.id));
                cursor += cw + 8.0f;
            }
        }

        void mockRow(const HomeWidgets::WidgetKind& kind, float x, float y, float w, float h, float alpha,
                     bool launch) {
            float saved = UiRender::alphaMul;
            UiRender::alphaMul = saved * alpha;
            UiRender::roundRect(x, y, w, h, 6.0f, launch ? UiTheme::accent() : UiTheme::surfaceRaised());
            if (!launch) {
                UiRender::outline(x, y, w, h, 6.0f, UiTheme::outlineSoft());
            }
            uint32_t ink = launch ? UiTheme::onAccent() : UiTheme::textSecondary();
            UiWidgets::icon(Ui::glyph(kind.glyph), x + 14.0f, y + h * 0.5f, ink, 10.0f);
            UiRender::textY(kind.label, x + 26.0f, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::alphaMul = saved;
        }

        void cover(float w, float h, float textureW, float textureH, float& uSpan, float& vSpan) {
            float scale = std::max(w / textureW, h / textureH);
            uSpan = w / (textureW * scale);
            vSpan = h / (textureH * scale);
        }

        void preview(float x, float y, float w, float h, float alpha, float glow) {
            const float radius = 14.0f;
            const float inset = 8.0f;
            if (alpha <= 0.002f) {
                return;
            }
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * alpha;

            UiRender::shadow(x, y, w, h, radius, 10.0f, 4.0f, UiTheme::shadow());
            UiRender::roundRect(x, y, w, h, radius, UiTheme::surface());
            UiRender::outline(x, y, w, h, radius, UiTheme::outlineSoft());
            if (glow > 0.01f) {
                UiRender::outline(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, radius + 2.0f,
                                  (UiTheme::accent() & 0x00FFFFFFu) | (Ui::round(46.0f * glow) << 24));
            }

            float sx = x + inset;
            float sy = y + inset;
            float sw = w - 2.0f * inset;
            float sh = h - 2.0f * inset;
            const float cr = 9.0f;
            float sideW = std::clamp(sw * 0.2f, 70.0f, 104.0f);
            float panelX = sx + sideW;
            float panelW = sw - sideW;
            uint32_t sideFill = UiTheme::sidebar();
            uint32_t panelFill = UiTheme::panel();

            bool image = Appearance::backgroundImage() && Background::sidebar() != 0 && Background::panel() != 0;
            if (image) {
                float uSpan = 1.0f;
                float vSpan = 1.0f;
                cover(sw, sh, MainWindow::MIN_WIDTH - MainWindow::SIDEBAR_WIDTH, MainWindow::MIN_HEIGHT, uSpan,
                      vSpan);
                ShaderUtil::roundTex(UiRender::transformX(sx), UiRender::transformY(sy), UiRender::transformW(sw),
                                     UiRender::transformW(sh), UiRender::transformW(cr), Background::panel(), 0.0f,
                                     (1.0f - vSpan) * 0.5f, uSpan, (1.0f + vSpan) * 0.5f, 0xFFFFFFFFu, 0xF);
                cover(sideW, sh, MainWindow::SIDEBAR_WIDTH + radius, MainWindow::MIN_HEIGHT, uSpan, vSpan);
                ShaderUtil::roundTex(UiRender::transformX(sx), UiRender::transformY(sy), UiRender::transformW(sideW),
                                     UiRender::transformW(sh), UiRender::transformW(cr), Background::sidebar(), 0.0f,
                                     (1.0f - vSpan) * 0.5f, uSpan, (1.0f + vSpan) * 0.5f, 0xFFFFFFFFu, 0xF);
                UiRender::roundRect(sx, sy, sw, sh, cr, UiTheme::isLight() ? 0x33000000u : 0x59000000u);
                UiRender::roundRect(sx, sy, sideW, sh, cr, (sideFill & 0x00FFFFFFu) | 0x4D000000u);
                UiRender::vLine(panelX, sy + 12.0f, sy + sh - 12.0f, UiTheme::divider());
            } else {
                UiRender::roundRect(sx, sy, sw, sh, cr, panelFill);
                UiRender::roundRect(sx, sy, sideW, sh, cr, sideFill);
            }

            UiRender::scissor(sx, sy, sw, sh);
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            float zoom = std::clamp((float) Appearance::zoom() / 100.0f, 0.8f, 1.3f);
            UiRender::scaleFactor = savedScale * zoom;
            UiRender::pivotX = (sx + sw * 0.5f) * UiRender::uiScale;
            UiRender::pivotY = (sy + sh * 0.5f) * UiRender::uiScale;

            uint32_t faint = (UiTheme::textFaint() & 0x00FFFFFFu) | 0x5A000000u;
            uint32_t soft = (UiTheme::textSecondary() & 0x00FFFFFFu) | 0xB4000000u;
            UiRender::circle(sx + 13.0f, sy + 13.0f, 4.0f, UiTheme::accent());
            float rowH = 12.0f;
            float navX = sx + 7.0f;
            float navW = sideW - 14.0f;
            for (int i = 0; i < 5; i++) {
                float ry = sy + 26.0f + (float) i * (rowH + 5.0f);
                if (ry + rowH > sy + sh - 4.0f) {
                    break;
                }
                bool current = i == 1;
                if (current) {
                    UiRender::roundRect(navX, ry, navW, rowH, 4.0f, UiTheme::accentSoft(40));
                }
                UiRender::circle(navX + 8.0f, ry + rowH * 0.5f, 1.7f, current ? UiTheme::accent() : faint);
                UiRender::roundRect(navX + 15.0f, ry + rowH * 0.5f - 1.5f, std::max(10.0f, navW * 0.46f), 3.0f, 1.5f,
                                    current ? soft : faint);
            }

            float midX = panelX + panelW * 0.5f;
            float colW = std::min(panelW * 0.68f, 240.0f);
            float colX = midX - colW * 0.5f;
            float gap = 6.0f;
            float avail = sh - 24.0f;
            float rowH2 = std::clamp((avail - gap * 4.0f) / 5.0f, 15.0f, 24.0f);

            std::vector<std::string> active;
            for (const HomeWidgets::WidgetKind& kind : HomeWidgets::widgetKinds()) {
                if (HomeWidgets::enabled(kind.id)) {
                    active.push_back(kind.id);
                }
            }
            float stack = active.empty()
                    ? 0.0f
                    : (float) active.size() * rowH2 + (float) (active.size() - 1) * gap;
            float top = sy + (sh - stack) * 0.5f;
            size_t index = 0;
            for (const HomeWidgets::WidgetKind& kind : HomeWidgets::widgetKinds()) {
                std::string id = "onboarding#pv#" + std::string(kind.id);
                bool on = HomeWidgets::enabled(kind.id);
                float vis = UiState::motion(id, on ? 1.0f : 0.0f, 20.0f);
                if (vis <= 0.01f) {
                    continue;
                }
                float target = on ? top + (float) index * (rowH2 + gap)
                                  : UiState::getOrDefault(id + "#y", top + (float) index * (rowH2 + gap));
                float at = UiState::motion(id + "#y", target, 18.0f);
                mockRow(kind, colX, at, colW, rowH2, vis, std::string(kind.id) == HomeWidgets::LAUNCH);
                if (on) {
                    index++;
                }
            }

            float empty = UiState::motion("onboarding#pv#empty", active.empty() ? 1.0f : 0.0f, 16.0f);
            if (empty > 0.01f) {
                float saved = UiRender::alphaMul;
                UiRender::alphaMul = saved * empty;
                UiRender::circle(midX, sy + sh * 0.5f - 8.0f, 12.0f, UiTheme::accentSoft(40));
                UiWidgets::icon(Ui::glyph(0xf04b), midX, sy + sh * 0.5f - 8.0f, UiTheme::accent(), 10.0f);
                Page::centredText("主页不显示组件", midX, sy + sh * 0.5f + 8.0f, 18.0f, UiTheme::textFaint(),
                                  UiWidgets::TEXT_SMALL);
                UiRender::alphaMul = saved;
            }

            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedPivotX;
            UiRender::pivotY = savedPivotY;
            UiRender::unscissor();
            UiRender::alphaMul = savedAlpha;
        }

        void footer(float x, float right, float y) {
            bool last = step == STEPS - 1;
            const float nextW = 104.0f;
            if (action("onboarding#next", last ? "完成" : "下一步", right - nextW, y, nextW, true,
                       last ? 0xf00c : 0xf061)) {
                if (last) {
                    complete();
                } else {
                    go(step + 1);
                }
            }
            if (step > 0 &&
                action("onboarding#back", "上一步", right - nextW - 10.0f - 92.0f, y, 92.0f, false, 0xf060)) {
                go(step - 1);
            }
            if (!last && action("onboarding#skip", "跳过", x, y, 68.0f, false, 0)) {
                complete();
            }
        }
    }

    bool drawing() {
        if (!checked) {
            checked = true;
            // Anything but a recorded completion counts as a first start, including
            // an install that stored settings before the wizard existed.
            shown = !b_launcher_base::json_bool(Config::data(), DONE_KEY);
        }
        return shown;
    }

    void start() {
        checked = true;
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
        UiState::remove("onboarding#zoom#thumb");
        UiState::remove("onboarding#pv#empty");
    }

    bool covering() {
        return drawing() && !leaving;
    }

    void remember() {
        if (!checked || !shown || leaving) {
            return;
        }
        // A repeated run that was abandoned keeps the earlier completion.
        if (!b_launcher_base::json_bool(Config::data(), DONE_KEY)) {
            Config::data()[DONE_KEY] = false;
            Config::save();
        }
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

        float pad = std::clamp(w * 0.06f, 28.0f, 64.0f);
        float colW = std::min(w - 2.0f * pad, CONTENT_MAX);
        float colX = std::round((w - colW) * 0.5f);
        float right = colX + colW;

        float previewH = std::clamp((h - PREVIEW_Y - 92.0f) * 0.52f, 118.0f, 340.0f);
        float footerY = h - 48.0f;
        float areaTop = PREVIEW_Y + previewH + 24.0f;
        float areaBottom = footerY - 22.0f;
        float blockY = areaTop + std::clamp((areaBottom - areaTop - BLOCK_H[step]) * 0.5f, 0.0f, 40.0f);

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

        group(0.00f, 1.0f, 0.0f, 0.0f, [&] { progressBar(colX, PROGRESS_Y, colW); });
        preview(colX, PREVIEW_Y + (1.0f - enter(0.08f)) * 12.0f, colW, previewH, enter(0.08f), pulse);

        group(0.06f, pageMix, pageShift, pageRise, [&] {
            UiRender::textY(TITLES[step], colX, TITLE_Y, 30.0f, UiTheme::textPrimary(), UiFont::MEDIUM, 22.0f);
            std::string counter = std::to_string(step + 1) + " / " + std::to_string(STEPS);
            float counterW = UiFont::width(counter, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(counter, right - counterW, TITLE_Y, 30.0f, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        });
        group(0.11f, pageMix, pageShift, pageRise, [&] {
            UiRender::textY(HINTS[step], colX, HINT_Y, 20.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
        });
        group(0.17f, pageMix, pageShift, pageRise, [&] {
            switch (step) {
                case STEP_THEME: themeStep(colX, blockY, colW); break;
                case STEP_ZOOM: zoomStep(colX, blockY); break;
                case STEP_BACKGROUND: backgroundStep(colX, blockY, colW); break;
                default: homeStep(colX, blockY, colW); break;
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
