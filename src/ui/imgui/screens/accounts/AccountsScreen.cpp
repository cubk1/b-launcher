#include "ui/imgui/screens/accounts/AccountsScreen.h"
#include "app/system/Customization.h"
#include "app/accounts/Accounts.h"
#include "app/accounts/Avatars.h"
#include "ui/imgui/screens/accounts/ProfilePage.h"
#include "ui/imgui/screens/home/WebImages.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/screens/shell/SvgPath.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "app/system/Platform.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace AccountsScreen {

    namespace blb = b_launcher_base;

    namespace {
        const char* const OFFLINE_OWNER = "accounts#offline";
        float actionY() {
            return Page::top() + 1.0f;
        }
        constexpr float CARD_H = 64.0f;
        constexpr float CARD_MIN_W = 236.0f;
        constexpr float CARD_GAP = 10.0f;
        constexpr float CARD_RADIUS = 12.0f;
        constexpr float BUST = 56.0f;
        constexpr float HEAD = 40.0f;
        constexpr float METHOD = 14.0f;
        constexpr float LINK_H = 18.0f;
        const char* const NIDE8_MARK =
                "F0 M17.79 0.55L31.01 6.15Q32.3 6.7 32.3 8.1L32.3 21.6Q32.3 23 31.05 23.63L17.75 30.37Q16.5 31 15.25 "
                "30.37L1.95 23.63Q0.7 23 0.7 21.6L0.7 8.1Q0.7 6.7 1.99 6.15L15.21 0.55Q16.5 0 17.79 0.55ZM16.5 "
                "1.9L28.9 6.9L16.5 11.9L4.1 6.9ZM17.8 13.8L29 9.3L29 22.1L17.8 27.2Z";

        std::string offlineName;
        bool addPending = false;

        constexpr int VIEW_LIST = 0;
        constexpr int VIEW_PROFILE = 2;
        PageTransition views(VIEW_LIST);

        const char* const ADDRESS_OWNER = "accounts#server#address";
        const char* const USER_OWNER = "accounts#server#user";
        const char* const PASSWORD_OWNER = "accounts#server#password";

        std::string serverUser;
        std::string serverPassword;
        std::string focusNext;

        bool hasOnline() {
            const std::vector<blb::auth::account>& accounts = Accounts::list();
            return std::any_of(accounts.begin(), accounts.end(), [](const blb::auth::account& account) {
                return account.type != blb::auth::account_type::offline;
            });
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
                                items.push_back(ActionMenuItem::make("用户资料", 0xf553, [key] {
                                    ProfilePage::open(key);
                                    views.go(VIEW_PROFILE);
                                }));
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

        void relogin(const std::string& key) {
            const blb::auth::account* account = Accounts::find(key);
            if (account == nullptr) {
                return;
            }
            if (account->type == blb::auth::account_type::microsoft) {
                if (Accounts::login() == Accounts::Login::Idle) {
                    Accounts::startLogin();
                }
            } else if (blb::auth::is_yggdrasil(account->type)) {
                openServerLogin(account->type, account->server, account->username);
            }
        }

        std::string originOf(const std::string& url) {
            size_t scheme = url.find("://");
            if (scheme == std::string::npos) {
                return std::string();
            }
            size_t path = url.find('/', scheme + 3);
            return path == std::string::npos ? url : url.substr(0, path);
        }

        void methodIcon(const blb::auth::account& account, float x, float cy, bool blocked) {
            float y = cy - METHOD * 0.5f;
            uint32_t image = 0;
            int glyph = 0xf007;
            if (account.type == blb::auth::account_type::microsoft) {
                image = Icons::microsoft();
            } else if (account.type == blb::auth::account_type::yggdrasil) {
                glyph = 0xf233;
                if (blb::auth::same_yggdrasil_server(account.server, blb::auth::littleskin_api)) {
                    image = Icons::littleskin();
                } else if (std::string origin = originOf(blb::auth::yggdrasil_api(account)); !origin.empty()) {
                    image = WebImages::get(origin + "/favicon.ico", origin + "/favicon.png").texture;
                }
            } else if (account.type == blb::auth::account_type::nide8) {
                SvgPath::draw(NIDE8_MARK, x, y, METHOD, METHOD, UiTheme::textSecondary());
                glyph = 0;
            }
            if (image != 0) {
                UiRender::texture(image, x, y, METHOD, METHOD);
            } else if (glyph != 0) {
                UiWidgets::icon(Ui::glyph(glyph), x + METHOD * 0.5f, cy, UiTheme::textMuted(), 10.0f);
            }
            if (!blocked && NewUi::hit(x, y, METHOD, METHOD)) {
                Page::tooltip(Accounts::kindOf(account), x, y, METHOD, METHOD);
            }
        }

        bool textLink(const std::string& text, float x, float y, uint32_t ink, uint32_t hot, bool blocked) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            bool over = !blocked && NewUi::hit(x, y, w, LINK_H);
            uint32_t color = over ? hot : ink;
            UiRender::textY(text, x, y, LINK_H, color, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::hLine(x, x + w, y + LINK_H * 0.5f + 6.0f, color);
            if (over) {
                UiCursor::hand();
            }
            return !blocked && NewUi::click(x, y, w, LINK_H);
        }

        void bustIn(uint32_t texture, float x, float size, float bottom) {
            if (texture == 0) {
                return;
            }
            float pixel = UiRender::pixelScale();
            float unit = UiRender::unit();
            float inner = std::round(UiRender::transformY(bottom) * pixel) / pixel - 1.0f / pixel;
            UiRender::flush();
            UiRender::drawList()->PushClipRect(
                    ImVec2(UiRender::transformX(x) * unit, UiRender::transformY(bottom - size) * unit),
                    ImVec2(UiRender::transformX(x + size) * unit, inner * unit), true);
            UiRender::texture(texture, x, bottom - size + 1.0f, size, size);
            UiRender::flush();
            UiRender::drawList()->PopClipRect();
        }

        void headIn(uint32_t texture, float x, float y, float size, float radius) {
            if (texture == 0) {
                return;
            }
            ShaderUtil::roundTex(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(size),
                                 UiRender::transformW(size), UiRender::transformW(radius), texture, 0.0f, 0.0f, 1.0f,
                                 1.0f, UiRender::applyAlpha(0xFFFFFFFFu));
        }

        void accountCard(const blb::auth::account& account, bool current, float x, float y, float w, bool blocked) {
            float h = CARD_H;
            std::string key = Accounts::keyOf(account);
            std::string id = "accounts#card#" + key;
            bool hovered = !blocked && NewUi::hit(x, y, w, h);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
            float lit = UiState::motion(id + "#current", current ? 1.0f : 0.0f, 14.0f);
            UiRender::roundRect(x, y, w, h, CARD_RADIUS, UiTheme::surface());
            float shine = 18.0f * lit + 8.0f * hover;
            if (shine > 0.1f) {
                UiRender::roundRect(x, y, w, h, CARD_RADIUS, UiTheme::overlay(Ui::round(shine)));
            }
            UiRender::outline(x, y, w, h, CARD_RADIUS, UiTheme::border(UiTheme::outlineSoft()));
            bool heads = Avatars::style() == Avatars::Style::Head;
            if (heads) {
                headIn(Textures::of(Avatars::head(account)), x + 12.0f, y + (h - HEAD) * 0.5f, HEAD, 8.0f);
            } else {
                bustIn(Textures::of(Avatars::bust(account)), x + 10.0f, BUST, y + h);
            }

            float textX = heads ? x + 12.0f + HEAD + 12.0f : x + 10.0f + BUST + 12.0f;
            float nameY = y + 11.0f;
            std::string name = UiFont::fitEnd(account.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                              std::max(0.0f, x + w - 30.0f - METHOD - 6.0f - textX));
            float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(name, textX, nameY, 22.0f, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            methodIcon(account, textX + nameW + 6.0f, nameY + 11.0f, blocked);

            float linkY = y + 34.0f;
            std::string edit = "编辑账号";
            if (textLink(edit, textX, linkY, UiTheme::textMuted(), UiTheme::textSecondary(), blocked)) {
                UiSound::request(UiSound::PAGE);
                ProfilePage::open(key);
                views.go(VIEW_PROFILE);
            }
            if (Accounts::expired(key)) {
                float reloginX = textX + UiFont::width(edit, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
                if (textLink("需要重新登录", reloginX, linkY, UiTheme::danger(), UiTheme::danger(), blocked)) {
                    relogin(key);
                }
            }

            float markX = x + w - 20.0f;
            float markY = y + h - 18.0f;
            if (Accounts::busy(key)) {
                Page::spinner(markX, markY, 7.0f, UiTheme::accent());
            } else if (lit > 0.01f) {
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * lit;
                UiWidgets::check(markX, markY, UiTheme::accent(), 10.0f);
                UiRender::alphaMul = savedAlpha;
            }
            if (hovered && !NewUi::overlayCoversMouse() && NewUi::rightClick) {
                NewUi::rightClick = false;
                openAccountMenu(key, NewUi::mouseX, NewUi::mouseY);
            }
            if (!blocked && NewUi::click(x, y, w, h)) {
                UiSound::request(UiSound::TICK);
                Accounts::select(key);
            }
        }

        void openOffline();

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
                                        case MicrosoftClients::App::Prism: image = Icons::prism(); break;
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

        std::string packServer() {
            for (const Customization::Login& rule : Customization::logins()) {
                if (rule.type == "yggdrasil" && !rule.server.empty()) {
                    return rule.server;
                }
            }
            return {};
        }

        void addServerItems(MenuItems& items) {
            if (!Customization::loginAllowed("yggdrasil", "")) {
                return;
            }
            items.push_back(ActionMenuItem::make("第三方账户", 0xf0c1, [] {
                openServerLogin(blb::auth::account_type::yggdrasil, packServer(), std::string());
            }));
        }

        void openAddMenu(float x, float y) {
            NewUi::openMenu(
                    "accounts#add",
                    [] {
                        MenuItems items;
                        if (Customization::loginAllowed("microsoft", "")) {
                            items.push_back(ActionMenuItem::make("微软账户", 0xf090, [] { Accounts::startLogin(); }));
                        }
                        addServerItems(items);
                        if (Customization::loginAllowed("offline", "")) {
                            bool offlineOnly =
                                    !Customization::logins().empty() && !Customization::loginAllowed("microsoft", "");
                            bool allowed = hasOnline() || offlineOnly;
                            items.push_back(ActionMenuItem::make(allowed ? "离线账户" : "离线账户（先登录微软账户）",
                                                                 0xf007, [] { openOffline(); })
                                                    ->enabledIf([offlineOnly] { return hasOnline() || offlineOnly; }));
                        }
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
                if (Page::button("accounts#cancel", cancel, cancelX, actionY(), cancelW, false, blocked)) {
                    Accounts::cancelLogin();
                }
                std::string status = state == Accounts::Login::Browser ? "在浏览器中完成登录" : "正在登录";
                float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float statusX = cancelX - 12.0f - statusW;
                UiRender::textY(status, statusX, actionY(), Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                Page::spinner(statusX - 14.0f, actionY() + Page::FIELD_H * 0.5f, 7.0f, UiTheme::accent());
                return;
            }
            const int ADD_GLYPH = 0xf234;
            const int IMPORT_GLYPH = 0xf56f;
            std::string add = "添加";
            float addW = Page::buttonWidth(add, ADD_GLYPH);
            float addX = Page::right() - addW;
            if (Page::button("accounts#add", add, addX, actionY(), addW, true, blocked, ADD_GLYPH) || addPending) {
                addPending = false;
                openAddMenu(Page::right() - 230.0f, actionY() + Page::FIELD_H + 6.0f);
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
            if (Page::button("accounts#import", import, importX, actionY(), importW, false, blocked, IMPORT_GLYPH)) {
                openImportMenu(importX, actionY() + Page::FIELD_H + 6.0f);
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

        void sourceIcon(const Source& source, float x, float y) {
            uint32_t image = 0;
            switch (source.app) {
                case MicrosoftClients::App::Official: image = Icons::vanilla(); break;
                case MicrosoftClients::App::Hmcl: image = Icons::hmcl(); break;
                case MicrosoftClients::App::Pcl: image = Icons::pcl(); break;
                case MicrosoftClients::App::Prism: image = Icons::prism(); break;
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
                                    Page::rowCorners(index, count));
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
                UiRender::outline(x, y, size, size, 4.0f,
                                  UiRender::mix(UiTheme::border(UiTheme::outline()), UiTheme::accent(), lit));
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
                                    Page::rowCorners(index, count));
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
            bool showMicrosoft = Customization::loginAllowed("microsoft", "");
            bool showOther = Customization::loginAllowed("yggdrasil", "");
            float microsoftW = showMicrosoft ? Page::buttonWidth(microsoft, SIGN_IN_GLYPH) : 0.0f;
            float otherW = showOther ? Page::buttonWidth(other, 0xf0c1) : 0.0f;
            float gap = showMicrosoft && showOther ? 8.0f : 0.0f;
            float x = cx - (microsoftW + gap + otherW) * 0.5f;
            if (showMicrosoft &&
                Page::button(id + "#microsoft", microsoft, x, y, microsoftW, primary, blocked, SIGN_IN_GLYPH)) {
                Accounts::startLogin();
            }
            float otherX = x + microsoftW + gap;
            if (showOther &&
                Page::button(id + "#third", other, otherX, y, otherW, !showMicrosoft && primary, blocked, 0xf0c1)) {
                openServerLogin(blb::auth::account_type::yggdrasil, packServer(), std::string());
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
                Page::centredText("还没有账户", cx, y, 24.0f, UiTheme::textPrimary(), Page::TITLE_SIZE);
                loginButtons("accounts#welcome#login", "登录微软账户", cx, y + 24.0f + 18.0f, true, blocked);
                return;
            }

            float listH = list.size() * SOURCE_H;
            float total = 24.0f + 18.0f + listH + 18.0f + Page::FIELD_H;
            float y = std::max(top, middle - total * 0.5f);
            Page::centredText("从其他启动器导入账户", cx, y, 24.0f, UiTheme::textPrimary(), Page::TITLE_SIZE);
            y += 24.0f + 18.0f;
            float x = cx - WELCOME_W * 0.5f;
            Page::listCard(x, y, WELCOME_W, SOURCE_H, (int) list.size());
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
            Page::centredText("选择要从 " + pickingLauncher + " 导入的账户", cx, y, 24.0f, UiTheme::textPrimary(),
                        Page::TITLE_SIZE);
            y += 24.0f + 18.0f;
            float x = cx - WELCOME_W * 0.5f;
            Page::listCard(x, y, WELCOME_W, Page::ROW_H, (int) rows.size());
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
            if (Page::button("accounts#pick#back", back, buttonsX, y, backW, false, blocked) || Page::escape(blocked)) {
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
            const float top = Page::top();
            float cx = Page::left() + Page::width() * 0.5f;
            float middle = MainWindow::height() * 0.5f;

            Accounts::Login state = Accounts::login();
            if (state != Accounts::Login::Idle) {
                std::string status = state == Accounts::Login::Browser ? "在浏览器中完成登录" : "正在登录";
                Page::centredText(status, cx + 11.0f, middle - 28.0f, 24.0f, UiTheme::textSecondary(), UiWidgets::TEXT_BODY);
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
        const char* const LOGIN_DIALOG = "accounts#login";
        const char* const SERVER_SOURCE = "accounts#login#server";
        constexpr float LOGIN_W = 360.0f;
        constexpr float LOGIN_PAD = 28.0f;
        constexpr float LOGIN_ICON = 44.0f;
        constexpr float LOGIN_TITLE = 26.0f;
        constexpr float LOGIN_FIELD = 30.0f;
        constexpr float LOGIN_GAP = 10.0f;
        constexpr float LOGIN_NOTE = 18.0f;
        constexpr float LOGIN_LABEL = 18.0f;
        constexpr float LOGIN_LABEL_GAP = 6.0f;
        constexpr float PICK_W = 470.0f;
        constexpr float PROFILE_H = 132.0f;
        constexpr float PROFILE_BUST = 92.0f;
        constexpr float PROFILE_HEAD = 64.0f;
        constexpr float PROFILE_ROW = 48.0f;
        constexpr float PROFILE_ROW_HEAD = 28.0f;
        constexpr float REMEMBER_H = 20.0f;
        constexpr float PROFILE_GAP = 12.0f;
        constexpr int PROFILE_COLUMNS = 3;
        constexpr float DIALOG_MARGIN = 24.0f;

        enum class Server { LittleSkin, Nide8, Custom };

        Server server = Server::LittleSkin;
        std::string customServer;
        std::string nide8Server;
        std::string loginProblem;
        bool rememberPassword = true;
        bool viaBrowser = false;
        std::set<std::string> pickedProfiles;
        float pickScroll = 0.0f;

        bool ours(const std::string& owner) {
            return owner == ADDRESS_OWNER || owner == USER_OWNER || owner == PASSWORD_OWNER;
        }

        std::string live(const char* owner, const std::string& stored) {
            if (NewUi::textEditing != owner) {
                return stored;
            }
            std::string text = UiInput::get(NewUi::INPUT_TEXT);
            return owner == PASSWORD_OWNER ? text : Ui::trim(text);
        }

        bool nide8() {
            return server == Server::Nide8;
        }

        blb::auth::account_type loginType() {
            return nide8() ? blb::auth::account_type::nide8 : blb::auth::account_type::yggdrasil;
        }

        std::string* addressTarget() {
            return nide8() ? &nide8Server : &customServer;
        }

        std::string loginAddress() {
            return server == Server::LittleSkin ? std::string(blb::auth::littleskin_api)
                                                : live(ADDRESS_OWNER, *addressTarget());
        }

        bool choosing() {
            Accounts::ServerLogin state = Accounts::serverLogin();
            return state == Accounts::ServerLogin::Choosing ||
                    (state == Accounts::ServerLogin::Working && !Accounts::serverProfiles().empty());
        }

        void recall() {
            std::optional<Accounts::Credential> saved = Accounts::rememberedLogin(loginType(), loginAddress());
            if (saved && (serverUser.empty() || serverUser == saved->username)) {
                serverUser = saved->username;
                serverPassword = saved->password;
            }
        }

        void finishLogin() {
            if (viaBrowser) {
                viaBrowser = false;
            } else if (rememberPassword) {
                Accounts::rememberLogin(loginType(), loginAddress(), serverUser, serverPassword);
            } else {
                Accounts::forgetLogin(loginType(), loginAddress());
            }
            Accounts::finishServerLogin();
            if (Dialog::isOpen(LOGIN_DIALOG)) {
                Dialog::close();
            }
        }

        void submitServer() {
            std::string address = loginAddress();
            std::string user = live(USER_OWNER, serverUser);
            std::string password = live(PASSWORD_OWNER, serverPassword);
            loginProblem = Accounts::serverLoginProblem(loginType(), address, user, password);
            if (!loginProblem.empty()) {
                return;
            }
            if (ours(NewUi::textEditing)) {
                NewUi::commitTextEdit();
            }
            if (server != Server::LittleSkin) {
                *addressTarget() = address;
            }
            pickedProfiles.clear();
            pickScroll = 0.0f;
            serverUser = user;
            serverPassword = password;
            viaBrowser = false;
            Accounts::startServerLogin(loginType(), address, user, password);
        }

        void submitBrowser() {
            if (ours(NewUi::textEditing)) {
                NewUi::commitTextEdit();
            }
            loginProblem.clear();
            pickedProfiles.clear();
            pickScroll = 0.0f;
            viaBrowser = true;
            Accounts::startBrowserLogin();
        }

        std::shared_ptr<TextBinding> serverField(const char* owner, std::string* target, const std::string& hint,
                                                 const char* next) {
            auto binding = std::make_shared<TextBinding>(
                    owner, [target] { return *target; },
                    [target, next](const std::string& text) {
                        bool moved = *target != text && (target == &customServer || target == &nide8Server);
                        *target = text;
                        loginProblem.clear();
                        if (moved && serverUser.empty() && serverPassword.empty()) {
                            recall();
                        }
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
            return serverField(ADDRESS_OWNER, addressTarget(), std::string(), USER_OWNER);
        }

        std::shared_ptr<TextBinding> userField() {
            return serverField(USER_OWNER, &serverUser, std::string(), PASSWORD_OWNER);
        }

        std::shared_ptr<TextBinding> passwordField() {
            return serverField(PASSWORD_OWNER, &serverPassword, std::string(), nullptr);
        }

        void switchServer(Server picked) {
            if (picked == server) {
                return;
            }
            if (NewUi::textEditing == ADDRESS_OWNER) {
                NewUi::cancelTextEdit();
            }
            server = picked;
            loginProblem.clear();
            if (std::optional<Accounts::Credential> saved = Accounts::rememberedLogin(loginType(), loginAddress())) {
                if (ours(NewUi::textEditing)) {
                    NewUi::cancelTextEdit();
                }
                serverUser = saved->username;
                serverPassword = saved->password;
            } else {
                serverPassword.clear();
            }
            if (picked != Server::LittleSkin && addressTarget()->empty()) {
                focusNext = ADDRESS_OWNER;
            } else if (serverUser.empty()) {
                focusNext = USER_OWNER;
            }
        }

        std::shared_ptr<ModeBinding> serverChoice() {
            static std::shared_ptr<ModeBinding> choice;
            if (choice == nullptr) {
                choice = ModeBinding::mapped<Server>(
                        SERVER_SOURCE, [] { return server; }, [](Server picked) { switchServer(picked); },
                        {{Server::LittleSkin, "LittleSkin"},
                         {Server::Nide8, "统一通行证"},
                         {Server::Custom, "其他认证服务器"}});
                choice->icon("其他认证服务器", 0xf233);
            }
            choice->image("LittleSkin", Icons::littleskin(), 0.0f);
            choice->image("统一通行证", SvgPath::texture(NIDE8_MARK, 48, UiTheme::textSecondary()), 0.0f);
            return choice;
        }

        void serverMark(float x, float y, float size) {
            uint32_t image = 0;
            if (server == Server::LittleSkin) {
                image = Icons::littleskin();
            } else if (server == Server::Nide8) {
                SvgPath::draw(NIDE8_MARK, x, y, size, size, UiTheme::textPrimary());
                return;
            } else if (!customServer.empty()) {
                std::string url =
                        customServer.find("://") == std::string::npos ? "https://" + customServer : customServer;
                std::string origin = originOf(url);
                image = WebImages::get(origin + "/favicon.ico", origin + "/favicon.png").texture;
            }
            if (image != 0) {
                UiRender::texture(image, x, y, size, size);
                return;
            }
            UiRender::roundRect(x, y, size, size, size * 0.25f, UiTheme::surfaceRaised());
            UiWidgets::icon(Ui::glyph(0xf233), x + size * 0.5f, y + size * 0.5f, UiTheme::textSecondary(), size * 0.4f);
        }

        void serverPicker(float x, float y, float w, bool blocked) {
            std::shared_ptr<ModeBinding> choice = serverChoice();
            float anchorY = y + (LOGIN_FIELD - 23.0f) * 0.5f;
            bool open = NewUi::popupOpen && NewUi::popupOwner == choice->owner && NewUi::popupSource == SERVER_SOURCE;
            bool hovered = !blocked && NewUi::hit(x, y, w, LOGIN_FIELD);
            float lit = UiState::motion(std::string(SERVER_SOURCE) + "#hover", open || hovered ? 1.0f : 0.0f, 18.0f);
            UiRender::roundRect(x, y, w, LOGIN_FIELD, 7.0f, UiTheme::surfaceRaised());
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, w, LOGIN_FIELD, 7.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            UiRender::outline(x, y, w, LOGIN_FIELD, 7.0f,
                              open ? UiTheme::accent() : UiTheme::border(UiTheme::outlineSoft()));
            serverMark(x + 10.0f, y + (LOGIN_FIELD - 16.0f) * 0.5f, 16.0f);
            UiRender::textY(choice->label(), x + 34.0f, y, LOGIN_FIELD, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            UiWidgets::icon(Ui::glyph(0xf078), x + w - 15.0f, y + LOGIN_FIELD * 0.5f, UiTheme::textMuted(), 9.0f);
            if (!blocked && NewUi::click(x, y, w, LOGIN_FIELD)) {
                UiSound::request(UiSound::TICK);
                NewUi::openChoice(choice, x, anchorY, w, SERVER_SOURCE);
            } else if (open) {
                NewUi::popupAnchorX = x;
                NewUi::popupAnchorY = anchorY;
                NewUi::popupWidth = w;
            }
        }

        void loginField(const std::shared_ptr<TextBinding>& value, float x, float y, float w, bool blocked) {
            TextFieldWidget field(value);
            field.box(x, y, w, LOGIN_FIELD).radius(7.0f).bounds(x, y, w, LOGIN_FIELD).block(blocked);
            field.paint();
        }

        std::vector<std::pair<std::string, std::function<void()>>> loginLinks() {
            std::vector<std::pair<std::string, std::function<void()>>> links;
            std::string signUp = Accounts::registrationUrl(loginType(), loginAddress());
            if (!signUp.empty()) {
                links.push_back({"注册账号", [signUp] { Platform::openUrl(signUp); }});
            }
            if (server == Server::LittleSkin) {
                links.push_back({"通过浏览器登录", [] { submitBrowser(); }});
            }
            if (nide8()) {
                links.push_back({"服主后台", [] { Platform::openUrl(std::string(blb::auth::nide8_admin_url)); }});
            }
            return links;
        }

        float formHeight() {
            int fields = server == Server::LittleSkin ? 3 : 4;
            float h = LOGIN_PAD + LOGIN_ICON + 12.0f + LOGIN_TITLE + 18.0f;
            h += fields * (LOGIN_LABEL + LOGIN_LABEL_GAP + LOGIN_FIELD) + (fields - 1) * LOGIN_GAP;
            if (!loginProblem.empty()) {
                h += 8.0f + LOGIN_NOTE;
            }
            h += 12.0f + REMEMBER_H;
            h += 18.0f + LOGIN_FIELD;
            if (!loginLinks().empty()) {
                h += 12.0f + LOGIN_NOTE;
            }
            return h + LOGIN_PAD;
        }

        bool headRows() {
            return Avatars::style() == Avatars::Style::Head;
        }

        float gridHeight() {
            if (headRows()) {
                return (float) Accounts::serverProfiles().size() * PROFILE_ROW;
            }
            int lines = ((int) Accounts::serverProfiles().size() + PROFILE_COLUMNS - 1) / PROFILE_COLUMNS;
            return lines > 0 ? lines * PROFILE_H + (lines - 1) * PROFILE_GAP : 0.0f;
        }

        float pickChrome() {
            return LOGIN_PAD + LOGIN_TITLE + 16.0f + 18.0f + LOGIN_FIELD + LOGIN_PAD;
        }

        float gridView() {
            float room = MainWindow::height() - 2.0f * DIALOG_MARGIN - pickChrome();
            return std::max(headRows() ? PROFILE_ROW : PROFILE_H, std::min(gridHeight(), room));
        }

        float loginHeight() {
            return choosing() ? pickChrome() + gridView() : formHeight();
        }

        float loginWidth() {
            return choosing() && !headRows() ? PICK_W : LOGIN_W;
        }

        void busyLabel(const std::string& text, float x, float y, float w) {
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float start = x + (w - (12.0f + 8.0f + textW)) * 0.5f;
            Page::spinner(start + 6.0f, y + LOGIN_FIELD * 0.5f, 6.0f, UiTheme::onAccent());
            UiRender::textY(text, start + 20.0f, y, LOGIN_FIELD, UiTheme::onAccent(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void formStep(float x, float y, float w) {
            bool working = Accounts::serverLogin() == Accounts::ServerLogin::Working;
            float left = x + LOGIN_PAD;
            float width = w - 2.0f * LOGIN_PAD;
            float cx = x + w * 0.5f;
            float cy = y + LOGIN_PAD;
            serverMark(cx - LOGIN_ICON * 0.5f, cy, LOGIN_ICON);
            cy += LOGIN_ICON + 12.0f;
            std::string title = server == Server::LittleSkin ? std::string("登录 LittleSkin")
                    : nide8()                                ? std::string("登录统一通行证")
                                                             : std::string("第三方登录");
            Page::centredText(title, cx, cy, LOGIN_TITLE, UiTheme::textPrimary(), Page::TITLE_SIZE);
            cy += LOGIN_TITLE + 18.0f;
            auto label = [&](const std::string& text) {
                UiRender::textY(text, left + 2.0f, cy, LOGIN_LABEL, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                cy += LOGIN_LABEL + LOGIN_LABEL_GAP;
            };
            label("认证服务器");
            serverPicker(left, cy, width, working);
            cy += LOGIN_FIELD + LOGIN_GAP;
            if (server != Server::LittleSkin) {
                label(nide8() ? "服务器 ID" : "认证服务器地址");
                loginField(addressField(), left, cy, width, working);
                cy += LOGIN_FIELD + LOGIN_GAP;
            }
            label(nide8() ? "邮箱或统一通行证账号" : "邮箱或用户名");
            loginField(userField(), left, cy, width, working);
            cy += LOGIN_FIELD + LOGIN_GAP;
            label("密码");
            loginField(passwordField(), left, cy, width, working);
            cy += LOGIN_FIELD;
            if (!focusNext.empty() && NewUi::textEditing.empty()) {
                std::string next = focusNext;
                focusNext.clear();
                NewUi::beginTextEdit(next == ADDRESS_OWNER && server != Server::LittleSkin ? addressField()
                                             : next == PASSWORD_OWNER                      ? passwordField()
                                                                                           : userField());
            }
            if (!loginProblem.empty()) {
                cy += 8.0f;
                UiRender::textY(UiFont::fitEnd(loginProblem, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, width), left, cy,
                                LOGIN_NOTE, UiTheme::danger(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                cy += LOGIN_NOTE;
            }
            cy += 12.0f;
            const std::string remember = "记住密码";
            float rememberW = 24.0f + UiFont::width(remember, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            tickBox("accounts#login#remember", left + 2.0f, cy + (REMEMBER_H - 16.0f) * 0.5f, rememberPassword);
            UiRender::textY(remember, left + 26.0f, cy, REMEMBER_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (!working && NewUi::hit(left, cy, rememberW, REMEMBER_H)) {
                UiCursor::hand();
            }
            if (!working && NewUi::click(left, cy, rememberW, REMEMBER_H)) {
                UiSound::request(UiSound::TICK);
                rememberPassword = !rememberPassword;
            }
            cy += REMEMBER_H + 18.0f;
            if (Page::button("accounts#login#submit", working ? std::string() : std::string("登录"), left, cy, width,
                             true, working, working ? 0 : SIGN_IN_GLYPH, LOGIN_FIELD)) {
                submitServer();
            }
            if (working) {
                busyLabel(viaBrowser ? "在浏览器中完成授权" : "正在登录", left, cy, width);
            }
            cy += LOGIN_FIELD;
            std::vector<std::pair<std::string, std::function<void()>>> links = loginLinks();
            if (links.empty()) {
                return;
            }
            cy += 12.0f;
            float total = 0.0f;
            for (const auto& [text, action] : links) {
                total += UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 20.0f;
            }
            float linkX = cx - (total - 20.0f) * 0.5f;
            for (const auto& [text, action] : links) {
                if (textLink(text, linkX, cy, UiTheme::textMuted(), UiTheme::textSecondary(), working)) {
                    action();
                }
                linkX += UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 20.0f;
            }
        }

        void profileCard(const blb::auth::yggdrasil_profile& profile, size_t index, float x, float y, float w,
                         bool working, bool reachable) {
            float top = y;
            float h = PROFILE_H;
            float bottom = top + h;
            std::string id = "accounts#login#profile#" + profile.id;
            bool picked = pickedProfiles.count(profile.id) != 0;
            bool hovered = !working && reachable && NewUi::hit(x, top, w, h);
            float lit = UiState::motion(id + "#picked", picked ? 1.0f : 0.0f, 16.0f);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
            UiRender::roundRect(x, top, w, h, 12.0f, UiTheme::surfaceRaised());
            float shine = 22.0f * lit + 8.0f * hover;
            if (shine > 0.1f) {
                UiRender::roundRect(x, top, w, h, 12.0f, UiTheme::overlay(Ui::round(shine)));
            }
            std::string name = UiFont::fitEnd(profile.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 24.0f);
            Page::centredText(name, x + w * 0.5f, top + 10.0f, 24.0f, UiTheme::textPrimary(), UiWidgets::TEXT_CONTROL);
            UiRender::outline(x, top, w, h, 12.0f, UiTheme::border(UiTheme::outlineSoft()));
            blb::auth::account candidate = Accounts::serverCandidate(index);
            if (Avatars::style() == Avatars::Style::Head) {
                headIn(Textures::of(Avatars::head(candidate)), x + (w - PROFILE_HEAD) * 0.5f,
                       top + 40.0f + (h - 54.0f - PROFILE_HEAD) * 0.5f, PROFILE_HEAD, 12.0f);
            } else {
                bustIn(Textures::of(Avatars::bust(candidate)), x + (w - PROFILE_BUST) * 0.5f, PROFILE_BUST, bottom);
            }
            if (lit > 0.01f) {
                float savedAlpha = UiRender::alphaMul;
                UiRender::alphaMul = savedAlpha * lit;
                UiRender::circle(x + w - 15.0f, top + 15.0f, 8.0f, UiTheme::accent());
                UiWidgets::check(x + w - 15.0f, top + 15.0f, UiTheme::onAccent(), 8.0f);
                UiRender::alphaMul = savedAlpha;
            }
            if (!working && reachable && NewUi::click(x, top, w, h)) {
                UiSound::request(UiSound::TICK);
                if (picked) {
                    pickedProfiles.erase(profile.id);
                } else {
                    pickedProfiles.insert(profile.id);
                }
            }
        }

        void profileRow(const blb::auth::yggdrasil_profile& profile, int index, int count, float x, float y, float w,
                        bool working, bool reachable) {
            std::string id = "accounts#login#profile#" + profile.id;
            bool picked = pickedProfiles.count(profile.id) != 0;
            bool hovered = !working && reachable && NewUi::hit(x, y, w, PROFILE_ROW);
            float hover = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (hover > 0.001f) {
                UiRender::roundRect(x, y, w, PROFILE_ROW, 12.0f, UiTheme::overlay(Ui::round(14 * hover)),
                                    Page::rowCorners(index, count));
            }
            headIn(Textures::of(Avatars::head(Accounts::serverCandidate((size_t) index))), x + 12.0f,
                   y + (PROFILE_ROW - PROFILE_ROW_HEAD) * 0.5f, PROFILE_ROW_HEAD, 6.0f);
            std::string name = UiFont::fitEnd(profile.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                              w - 24.0f - PROFILE_ROW_HEAD - 44.0f);
            UiRender::textY(name, x + 24.0f + PROFILE_ROW_HEAD, y, PROFILE_ROW, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            tickBox(id, x + w - 30.0f, y + (PROFILE_ROW - 16.0f) * 0.5f, picked);
            if (!working && reachable && NewUi::click(x, y, w, PROFILE_ROW)) {
                UiSound::request(UiSound::TICK);
                if (picked) {
                    pickedProfiles.erase(profile.id);
                } else {
                    pickedProfiles.insert(profile.id);
                }
            }
        }

        void addPicked() {
            const std::vector<blb::auth::yggdrasil_profile>& profiles = Accounts::serverProfiles();
            std::vector<size_t> indices;
            for (size_t i = 0; i < profiles.size(); i++) {
                if (pickedProfiles.count(profiles[i].id) != 0) {
                    indices.push_back(i);
                }
            }
            if (!indices.empty()) {
                Accounts::chooseProfiles(indices, serverUser, serverPassword);
            }
        }

        void profileStep(float x, float y, float w) {
            const std::vector<blb::auth::yggdrasil_profile>& profiles = Accounts::serverProfiles();
            bool working = Accounts::serverLogin() == Accounts::ServerLogin::Working;
            float left = x + LOGIN_PAD;
            float width = w - 2.0f * LOGIN_PAD;
            float cx = x + w * 0.5f;
            float cy = y + LOGIN_PAD;
            Page::centredText("选择角色", cx, cy, LOGIN_TITLE, UiTheme::textPrimary(), Page::TITLE_SIZE);
            cy += LOGIN_TITLE + 16.0f;

            int count = (int) profiles.size();
            float view = gridView();
            float maxScroll = std::max(0.0f, gridHeight() - view);
            bool reachable = NewUi::hit(left, cy, width, view);
            if (reachable && NewUi::wheel() != 0.0f) {
                pickScroll -= NewUi::wheel() * 60.0f;
            }
            pickScroll = std::clamp(pickScroll, 0.0f, maxScroll);
            float shown = UiRender::snapLayout(UiState::motion("accounts#login#pickScroll", pickScroll, 18.0f));
            float cardW = (width - PROFILE_GAP * (PROFILE_COLUMNS - 1)) / (float) PROFILE_COLUMNS;
            UiRender::scissor(left - 1.0f, cy, width + 2.0f, view);
            if (headRows()) {
                float listY = cy - shown;
                Page::listCard(left, listY, width, PROFILE_ROW, count);
                for (int i = 0; i < count; i++) {
                    float rowY = listY + (float) i * PROFILE_ROW;
                    if (rowY + PROFILE_ROW >= cy && rowY <= cy + view) {
                        profileRow(profiles[(size_t) i], i, count, left, rowY, width, working, reachable);
                    }
                }
            } else {
                for (int i = 0; i < count; i++) {
                    int column = i % PROFILE_COLUMNS;
                    int line = i / PROFILE_COLUMNS;
                    int inLine = std::min(PROFILE_COLUMNS, count - line * PROFILE_COLUMNS);
                    float lineW = inLine * cardW + (inLine - 1) * PROFILE_GAP;
                    float cardX = cx - lineW * 0.5f + column * (cardW + PROFILE_GAP);
                    float cardY = cy + line * (PROFILE_H + PROFILE_GAP) - shown;
                    if (cardY + PROFILE_H < cy || cardY > cy + view) {
                        continue;
                    }
                    profileCard(profiles[(size_t) i], (size_t) i, cardX, cardY, cardW, working, reachable);
                }
            }
            UiRender::unscissor();
            if (maxScroll > 0.5f) {
                float dragged = NewUi::listScrollbar("accounts#login#pickbar", x + w - LOGIN_PAD * 0.5f - 3.0f,
                                                     cy + 2.0f, view - 4.0f, view, maxScroll, shown, pickScroll);
                if (dragged != pickScroll) {
                    pickScroll = dragged;
                }
            }
            cy += view + 18.0f;

            int chosen = 0;
            for (const blb::auth::yggdrasil_profile& profile : profiles) {
                chosen += pickedProfiles.count(profile.id) != 0 ? 1 : 0;
            }
            std::string back = "返回";
            std::string add = working ? std::string("正在添加")
                    : chosen > 0      ? "添加 " + std::to_string(chosen) + " 个账户"
                                      : std::string("添加账户");
            float backW = std::max(88.0f, Page::buttonWidth(back));
            float addW = std::max(128.0f, Page::buttonWidth(add, 0xf234));
            float buttonsX = cx - (backW + 8.0f + addW) * 0.5f;
            if (Page::button("accounts#login#back", back, buttonsX, cy, backW, false, working, 0, LOGIN_FIELD)) {
                pickedProfiles.clear();
                pickScroll = 0.0f;
                Accounts::cancelServerLogin();
            }
            float addX = buttonsX + backW + 8.0f;
            float ready = UiState::motion("accounts#login#addReady", chosen > 0 || working ? 1.0f : 0.0f, 14.0f);
            float savedAlpha = UiRender::alphaMul;
            UiRender::alphaMul = savedAlpha * UiRender::lerp(0.45f, 1.0f, ready);
            if (Page::button("accounts#login#add", working ? std::string() : add, addX, cy, addW, true,
                             working || chosen == 0, working ? 0 : 0xf234, LOGIN_FIELD)) {
                addPicked();
            }
            UiRender::alphaMul = savedAlpha;
            if (working) {
                busyLabel(add, addX, cy, addW);
            }
        }

        void loginBody(float x, float y, float w, float h) {
            if (Accounts::serverLogin() == Accounts::ServerLogin::Done) {
                finishLogin();
                return;
            }
            Dialog::resize(LOGIN_DIALOG, UiState::motion("accounts#login#width", loginWidth(), 18.0f),
                           UiState::motion("accounts#login#height", loginHeight(), 18.0f));
            UiRender::scissor(x, y, w, h);
            if (choosing()) {
                profileStep(x, y, w);
            } else {
                formStep(x, y, w);
            }
            UiRender::unscissor();
            if (Page::iconButton("accounts#login#close", 0xf00d, "关闭", x + w - 40.0f, y + 12.0f)) {
                Dialog::close();
            }
        }

        const char* const OFFLINE_DIALOG = "accounts#offline#dialog";
        constexpr float OFFLINE_TOP = 13.0f;
        std::string offlineProblem;
        bool offlineFocus = false;

        void submitOffline() {
            std::string name =
                    Ui::trim(NewUi::textEditing == OFFLINE_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : offlineName);
            offlineName = name;
            offlineProblem = Accounts::addOffline(name);
            if (offlineProblem.empty()) {
                Dialog::close();
            } else {
                offlineFocus = true;
            }
        }

        std::shared_ptr<TextBinding> offlineValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        OFFLINE_OWNER, [] { return offlineName; },
                        [](const std::string& text) {
                            offlineName = text;
                            offlineProblem.clear();
                            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                                submitOffline();
                            }
                        },
                        std::string());
                binding->limit = 16;
                return binding;
            }();
            return value;
        }

        float offlineHeight() {
            float h = OFFLINE_TOP + LOGIN_TITLE + 16.0f + LOGIN_LABEL + LOGIN_LABEL_GAP + LOGIN_FIELD;
            if (!offlineProblem.empty()) {
                h += 8.0f + LOGIN_NOTE;
            }
            return h + 18.0f + LOGIN_FIELD + LOGIN_PAD;
        }

        void offlineBody(float x, float y, float w, float h) {
            if (!hasOnline()) {
                Dialog::close();
                return;
            }
            Dialog::resize(OFFLINE_DIALOG, LOGIN_W, UiState::motion("accounts#offline#height", offlineHeight(), 18.0f));
            UiRender::scissor(x, y, w, h);
            float left = x + LOGIN_PAD;
            float width = w - 2.0f * LOGIN_PAD;
            float cy = y + OFFLINE_TOP;
            UiRender::textY("添加离线账户", left, cy, LOGIN_TITLE, UiTheme::textPrimary(), UiFont::MEDIUM,
                            Page::TITLE_SIZE);
            cy += LOGIN_TITLE + 16.0f;
            UiRender::textY("玩家名", left + 2.0f, cy, LOGIN_LABEL, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            cy += LOGIN_LABEL + LOGIN_LABEL_GAP;
            loginField(offlineValue(), left, cy, width, false);
            cy += LOGIN_FIELD;
            if (offlineFocus && NewUi::textEditing.empty()) {
                offlineFocus = false;
                NewUi::beginTextEdit(offlineValue());
            }
            if (!offlineProblem.empty()) {
                cy += 8.0f;
                UiRender::textY(UiFont::fitEnd(offlineProblem, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, width), left, cy,
                                LOGIN_NOTE, UiTheme::danger(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                cy += LOGIN_NOTE;
            }
            cy += 18.0f;
            if (Page::button("accounts#offline#submit", "添加", left, cy, width, true, false, 0xf234, LOGIN_FIELD)) {
                submitOffline();
            }
            UiRender::unscissor();
            if (Page::iconButton("accounts#offline#close", 0xf00d, "关闭", x + w - 40.0f, y + 12.0f)) {
                Dialog::close();
            }
        }

        void offlineClosed() {
            if (NewUi::textEditing == OFFLINE_OWNER) {
                NewUi::cancelTextEdit();
            }
            offlineName.clear();
            offlineProblem.clear();
            offlineFocus = false;
        }

        void openOffline() {
            if (ours(NewUi::textEditing) || NewUi::textEditing == OFFLINE_OWNER) {
                NewUi::cancelTextEdit();
            }
            offlineName.clear();
            offlineProblem.clear();
            offlineFocus = true;
            UiState::remove("accounts#offline#height");
            Dialog::open(OFFLINE_DIALOG, LOGIN_W, offlineHeight(), offlineBody, offlineClosed);
        }

        void loginClosed() {
            Accounts::ServerLogin state = Accounts::serverLogin();
            if (state == Accounts::ServerLogin::Working || state == Accounts::ServerLogin::Choosing) {
                Accounts::cancelServerLogin();
            }
            if (ours(NewUi::textEditing)) {
                NewUi::cancelTextEdit();
            }
            serverPassword.clear();
            focusNext.clear();
            loginProblem.clear();
            viaBrowser = false;
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
            int count = (int) accounts.size();
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
            Page::title("账户", Page::left(), Page::top());
            bool listBlocked = covered || first > 0.5f;
            actions(listBlocked);

            float x0 = Page::left();
            float width = Page::width();
            int columns = std::max(1, (int) ((width + CARD_GAP) / (CARD_MIN_W + CARD_GAP)));
            float cardW = (width - CARD_GAP * (float) (columns - 1)) / (float) columns;
            int cardRows = ((int) accounts.size() + columns - 1) / columns;
            float gridH = cardRows > 0 ? cardRows * (CARD_H + CARD_GAP) - CARD_GAP : 0.0f;
            Page::Scroll body("accounts#body", Page::top() + 40.0f, MainWindow::height() - 14.0f, gridH);
            bool outside = body.begin();
            bool blocked = outside || listBlocked;
            float y = body.top - body.offset;

            std::vector<blb::auth::account> shown = accounts;
            std::string currentKey = current != nullptr ? Accounts::keyOf(*current) : std::string();
            for (size_t i = 0; i < shown.size(); i++) {
                float cardX = x0 + (float) (i % columns) * (cardW + CARD_GAP);
                float cardY = y + (float) (i / columns) * (CARD_H + CARD_GAP);
                if (Page::shown(cardY, CARD_H)) {
                    accountCard(shown[i], Accounts::keyOf(shown[i]) == currentKey, cardX, cardY, cardW, blocked);
                }
            }
            body.end();
            UiRender::alphaMul = savedAlpha;
        }
    }

    void reset() {
        views.page = VIEW_LIST;
        views.mix = 1.0f;
    }

    void draw() {
        if (Accounts::serverLogin() == Accounts::ServerLogin::Done) {
            finishLogin();
        }
        bool covered = NewUi::overlayCoversMouse();
        views.begin();
        if (views.page == VIEW_PROFILE) {
            if (!ProfilePage::draw(covered)) {
                views.go(VIEW_LIST);
            }
        } else {
            listView();
        }
        views.end();
    }

    void openAdd() {
        addPending = true;
    }

    void openServerLogin(blb::auth::account_type type, const std::string& address, const std::string& username) {
        Accounts::ServerLogin state = Accounts::serverLogin();
        if (state == Accounts::ServerLogin::Working || state == Accounts::ServerLogin::Choosing) {
            Accounts::cancelServerLogin();
        }
        if (ours(NewUi::textEditing)) {
            NewUi::cancelTextEdit();
        }
        if (type == blb::auth::account_type::nide8) {
            server = Server::Nide8;
            nide8Server = address;
        } else if (address.empty() || blb::auth::same_yggdrasil_server(address, blb::auth::littleskin_api)) {
            server = Server::LittleSkin;
        } else {
            server = Server::Custom;
            customServer = address;
        }
        serverUser = username;
        serverPassword.clear();
        loginProblem.clear();
        rememberPassword = true;
        recall();
        focusNext = server != Server::LittleSkin && addressTarget()->empty() ? ADDRESS_OWNER
                : serverUser.empty()                                         ? USER_OWNER
                                                                             : PASSWORD_OWNER;
        pickedProfiles.clear();
        pickScroll = 0.0f;
        UiState::remove("accounts#login#width");
        UiState::remove("accounts#login#height");
        Dialog::open(LOGIN_DIALOG, loginWidth(), loginHeight(), loginBody, loginClosed);
    }
}
