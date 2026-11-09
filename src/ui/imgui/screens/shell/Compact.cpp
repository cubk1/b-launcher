#include "ui/imgui/screens/shell/Compact.h"
#include "app/accounts/Accounts.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/system/Config.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "ui/imgui/screens/instances/InstancePage.h"
#include "ui/imgui/screens/launch/LaunchButton.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/Drop.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/overlay/UiOverlays.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/widget/InputWidget.h"

#include "imgui.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Compact {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float PAD = 10.0f;
        constexpr float INDENT = 10.0f;
        constexpr float HEADER_H = 54.0f;
        constexpr float BAR_H = 34.0f;
        constexpr float GROUP_H = 28.0f;
        constexpr float GROUP_GAP = 6.0f;
        constexpr float ROW_H = 46.0f;
        constexpr float ICON = 28.0f;
        constexpr float FIELD_H = 30.0f;
        constexpr float CHIP_H = 28.0f;
        constexpr float DOTS_W = 22.0f;
        const char* const SEARCH_ID = "compact#search";

        bool current = false;
        bool pending = false;
        bool pendingOn = false;
        bool dismiss = false;
        bool revealing = false;
        bool closeOverlays = false;
        bool searching = false;
        std::string pressKey;
        long long pressAt = 0;

        void picture(uint32_t texture, float x, float y, float size, float radius) {
            ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                 UiRender::transformW(size), UiRender::transformW(radius), texture, 0.0f, 0.0f, 1.0f,
                                 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        std::string brandText() {
            std::string title = Ui::trim(blb::replace_all(Appearance::brandTitle(), "{FPS}", ""));
            return title.empty() ? std::string(Appearance::defaultBrandTitle()) : title;
        }

        void accountsMenu(float x, float y) {
            NewUi::openMenu(
                    "compact#accounts",
                    [] {
                        MenuItems items;
                        const blb::auth::account* chosen = Accounts::current();
                        std::string chosenKey = chosen != nullptr ? Accounts::keyOf(*chosen) : std::string();
                        for (const blb::auth::account& account : Accounts::list()) {
                            std::string key = Accounts::keyOf(account);
                            std::shared_ptr<ActionMenuItem> item = ActionMenuItem::make(account.name, 0xf007, [key] {
                                                                       Accounts::select(key);
                                                                   })->keys(Accounts::kindOf(account));
                            uint32_t face = Textures::of(Accounts::face(account));
                            if (face != 0) {
                                item->image(face, 4.0f);
                            }
                            if (key == chosenKey) {
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
                    x, y, 200.0f);
        }

        float accountChip(float right, float y, bool blocked) {
            const blb::auth::account* account = Accounts::current();
            std::string name = account != nullptr ? account->name : std::string("未登录");
            uint32_t face = account != nullptr ? Textures::of(Accounts::face(*account)) : 0;
            float textW = std::min(110.0f, UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL));
            float w = 8.0f + 18.0f + 8.0f + textW + 8.0f + 10.0f + 8.0f;
            float x = right - w;
            bool hovered = !blocked && NewUi::hit(x, y, w, CHIP_H);
            float hover = UiState::motion("compact#account#hover", hovered ? 1.0f : 0.0f, 18.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, CHIP_H, 8.0f, UiTheme::overlay(Ui::round(14 * hover)));
            }
            float faceX = x + 8.0f;
            float faceY = y + (CHIP_H - 18.0f) * 0.5f;
            if (face != 0) {
                picture(face, faceX, faceY, 18.0f, 4.0f);
            } else {
                UiWidgets::icon(Ui::glyph(0xf007), faceX + 9.0f, y + CHIP_H * 0.5f, UiTheme::textMuted(), 11.0f);
            }
            UiRender::textY(UiFont::fitEnd(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, textW), faceX + 26.0f, y,
                            CHIP_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiWidgets::icon(Ui::glyph(0xf078), x + w - 13.0f, y + CHIP_H * 0.5f, UiTheme::textMuted(), 8.0f);
            if (!blocked && NewUi::click(x, y, w, CHIP_H)) {
                UiSound::request(UiSound::PAGE);
                if (Accounts::list().empty()) {
                    MainWindow::showAccounts();
                    AccountsScreen::openAdd();
                } else {
                    accountsMenu(x, y + CHIP_H + 4.0f);
                }
            }
            return x;
        }

        void header(float w, bool blocked) {
            const float logo = 24.0f;
            float logoX = PAD + INDENT;
            float logoY = (HEADER_H - logo) * 0.5f;
            Logo::brand(logoX, logoY, logo, 1.0f);
            float expandX = w - PAD - 28.0f;
            float buttonY = (HEADER_H - 28.0f) * 0.5f;
            if (Page::iconButton("compact#expand", 0xf424, "打开主窗口（Ctrl+M）", expandX, buttonY, blocked)) {
                UiSound::request(UiSound::PAGE);
                request(false);
            }
            float chipX = accountChip(expandX - 4.0f, (HEADER_H - CHIP_H) * 0.5f, blocked);
            float titleX = logoX + logo + 10.0f;
            float room = std::max(0.0f, chipX - 8.0f - titleX);
            UiRender::textY(UiFont::fitEnd(brandText(), UiFont::MEDIUM, 15.0f, room), titleX, logoY, logo,
                            UiTheme::textPrimary(), UiFont::MEDIUM, 15.0f);
        }

        void leaveSearch() {
            searching = false;
            UiInput::set(SEARCH_ID, "");
            if (UiInput::isFocused(SEARCH_ID)) {
                UiInput::blur();
            }
        }

        void toolbar(float x, float y, float w, size_t count, bool blocked) {
            float buttonX = x + w - 28.0f;
            float buttonY = y + (BAR_H - 28.0f) * 0.5f;
            if (!searching) {
                std::string label = "实例";
                float labelW = UiFont::width(label, UiFont::BOLD, UiWidgets::TEXT_BODY);
                UiRender::textY(label, x + INDENT, y, BAR_H, UiTheme::textPrimary(), UiFont::BOLD,
                                UiWidgets::TEXT_BODY);
                if (count > 0) {
                    UiRender::textY(std::to_string(count), x + INDENT + labelW + 3.0f, y - 6.0f, BAR_H,
                                    UiTheme::accentText(), UiFont::BOLD, UiWidgets::TEXT_CAPTION);
                }
                if (Page::iconButton("compact#search#open", 0xf002, "搜索实例", buttonX, buttonY, blocked)) {
                    UiSound::request(UiSound::TICK);
                    searching = true;
                    UiInput::set(SEARCH_ID, "");
                    UiInput::focus(SEARCH_ID, 64);
                }
                return;
            }
            float fieldY = y + (BAR_H - FIELD_H) * 0.5f;
            bool focused = UiInput::isFocused(SEARCH_ID);
            bool hovered = !blocked && NewUi::hit(x, fieldY, w, FIELD_H);
            float ring = UiState::motion("compact#search#ring", focused ? 1.0f : (hovered ? 0.5f : 0.0f), 18.0f);
            UiRender::roundRect(x, fieldY, w, FIELD_H, 8.0f, UiTheme::overlay(10));
            UiRender::outline(x, fieldY, w, FIELD_H, 8.0f,
                              focused ? UiTheme::accent()
                                      : UiRender::mix(UiTheme::border(UiTheme::outlineSoft()),
                                                      UiTheme::border(UiTheme::outline()), ring));
            UiWidgets::icon(Ui::glyph(0xf002), x + INDENT + 6.0f, fieldY + FIELD_H * 0.5f,
                            focused ? UiTheme::textSecondary() : UiTheme::textFaint(), 11.0f);
            float closeX = x + w - 16.0f;
            InputWidget input(SEARCH_ID);
            input.area(x + INDENT + 22.0f, closeX - 12.0f - (x + INDENT + 22.0f), UiWidgets::TEXT_CONTROL, 14.0f)
                    .limit(64)
                    .hint("搜索实例")
                    .bounds(x, fieldY, closeX - 10.0f - x, FIELD_H)
                    .block(blocked);
            input.paint();
            bool overClose = !blocked && NewUi::hit(closeX - 10.0f, fieldY, 20.0f, FIELD_H);
            UiWidgets::icon(Ui::glyph(0xf00d), closeX, fieldY + FIELD_H * 0.5f,
                            overClose ? UiTheme::textPrimary() : UiTheme::textFaint(), 9.0f);
            if (!blocked && NewUi::click(closeX - 10.0f, fieldY, 20.0f, FIELD_H)) {
                UiSound::request(UiSound::RELEASE);
                leaveSearch();
            }
        }

        void rowMenu(const std::string& key, float x, float y) {
            NewUi::openMenu(
                    "compact#instance#" + key,
                    [key] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("实例设置", 0xf013,
                                                             [key] { MainWindow::openVersionSettings(key); }));
                        items.push_back(ActionMenuItem::make("打开游戏目录", 0xf07c, [key] {
                            if (const Instances::Entry* entry = Instances::find(key)) {
                                Platform::open(entry->gameDirectory);
                            }
                        }));
                        return items;
                    },
                    x, y, 160.0f);
        }

        void action(const Instances::Entry& entry, float right, float rowY, bool blocked) {
            const std::string& key = entry.key;
            Game::State state = Game::state(key);
            std::string label = state == Game::State::Idle ? std::string("启动")
                    : state == Game::State::Preparing      ? std::string("取消")
                                                           : std::string("结束");
            float w = std::max(52.0f, Page::buttonWidth(label));
            float x = right - w;
            float y = rowY + (ROW_H - Page::FIELD_H) * 0.5f;
            if (state == Game::State::Preparing) {
                Page::progressRing(x - 14.0f, rowY + ROW_H * 0.5f, Game::progress(key));
            }
            bool usable = state != Game::State::Idle || entry.info.valid;
            float savedAlpha = UiRender::alphaMul;
            if (!usable) {
                UiRender::alphaMul = savedAlpha * 0.45f;
            }
            bool pressed =
                    Page::button("compact#act#" + key, label, x, y, w, state == Game::State::Idle, blocked || !usable);
            UiRender::alphaMul = savedAlpha;
            if (!pressed) {
                return;
            }
            switch (state) {
                case Game::State::Idle: LaunchButton::start(key); break;
                case Game::State::Preparing:
                    UiSound::request(UiSound::RELEASE);
                    Game::cancel(key);
                    break;
                case Game::State::Running:
                    UiSound::request(UiSound::RELEASE);
                    Game::stop(key);
                    break;
            }
        }

        void row(const Instances::Entry& entry, float x, float y, float w, bool blocked) {
            const std::string& key = entry.key;
            bool hovered = !blocked && NewUi::hit(x, y, w, ROW_H);
            float hover = UiState::motion("compact#row#" + key, hovered ? 1.0f : 0.0f, 18.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, ROW_H, 9.0f, UiTheme::overlay(Ui::round(10 * hover)));
            }
            float iconX = x + 10.0f;
            float iconY = y + (ROW_H - ICON) * 0.5f;
            uint32_t image = InstancePage::icon(entry);
            if (image != 0) {
                UiRender::texture(image, iconX, iconY, ICON, ICON);
            } else {
                UiWidgets::icon(Ui::glyph(0xf1b2), iconX + ICON * 0.5f, iconY + ICON * 0.5f, UiTheme::textMuted(),
                                16.0f);
            }
            bool running = false;
            for (const Game::Session& session : Game::sessionsOf(key)) {
                running = running || session.state == Game::State::Running;
            }
            float live = UiState::motion("compact#live#" + key, running ? 1.0f : 0.0f, 14.0f);
            if (live > 0.01f) {
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * live;
                UiRender::circle(iconX + ICON - 1.0f, iconY + ICON - 1.0f, 5.5f, UiTheme::surface());
                UiRender::circle(iconX + ICON - 1.0f, iconY + ICON - 1.0f, 3.5f, UiTheme::success());
                UiRender::alphaMul = savedAlpha;
            }

            float dotsX = x + w - 4.0f - DOTS_W;
            float dotsY = y + (ROW_H - 28.0f) * 0.5f;
            bool overDots = !blocked && NewUi::hit(dotsX, dotsY, DOTS_W, 28.0f);
            float dotsHover = UiState::motion("compact#dots#" + key, overDots ? 1.0f : 0.0f, 18.0f);
            if (dotsHover > 0.001f) {
                UiRender::roundRect(dotsX, dotsY, DOTS_W, 28.0f, 7.0f, UiTheme::overlay(Ui::round(14 * dotsHover)));
            }
            UiWidgets::icon(Ui::glyph(0xf142), dotsX + DOTS_W * 0.5f, dotsY + 14.0f,
                            UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), dotsHover), 11.0f);
            if (!blocked && NewUi::click(dotsX, dotsY, DOTS_W, 28.0f)) {
                UiSound::request(UiSound::PAGE);
                rowMenu(key, dotsX, dotsY + 28.0f + 4.0f);
            }

            float actionRight = dotsX - 6.0f;
            action(entry, actionRight, y, blocked);

            float nameX = iconX + ICON + 12.0f;
            float room = std::max(0.0f, actionRight - 52.0f - 30.0f - nameX);
            UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_BODY, room), nameX, y, ROW_H,
                            entry.info.valid ? UiTheme::textPrimary() : UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);

            if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
                NewUi::rightClick = false;
                UiSound::request(UiSound::PAGE);
                rowMenu(key, NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(x, y, w, ROW_H)) {
                long long now = Ui::millis();
                bool twice = pressKey == key && now - pressAt < 400;
                pressKey = twice ? std::string() : key;
                pressAt = now;
                Instances::select(key);
                if (twice) {
                    LaunchButton::start(key);
                }
            }
        }

        void emptyState(float x, float y, float w, float h, bool none, bool blocked) {
            float cy = y + h * 0.5f;
            if (Instances::loading()) {
                Page::spinner(x + w * 0.5f, cy, 8.0f, UiTheme::accent());
                return;
            }
            std::string text = none ? std::string("还没有实例") : std::string("没有匹配的实例");
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            UiRender::textY(text, x + (w - textW) * 0.5f, cy - (none ? 30.0f : 12.0f), 24.0f, UiTheme::textMuted(),
                            UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            if (!none) {
                return;
            }
            std::string label = "新建实例";
            float buttonW = Page::buttonWidth(label, 0xf067);
            if (Page::button("compact#new", label, x + (w - buttonW) * 0.5f, cy + 2.0f, buttonW, true, blocked,
                             0xf067)) {
                UiSound::request(UiSound::PAGE);
                MainWindow::startNewInstance();
            }
        }

        void groupHeader(const Instances::Source& source, size_t count, float x, float y, float w, float open,
                         bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, GROUP_H);
            float lit = UiState::motion("compact#group#hover#" + source.key, hovered ? 1.0f : 0.0f, 18.0f);
            uint32_t tint = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
            float chevronX = x + w - 14.0f;
            float cy = y + GROUP_H * 0.5f;
            std::string number = std::to_string(count);
            float numberW = UiFont::width(number, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float room = std::max(0.0f, chevronX - 12.0f - numberW - 8.0f - (x + INDENT));
            UiRender::textY(UiFont::fitEnd(source.label, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, room), x + INDENT, y,
                            GROUP_H, tint, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(number, chevronX - 12.0f - numberW, y, GROUP_H, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * open;
            UiWidgets::icon(Ui::glyph(0xf078), chevronX, cy, tint, 9.0f);
            UiRender::alphaMul = savedAlpha * (1.0f - open);
            UiWidgets::icon(Ui::glyph(0xf054), chevronX, cy, tint, 9.0f);
            UiRender::alphaMul = savedAlpha;
            if (!blocked && NewUi::click(x, y, w, GROUP_H)) {
                UiSound::request(UiSound::TICK);
                MainWindow::toggleGroup(source.key);
            }
        }

        void list(float x, float y, float w, float h, bool covered) {
            std::string query = searching ? Ui::trim(UiInput::get(SEARCH_ID)) : std::string();
            const std::vector<Instances::Source>& sources = Instances::sources();
            std::vector<std::vector<const Instances::Entry*>> grouped(sources.size());
            size_t total = 0;
            for (const Instances::Entry& entry : Instances::list()) {
                for (size_t i = 0; i < sources.size(); i++) {
                    if (sources[i].key != entry.source || sources[i].hidden) {
                        continue;
                    }
                    total++;
                    if (blb::icontains(entry.info.id, query)) {
                        grouped[i].push_back(&entry);
                    }
                }
            }
            std::vector<float> opens(sources.size(), 0.0f);
            float contentH = 0.0f;
            for (size_t i = 0; i < sources.size(); i++) {
                if (grouped[i].empty()) {
                    continue;
                }
                bool closed = MainWindow::isCollapsed(sources[i].key) && query.empty();
                opens[i] = UiState::motion("compact#group#open#" + sources[i].key, closed ? 0.0f : 1.0f, 16.0f);
                contentH += GROUP_H + grouped[i].size() * ROW_H * opens[i] + GROUP_GAP;
            }
            if (contentH <= 0.0f) {
                emptyState(x, y, w, h, total == 0, covered);
                return;
            }
            float width = w - 8.0f;
            Page::Scroll scroll("compact#list", y, y + h, contentH, x, width);
            bool blocked = scroll.begin() || covered;
            float cursor = y - scroll.offset;
            for (size_t i = 0; i < sources.size(); i++) {
                const std::vector<const Instances::Entry*>& rows = grouped[i];
                if (rows.empty()) {
                    continue;
                }
                if (cursor + GROUP_H >= scroll.top && cursor <= scroll.bottom) {
                    groupHeader(sources[i], rows.size(), x, cursor, width, opens[i], blocked);
                }
                cursor += GROUP_H;
                float shownH = rows.size() * ROW_H * opens[i];
                float clipTop = std::max(scroll.top, cursor);
                float clipBottom = std::min(scroll.bottom, cursor + shownH);
                if (clipBottom > clipTop + 0.5f) {
                    UiRender::scissor(x - 4.0f, clipTop, width + 8.0f, clipBottom - clipTop);
                    bool rowsBlocked = blocked || !NewUi::rawHit(x, clipTop, width, clipBottom - clipTop);
                    float savedAlpha = UiRender::alphaMul;
                    UiRender::alphaMul = savedAlpha * opens[i];
                    float rowY = cursor;
                    for (const Instances::Entry* entry : rows) {
                        if (rowY + ROW_H >= clipTop && rowY <= clipBottom) {
                            row(*entry, x, rowY, width, rowsBlocked);
                        }
                        rowY += ROW_H;
                    }
                    UiRender::alphaMul = savedAlpha;
                    UiRender::unscissor();
                }
                cursor += shownH + GROUP_GAP;
            }
            scroll.end();
        }

        void keys() {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false) || NewUi::popupOpen || !UiOverlays::isEmpty() ||
                !NewUi::textEditing.empty()) {
                return;
            }
            if (searching) {
                leaveSearch();
            } else {
                dismiss = true;
            }
        }
    }

    bool active() {
        return current;
    }

    void setActive(bool on) {
        current = on;
        Config::data()["compactMode"] = on;
        Config::save();
    }

    bool stored() {
        return blb::json_bool(Config::data(), "compactMode");
    }

    void request(bool on) {
        pending = true;
        pendingOn = on;
    }

    bool takeRequest(bool& on) {
        if (!pending) {
            return false;
        }
        pending = false;
        on = pendingOn;
        return on != current;
    }

    bool takeDismiss() {
        return std::exchange(dismiss, false);
    }

    void shortcut() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_M, false) &&
            NewUi::bindingKey == nullptr && NewUi::recordingShortcut == nullptr) {
            request(!current);
        }
    }

    void hidden() {
        closeOverlays = true;
    }

    void reveal() {
        revealing = true;
    }

    bool takeReveal() {
        return std::exchange(revealing, false);
    }

    void draw() {
        UiRender::reset();
        if (std::exchange(closeOverlays, false)) {
            if (Dialog::active()) {
                Dialog::closeAbove();
            } else {
                NewUi::closeAllOverlays();
            }
        }
        shortcut();
        keys();
        float w = MainWindow::width();
        float h = MainWindow::height();
        bool covered = NewUi::overlayCoversMouse();
        UiRender::roundRect(0, 0, w, h, MainWindow::cornerRadius, UiTheme::panel());
        header(w, covered);
        size_t count = 0;
        for (const Instances::Entry& entry : Instances::list()) {
            const Instances::Source* source = Instances::source(entry.source);
            count += source != nullptr && !source->hidden ? 1 : 0;
        }
        toolbar(PAD, HEADER_H, w - PAD * 2.0f, count, covered);
        float listY = HEADER_H + BAR_H + 4.0f;
        list(PAD, listY, w - PAD * 2.0f, h - PAD - listY, covered);
        Notifications::render(w, h);
        Drop::draw(w, h);
    }
}
