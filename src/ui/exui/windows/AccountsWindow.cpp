#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/AccountsWindow.h"
#include "app/accounts/Accounts.h"
#include "app/system/Platform.h"
#include "ui/exui/Form.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"

#include <string>
#include <vector>

namespace ForgottenMelody {

    namespace {
        namespace blb = b_launcher_base;
        using exui::ControlKind;
        using Rows = LiveForm::Rows;

        // What was typed into the login dialog last, but the password, for the rest of the run.
        struct Typed {
            bool nide8 = false;
            std::wstring server, user;
        } remembered;

        // The profiles of a server account, picked from a short list.
        class ProfileDialog : public exui::Form {
        public:
            enum : uint32_t { PROFILES = 1, PICK, CANCEL };

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {PROFILES, ControlKind::List, {18, 35, 264, 160}},
                        {PICK, ControlKind::Button, {92, 202, 90, 35}, L"选择"},
                        {CANCEL, ControlKind::Button, {192, 202, 90, 35}, L"取消"},
                };
                if (!createDialog(*this, parent, L"选择角色", 300, 245)) {
                    return false;
                }
                AddControls(CONTROLS);
                auto& list = Control<exui::ListView>(PROFILES);
                list.InsertColumn(L"角色", 257);
                for (const auto& profile : Accounts::serverProfiles()) {
                    list.InsertRow({Platform::widen(profile.name)});
                }
                list.SetSelectedRow(0);
                return true;
            }

        protected:
            void OnAction(uint32_t id) override {
                int row = Control<exui::ListView>(PROFILES).GetSelectedRow();
                if (id == PICK && row >= 0) {
                    Accounts::chooseProfile((size_t) row);
                    CloseWindow();
                } else if (id == CANCEL) {
                    CloseWindow();
                }
            }
        };

        // Third-party login in the layout of W02: an authlib-injector server or a 统一通行证 server ID, then the
        // account and its password. Like the default interface it offers LittleSkin, or the 统一通行证 admin page,
        // and the sign-up page of a server it knows.
        class ExternalDialog : public LiveForm {
        public:
            enum : uint32_t {
                KIND = 1,
                YGGDRASIL,
                NIDE8,
                SERVER_LABEL,
                SERVER,
                USER_LABEL,
                USER,
                PASSWORD_LABEL,
                PASSWORD,
                STATUS,
                PRESET,
                REGISTER,
                LOGIN,
                CANCEL
            };

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {KIND, ControlKind::Label, {25, 35, 65, 25}, L"登录方式"},
                        {YGGDRASIL, ControlKind::Radio, {95, 35, 90, 25}, L"外置登录"},
                        {NIDE8, ControlKind::Radio, {190, 35, 100, 25}, L"统一通行证"},
                        {SERVER_LABEL, ControlKind::Label, {25, 70, 65, 25}},
                        {SERVER, ControlKind::Edit, {95, 70, 280, 25}},
                        {USER_LABEL, ControlKind::Label, {25, 105, 65, 25}, L"账号"},
                        {USER, ControlKind::Edit, {95, 105, 120, 25}},
                        {PASSWORD_LABEL, ControlKind::Label, {225, 105, 35, 25}, L"密码"},
                        {PASSWORD, ControlKind::Edit, {255, 105, 120, 25}},
                        {STATUS, ControlKind::Label, {295, 35, 80, 25}},
                        {PRESET, ControlKind::Button, {25, 137, 120, 35}},
                        {REGISTER, ControlKind::Button, {150, 137, 75, 35}, L"注册账号"},
                        {LOGIN, ControlKind::Button, {230, 137, 70, 35}, L"登录"},
                        {CANCEL, ControlKind::Button, {305, 137, 70, 35}, L"取消"},
                };
                if (!createDialog(*this, parent, L"外置登录", 400, 197)) {
                    return false;
                }
                AddControls(CONTROLS);
                GroupRadios({YGGDRASIL, NIDE8});
                SetChecked(remembered.nide8 ? NIDE8 : YGGDRASIL, true);
                SetText(SERVER, remembered.server);
                SetText(USER, remembered.user);
                Control<exui::EditBox>(PASSWORD).SetPasswordMode(true);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                bool nide8 = Checked(NIDE8);
                std::wstring serverLabel = nide8 ? L"服务器 ID" : L"认证服务器";
                if (Text(SERVER_LABEL) != serverLabel) {
                    SetText(SERVER_LABEL, serverLabel);
                }
                std::wstring preset = nide8 ? L"服主后台" : L"设置为 LittleSkin";
                if (Text(PRESET) != preset) {
                    SetText(PRESET, preset);
                }
                bool registrable = !registerUrl().empty();
                if (FindControl(REGISTER)->IsVisible() != registrable) {
                    FindControl(REGISTER)->SetVisible(registrable);
                }
                Accounts::ServerLogin state = Accounts::serverLogin();
                if (state == Accounts::ServerLogin::Done) {
                    Accounts::finishServerLogin();
                    CloseWindow();
                    return;
                }
                // The profile list opens after this refresh, as a window of its own runs a loop of its own.
                if (state == Accounts::ServerLogin::Choosing && !choosing) {
                    choosing = true;
                    PostMessageW(GetHwnd(), CHOOSE_PROFILE, 0, 0);
                }
                std::wstring status = state == Accounts::ServerLogin::Working ? L"正在登录…" : L"";
                if (Text(STATUS) != status) {
                    SetText(STATUS, status);
                }
                FindControl(LOGIN)->SetEnabled(state == Accounts::ServerLogin::Idle);
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == CHOOSE_PROFILE) {
                    ProfileDialog dialog;
                    if (dialog.create(GetHwnd())) {
                        runModal(dialog);
                    }
                    if (Accounts::serverLogin() == Accounts::ServerLogin::Choosing) {
                        Accounts::cancelServerLogin();
                    }
                    choosing = false;
                    return 0;
                }
                if (msg == WM_KEYDOWN && wp == VK_RETURN) {
                    login();
                    return 0;
                }
                if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
                    CloseWindow();
                    return 0;
                }
                return LiveForm::HandleMessage(msg, wp, lp);
            }

            void OnAction(uint32_t id) override {
                switch (id) {
                    case LOGIN: login(); break;
                    case CANCEL: CloseWindow(); break;
                    case REGISTER: Platform::openUrl(registerUrl()); break;
                    case PRESET:
                        if (Checked(NIDE8)) {
                            Platform::openUrl(std::string(blb::auth::nide8_admin_url));
                        } else {
                            SetText(SERVER, Platform::widen(std::string(blb::auth::littleskin_api)));
                            if (Text(USER).empty()) {
                                SetFocusedWidget(FindControl(USER));
                            }
                        }
                        break;
                }
                refresh();
            }

        private:
            static constexpr UINT CHOOSE_PROFILE = WM_APP + 1;

            bool choosing = false;

            std::string registerUrl() const {
                std::string server = trimmed(Text(SERVER));
                if (Checked(NIDE8)) {
                    return blb::auth::valid_nide8_id(server) ? blb::auth::nide8_register_url(server) : std::string();
                }
                return blb::auth::same_yggdrasil_server(server, blb::auth::littleskin_api)
                        ? std::string("https://littleskin.cn/auth/register")
                        : std::string();
            }

            void login() {
                if (Accounts::serverLogin() != Accounts::ServerLogin::Idle) {
                    return;
                }
                bool nide8 = Checked(NIDE8);
                std::string server = trimmed(Text(SERVER));
                std::string user = trimmed(Text(USER));
                std::string password = Platform::narrow(Text(PASSWORD));
                std::wstring problem;
                if (server.empty()) {
                    problem = nide8 ? L"请填写服务器 ID" : L"请填写认证服务器地址";
                } else if (nide8 && !blb::auth::valid_nide8_id(server)) {
                    problem = L"服务器 ID 应该是 32 位的字母和数字";
                } else if (user.empty()) {
                    problem = L"请填写账号";
                } else if (password.empty()) {
                    problem = L"请填写密码";
                }
                if (!problem.empty()) {
                    message(GetHwnd(), problem, L"外置登录");
                    return;
                }
                remembered = {nide8, Text(SERVER), Text(USER)};
                Accounts::startServerLogin(nide8 ? blb::auth::account_type::nide8 : blb::auth::account_type::yggdrasil,
                                           server, user, password);
                refresh();
            }
        };

        // Microsoft accounts other launchers saved: a smaller list in the same look.
        class ImportWindow : public LiveForm {
        public:
            enum : uint32_t { FOUND = 1, RESCAN, TAKE };

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {FOUND, ControlKind::List, {18, 35, 444, 213}},
                        {RESCAN, ControlKind::Button, {272, 255, 90, 35}, L"重新查找"},
                        {TAKE, ControlKind::Button, {372, 255, 90, 35}, L"导入"},
                };
                if (!createDialog(*this, parent, L"从其他启动器导入", 480, 300)) {
                    return false;
                }
                AddControls(CONTROLS);
                auto& list = Control<exui::ListView>(FOUND);
                list.InsertColumn(L"启动器", 120);
                list.InsertColumn(L"名称", 160);
                list.InsertColumn(L"状态", 157);
                if (Accounts::found().empty() && !Accounts::scanning()) {
                    Accounts::rescan();
                }
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const std::vector<Accounts::Found>& found = Accounts::found();
                Rows rows;
                int unnamed = 0;
                for (const Accounts::Found& entry : found) {
                    std::wstring name = entry.source.name.empty() ? L"账号 " + std::to_wstring(++unnamed)
                                                                  : Platform::widen(entry.source.name);
                    rows.push_back({Platform::widen(entry.source.launcher), name, stateOf(entry.state)});
                }
                if (found.empty()) {
                    rows.push_back(
                            {L"", Accounts::scanning() ? L"正在查找…" : L"没有找到其他启动器保存的微软账号", L""});
                }
                fill(Control<exui::ListView>(FOUND), shown, std::move(rows));
                FindControl(RESCAN)->SetEnabled(!Accounts::scanning());
                int row = Control<exui::ListView>(FOUND).GetSelectedRow();
                bool importable = row >= 0 && row < (int) found.size() &&
                        (found[row].state == Accounts::Found::State::Ready ||
                         found[row].state == Accounts::Found::State::Failed);
                FindControl(TAKE)->SetEnabled(importable);
            }

            void OnAction(uint32_t id) override {
                if (id == RESCAN) {
                    Accounts::rescan();
                } else if (id == TAKE) {
                    Accounts::import((size_t) Control<exui::ListView>(FOUND).GetSelectedRow());
                }
                refresh();
            }

            void OnSelection(uint32_t, int) override {
                refresh();
            }

        private:
            Rows shown;

            static std::wstring stateOf(Accounts::Found::State state) {
                switch (state) {
                    case Accounts::Found::State::Importing: return L"正在导入";
                    case Accounts::Found::State::Imported: return L"已导入";
                    case Accounts::Found::State::Failed: return L"导入失败";
                    default: return L"";
                }
            }
        };

        // 更新游戏's list and button row: the ways to add an account on the left, what to do with the selected one
        // on the right.
        class AccountsWindow : public LiveForm {
        public:
            enum : uint32_t { LIST = 1, MICROSOFT, OFFLINE, EXTERNAL, IMPORT, REMOVE, CURRENT };
            // Opens the login dialog once the window runs, as a window of its own runs a loop of its own.
            static constexpr UINT OPEN_EXTERNAL = WM_APP + 1;

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {LIST, ControlKind::List, {18, 35, 684, 360}},
                        {MICROSOFT, ControlKind::Button, {20, 402, 100, 35}, L"添加微软账号"},
                        {OFFLINE, ControlKind::Button, {130, 402, 100, 35}, L"添加离线账号"},
                        {EXTERNAL, ControlKind::Button, {240, 402, 90, 35}, L"外置登录"},
                        {IMPORT, ControlKind::Button, {340, 402, 90, 35}, L"导入"},
                        {REMOVE, ControlKind::Button, {500, 402, 90, 35}, L"删除"},
                        {CURRENT, ControlKind::Button, {610, 402, 90, 35}, L"设为当前"},
                };
                if (!createDialog(*this, parent, L"账号管理", 720, 450)) {
                    return false;
                }
                AddControls(CONTROLS);
                auto& list = Control<exui::ListView>(LIST);
                list.InsertColumn(L"名称", 160);
                list.InsertColumn(L"类型", 160);
                list.InsertColumn(L"状态", 350);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const auto* current = Accounts::current();
                std::string currentKey = current != nullptr ? Accounts::keyOf(*current) : std::string();
                std::vector<std::string> keys;
                Rows rows;
                for (const auto& account : Accounts::list()) {
                    std::string key = Accounts::keyOf(account);
                    rows.push_back({Platform::widen(account.name), Platform::widen(Accounts::kindOf(account)),
                                    statusOf(key, key == currentKey)});
                    keys.push_back(std::move(key));
                }
                // A Microsoft login in progress shows as a row of its own.
                Accounts::Login login = Accounts::login();
                if (login != Accounts::Login::Idle) {
                    rows.push_back({L"微软账号", L"微软",
                                    login == Accounts::Login::Browser ? L"请在浏览器中完成登录" : L"正在登录"});
                    keys.emplace_back();
                }
                fill(Control<exui::ListView>(LIST), shown, shownKeys, std::move(rows), std::move(keys));

                std::string key = selectedKey();
                FindControl(REMOVE)->SetEnabled(!key.empty());
                FindControl(CURRENT)->SetEnabled(!key.empty() && key != currentKey);
                std::wstring microsoft = login == Accounts::Login::Idle ? L"添加微软账号" : L"取消登录";
                if (Text(MICROSOFT) != microsoft) {
                    SetText(MICROSOFT, microsoft);
                }
            }

            void OnAction(uint32_t id) override {
                switch (id) {
                    case MICROSOFT:
                        if (Accounts::login() == Accounts::Login::Idle) {
                            Accounts::startLogin();
                        } else {
                            Accounts::cancelLogin();
                        }
                        break;
                    case OFFLINE:
                        askName(GetHwnd(), L"添加离线账号", L"输入游戏名字", L"", [](const std::wstring& name) {
                            return Platform::widen(Accounts::addOffline(Platform::narrow(name)));
                        });
                        break;
                    case EXTERNAL: external(); break;
                    case IMPORT: {
                        ImportWindow window;
                        if (window.create(GetHwnd())) {
                            runModal(window);
                        }
                        break;
                    }
                    case REMOVE: {
                        // The list keeps refreshing under the question, so the account is taken before it.
                        std::string key = selectedKey();
                        const auto* account = Accounts::find(key);
                        if (account != nullptr &&
                            message(GetHwnd(), L"要删除账号 " + Platform::widen(account->name) + L" 吗？", L"账号管理",
                                    exui::MsgBoxButtons::OkCancel, {L"取消", L"删除"}) == 0) {
                            Accounts::remove(key);
                        }
                        break;
                    }
                    case CURRENT: Accounts::select(selectedKey()); break;
                }
                refresh();
            }

            void OnSelection(uint32_t, int) override {
                refresh();
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == OPEN_EXTERNAL) {
                    external();
                    return 0;
                }
                return LiveForm::HandleMessage(msg, wp, lp);
            }

        private:
            Rows shown;
            // The account of each row; empty for the row of a login in progress.
            std::vector<std::string> shownKeys;

            void external() {
                ExternalDialog dialog;
                if (dialog.create(GetHwnd())) {
                    runModal(dialog, dialog.FindControl(ExternalDialog::SERVER));
                }
                // A login the dialog left running ends with it.
                if (Accounts::serverLogin() == Accounts::ServerLogin::Done) {
                    Accounts::finishServerLogin();
                } else if (Accounts::serverLogin() != Accounts::ServerLogin::Idle) {
                    Accounts::cancelServerLogin();
                }
                refresh();
            }

            std::string selectedKey() const {
                return LiveForm::selectedKey(Control<exui::ListView>(LIST), shownKeys);
            }

            static std::wstring statusOf(const std::string& key, bool current) {
                std::wstring status = current ? L"当前使用" : L"";
                if (Accounts::busy(key)) {
                    status += status.empty() ? L"正在刷新" : L"，正在刷新";
                } else if (Accounts::expired(key)) {
                    status += status.empty() ? L"需要重新登录" : L"，需要重新登录";
                }
                return status;
            }
        };
    }

    void showAccounts(HWND parent, const std::string& login, const std::string& authServer) {
        AccountsWindow window;
        if (!window.create(parent)) {
            return;
        }
        // Like the default interface: the kind of login and the server of the instance, without an account name
        // that belongs to another server.
        if (login == "yggdrasil" || login == "nide8") {
            std::wstring server = Platform::widen(authServer);
            if (!server.empty() && server != remembered.server) {
                remembered.server = server;
                remembered.user.clear();
            }
            remembered.nide8 = login == "nide8";
            PostMessageW(window.GetHwnd(), AccountsWindow::OPEN_EXTERNAL, 0, 0);
        }
        runModal(window);
    }
}
