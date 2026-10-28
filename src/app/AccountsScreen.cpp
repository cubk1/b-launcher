#include "AccountsScreen.h"
#include "Accounts.h"
#include "Icons.h"
#include "MainWindow.h"
#include "Page.h"
#include "Platform.h"

#include "ui/gl/ShaderUtil.h"
#include "ui/menu/MenuItems.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/Notifications.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiInput.h"
#include "ui/panel/UiPageTransition.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"
#include "ui/value/Bind.h"
#include "ui/widget/IconButtonWidget.h"
#include "ui/widget/TextFieldWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace AccountsScreen {

    namespace blb = b_launcher_base;

    namespace {
        const char* const OFFLINE_OWNER = "accounts#offline";
        constexpr float AVATAR = 24.0f;
        constexpr float AVATAR_RADIUS = 6.0f;
        constexpr float NAME_X = Page::LEFT + 52.0f;
        constexpr float ACTION_Y = Page::TOP + 1.0f;

        bool addingOffline = false;
        std::string offlineName;
        bool addPending = false;

        constexpr int VIEW_LIST = 0;
        constexpr int VIEW_SERVER = 1;
        PageTransition views(VIEW_LIST);

        const char* const ADDRESS_OWNER = "accounts#server#address";
        const char* const USER_OWNER = "accounts#server#user";
        const char* const PASSWORD_OWNER = "accounts#server#password";
        const char* const LITTLESKIN_REGISTER = "https://littleskin.cn/auth/register";

        blb::auth::account_type serverType = blb::auth::account_type::yggdrasil;
        std::string serverAddress;
        std::string serverUser;
        std::string serverPassword;
        std::string focusNext;
        size_t chosenProfile = 0;

        void tip(const std::string& text, float x, float y, float w, float h) {
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
                tip(hint, x, y, 28, 28);
            }
            return button.pressed;
        }

        int corners(int index, int count) {
            return (index == 0 ? 1 | 2 : 0) | (index == count - 1 ? 4 | 8 : 0);
        }

        bool hasMicrosoft() {
            const std::vector<blb::auth::account>& accounts = Accounts::list();
            return std::any_of(accounts.begin(), accounts.end(), [](const blb::auth::account& account) {
                return account.type == blb::auth::account_type::microsoft;
            });
        }

        float tag(const std::string& text, float x, float rowY, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 14.0f;
            float y = rowY + (Page::ROW_H - 18.0f) * 0.5f;
            UiRender::roundRect(x, y, w, 18.0f, 6.0f, fill);
            UiRender::textY(text, x + 7.0f, y, 18.0f, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        void avatar(uint32_t face, const std::string& name, float rowY) {
            float x = Page::LEFT + 16.0f;
            float y = rowY + (Page::ROW_H - AVATAR) * 0.5f;
            if (face != 0) {
                ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(AVATAR),
                                     UiRender::transformW(AVATAR), UiRender::transformW(AVATAR_RADIUS), face, 0.0f,
                                     0.0f, 1.0f, 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
                return;
            }
            UiRender::roundRect(x, y, AVATAR, AVATAR, AVATAR_RADIUS, UiTheme::surfaceRaised());
            std::string letter = name.empty() ? "?" : std::string(1, (char) std::toupper((unsigned char) name[0]));
            float letterW = UiFont::width(letter, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(letter, x + (AVATAR - letterW) * 0.5f, y, AVATAR, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void openAccountMenu(const std::string& key, float x, float y) {
            NewUi::openMenu(
                    "accounts#menu#" + key,
                    [key] {
                        MenuItems items;
                        const blb::auth::account* account = Accounts::find(key);
                        bool microsoft = account != nullptr && account->type == blb::auth::account_type::microsoft;
                        bool thirdParty = account != nullptr && blb::auth::is_yggdrasil(account->type);
                        items.push_back(ActionMenuItem::make("设为当前账户", 0xf00c, [key] {
                                            Accounts::select(key);
                                        })->enabledIf([key] {
                            const blb::auth::account* current = Accounts::current();
                            return current == nullptr || Accounts::keyOf(*current) != key;
                        }));
                        if (thirdParty) {
                            if (Accounts::expired(key)) {
                                items.push_back(ActionMenuItem::make("重新登录", 0xf090, [key] {
                                    if (const blb::auth::account* found = Accounts::find(key)) {
                                        openServerLogin(found->type, found->server, found->username);
                                    }
                                }));
                            } else {
                                items.push_back(ActionMenuItem::make("刷新", 0xf2f1, [key] {
                                                    Accounts::refresh(key);
                                                })->enabledIf([key] { return !Accounts::busy(key); }));
                            }
                        }
                        if (microsoft) {
                            if (Accounts::expired(key)) {
                                items.push_back(
                                        ActionMenuItem::make("重新登录", 0xf090, [] {
                                            Accounts::startLogin();
                                        })->enabledIf([] { return Accounts::login() == Accounts::Login::Idle; }));
                            } else {
                                items.push_back(ActionMenuItem::make("刷新", 0xf2f1, [key] {
                                                    Accounts::refresh(key);
                                                })->enabledIf([key] { return !Accounts::busy(key); }));
                            }
                        }
                        items.push_back(ActionMenuItem::make("复制 UUID", 0xf0c5, [key] {
                            if (const blb::auth::account* found = Accounts::find(key)) {
                                ImGui::SetClipboardText(blb::auth::format_uuid(found->uuid).c_str());
                            }
                        }));
                        items.push_back(ActionMenuItem::make("删除", 0xf2ed, [key] { Accounts::remove(key); })
                                                ->dangerous()
                                                ->confirmFirst());
                        return items;
                    },
                    x, y, 160.0f);
        }

        void accountRow(const blb::auth::account& account, bool current, float rowY, int index, int count,
                        bool blocked) {
            std::string key = Accounts::keyOf(account);
            bool hovered = !blocked && NewUi::hit(Page::LEFT, rowY, Page::width(), Page::ROW_H);
            Page::rowHover("accounts#row#" + key, rowY, hovered, corners(index, count));
            avatar(Accounts::face(account), account.name, rowY);
            float nameW = UiFont::width(account.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(account.name, NAME_X, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float tagX = NAME_X + nameW + 10.0f;
            tagX += tag(Accounts::kindOf(account), tagX, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) +
                    6.0f;
            if (Accounts::expired(key)) {
                tag("需要重新登录", tagX, rowY, UiTheme::dangerSoft(), UiTheme::danger());
            }

            float menuX = Page::controlRight() - 26.0f;
            float markX = menuX - 16.0f;
            if (Accounts::busy(key)) {
                Page::spinner(markX, rowY + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
            } else if (current) {
                UiWidgets::check(markX, rowY + Page::ROW_H * 0.5f, UiTheme::accent(), 10.0f);
            }
            if (iconButton("accounts#more#" + key, 0xf141, "更多操作", menuX, rowY + 6.0f, blocked)) {
                openAccountMenu(key, menuX - 132.0f, rowY + 36.0f);
            }
            if (hovered && NewUi::rightClick) {
                NewUi::rightClick = false;
                openAccountMenu(key, NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(Page::LEFT, rowY, Page::width(), Page::ROW_H)) {
                UiSound::request(UiSound::TICK);
                Accounts::select(key);
            }
        }

        void finishOffline() {
            addingOffline = false;
            offlineName.clear();
            if (NewUi::textEditing == OFFLINE_OWNER) {
                NewUi::cancelTextEdit();
            }
        }

        void addOffline(const std::string& name) {
            std::string problem = Accounts::addOffline(name);
            if (!problem.empty()) {
                Notifications::add(Notifications::FAILED, problem, 3500);
                return;
            }
            finishOffline();
        }

        std::shared_ptr<TextBinding> offlineValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        OFFLINE_OWNER, [] { return offlineName; },
                        [](const std::string& text) {
                            offlineName = text;
                            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                                addOffline(text);
                            }
                        },
                        "玩家名");
                binding->limit = 16;
                return binding;
            }();
            return value;
        }

        void beginOffline() {
            addingOffline = true;
            offlineName.clear();
            NewUi::beginTextEdit(offlineValue());
        }

        void offlineRow(float rowY, int index, int count, bool blocked) {
            bool editing = NewUi::textEditing == OFFLINE_OWNER;
            if (!editing && offlineName.empty()) {
                addingOffline = false;
                return;
            }
            Page::rowHover("accounts#row#new", rowY, false, corners(index, count));
            std::string typed = editing ? UiInput::get(NewUi::INPUT_TEXT) : offlineName;
            avatar(0, typed, rowY);
            float fieldX = NAME_X - 9.0f;
            TextFieldWidget field(offlineValue());
            field.box(fieldX, rowY + Page::FIELD_Y, 180.0f, Page::FIELD_H)
                    .bounds(fieldX, rowY + Page::FIELD_Y, 180.0f, Page::FIELD_H)
                    .block(blocked);
            field.paint();

            std::string add = "添加";
            float addW = Page::buttonWidth(add);
            float addX = Page::controlRight() - addW;
            if (Page::button("accounts#offline#add", add, addX, rowY + Page::FIELD_Y, addW, true, blocked)) {
                addOffline(typed);
            }
            std::string cancel = "取消";
            float cancelW = Page::buttonWidth(cancel);
            if (Page::button("accounts#offline#cancel", cancel, addX - 8.0f - cancelW, rowY + Page::FIELD_Y, cancelW,
                             false, blocked)) {
                finishOffline();
            }
        }

        bool importable() {
            const std::vector<Accounts::Found>& found = Accounts::found();
            return std::any_of(found.begin(), found.end(), [](const Accounts::Found& entry) {
                return entry.state != Accounts::Found::State::Imported;
            });
        }

        void openImportMenu(float x, float y) {
            NewUi::openMenu(
                    "accounts#import",
                    [] {
                        MenuItems items;
                        const std::vector<Accounts::Found>& found = Accounts::found();
                        if (found.empty()) {
                            std::string text =
                                    Accounts::scanning() ? "正在查找\xE2\x80\xA6" : "没有找到其他启动器保存的微软账户";
                            items.push_back(ActionMenuItem::make(text, 0xf05a)->enabledIf([] { return false; }));
                            return items;
                        }
                        std::vector<std::string> launchers;
                        for (const Accounts::Found& entry : found) {
                            if (std::find(launchers.begin(), launchers.end(), entry.source.launcher) ==
                                launchers.end()) {
                                launchers.push_back(entry.source.launcher);
                            }
                        }
                        for (const std::string& launcher : launchers) {
                            MenuItems accounts;
                            int unnamed = 0;
                            for (size_t i = 0; i < found.size(); i++) {
                                const Accounts::Found& entry = found[i];
                                if (entry.source.launcher != launcher) {
                                    continue;
                                }
                                std::string label = entry.source.name.empty() ? "账户 " + std::to_string(++unnamed)
                                                                              : entry.source.name;
                                int glyph = 0xf007;
                                switch (entry.state) {
                                    case Accounts::Found::State::Importing:
                                        label += "（正在导入）";
                                        glyph = 0xf110;
                                        break;
                                    case Accounts::Found::State::Imported:
                                        label += "（已导入）";
                                        glyph = 0xf00c;
                                        break;
                                    case Accounts::Found::State::Failed:
                                        label += "（重试）";
                                        glyph = 0xf071;
                                        break;
                                    default: break;
                                }
                                accounts.push_back(ActionMenuItem::make(label, glyph, [i] {
                                                       Accounts::import(i);
                                                   })->enabledIf([i] {
                                    const std::vector<Accounts::Found>& now = Accounts::found();
                                    return i < now.size() &&
                                            (now[i].state == Accounts::Found::State::Ready ||
                                             now[i].state == Accounts::Found::State::Failed);
                                }));
                            }
                            uint32_t image = 0;
                            for (const Accounts::Found& entry : found) {
                                if (entry.source.launcher == launcher) {
                                    switch (entry.source.app) {
                                        case MicrosoftClients::App::Official: image = Icons::vanilla(); break;
                                        case MicrosoftClients::App::Hmcl: image = Icons::hmcl(); break;
                                        case MicrosoftClients::App::Pcl: image = Icons::pcl(); break;
                                        default: break;
                                    }
                                    break;
                                }
                            }
                            items.push_back(ActionMenuItem::make(launcher, 0xf1b2)->image(image)->nest(accounts));
                        }
                        return items;
                    },
                    x, y, 180.0f);
        }

        void addServerItems(MenuItems& items) {
            items.push_back(ActionMenuItem::make("第三方账户（Authlib Injector）", 0xf0c1, [] {
                openServerLogin(blb::auth::account_type::yggdrasil, std::string(), std::string());
            }));
            items.push_back(ActionMenuItem::make("统一通行证", 0xf2c2, [] {
                openServerLogin(blb::auth::account_type::nide8, std::string(), std::string());
            }));
        }

        void openServerMenu(float x, float y) {
            NewUi::openMenu(
                    "accounts#third",
                    [] {
                        MenuItems items;
                        addServerItems(items);
                        return items;
                    },
                    x, y, 230.0f);
        }

        void openAddMenu(float x, float y) {
            NewUi::openMenu(
                    "accounts#add",
                    [] {
                        MenuItems items;
                        items.push_back(ActionMenuItem::make("微软账户", 0xf090, [] { Accounts::startLogin(); }));
                        addServerItems(items);
                        bool allowed = hasMicrosoft();
                        items.push_back(
                                ActionMenuItem::make(allowed ? "离线账户" : "离线账户（先登录微软账户）", 0xf007, [] {
                                    beginOffline();
                                })->enabledIf([] { return hasMicrosoft(); }));
                        return items;
                    },
                    x, y, 230.0f);
        }

        void actions(bool blocked) {
            Accounts::Login state = Accounts::login();
            if (state != Accounts::Login::Idle) {
                addPending = false;
                std::string cancel = "取消";
                float cancelW = Page::buttonWidth(cancel);
                float cancelX = Page::right() - cancelW;
                if (Page::button("accounts#cancel", cancel, cancelX, ACTION_Y, cancelW, false, blocked)) {
                    Accounts::cancelLogin();
                }
                std::string status = state == Accounts::Login::Browser ? "在浏览器中完成登录" : "正在登录";
                float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float statusX = cancelX - 12.0f - statusW;
                UiRender::textY(status, statusX, ACTION_Y, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                Page::spinner(statusX - 14.0f, ACTION_Y + Page::FIELD_H * 0.5f, 7.0f, UiTheme::accent());
                return;
            }
            const int ADD_GLYPH = 0xf234;
            const int IMPORT_GLYPH = 0xf56f;
            std::string add = "添加";
            float addW = Page::buttonWidth(add, ADD_GLYPH);
            float addX = Page::right() - addW;
            if (Page::button("accounts#add", add, addX, ACTION_Y, addW, true, blocked, ADD_GLYPH) || addPending) {
                addPending = false;
                openAddMenu(Page::right() - 230.0f, ACTION_Y + Page::FIELD_H + 6.0f);
            }
            if (!importable()) {
                if (NewUi::menuOwner == "accounts#import") {
                    NewUi::closeMenu();
                }
                return;
            }
            std::string import = "导入";
            float importW = Page::buttonWidth(import, IMPORT_GLYPH);
            float importX = addX - 8.0f - importW;
            if (Page::button("accounts#import", import, importX, ACTION_Y, importW, false, blocked, IMPORT_GLYPH)) {
                openImportMenu(importX, ACTION_Y + Page::FIELD_H + 6.0f);
            }
        }

        constexpr float WELCOME_W = 360.0f;
        constexpr float SOURCE_H = 48.0f;
        constexpr float SOURCE_ICON = 28.0f;
        constexpr int SIGN_IN_GLYPH = 0xf090;

        struct Source {
            std::string launcher;
            MicrosoftClients::App app = MicrosoftClients::App::Launcher;
            int count = 0;
            bool importing = false;
        };

        std::vector<Source> sources() {
            std::vector<Source> out;
            for (const Accounts::Found& entry : Accounts::found()) {
                if (entry.state == Accounts::Found::State::Imported) {
                    continue;
                }
                auto at = std::find_if(out.begin(), out.end(),
                                       [&](const Source& source) { return source.launcher == entry.source.launcher; });
                if (at == out.end()) {
                    out.push_back(Source{entry.source.launcher, entry.source.app});
                    at = out.end() - 1;
                }
                at->count++;
                at->importing = at->importing || entry.state == Accounts::Found::State::Importing;
            }
            return out;
        }

        constexpr int STEP_LAUNCHERS = 0;
        constexpr int STEP_ACCOUNTS = 1;
        PageTransition welcomeSteps(STEP_LAUNCHERS);
        std::string pickingLauncher;
        std::set<std::string> picked;

        void choose(const std::string& launcher) {
            pickingLauncher = launcher;
            picked.clear();
            for (const Accounts::Found& entry : Accounts::found()) {
                if (entry.source.launcher == launcher && entry.state != Accounts::Found::State::Imported) {
                    picked.insert(entry.source.refreshToken);
                }
            }
            welcomeSteps.go(STEP_ACCOUNTS);
        }

        void backToLaunchers() {
            pickingLauncher.clear();
            picked.clear();
            welcomeSteps.go(STEP_LAUNCHERS);
        }

        void centredText(const std::string& text, float cx, float y, float h, uint32_t color, float size) {
            float w = UiFont::width(text, UiFont::MEDIUM, size);
            UiRender::textY(text, cx - w * 0.5f, y, h, color, UiFont::MEDIUM, size);
        }

        void sourceIcon(const Source& source, float x, float y) {
            uint32_t image = 0;
            switch (source.app) {
                case MicrosoftClients::App::Official: image = Icons::vanilla(); break;
                case MicrosoftClients::App::Hmcl: image = Icons::hmcl(); break;
                case MicrosoftClients::App::Pcl: image = Icons::pcl(); break;
                default: break;
            }
            if (image != 0) {
                UiRender::texture(image, x, y, SOURCE_ICON, SOURCE_ICON);
                return;
            }
            UiRender::roundRect(x, y, SOURCE_ICON, SOURCE_ICON, 7.0f, UiTheme::surfaceRaised());
            std::string letter = source.launcher.empty() ? "?" : std::string(1, source.launcher[0]);
            float letterW = UiFont::width(letter, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            UiRender::textY(letter, x + (SOURCE_ICON - letterW) * 0.5f, y, SOURCE_ICON, UiTheme::textSecondary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_BODY);
        }

        void sourceRow(const Source& source, float x, float y, int index, int count, bool blocked) {
            bool hovered = !blocked && !source.importing && NewUi::hit(x, y, WELCOME_W, SOURCE_H);
            float hover = UiState::motion("accounts#source#" + source.launcher, hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, WELCOME_W, SOURCE_H, 12.0f, UiTheme::overlay(Ui::round(14 * hover)),
                                    corners(index, count));
            }
            sourceIcon(source, x + 14.0f, y + (SOURCE_H - SOURCE_ICON) * 0.5f);
            UiRender::textY(source.launcher, x + 54.0f, y, SOURCE_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float right = x + WELCOME_W - 16.0f;
            float centerY = y + SOURCE_H * 0.5f;
            if (source.importing) {
                Page::spinner(right - 6.0f, centerY, 7.0f, UiTheme::accent());
            } else {
                UiWidgets::icon(Ui::glyph(0xf054), right - 4.0f, centerY,
                                UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), hover), 9.0f);
                std::string accounts = std::to_string(source.count) + " 个账户";
                float accountsW = UiFont::width(accounts, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(accounts, right - 20.0f - accountsW, y, SOURCE_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (!blocked && !source.importing && NewUi::click(x, y, WELCOME_W, SOURCE_H)) {
                UiSound::request(UiSound::PAGE);
                choose(source.launcher);
            }
        }

        void tickBox(const std::string& id, float x, float y, bool on) {
            const float size = 16.0f;
            float lit = UiState::motion(id + "#tick", on ? 1.0f : 0.0f, 18.0f);
            UiRender::roundRect(x, y, size, size, 4.0f,
                                UiRender::mix(UiTheme::surfaceRaised(), UiTheme::accent(), lit));
            if (lit < 0.99f) {
                UiRender::outline(x, y, size, size, 4.0f, UiRender::mix(UiTheme::outline(), UiTheme::accent(), lit));
            }
            if (lit > 0.01f) {
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * lit;
                UiWidgets::check(x + size * 0.5f, y + size * 0.5f, UiTheme::onAccent(), 8.0f);
                UiRender::alphaMul = savedAlpha;
            }
        }

        void pickRow(const Accounts::Found& entry, const std::string& name, const std::string& id, float x, float y,
                     int index, int count, bool blocked) {
            const std::string& token = entry.source.refreshToken;
            bool imported = entry.state == Accounts::Found::State::Imported;
            bool settled = imported || entry.state == Accounts::Found::State::Importing;
            bool hovered = !blocked && !settled && NewUi::hit(x, y, WELCOME_W, Page::ROW_H);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, WELCOME_W, Page::ROW_H, 12.0f, UiTheme::overlay(Ui::round(14 * hover)),
                                    corners(index, count));
            }
            float centerY = y + Page::ROW_H * 0.5f;
            if (imported) {
                UiWidgets::check(x + 24.0f, centerY, UiTheme::accent(), 10.0f);
            } else {
                tickBox(id, x + 16.0f, centerY - 8.0f, picked.count(token) != 0);
            }
            UiRender::textY(name, x + 44.0f, y, Page::ROW_H, imported ? UiTheme::textMuted() : UiTheme::textPrimary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float right = x + WELCOME_W - 16.0f;
            if (entry.state == Accounts::Found::State::Importing) {
                Page::spinner(right - 7.0f, centerY, 7.0f, UiTheme::accent());
            } else if (imported || entry.state == Accounts::Found::State::Failed) {
                std::string text = imported ? "已导入" : "导入失败";
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(text, right - w, y, Page::ROW_H, imported ? UiTheme::textMuted() : UiTheme::danger(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            if (!blocked && !settled && NewUi::click(x, y, WELCOME_W, Page::ROW_H)) {
                UiSound::request(UiSound::TICK);
                if (!picked.erase(token)) {
                    picked.insert(token);
                }
            }
        }

        bool centredButton(const std::string& id, const std::string& text, float cx, float y, bool primary,
                           bool blocked, int glyph) {
            float w = Page::buttonWidth(text, glyph);
            return Page::button(id, text, cx - w * 0.5f, y, w, primary, blocked, glyph);
        }

        void loginButtons(const std::string& id, const std::string& microsoft, float cx, float y, bool primary,
                          bool blocked) {
            std::string other = "第三方登录";
            float microsoftW = Page::buttonWidth(microsoft, SIGN_IN_GLYPH);
            float otherW = Page::buttonWidth(other, 0xf0c1);
            float x = cx - (microsoftW + 8.0f + otherW) * 0.5f;
            if (Page::button(id + "#microsoft", microsoft, x, y, microsoftW, primary, blocked, SIGN_IN_GLYPH)) {
                Accounts::startLogin();
            }
            float otherX = x + microsoftW + 8.0f;
            if (Page::button(id + "#third", other, otherX, y, otherW, false, blocked, 0xf0c1)) {
                openServerMenu(otherX, y + Page::FIELD_H + 6.0f);
            }
        }

        void launcherStep(float cx, float middle, float top, bool blocked) {
            std::vector<Source> list = sources();
            if (list.empty()) {
                if (Accounts::scanning() && Accounts::found().empty()) {
                    std::string text = "正在查找其他启动器的账户";
                    float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    Page::spinner(cx - (w + 22.0f) * 0.5f + 7.0f, middle, 7.0f, UiTheme::accent());
                    UiRender::textY(text, cx - (w + 22.0f) * 0.5f + 22.0f, middle - 12.0f, 24.0f, UiTheme::textMuted(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    return;
                }
                float y = middle - (24.0f + 18.0f + Page::FIELD_H) * 0.5f;
                centredText("还没有账户", cx, y, 24.0f, UiTheme::textPrimary(), Page::TITLE_SIZE);
                loginButtons("accounts#welcome#login", "登录微软账户", cx, y + 24.0f + 18.0f, true, blocked);
                return;
            }

            float listH = list.size() * SOURCE_H;
            float total = 24.0f + 18.0f + listH + 18.0f + Page::FIELD_H;
            float y = std::max(top, middle - total * 0.5f);
            centredText("从其他启动器导入账户", cx, y, 24.0f, UiTheme::textPrimary(), Page::TITLE_SIZE);
            y += 24.0f + 18.0f;
            float x = cx - WELCOME_W * 0.5f;
            UiRender::roundRect(x, y, WELCOME_W, listH, 12.0f, UiTheme::surface());
            UiRender::outline(x, y, WELCOME_W, listH, 12.0f, UiTheme::outlineSoft());
            for (size_t i = 1; i < list.size(); i++) {
                UiRender::hLine(x + 14.0f, x + WELCOME_W - 14.0f, y + i * SOURCE_H, UiTheme::outlineSoft());
            }
            for (size_t i = 0; i < list.size(); i++) {
                sourceRow(list[i], x, y + i * SOURCE_H, (int) i, (int) list.size(), blocked);
            }
            y += listH + 18.0f;
            loginButtons("accounts#welcome#skip", "直接登录微软账户", cx, y, false, blocked);
        }

        void pickStep(float cx, float middle, float top, bool blocked) {
            const std::vector<Accounts::Found>& found = Accounts::found();
            std::vector<size_t> rows;
            for (size_t i = 0; i < found.size(); i++) {
                if (found[i].source.launcher == pickingLauncher) {
                    rows.push_back(i);
                }
            }
            float listH = rows.size() * Page::ROW_H;
            float total = 24.0f + 18.0f + listH + 18.0f + Page::FIELD_H;
            float y = std::max(top, middle - total * 0.5f);
            centredText("选择要从 " + pickingLauncher + " 导入的账户", cx, y, 24.0f, UiTheme::textPrimary(),
                        Page::TITLE_SIZE);
            y += 24.0f + 18.0f;
            float x = cx - WELCOME_W * 0.5f;
            UiRender::roundRect(x, y, WELCOME_W, listH, 12.0f, UiTheme::surface());
            UiRender::outline(x, y, WELCOME_W, listH, 12.0f, UiTheme::outlineSoft());
            for (size_t i = 1; i < rows.size(); i++) {
                UiRender::hLine(x + 14.0f, x + WELCOME_W - 14.0f, y + i * Page::ROW_H, UiTheme::outlineSoft());
            }
            int unnamed = 0;
            int chosen = 0;
            bool importing = false;
            for (size_t i = 0; i < rows.size(); i++) {
                const Accounts::Found& entry = found[rows[i]];
                std::string name = entry.source.name.empty() ? "账户 " + std::to_string(++unnamed) : entry.source.name;
                pickRow(entry, name, "accounts#pick#" + std::to_string(rows[i]), x, y + i * Page::ROW_H, (int) i,
                        (int) rows.size(), blocked);
                bool ready =
                        entry.state == Accounts::Found::State::Ready || entry.state == Accounts::Found::State::Failed;
                chosen += ready && picked.count(entry.source.refreshToken) != 0 ? 1 : 0;
                importing = importing || entry.state == Accounts::Found::State::Importing;
            }
            y += listH + 18.0f;

            std::string back = "返回";
            std::string import = chosen > 0 ? "导入 " + std::to_string(chosen) + " 个账户" : std::string("导入");
            float backW = Page::buttonWidth(back);
            float importW = Page::buttonWidth(import, 0xf56f);
            float buttonsX = cx - (backW + 8.0f + importW) * 0.5f;
            if (Page::button("accounts#pick#back", back, buttonsX, y, backW, false, blocked)) {
                backToLaunchers();
            }
            bool usable = chosen > 0 && !importing;
            float enabled = UiState::motion("accounts#pick#enabled", usable ? 1.0f : 0.0f, 14.0f);
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * UiRender::lerp(0.45f, 1.0f, enabled);
            if (Page::button("accounts#pick#import", import, buttonsX + backW + 8.0f, y, importW, true,
                             blocked || !usable, 0xf56f)) {
                for (size_t index : rows) {
                    if (picked.count(found[index].source.refreshToken) != 0) {
                        Accounts::import(index);
                    }
                }
            }
            UiRender::alphaMul = savedAlpha;
        }

        void welcome(bool blocked) {
            const float top = Page::TOP;
            float cx = Page::LEFT + Page::width() * 0.5f;
            float middle = MainWindow::height() * 0.5f;

            Accounts::Login state = Accounts::login();
            if (state != Accounts::Login::Idle) {
                std::string status = state == Accounts::Login::Browser ? "在浏览器中完成登录" : "正在登录";
                centredText(status, cx + 11.0f, middle - 28.0f, 24.0f, UiTheme::textSecondary(), UiWidgets::TEXT_BODY);
                float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_BODY);
                Page::spinner(cx + 11.0f - statusW * 0.5f - 14.0f, middle - 16.0f, 7.0f, UiTheme::accent());
                if (centredButton("accounts#welcome#cancel", "取消", cx, middle + 8.0f, false, blocked, 0)) {
                    Accounts::cancelLogin();
                }
                return;
            }

            const std::vector<Accounts::Found>& found = Accounts::found();
            if (!pickingLauncher.empty() && std::none_of(found.begin(), found.end(), [](const Accounts::Found& entry) {
                    return entry.source.launcher == pickingLauncher;
                })) {
                backToLaunchers();
            }
            welcomeSteps.begin();
            if (welcomeSteps.page == STEP_ACCOUNTS && !pickingLauncher.empty()) {
                pickStep(cx, middle, top, blocked);
            } else {
                launcherStep(cx, middle, top, blocked);
            }
            welcomeSteps.end();
        }
    }

    namespace {
        bool ours(const std::string& owner) {
            return owner == ADDRESS_OWNER || owner == USER_OWNER || owner == PASSWORD_OWNER;
        }

        std::string live(const char* owner, const std::string& stored) {
            return NewUi::textEditing == owner ? Ui::trim(UiInput::get(NewUi::INPUT_TEXT)) : stored;
        }

        bool nide8() {
            return serverType == blb::auth::account_type::nide8;
        }

        void submitServer() {
            std::string address = live(ADDRESS_OWNER, serverAddress);
            std::string user = live(USER_OWNER, serverUser);
            std::string password = live(PASSWORD_OWNER, serverPassword);
            std::string problem;
            if (address.empty()) {
                problem = nide8() ? "请填写服务器 ID" : "请填写认证服务器地址";
            } else if (nide8() && !blb::auth::valid_nide8_id(address)) {
                problem = "服务器 ID 应该是 32 位的字母和数字";
            } else if (user.empty()) {
                problem = "请填写账号";
            } else if (password.empty()) {
                problem = "请填写密码";
            }
            if (!problem.empty()) {
                Notifications::add(Notifications::FAILED, problem, 3500);
                return;
            }
            if (ours(NewUi::textEditing)) {
                NewUi::commitTextEdit();
            }
            serverAddress = address;
            serverUser = user;
            serverPassword = password;
            Accounts::startServerLogin(serverType, address, user, password);
        }

        std::shared_ptr<TextBinding> serverField(const char* owner, std::string* target, const std::string& hint,
                                                 const char* next) {
            auto binding = std::make_shared<TextBinding>(
                    owner, [target] { return *target; },
                    [target, next](const std::string& text) {
                        *target = text;
                        if (!ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                            return;
                        }
                        if (next != nullptr) {
                            focusNext = next;
                        } else {
                            submitServer();
                        }
                    },
                    hint);
            binding->limit = 256;
            binding->masked = target == &serverPassword;
            return binding;
        }

        std::shared_ptr<TextBinding> addressField() {
            return serverField(ADDRESS_OWNER, &serverAddress,
                               nide8() ? "服主后台 → 集成说明中的服务器 ID" : "Yggdrasil API 地址，例如 littleskin.cn",
                               USER_OWNER);
        }

        std::shared_ptr<TextBinding> userField() {
            return serverField(USER_OWNER, &serverUser, nide8() ? "邮箱或统一通行证账号" : "邮箱或用户名",
                               PASSWORD_OWNER);
        }

        std::shared_ptr<TextBinding> passwordField() {
            return serverField(PASSWORD_OWNER, &serverPassword, "密码", nullptr);
        }

        void closeServer(bool cancel) {
            if (cancel) {
                Accounts::cancelServerLogin();
            }
            if (ours(NewUi::textEditing)) {
                NewUi::cancelTextEdit();
            }
            serverPassword.clear();
            focusNext.clear();
            views.go(VIEW_LIST);
        }

        std::string registerUrl(const std::string& address) {
            if (nide8()) {
                return blb::auth::valid_nide8_id(address) ? blb::auth::nide8_register_url(address) : std::string();
            }
            return blb::auth::same_yggdrasil_server(address, blb::auth::littleskin_api) ? LITTLESKIN_REGISTER
                                                                                        : std::string();
        }

        float fieldWidth() {
            return std::max(160.0f, std::min(360.0f, Page::controlRight() - Page::LEFT - 130.0f));
        }

        void formRow(const char* label, const std::shared_ptr<TextBinding>& value, float rowY, bool blocked) {
            Page::label(label, rowY);
            float w = fieldWidth();
            float x = Page::controlRight() - w;
            TextFieldWidget field(value);
            field.box(x, rowY + Page::FIELD_Y, w, Page::FIELD_H)
                    .bounds(x, rowY + Page::FIELD_Y, w, Page::FIELD_H)
                    .block(blocked);
            field.paint();
        }

        void serverForm(float y, bool covered) {
            bool working = Accounts::serverLogin() == Accounts::ServerLogin::Working;
            bool blocked = covered || working;
            Page::card(y, 3);
            formRow(nide8() ? "服务器 ID" : "认证服务器", addressField(), y, blocked);
            formRow("账号", userField(), y + Page::ROW_H, blocked);
            formRow("密码", passwordField(), y + 2 * Page::ROW_H, blocked);
            if (!focusNext.empty() && NewUi::textEditing.empty()) {
                std::string next = focusNext;
                focusNext.clear();
                NewUi::beginTextEdit(next == ADDRESS_OWNER        ? addressField()
                                             : next == USER_OWNER ? userField()
                                                                  : passwordField());
            }

            float buttonY = y + 3 * Page::ROW_H + 14.0f;
            float x = Page::LEFT;
            std::string preset = nide8() ? "服主后台" : "设置为 LittleSkin";
            float presetW = Page::buttonWidth(preset);
            if (Page::button("accounts#server#preset", preset, x, buttonY, presetW, false, blocked)) {
                if (nide8()) {
                    Platform::openUrl(std::string(blb::auth::nide8_admin_url));
                } else {
                    if (NewUi::textEditing == ADDRESS_OWNER) {
                        NewUi::cancelTextEdit();
                    }
                    serverAddress = std::string(blb::auth::littleskin_api);
                    if (serverUser.empty()) {
                        focusNext = USER_OWNER;
                    }
                }
            }
            x += presetW + 8.0f;
            std::string signUp = registerUrl(live(ADDRESS_OWNER, serverAddress));
            if (!signUp.empty()) {
                std::string text = "注册账号";
                float w = Page::buttonWidth(text);
                if (Page::button("accounts#server#register", text, x, buttonY, w, false, blocked)) {
                    Platform::openUrl(signUp);
                }
            }

            std::string login = "登录";
            float loginW = Page::buttonWidth(login, SIGN_IN_GLYPH);
            float loginX = Page::right() - loginW;
            if (working) {
                std::string status = "正在登录";
                float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float statusX = Page::right() - statusW;
                UiRender::textY(status, statusX, buttonY, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                Page::spinner(statusX - 14.0f, buttonY + Page::FIELD_H * 0.5f, 7.0f, UiTheme::accent());
                loginX = statusX - 22.0f;
            } else if (Page::button("accounts#server#login", login, loginX, buttonY, loginW, true, covered,
                                    SIGN_IN_GLYPH)) {
                submitServer();
            }
            std::string cancel = "取消";
            float cancelW = Page::buttonWidth(cancel);
            if (Page::button("accounts#server#cancel", cancel, loginX - 8.0f - cancelW, buttonY, cancelW, false,
                             covered)) {
                if (working) {
                    Accounts::cancelServerLogin();
                } else {
                    closeServer(true);
                }
            }
        }

        void profileList(float y, bool covered) {
            const std::vector<blb::auth::yggdrasil_profile>& profiles = Accounts::serverProfiles();
            bool working = Accounts::serverLogin() == Accounts::ServerLogin::Working;
            bool blocked = covered || working;
            int count = (int) profiles.size();
            Page::Scroll body("accounts#profiles", y, MainWindow::height() - 14.0f, count * Page::ROW_H + 52.0f);
            bool outside = body.begin();
            float top = body.top - body.offset;
            Page::card(top, count);
            for (int i = 0; i < count; i++) {
                float rowY = top + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const blb::auth::yggdrasil_profile& profile = profiles[i];
                bool hovered = !blocked && !outside && NewUi::hit(Page::LEFT, rowY, Page::width(), Page::ROW_H);
                Page::rowHover("accounts#profile#" + profile.id, rowY, hovered, corners(i, count));
                avatar(0, profile.name, rowY);
                UiRender::textY(profile.name, NAME_X, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                float cy = rowY + Page::ROW_H * 0.5f;
                if (working && chosenProfile == (size_t) i) {
                    Page::spinner(Page::controlRight() - 8.0f, cy, 7.0f, UiTheme::accent());
                } else {
                    UiWidgets::icon(Ui::glyph(0xf054), Page::controlRight() - 6.0f, cy, UiTheme::textMuted(), 9.0f);
                }
                if (!blocked && !outside && NewUi::click(Page::LEFT, rowY, Page::width(), Page::ROW_H)) {
                    UiSound::request(UiSound::TICK);
                    chosenProfile = (size_t) i;
                    Accounts::chooseProfile((size_t) i);
                }
            }
            std::string back = "返回";
            float backW = Page::buttonWidth(back);
            if (Page::button("accounts#profiles#back", back, Page::right() - backW, top + count * Page::ROW_H + 14.0f,
                             backW, false, covered || outside)) {
                Accounts::cancelServerLogin();
            }
            body.end();
        }

        void serverView(bool covered) {
            bool choosing = Accounts::serverLogin() == Accounts::ServerLogin::Choosing ||
                    (Accounts::serverLogin() == Accounts::ServerLogin::Working && !Accounts::serverProfiles().empty());
            if (iconButton("accounts#server#back", 0xf053, "返回", Page::LEFT - 6.0f, Page::TOP - 2.0f, covered)) {
                closeServer(true);
                return;
            }
            std::string title = choosing ? "选择角色" : nide8() ? "统一通行证" : "第三方登录";
            Page::title(title, Page::LEFT + 28.0f, Page::TOP);
            if (choosing) {
                profileList(Page::TOP + 40.0f, covered);
            } else {
                serverForm(Page::TOP + 40.0f, covered);
            }
        }

        void listView() {
            static int lastFrame = -10;
            int frame = ImGui::GetFrameCount();
            if (frame - lastFrame > 1) {
                Accounts::rescan();
            }
            lastFrame = frame;
            const std::vector<blb::auth::account>& accounts = Accounts::list();
            const blb::auth::account* current = Accounts::current();
            bool covered = NewUi::overlayCoversMouse();
            if (addingOffline && !hasMicrosoft()) {
                finishOffline();
            }

            int count = (int) accounts.size() + (addingOffline ? 1 : 0);
            if (count == 0 || views.page != VIEW_LIST) {
                addPending = false;
            }
            float first = UiState::motion("accounts#welcome", count == 0 ? 1.0f : 0.0f, 10.0f);
            float savedAlpha = UiRender::alphaMul;
            if (first > 0.01f) {
                UiRender::alphaMul = savedAlpha * first;
                welcome(covered || first < 0.5f);
                UiRender::alphaMul = savedAlpha;
            }
            if (first > 0.99f) {
                return;
            }
            if (first < 0.01f && welcomeSteps.page != STEP_LAUNCHERS) {
                pickingLauncher.clear();
                picked.clear();
                welcomeSteps.page = STEP_LAUNCHERS;
                welcomeSteps.mix = 1.0f;
            }
            UiRender::alphaMul = savedAlpha * (1.0f - first);
            Page::title("账户", Page::LEFT, Page::TOP);
            bool listBlocked = covered || first > 0.5f;
            actions(listBlocked);

            int rows = std::max(1, count);
            Page::Scroll body("accounts#body", Page::TOP + 40.0f, MainWindow::height() - 14.0f, rows * Page::ROW_H);
            bool outside = body.begin();
            bool blocked = outside || listBlocked;
            float y = body.top - body.offset;

            Page::card(y, rows);
            std::vector<blb::auth::account> shown = accounts;
            std::string currentKey = current != nullptr ? Accounts::keyOf(*current) : std::string();
            for (size_t i = 0; i < shown.size(); i++) {
                if (Page::shown(y + i * Page::ROW_H)) {
                    accountRow(shown[i], Accounts::keyOf(shown[i]) == currentKey, y + i * Page::ROW_H, (int) i, count,
                               blocked);
                }
            }
            if (addingOffline) {
                offlineRow(y + shown.size() * Page::ROW_H, (int) shown.size(), count, blocked);
            }

            body.end();
            UiRender::alphaMul = savedAlpha;
        }
    }

    void draw() {
        if (Accounts::serverLogin() == Accounts::ServerLogin::Done) {
            Accounts::finishServerLogin();
            closeServer(false);
        }
        bool covered = NewUi::overlayCoversMouse();
        views.begin();
        if (views.page == VIEW_SERVER) {
            serverView(covered);
        } else {
            listView();
        }
        views.end();
    }

    void openAdd() {
        addPending = true;
    }

    void openServerLogin(blb::auth::account_type type, const std::string& server, const std::string& username) {
        Accounts::ServerLogin state = Accounts::serverLogin();
        if (state == Accounts::ServerLogin::Working || state == Accounts::ServerLogin::Choosing) {
            Accounts::cancelServerLogin();
        }
        if (ours(NewUi::textEditing)) {
            NewUi::cancelTextEdit();
        }
        serverType = type;
        serverAddress = server;
        serverUser = username;
        serverPassword.clear();
        focusNext = server.empty() ? ADDRESS_OWNER : username.empty() ? USER_OWNER : PASSWORD_OWNER;
        views.go(VIEW_SERVER);
    }
}
