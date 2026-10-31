#include "ui/imgui/screens/launch/LaunchButton.h"
#include "app/accounts/Accounts.h"
#include "app/launch/Game.h"
#include "app/instances/Instances.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace LaunchButton {

    namespace {
        constexpr float RADIUS = 14.0f;
        constexpr float STATUS_MAX_W = 520.0f;
        constexpr float TAU = 6.28318531f;
        constexpr float RING = 11.0f;
        constexpr float STROKE = 2.6f;

        float pivotX(float x) {
            return x * UiRender::uiScale + UiRender::offsetX;
        }

        float pivotY(float y) {
            return y * UiRender::uiScale + UiRender::offsetY;
        }

        template <typename Paint>
        void morph(float cx, float cy, float amount, Paint paint) {
            if (amount < 0.01f) {
                return;
            }
            float savedAlpha = UiRender::alphaMul;
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            UiRender::alphaMul = savedAlpha * amount;
            UiRender::scaleFactor = savedScale * UiRender::lerp(0.4f, 1.0f, amount);
            UiRender::pivotX = pivotX(cx);
            UiRender::pivotY = pivotY(cy);
            paint();
            UiRender::alphaMul = savedAlpha;
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedPivotX;
            UiRender::pivotY = savedPivotY;
        }

        void ring(float cx, float cy, float from, float to, uint32_t color) {
            ShaderUtil::arc(UiRender::transformX(cx), UiRender::transformY(cy), UiRender::transformW(RING),
                            UiRender::transformW(STROKE), from, to, UiRender::applyAlpha(color));
        }

        void progress(float cx, float cy, uint32_t color, float value) {
            float known = UiState::motion("launch#known", value >= 0.0f ? 1.0f : 0.0f, 8.0f);
            float shown = UiState::motion("launch#fraction", std::max(0.0f, value), 10.0f);
            ring(cx, cy, 0.0f, TAU, (color & 0x00FFFFFFu) | 0x47000000u);
            float spinFrom = 0.0f;
            float spinTo = 0.0f;
            Page::spinnerArc(spinFrom, spinTo);
            float from = UiRender::lerp(spinFrom - TAU * 0.25f, -TAU * 0.25f, known);
            float to = UiRender::lerp(spinTo - TAU * 0.25f, -TAU * 0.25f + std::max(0.02f, shown) * TAU, known);
            ring(cx, cy, from, to, color);
        }

        constexpr float CARD_RADIUS = 12.0f;
        constexpr float STEP_H = 26.0f;
        constexpr float CARD_PAD = 8.0f;
        constexpr float CARD_BLUR = 12.0f;

        void marker(Game::Step::Status status, float cx, float cy) {
            float x = UiRender::transformX(cx);
            float y = UiRender::transformY(cy);
            switch (status) {
                case Game::Step::Status::Waiting:
                    ShaderUtil::arc(x, y, UiRender::transformW(4.5f), UiRender::transformW(1.4f), 0.0f, TAU,
                                    UiRender::applyAlpha(UiTheme::textFaint()));
                    break;
                case Game::Step::Status::Running: {
                    float from = 0.0f;
                    float to = 0.0f;
                    Page::spinnerArc(from, to);
                    ShaderUtil::arc(x, y, UiRender::transformW(4.5f), UiRender::transformW(1.6f), 0.0f, TAU,
                                    UiRender::applyAlpha((UiTheme::accent() & 0x00FFFFFFu) | 0x40000000u));
                    ShaderUtil::arc(x, y, UiRender::transformW(4.5f), UiRender::transformW(1.6f), from, to,
                                    UiRender::applyAlpha(UiTheme::accent()));
                    break;
                }
                case Game::Step::Status::Done: UiWidgets::check(cx, cy, UiTheme::accent(), 9.0f); break;
                case Game::Step::Status::Failed:
                    UiWidgets::icon(Ui::glyph(0xf00d), cx, cy, UiTheme::danger(), 9.0f);
                    break;
            }
        }

        constexpr float GAP = 12.0f;
        constexpr float MENU_W = 220.0f;
        constexpr float MENU_ROW = 32.0f;

        std::string hint(Game::State state, const Instances::Entry* entry, bool signedOut, size_t running) {
            std::string name = entry != nullptr ? entry->info.id : std::string();
            switch (state) {
                case Game::State::Idle:
                    if (signedOut) {
                        return "需要先登录";
                    }
                    return name.empty() ? std::string("没有可启动的实例") : "启动 " + name;
                case Game::State::Preparing: return "取消启动";
                case Game::State::Running:
                    return running > 1 ? "结束 " + name + "（" + std::to_string(running) + " 个在运行）"
                                       : "结束 " + name;
            }
            return std::string();
        }

        std::string duration(double seconds) {
            int total = (int) seconds;
            char text[24];
            if (total >= 3600) {
                std::snprintf(text, sizeof(text), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
            } else {
                std::snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
            }
            return text;
        }

        void tip(const std::string& text, float right, float y) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
            NewUi::tooltipText = text;
            NewUi::tooltipX = right - tipW;
            NewUi::tooltipY = y - 34.0f;
        }

        void stopMenu(const std::string& key, float right, float top) {
            size_t count = Game::sessionsOf(key).size() + 1;
            NewUi::openMenu(
                    "launch#stop#menu",
                    [key] {
                        MenuItems items;
                        for (const Game::Session& session : Game::sessionsOf(key)) {
                            if (session.state != Game::State::Running) {
                                continue;
                            }
                            int id = session.id;
                            items.push_back(ActionMenuItem::make("结束 " + session.account, 0xf04d, [id] {
                                                Game::stop(id);
                                            })->keys("已运行 " + duration(session.uptime)));
                        }
                        items.push_back(
                                ActionMenuItem::make("全部结束", 0xf00d, [key] { Game::stop(key); })->dangerous());
                        return items;
                    },
                    right - MENU_W, top - 6.0f - count * MENU_ROW - 8.0f, MENU_W);
        }

        std::vector<const b_launcher_base::auth::account*> suitable(const Instances::Entry& entry) {
            std::vector<const b_launcher_base::auth::account*> out;
            for (const b_launcher_base::auth::account& account : Accounts::list()) {
                if (Accounts::suits(account, entry.info.settings.login, entry.info.settings.auth_server)) {
                    out.push_back(&account);
                }
            }
            return out;
        }

        void launchMenu(const Instances::Entry& entry, float left, float top) {
            std::string key = entry.key;
            size_t count = suitable(entry).size() + 1;
            NewUi::openMenu(
                    "launch#more#menu",
                    [key] {
                        MenuItems items;
                        const Instances::Entry* found = Instances::find(key);
                        if (found == nullptr) {
                            return items;
                        }
                        for (const b_launcher_base::auth::account* account : suitable(*found)) {
                            std::string accountKey = Accounts::keyOf(*account);
                            std::shared_ptr<ActionMenuItem> item =
                                    ActionMenuItem::make(account->name, 0xf007,
                                                         [key, accountKey] { Game::launch(key, false, accountKey); })
                                            ->keys(Game::inUse(accountKey) ? std::string("使用中")
                                                                           : Accounts::kindOf(*account));
                            uint32_t face = Textures::of(Accounts::face(*account));
                            if (face != 0) {
                                item->image(face, 4.0f);
                            }
                            items.push_back(item);
                        }
                        items.push_back(ActionMenuItem::make("添加账号", 0xf234, [] {
                            MainWindow::showAccounts();
                            AccountsScreen::openAdd();
                        }));
                        return items;
                    },
                    left, top - 6.0f - count * MENU_ROW - 8.0f, MENU_W);
        }

        float moreButton(const Instances::Entry& entry, float right, float y, float shown) {
            if (shown < 0.01f) {
                return right;
            }
            const float size = SIZE;
            float x = right - size;
            float cx = x + size * 0.5f;
            float cy = y + size * 0.5f;
            bool hovered = shown > 0.99f && !NewUi::overlayCoversMouse() && NewUi::hit(x, y, size, size);
            float hover = UiState::motion("launch#more#hover", hovered ? 1.0f : 0.0f, 18.0f);
            float press = UiState::motion("launch#more#press", hovered && NewUi::mouseDown ? 1.0f : 0.0f, 26.0f);
            float savedAlpha = UiRender::alphaMul;
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            UiRender::pivotX = pivotX(cx);
            UiRender::pivotY = pivotY(cy);
            UiRender::scaleFactor = savedScale * UiRender::lerp(0.6f, 1.0f, shown) * UiRender::lerp(1.0f, 0.94f, press);
            UiRender::alphaMul = savedAlpha * shown;
            UiRender::shadow(x, y, size, size, RADIUS, 12.0f, 4.0f, UiTheme::shadow());
            UiRender::roundRect(x, y, size, size, RADIUS,
                                UiRender::mix(UiTheme::surface(), UiTheme::surfaceRaised(), hover));
            UiRender::outline(x, y, size, size, RADIUS, UiTheme::outlineSoft());
            UiWidgets::icon(Ui::glyph(0xf067), cx, cy,
                            UiRender::mix(UiTheme::textSecondary(), UiTheme::accentText(), hover), 16.0f);
            UiRender::alphaMul = savedAlpha;
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedPivotX;
            UiRender::pivotY = savedPivotY;
            if (hovered) {
                tip("再开一个 " + entry.info.id, right, y);
            }
            if (shown > 0.99f && NewUi::click(x, y, size, size)) {
                UiSound::request(UiSound::TICK);
                launchMenu(entry, x, y);
            }
            return right - (size + GAP) * shown;
        }
    }

    void drawStatus(float right, float bottom) {
        static std::vector<Game::Step> shown;
        std::string key = Instances::selected();
        bool preparing = Game::state(key) == Game::State::Preparing;
        if (preparing) {
            shown = Game::steps(key);
        }
        float visible = UiState::motion("launch#status", preparing ? 1.0f : 0.0f, 12.0f);
        if (visible < 0.01f || shown.empty()) {
            return;
        }
        float x = std::max(Page::left(), right - STATUS_MAX_W);
        float w = right - x;
        float h = shown.size() * STEP_H + CARD_PAD * 2.0f;
        float rise = visible > 0.995f ? 0.0f : (1.0f - visible) * 10.0f;
        float y = bottom - h + rise;
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * visible;

        uint32_t blurTint = ((uint32_t) Ui::round(255.0f * UiRender::alphaMul) << 24) | 0x00FFFFFFu;
        ShaderUtil::queueBlur(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(w),
                              UiRender::transformW(h), UiRender::transformW(CARD_RADIUS), CARD_BLUR, blurTint, 0xF,
                              6.0f);
        ShaderUtil::flushBlur();
        float fillAlpha = UiTheme::isLight() ? 0.60f : 0.55f;
        UiRender::roundRect(x, y, w, h, CARD_RADIUS,
                            (UiTheme::surface() & 0x00FFFFFFu) | ((uint32_t) Ui::round(255.0f * fillAlpha) << 24));

        float rowY = y + CARD_PAD;
        for (const Game::Step& step : shown) {
            float cy = rowY + STEP_H * 0.5f;
            marker(step.status, x + 24.0f, cy);
            uint32_t tint = step.status == Game::Step::Status::Running ? UiTheme::textPrimary()
                    : step.status == Game::Step::Status::Waiting       ? UiTheme::textMuted()
                                                                       : UiTheme::textSecondary();
            UiRender::textY(step.label, x + 40.0f, rowY, STEP_H, tint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (!step.detail.empty()) {
                float detailW = UiFont::width(step.detail, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(step.detail, x + w - 16.0f - detailW, rowY, STEP_H, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            rowY += STEP_H;
        }
        UiRender::alphaMul = savedAlpha;
    }

    void draw(float x, float y) {
        const float size = SIZE;
        std::string key = Instances::selected();
        Game::State state = Game::state(key);
        const Instances::Entry* entry = Instances::find(key);
        size_t running = 0;
        for (const Game::Session& session : Game::sessionsOf(key)) {
            running += session.state == Game::State::Running ? 1 : 0;
        }
        bool signedOut = state == Game::State::Idle && Accounts::current() == nullptr;
        bool usable = state != Game::State::Idle || signedOut || (entry != nullptr && entry->info.valid);
        bool hovered = !NewUi::overlayCoversMouse() && NewUi::hit(x, y, size, size);
        float hover = UiState::motion("launch#hover", usable && hovered ? 1.0f : 0.0f, 18.0f);
        float press = UiState::motion("launch#press", usable && hovered && NewUi::mouseDown ? 1.0f : 0.0f, 26.0f);
        float play = UiState::motion("launch#play", state == Game::State::Idle && !signedOut ? 1.0f : 0.0f, 14.0f);
        float login = UiState::motion("launch#login", signedOut ? 1.0f : 0.0f, 14.0f);
        float busy = UiState::motion("launch#busy", state == Game::State::Preparing ? 1.0f : 0.0f, 14.0f);
        float stop = UiState::motion("launch#stop", state == Game::State::Running ? 1.0f : 0.0f, 14.0f);
        float enabled = UiState::motion("launch#enabled", usable ? 1.0f : 0.0f, 14.0f);
        float more =
                UiState::motion("launch#more", state == Game::State::Running && entry != nullptr ? 1.0f : 0.0f, 14.0f);

        float cx = x + size * 0.5f;
        float cy = y + size * 0.5f;
        float savedAlpha = UiRender::alphaMul;
        float savedScale = UiRender::scaleFactor;
        float savedPivotX = UiRender::pivotX;
        float savedPivotY = UiRender::pivotY;
        UiRender::pivotX = pivotX(cx);
        UiRender::pivotY = pivotY(cy);
        UiRender::scaleFactor = savedScale * UiRender::lerp(1.0f, 0.94f, press);
        UiRender::alphaMul = savedAlpha * UiRender::lerp(0.45f, 1.0f, enabled);

        uint32_t fill = UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.10f * hover);
        UiRender::shadow(x, y, size, size, RADIUS, 12.0f, 4.0f, UiTheme::shadow());
        UiRender::roundRect(x, y, size, size, RADIUS, fill);
        uint32_t ink = UiTheme::onAccent();
        float fraction = Game::progress(key);
        morph(cx + 1.5f, cy, play, [&] { UiWidgets::icon(Ui::glyph(0xf04b), cx + 1.5f, cy, ink, 17.0f); });
        morph(cx, cy, login, [&] { UiWidgets::icon(Ui::glyph(0xf090), cx, cy, ink, 16.0f); });
        morph(cx, cy, busy, [&] { progress(cx, cy, ink, fraction); });
        morph(cx, cy, stop, [&] { UiWidgets::icon(Ui::glyph(0xf04d), cx, cy, ink, 15.0f); });

        UiRender::alphaMul = savedAlpha;
        UiRender::scaleFactor = savedScale;
        UiRender::pivotX = savedPivotX;
        UiRender::pivotY = savedPivotY;

        float left = x - GAP;
        if (entry != nullptr) {
            moreButton(*entry, left, y, more);
        }

        if (hovered) {
            tip(hint(state, entry, signedOut, running), x + size, y);
        }
        if (usable && NewUi::click(x, y, size, size)) {
            UiSound::request(state == Game::State::Idle ? (signedOut ? UiSound::PAGE : UiSound::PULSE)
                                                        : UiSound::RELEASE);
            switch (state) {
                case Game::State::Idle:
                    if (signedOut) {
                        MainWindow::showAccounts();
                    } else {
                        Game::launch(key);
                    }
                    break;
                case Game::State::Preparing: Game::cancel(key); break;
                case Game::State::Running:
                    if (running > 1) {
                        stopMenu(key, x + size, y);
                    } else {
                        Game::stop(key);
                    }
                    break;
            }
        }
    }
}
