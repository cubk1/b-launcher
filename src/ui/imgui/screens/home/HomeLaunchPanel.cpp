#include "ui/imgui/screens/home/HomeLaunchPanel.h"
#include "app/accounts/Accounts.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

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
#include <cstdio>
#include <string>

namespace HomeLaunchPanel {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float CARD_MAX = 560.0f;
        constexpr float ACCOUNT_H = 74.0f;
        constexpr float ACCOUNT_GAP = 14.0f;
        constexpr float LAUNCH_H = 80.0f;
        constexpr float SIDE_W = 120.0f;
        constexpr float SIDE_GAP = 10.0f;
        constexpr float SIDE_BTN = 35.0f;
        constexpr float RADIUS = 16.0f;
        constexpr float PAD = 18.0f;
        constexpr float FACE = 46.0f;
        constexpr float ACTION = 32.0f;
        constexpr float MENU_W = 200.0f;

        void picture(uint32_t texture, float x, float y, float size, float radius) {
            ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                 UiRender::transformW(size), UiRender::transformW(radius), texture, 0.0f, 0.0f, 1.0f,
                                 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        void letter(const std::string& name, float x, float y, float size) {
            UiRender::roundRect(x, y, size, size, 12.0f, UiTheme::surfaceRaised());
            std::string text = name.empty() ? "?" : std::string(1, (char) std::toupper((unsigned char) name[0]));
            float textW = UiFont::width(text, UiFont::MEDIUM, 18.0f);
            UiRender::textY(text, x + (size - textW) * 0.5f, y, size, UiTheme::textSecondary(), UiFont::MEDIUM,
                            18.0f);
        }

        void accountsMenu(float x, float y) {
            NewUi::openMenu(
                    "home#launch#accounts",
                    [] {
                        MenuItems items;
                        const blb::auth::account* current = Accounts::current();
                        std::string currentKey = current != nullptr ? Accounts::keyOf(*current) : std::string();
                        for (const blb::auth::account& account : Accounts::list()) {
                            std::string key = Accounts::keyOf(account);
                            std::shared_ptr<ActionMenuItem> item =
                                    ActionMenuItem::make(account.name, 0xf007, [key] { Accounts::select(key); })
                                            ->keys(Accounts::kindOf(account));
                            uint32_t face = Textures::of(Accounts::face(account));
                            if (face != 0) {
                                item->image(face, 4.0f);
                            }
                            if (key == currentKey) {
                                item->trailing(0xf00c);
                            }
                            items.push_back(item);
                        }
                        items.push_back(ActionMenuItem::make("添加账号", 0xf234, [] {
                            MainWindow::showAccounts();
                            AccountsScreen::openAdd();
                        }));
                        items.push_back(ActionMenuItem::make("管理账号", 0xf013, [] { MainWindow::showAccounts(); }));
                        return items;
                    },
                    x, y, MENU_W);
        }

        bool actionButton(const std::string& id, int glyph, const std::string& hint, float x, float y, float size,
                          bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, size, size);
            float hover = UiState::motion(id, hovered ? 1.0f : 0.0f, 18.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, size, size, 9.0f, UiTheme::overlay(Ui::round(16 * hover)));
            }
            UiWidgets::icon(Ui::glyph(glyph), x + size * 0.5f, y + size * 0.5f,
                            UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), hover), 14.0f);
            if (hovered && !NewUi::overlayCoversMouse()) {
                Page::tooltip(hint, x, y, size, size);
            }
            return !blocked && NewUi::click(x, y, size, size);
        }

        void accountCard(float x, float y, float w, bool blocked) {
            HomeWidgets::panel(x, y, w, ACCOUNT_H);
            const blb::auth::account* account = Accounts::current();
            float faceY = y + (ACCOUNT_H - FACE) * 0.5f;
            float faceX = x + PAD;
            float buttonY = y + (ACCOUNT_H - ACTION) * 0.5f;
            float switchX = x + w - PAD - ACTION;
            float refreshX = switchX - 10.0f - ACTION;
            float textX = faceX + FACE + 14.0f;
            float textRoom = std::max(0.0f, refreshX - 12.0f - textX);

            if (account == nullptr) {
                UiRender::roundRect(faceX, faceY, FACE, FACE, 12.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(0xf007), faceX + FACE * 0.5f, faceY + FACE * 0.5f, UiTheme::textMuted(),
                                FACE * 0.36f);
                UiRender::textY("未登录", textX, y + 14.0f, 22.0f, UiTheme::textPrimary(), UiFont::BOLD, 17.0f);
                UiRender::textY(UiFont::fitEnd("添加账号后即可启动游戏", UiFont::MEDIUM, 12.5f, textRoom), textX,
                                y + 40.0f, 18.0f, UiTheme::textMuted(), UiFont::MEDIUM, 12.5f);
                bool hovered = !blocked && NewUi::hit(x, y, w, ACCOUNT_H);
                if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
                    NewUi::rightClick = false;
                    accountsMenu(NewUi::mouseX, NewUi::mouseY);
                }
                if (!blocked && NewUi::click(x, y, w, ACCOUNT_H)) {
                    UiSound::request(UiSound::PAGE);
                    MainWindow::showAccounts();
                    AccountsScreen::openAdd();
                }
                return;
            }

            std::string key = Accounts::keyOf(*account);
            uint32_t face = Textures::of(Accounts::face(*account));
            if (face != 0) {
                picture(face, faceX, faceY, FACE, 12.0f);
            } else {
                letter(account->name, faceX, faceY, FACE);
            }
            UiRender::textY(UiFont::fitEnd(account->name, UiFont::BOLD, 17.0f, textRoom), textX, y + 14.0f, 22.0f,
                            UiTheme::textPrimary(), UiFont::BOLD, 17.0f);
            bool expired = Accounts::expired(key);
            std::string kind = expired ? std::string("登录已过期，点击刷新") : Accounts::kindOf(*account);
            UiRender::textY(UiFont::fitEnd(kind, UiFont::MEDIUM, 12.5f, textRoom), textX, y + 40.0f, 18.0f,
                            expired ? UiTheme::danger() : UiTheme::textMuted(), UiFont::MEDIUM, 12.5f);

            if (Accounts::busy(key)) {
                Page::spinner(refreshX + ACTION * 0.5f, buttonY + ACTION * 0.5f, 7.0f, UiTheme::accent());
            } else if (actionButton("home#account#refresh", 0xf021, "重新登录", refreshX, buttonY, ACTION, blocked)) {
                UiSound::request(UiSound::TICK);
                Accounts::refresh(key);
            }
            if (actionButton("home#account#switch", 0xf0c0, "切换账号", switchX, buttonY, ACTION, blocked)) {
                UiSound::request(UiSound::TICK);
                accountsMenu(switchX + ACTION - MENU_W, y + ACCOUNT_H + 6.0f);
            }
        }

        std::string loaderDetail(const Instances::Entry& entry) {
            std::string text = entry.info.id;
            for (const auto& loader : entry.info.loaders) {
                if (!blb::install::is_primary_loader(loader.type)) {
                    continue;
                }
                text += " · " + Installer::loaderName(loader.type);
                if (!loader.version.empty()) {
                    text += " " + loader.version;
                }
                break;
            }
            return text;
        }

        std::string uptime(double seconds) {
            int total = (int) seconds;
            char text[24];
            if (total >= 3600) {
                std::snprintf(text, sizeof(text), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
            } else {
                std::snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
            }
            return text;
        }

        void stopMenu(const std::string& key, float x, float y) {
            NewUi::openMenu(
                    "home#launch#stop",
                    [key] {
                        MenuItems items;
                        for (const Game::Session& session : Game::sessionsOf(key)) {
                            if (session.state != Game::State::Running) {
                                continue;
                            }
                            int id = session.id;
                            items.push_back(ActionMenuItem::make("结束 " + session.account, 0xf04d, [id] {
                                                Game::stop(id);
                                            })->keys("已运行 " + uptime(session.uptime)));
                        }
                        items.push_back(
                                ActionMenuItem::make("全部结束", 0xf00d, [key] { Game::stop(key); })->dangerous());
                        return items;
                    },
                    x, y, 190.0f);
        }

        void launchCard(float x, float y, float w, bool blocked) {
            std::string key = Instances::selected();
            const Instances::Entry* entry = Instances::find(key);
            Game::State state = Game::state(key);
            bool signedOut = state == Game::State::Idle && Accounts::current() == nullptr;
            bool usable = state != Game::State::Idle || (!signedOut && entry != nullptr && entry->info.valid);

            bool hovered = usable && !blocked && NewUi::hit(x, y, w, LAUNCH_H);
            float hover = UiState::motion("home#launch#hover", hovered ? 1.0f : 0.0f, 18.0f);
            float press = UiState::motion("home#launch#press", hovered && NewUi::mouseDown ? 1.0f : 0.0f, 26.0f);
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            if (press > 0.001f) {
                float cx = x + w * 0.5f;
                float cy = y + LAUNCH_H * 0.5f;
                UiRender::scaleFactor = savedScale * UiRender::lerp(1.0f, 0.985f, press);
                UiRender::pivotX = cx * UiRender::uiScale;
                UiRender::pivotY = cy * UiRender::uiScale;
            }
            UiRender::shadow(x, y, w, LAUNCH_H, RADIUS, 14.0f, 4.0f, UiTheme::shadow());
            uint32_t fill = UiRender::mix(UiTheme::accent(), 0xFFFFFFFFu, 0.08f * hover);
            UiRender::roundRect(x, y, w, LAUNCH_H, RADIUS, fill);
            if (press > 0.001f) {
                UiRender::scaleFactor = savedScale;
                UiRender::pivotX = savedPivotX;
                UiRender::pivotY = savedPivotY;
            }

            std::string title;
            std::string detail;
            size_t running = 0;
            for (const Game::Session& session : Game::sessionsOf(key)) {
                running += session.state == Game::State::Running ? 1 : 0;
            }
            switch (state) {
                case Game::State::Idle:
                    if (signedOut) {
                        title = "登录后启动";
                        detail = "还没有选择账号";
                    } else if (entry == nullptr) {
                        title = "没有可启动的版本";
                        detail = "先安装一个版本";
                    } else {
                        title = "启动游戏";
                        detail = loaderDetail(*entry);
                    }
                    break;
                case Game::State::Preparing: {
                    title = "取消启动";
                    std::string step;
                    for (const Game::Step& item : Game::steps(key)) {
                        if (!item.label.empty()) {
                            step = item.label;
                        }
                    }
                    detail = step.empty() ? std::string("正在准备…") : step;
                    break;
                }
                case Game::State::Running:
                    title = running > 1 ? "结束游戏（" + std::to_string(running) + " 个在运行）" : "结束游戏";
                    detail.clear();
                    for (const Game::Session& session : Game::sessionsOf(key)) {
                        if (session.state == Game::State::Running) {
                            detail = "已运行 " + uptime(session.uptime);
                            break;
                        }
                    }
                    break;
            }

            float titleW = UiFont::width(title, UiFont::BOLD, 21.0f);
            UiRender::textY(title, x + (w - titleW) * 0.5f, y + 15.0f, 28.0f, UiTheme::onAccent(), UiFont::BOLD,
                            21.0f);
            if (!detail.empty()) {
                float detailW = UiFont::width(detail, UiFont::MEDIUM, 12.5f);
                std::string shown = detailW > w - 2.0f * PAD
                        ? UiFont::fitEnd(detail, UiFont::MEDIUM, 12.5f, w - 2.0f * PAD)
                        : detail;
                detailW = UiFont::width(shown, UiFont::MEDIUM, 12.5f);
                UiRender::textY(shown, x + (w - detailW) * 0.5f, y + 45.0f, 18.0f,
                                (UiTheme::onAccent() & 0x00FFFFFFu) | 0xB4000000u, UiFont::MEDIUM, 12.5f);
            }
            if (state == Game::State::Preparing) {
                float fraction = std::clamp(Game::progress(key), 0.0f, 1.0f);
                float trackX = x + PAD;
                float trackW = w - 2.0f * PAD;
                UiRender::roundRect(trackX, y + LAUNCH_H - 10.0f, trackW, 3.0f, 1.5f,
                                    (UiTheme::onAccent() & 0x00FFFFFFu) | 0x40000000u);
                if (fraction > 0.001f) {
                    UiRender::roundRect(trackX, y + LAUNCH_H - 10.0f, trackW * fraction, 3.0f, 1.5f,
                                        UiTheme::onAccent());
                }
            }

            if (hovered) {
                std::string hint = state == Game::State::Idle ? detail : title;
                if (!hint.empty()) {
                    NewUi::tooltipText = hint;
                    float tipW = UiFont::width(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
                    NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
                    NewUi::tooltipY = y + LAUNCH_H + 6.0f;
                }
            }
            if (usable && NewUi::click(x, y, w, LAUNCH_H)) {
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
                            stopMenu(key, x + w * 0.5f - 95.0f, y + LAUNCH_H + 6.0f);
                        } else {
                            Game::stop(key);
                        }
                        break;
                }
            }
        }

        bool sideButton(const std::string& id, const std::string& label, float x, float y, float w, bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, SIDE_BTN);
            float hover = UiState::motion(id, hovered ? 1.0f : 0.0f, 18.0f);
            UiRender::roundRect(x, y, w, SIDE_BTN, 10.0f, UiTheme::surfaceRaised());
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, SIDE_BTN, 10.0f, UiTheme::overlay(Ui::round(16 * hover)));
            }
            UiRender::outline(x, y, w, SIDE_BTN, 10.0f, UiTheme::outlineSoft());
            Page::centredText(label, x + w * 0.5f, y, SIDE_BTN,
                              UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), hover), 12.5f);
            return !blocked && NewUi::click(x, y, w, SIDE_BTN);
        }
    }

    float width(float left, float right) {
        return std::clamp(right - left - 80.0f, 320.0f, CARD_MAX);
    }

    float draw(float left, float top, float right, bool blocked) {
        float totalW = width(left, right);
        float mainW = totalW - SIDE_W - SIDE_GAP;
        float x = (left + right - totalW) * 0.5f;
        accountCard(x, top, totalW, blocked);
        float launchY = top + ACCOUNT_H + ACCOUNT_GAP;
        launchCard(x, launchY, mainW, blocked);

        float sideX = x + mainW + SIDE_GAP;
        if (sideButton("home#versions", "切换版本", sideX, launchY, SIDE_W, blocked)) {
            UiSound::request(UiSound::PAGE);
            MainWindow::showVersions();
        }
        if (sideButton("home#versionSettings", "版本设置", sideX, launchY + SIDE_BTN + SIDE_GAP, SIDE_W, blocked)) {
            UiSound::request(UiSound::PAGE);
            std::string key = Instances::selected();
            if (!key.empty()) {
                MainWindow::openVersionSettings(key);
            }
        }
        return launchY + LAUNCH_H;
    }
}
