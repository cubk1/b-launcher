#include "ui/imgui/screens/home/HomeLaunchPanel.h"
#include "app/accounts/Accounts.h"
#include "app/system/Features.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/launch/LaunchButton.h"
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
#include "ui/imgui/screens/theme/ThemeEffects.h"
#include "ui/imgui/screens/theme/ThemePaint.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace HomeLaunchPanel {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float SIDE_W = 104.0f;
        constexpr float SIDE_GAP = 10.0f;
        constexpr float SIDE_BTN = 27.0f;
        constexpr float RADIUS = 14.0f;
        constexpr float PAD = 14.0f;
        constexpr float FACE = 36.0f;
        constexpr float MENU_W = 200.0f;

        void picture(uint32_t texture, float x, float y, float size, float radius) {
            ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                 UiRender::transformW(size), UiRender::transformW(radius), texture, 0.0f, 0.0f, 1.0f,
                                 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        void letter(const std::string& name, float x, float y, float size) {
            UiRender::roundRect(x, y, size, size, 10.0f, UiTheme::surfaceRaised());
            std::string text = name.empty() ? "?" : std::string(1, (char) std::toupper((unsigned char) name[0]));
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_TITLE);
            UiRender::textY(text, x + (size - textW) * 0.5f, y, size, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_TITLE);
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

        void accountCard(float x, float y, float w, bool blocked) {
            HomeWidgets::panel(x, y, w, ACCOUNT_H);
            const blb::auth::account* account = Accounts::current();
            float faceY = y + (ACCOUNT_H - FACE) * 0.5f;
            float faceX = x + PAD;
            float buttonY = y + (ACCOUNT_H - 28.0f) * 0.5f;
            float switchX = x + w - PAD - 28.0f;
            float refreshX = switchX - 6.0f - 28.0f;
            float textX = faceX + FACE + 12.0f;
            float textRoom = std::max(0.0f, refreshX - 8.0f - textX);

            if (account == nullptr) {
                UiRender::roundRect(faceX, faceY, FACE, FACE, 10.0f, UiTheme::surfaceRaised());
                UiWidgets::icon(Ui::glyph(0xf007), faceX + FACE * 0.5f, faceY + FACE * 0.5f, UiTheme::textMuted(),
                                FACE * 0.36f);
                UiRender::textY("未登录", textX, y + 11.0f, 20.0f, UiTheme::textPrimary(), UiFont::BOLD,
                                UiWidgets::TEXT_TITLE);
                UiRender::textY(UiFont::fitEnd("添加账号后即可启动游戏", UiFont::MEDIUM, UiWidgets::TEXT_SMALL, textRoom),
                                textX, y + 32.0f, 16.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
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
                picture(face, faceX, faceY, FACE, 10.0f);
            } else {
                letter(account->name, faceX, faceY, FACE);
            }
            UiRender::textY(UiFont::fitEnd(account->name, UiFont::BOLD, UiWidgets::TEXT_TITLE, textRoom), textX,
                            y + 11.0f, 20.0f, UiTheme::textPrimary(), UiFont::BOLD, UiWidgets::TEXT_TITLE);
            bool expired = Accounts::expired(key);
            std::string kind = expired ? std::string("登录已过期，点击刷新") : Accounts::kindOf(*account);
            UiRender::textY(UiFont::fitEnd(kind, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, textRoom), textX, y + 32.0f,
                            16.0f, expired ? UiTheme::danger() : UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);

            if (Accounts::busy(key)) {
                Page::spinner(refreshX + 14.0f, buttonY + 14.0f, 6.0f, UiTheme::accent());
            } else if (Page::iconButton("home#account#refresh", 0xf021, "重新登录", refreshX, buttonY, blocked)) {
                UiSound::request(UiSound::TICK);
                Accounts::refresh(key);
            }
            if (Page::iconButton("home#account#switch", 0xf0c0, "切换账号", switchX, buttonY, blocked)) {
                UiSound::request(UiSound::TICK);
                accountsMenu(switchX - MENU_W + 28.0f, y + ACCOUNT_H + 6.0f);
            }
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

        uint32_t primaryInk() {
            return UiTheme::tokenOr(UiTheme::PRIMARY_TEXT, UiTheme::onAccent());
        }

        void launchCard(float x, float y, float w, bool blocked, float h = LAUNCH_H, bool flat = false) {
            float shiftY = (h - LAUNCH_H) * 0.5f;
            int titleFamily = flat ? UiFont::MEDIUM : UiFont::BOLD;
            float titleSize = flat ? 16.0f : 18.0f;
            std::string key = Instances::selected();
            const Instances::Entry* entry = Instances::find(key);
            Game::State state = Game::state(key);
            bool signedOut = state == Game::State::Idle && Accounts::current() == nullptr;
            bool usable = state != Game::State::Idle || signedOut || (entry != nullptr && entry->info.valid);

            bool hovered = usable && !blocked && NewUi::hit(x, y, w, h);
            float hover = UiState::motion("home#launch#hover", hovered ? 1.0f : 0.0f, 18.0f);
            float press = UiState::motion("home#launch#press", hovered && NewUi::mouseDown ? 1.0f : 0.0f, 26.0f);
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            if (press > 0.001f) {
                float cx = x + w * 0.5f;
                float cy = y + h * 0.5f;
                UiRender::scaleFactor = savedScale * UiRender::lerp(1.0f, 0.985f, press);
                UiRender::pivotX = cx * UiRender::uiScale + UiRender::offsetX;
                UiRender::pivotY = cy * UiRender::uiScale + UiRender::offsetY;
            }
            if (!flat) {
                UiRender::shadow(x, y, w, h, RADIUS, 14.0f, 4.0f, UiTheme::shadow());
            }
            ThemePaint::primaryFill(x, y, w, h, RADIUS, 0.08f * hover);
            ThemeEffects::button("home#launch", "primary", x, y, w, h, RADIUS, 0xF,
                                 UiTheme::tokenOr(UiTheme::PRIMARY_TEXT, UiTheme::onAccent()), hovered);
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
                        detail = entry->info.id;
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

            float titleW = UiFont::width(title, titleFamily, titleSize);
            UiRender::textY(title, x + (w - titleW) * 0.5f, y + 12.0f + shiftY, 24.0f, primaryInk(), titleFamily,
                            titleSize);
            if (!detail.empty()) {
                float detailW = UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                std::string shown = detailW > w - 2.0f * PAD
                        ? UiFont::fitEnd(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, w - 2.0f * PAD)
                        : detail;
                detailW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(shown, x + (w - detailW) * 0.5f, y + 37.0f + shiftY, 16.0f,
                                (primaryInk() & 0x00FFFFFFu) | 0xB4000000u, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
            if (state == Game::State::Preparing) {
                float fraction = std::clamp(Game::progress(key), 0.0f, 1.0f);
                float trackX = x + PAD;
                float trackW = w - 2.0f * PAD;
                UiRender::roundRect(trackX, y + h - 8.0f, trackW, 2.0f, 1.0f,
                                    (primaryInk() & 0x00FFFFFFu) | 0x40000000u);
                if (fraction > 0.001f) {
                    UiRender::roundRect(trackX, y + h - 8.0f, trackW * fraction, 2.0f, 1.0f, primaryInk());
                }
            }

            if (hovered) {
                std::string hint = state == Game::State::Idle ? detail : title;
                if (!hint.empty()) {
                    NewUi::tooltipText = hint;
                    float tipW = UiFont::width(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
                    NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
                    NewUi::tooltipY = y + h + 6.0f;
                }
            }
            if (usable && !blocked && NewUi::click(x, y, w, h)) {
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
                            stopMenu(key, x + w * 0.5f - 95.0f, y + h + 6.0f);
                        } else {
                            Game::stop(key);
                        }
                        break;
                }
            }
        }

        void anotherButton(const Instances::Entry& entry, float x, float y, float shown, bool blocked,
                           float size = LAUNCH_H) {
            if (shown < 0.01f) {
                return;
            }
            float cx = x + size * 0.5f;
            float cy = y + size * 0.5f;
            bool live = shown > 0.99f && !blocked;
            bool hovered = live && NewUi::hit(x, y, size, size);
            float hover = UiState::motion("home#launch#another#hover", hovered ? 1.0f : 0.0f, 18.0f);
            float press =
                    UiState::motion("home#launch#another#press", hovered && NewUi::mouseDown ? 1.0f : 0.0f, 26.0f);
            float savedAlpha = UiRender::alphaMul;
            float savedScale = UiRender::scaleFactor;
            float savedPivotX = UiRender::pivotX;
            float savedPivotY = UiRender::pivotY;
            UiRender::pivotX = cx * UiRender::uiScale + UiRender::offsetX;
            UiRender::pivotY = cy * UiRender::uiScale + UiRender::offsetY;
            UiRender::scaleFactor = savedScale * UiRender::lerp(0.6f, 1.0f, shown) * UiRender::lerp(1.0f, 0.94f, press);
            UiRender::alphaMul = savedAlpha * shown;
            UiRender::shadow(x, y, size, size, RADIUS, 14.0f, 4.0f, UiTheme::shadow());
            UiRender::roundRect(x, y, size, size, RADIUS,
                                UiRender::mix(UiTheme::surface(), UiTheme::surfaceRaised(), hover));
            UiRender::outline(x, y, size, size, RADIUS, UiTheme::border(UiTheme::outlineSoft()));
            UiWidgets::icon(Ui::glyph(0xf067), cx, cy,
                            UiRender::mix(UiTheme::textSecondary(), UiTheme::accentText(), hover), 18.0f);
            UiRender::alphaMul = savedAlpha;
            UiRender::scaleFactor = savedScale;
            UiRender::pivotX = savedPivotX;
            UiRender::pivotY = savedPivotY;
            if (hovered) {
                std::string hint = "再开一个 " + entry.info.id;
                float tipW = UiFont::width(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
                NewUi::tooltipText = hint;
                NewUi::tooltipX = UiRender::clamp(cx - tipW * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
                NewUi::tooltipY = y + size + 6.0f;
            }
            if (live && NewUi::click(x, y, size, size)) {
                UiSound::request(UiSound::TICK);
                LaunchButton::launchAnother(entry.key, x, y, y + size);
            }
        }

        bool sideButton(const std::string& id, const std::string& label, float x, float y, float w, bool blocked,
                        float h = SIDE_BTN, float size = UiWidgets::TEXT_SMALL, bool strong = false) {
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            float hover = UiState::motion(id, hovered ? 1.0f : 0.0f, 18.0f);
            UiRender::roundRect(x, y, w, h, 9.0f, UiTheme::surfaceRaised());
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, h, 9.0f, UiTheme::overlay(Ui::round(16 * hover)));
            }
            UiRender::outline(x, y, w, h, 9.0f,
                              UiTheme::border(UiTheme::tokenOr(UiTheme::BUTTON_OUTLINE, UiTheme::outlineSoft())));
            Page::centredText(label, x + w * 0.5f, y, h,
                              strong ? UiTheme::textPrimary()
                                     : UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), hover),
                              size);
            return !blocked && NewUi::click(x, y, w, h);
        }
    }

    void account(float x, float y, float w, bool blocked) {
        accountCard(x, y, w, blocked);
    }

    void column(float x, float top, float w, float bottom, bool blocked, float avatar, float avatarRadius) {
        constexpr float EDGE = 20.0f;
        constexpr float COLUMN_LAUNCH_H = 54.0f;
        constexpr float COLUMN_BUTTON_H = 34.0f;
        float inner = w - 2.0f * EDGE;
        float buttonsY = bottom - EDGE - COLUMN_BUTTON_H;
        float launchY = buttonsY - SIDE_GAP - COLUMN_LAUNCH_H;
        float face = avatar > 0.0f ? avatar : std::clamp(inner * 0.42f, 64.0f, 112.0f);
        float faceRadius = avatarRadius >= 0.0f ? avatarRadius : 16.0f;
        float blockH = face + 14.0f + 26.0f + 18.0f;
        float faceY = top + std::max(EDGE + 10.0f, (launchY - top - blockH) * 0.42f);
        float faceX = x + (w - face) * 0.5f;
        float nameY = faceY + face + 14.0f;
        const blb::auth::account* account = Accounts::current();
        std::string name = account != nullptr ? account->name : std::string("未登录");
        std::string kind;
        bool expired = false;
        if (account == nullptr) {
            kind = "点击添加账号";
            UiRender::roundRect(faceX, faceY, face, face, faceRadius, UiTheme::surfaceRaised());
            UiWidgets::icon(Ui::glyph(0xf007), faceX + face * 0.5f, faceY + face * 0.5f, UiTheme::textMuted(),
                            face * 0.36f);
        } else {
            expired = Accounts::expired(Accounts::keyOf(*account));
            kind = expired ? std::string("登录已过期，点击刷新") : Accounts::kindOf(*account);
            uint32_t texture = Textures::of(Accounts::face(*account));
            if (faceRadius > 0.0f) {
                UiRender::shadow(faceX, faceY, face, face, faceRadius, 16.0f, 6.0f, UiTheme::shadow());
            }
            if (texture != 0) {
                picture(texture, faceX, faceY, face, faceRadius);
            } else {
                letter(account->name, faceX, faceY, face);
            }
        }
        std::string shownName = UiFont::fitEnd(name, UiFont::BOLD, 18.0f, inner);
        float nameW = UiFont::width(shownName, UiFont::BOLD, 18.0f);
        UiRender::textY(shownName, x + (w - nameW) * 0.5f, nameY, 26.0f, UiTheme::textPrimary(), UiFont::BOLD, 18.0f);
        std::string shownKind = UiFont::fitEnd(kind, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, inner);
        float kindW = UiFont::width(shownKind, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        UiRender::textY(shownKind, x + (w - kindW) * 0.5f, nameY + 26.0f, 18.0f,
                        expired ? UiTheme::danger() : UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        float areaY = faceY - 6.0f;
        float areaH = nameY + 44.0f - areaY;
        float areaX = x + EDGE;
        bool hovered = !blocked && NewUi::hit(areaX, areaY, inner, areaH);
        float hover = UiState::motion("home#column#account", hovered ? 1.0f : 0.0f, 18.0f);
        if (hover > 0.001f) {
            UiRender::roundRect(areaX, areaY, inner, areaH, 14.0f, UiTheme::overlay(Ui::round(10 * hover)));
        }
        if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
            NewUi::rightClick = false;
            accountsMenu(NewUi::mouseX, NewUi::mouseY);
        }
        if (!blocked && NewUi::click(areaX, areaY, inner, areaH)) {
            UiSound::request(UiSound::TICK);
            if (account == nullptr) {
                MainWindow::showAccounts();
                AccountsScreen::openAdd();
            } else if (expired) {
                Accounts::refresh(Accounts::keyOf(*account));
            } else {
                accountsMenu(x + (w - MENU_W) * 0.5f, nameY + 48.0f);
            }
        }

        std::string key = Instances::selected();
        const Instances::Entry* entry = Instances::find(key);
        bool another = entry != nullptr && Game::state(key) == Game::State::Running;
        float shown = UiState::motion("home#launch#another", another ? 1.0f : 0.0f, 14.0f);
        launchCard(x + EDGE, launchY, inner - (COLUMN_LAUNCH_H + SIDE_GAP) * shown, blocked, COLUMN_LAUNCH_H, true);
        if (entry != nullptr) {
            anotherButton(*entry, x + EDGE + inner - COLUMN_LAUNCH_H, launchY, shown, blocked, COLUMN_LAUNCH_H);
        }
        bool versions = Features::enabled(Features::VERSIONS);
        float half = versions ? (inner - SIDE_GAP) * 0.5f : inner;
        if (versions &&
            sideButton("home#versions", "切换版本", x + EDGE, buttonsY, half, blocked, COLUMN_BUTTON_H,
                       UiWidgets::TEXT_CONTROL, true)) {
            UiSound::request(UiSound::PAGE);
            MainWindow::showVersions();
        }
        float settingsX = versions ? x + EDGE + half + SIDE_GAP : x + EDGE;
        if (sideButton("home#versionSettings", "实例设置", settingsX, buttonsY, half, blocked, COLUMN_BUTTON_H,
                       UiWidgets::TEXT_CONTROL, true)) {
            UiSound::request(UiSound::PAGE);
            if (!key.empty()) {
                MainWindow::openVersionSettings(key);
            }
        }
    }

    void launch(float x, float y, float w, bool blocked) {
        float mainW = w - SIDE_W - SIDE_GAP;
        std::string key = Instances::selected();
        const Instances::Entry* entry = Instances::find(key);
        bool another = entry != nullptr && Game::state(key) == Game::State::Running;
        float shown = UiState::motion("home#launch#another", another ? 1.0f : 0.0f, 14.0f);
        launchCard(x, y, mainW - (LAUNCH_H + SIDE_GAP) * shown, blocked);
        if (entry != nullptr) {
            anotherButton(*entry, x + mainW - LAUNCH_H, y, shown, blocked);
        }
        float sideX = x + mainW + SIDE_GAP;
        if (sideButton("home#versions", "切换版本", sideX, y, SIDE_W,
                       blocked || !Features::enabled(Features::VERSIONS))) {
            UiSound::request(UiSound::PAGE);
            MainWindow::showVersions();
        }
        if (sideButton("home#versionSettings", "实例设置", sideX, y + SIDE_BTN + SIDE_GAP, SIDE_W, blocked)) {
            UiSound::request(UiSound::PAGE);
            std::string key = Instances::selected();
            if (!key.empty()) {
                MainWindow::openVersionSettings(key);
            }
        }
    }
}
