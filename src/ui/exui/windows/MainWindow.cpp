#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/MainWindow.h"
#include "app/accounts/Accounts.h"
#include "app/system/About.h"
#include "app/instances/Drops.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "ui/exui/Skins.h"
#include "ui/exui/windows/AccountsWindow.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/DownloadsWindow.h"
#include "ui/exui/windows/LogWindow.h"
#include "ui/exui/windows/SettingsWindow.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"

#include <algorithm>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        enum : uint32_t { LAUNCH = 1, DOWNLOADS, SETTINGS, INSTANCE, FOLDER, CREDIT, ACCOUNT, NOTICE, DROP };

        constexpr Gdiplus::ARGB CREDIT_COLOR = 0xFEC0C0C0;

        // The 2014 window, with 下载中心 in the place of 更新游戏 and the account across from the credit line,
        // as close to the right edge as the credit is to the left one.
        const exui::ControlSpec CONTROLS[] = {
                {LAUNCH, ControlKind::Button, {430, 310, 150, 75}, L"启动游戏"},
                {DOWNLOADS, ControlKind::Button, {590, 310, 100, 35}, L"下载中心"},
                {SETTINGS, ControlKind::Button, {590, 350, 100, 35}, L"设置"},
                {INSTANCE, ControlKind::Combo, {430, 390, 185, 30}},
                {.id = FOLDER,
                 .kind = ControlKind::Hyperlink,
                 .bounds = {620, 393, 100, 25},
                 .caption = L"打开目录",
                 .fontSize = 15,
                 .textColor = 0xFE0000FF,
                 .autoTextColor = true},
                {.id = CREDIT,
                 .kind = ControlKind::Hyperlink,
                 .bounds = {10, 423, 250, 25},
                 .caption = Platform::widen(std::string(About::NAME) + " " + About::VERSION + " by " + About::AUTHOR),
                 .textColor = CREDIT_COLOR},
                {.id = ACCOUNT,
                 .kind = ControlKind::Hyperlink,
                 .bounds = {461, 423, 252, 25},
                 .textColor = CREDIT_COLOR},
        };

        const wchar_t* launchCaption(Game::State state) {
            switch (state) {
                case Game::State::Preparing: return L"启动中…";
                case Game::State::Running: return L"游戏运行中";
                default: return L"启动游戏";
            }
        }

        constexpr UINT SHOW_FAILURES = WM_APP + 1;
        constexpr UINT ASK_FOR_ACCOUNT = WM_APP + 2;
        constexpr UINT OPEN_LOG = WM_APP + 3;
        constexpr UINT OPEN_MODPACK = WM_APP + 4;

        // One centered notice at a time, white with a dark glow to stand out from any background, hidden once its
        // duration is over.
        class NoticeLine : public exui::Label {
        public:
            NoticeLine(int x, int y, int width, int height) : Label(x, y, width, height, std::wstring(), 0xFEFFFFFF) {
                SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
                SetAutoGlowColor();
                SetVisible(false);
            }

            void show(const std::wstring& text, long long durationMs) {
                StopTimer();
                SetCaption(text);
                SetVisible(true);
                if (durationMs > 0) {
                    StartTimer((UINT) durationMs);
                }
            }

            void OnTimer() override {
                StopTimer();
                SetCaption(std::wstring());
                SetVisible(false);
            }
        };
    }

    bool MainWindow::create() {
        if (!CreateHostWindow(nullptr, Platform::widen(About::NAME), 720, 450,
                              exui::SkinBytes(exui::Skin::MainBackground), 0xFFFFFF, 7,
                              exui::CaptionButtonMask::Minimize | exui::CaptionButtonMask::Close, false)) {
            return false;
        }
        // The main window stretches its skin; the edge fade into the base color is for dialogs.
        SetBackgroundMode(exui::SkinBackgroundMode::Stretch);
        SetWindowAnimationTypes(exui::WindowAnimationKind::ZoomFade, exui::WindowAnimationKind::ZoomFade, false);
        AddControls(CONTROLS);
        FindControl(LAUNCH)->SetFont(L"微软雅黑", 25, Gdiplus::FontStyleBold);
        Control<exui::Hyperlink>(CREDIT).SetTargetUrl(Platform::widen(About::REPOSITORY_URL));
        auto& account = Control<exui::Hyperlink>(ACCOUNT);
        account.SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
        // It opens the account settings rather than a page, so it never turns to the visited color.
        account.SetColors(CREDIT_COLOR, 0xD20060C0, CREDIT_COLOR);
        // Notices go above 启动游戏, centered over it and 下载中心; failures get a message box.
        AddControl(NOTICE, std::make_shared<NoticeLine>(430, 283, 260, 25));
        // Over everything while files are dragged over the window, saying what dropping them does, as the default
        // interface does.
        auto drop = std::make_shared<exui::Label>(14, 14, 692, 422, std::wstring());
        drop->SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
        drop->SetFont(L"微软雅黑", 18, Gdiplus::FontStyleBold);
        drop->SetBackgroundColor(0xE0FFFFFF);
        drop->SetBorderColor(0x50000000);
        drop->SetVisible(false);
        AddControl(DROP, drop);
        refresh();
        return true;
    }

    void MainWindow::refresh() {
        const std::vector<Instances::Entry>& entries = Instances::list();
        auto& combo = Control<exui::ComboBox>(INSTANCE);
        // Until the instances are first read, the list says so and takes no choice.
        bool loading = Instances::loading();
        if (loading != listLoading ||
            (!loading &&
             !std::equal(entries.begin(), entries.end(), instanceKeys.begin(), instanceKeys.end(),
                         [](const Instances::Entry& entry, const std::string& key) { return entry.key == key; }))) {
            listLoading = loading;
            instanceKeys.clear();
            combo.ClearItems();
            if (loading) {
                combo.AddItem(L"正在读取…");
                combo.SetSelectedIndex(0);
            } else {
                for (const Instances::Entry& entry : entries) {
                    instanceKeys.push_back(entry.key);
                    combo.AddItem(Platform::widen(entry.info.id));
                }
            }
            combo.SetEnabled(!loading);
        }
        auto selected = std::find(instanceKeys.begin(), instanceKeys.end(), Instances::selected());
        int index = selected == instanceKeys.end() ? -1 : (int) (selected - instanceKeys.begin());
        if (index >= 0 && index != combo.GetSelectedIndex()) {
            combo.SetSelectedIndex(index);
        }

        const auto* current = Accounts::current();
        std::wstring account = current == nullptr ? std::wstring(L"还没有账号")
                                                  : Platform::widen(current->name + " - " + Accounts::kindOf(*current));
        if (account != Text(ACCOUNT)) {
            SetText(ACCOUNT, account);
        }

        Game::State state = Game::state(Instances::selected());
        std::wstring caption = launchCaption(state);
        if (caption != Text(LAUNCH)) {
            SetText(LAUNCH, caption);
        }
        bool launchable = state == Game::State::Idle && index >= 0;
        if (FindControl(LAUNCH)->IsEnabled() != launchable) {
            FindControl(LAUNCH)->SetEnabled(launchable);
        }

        bool dragging = Drops::hovering();
        auto& drop = Control<exui::Label>(DROP);
        if (dragging) {
            std::wstring promise = Platform::widen(Drops::promise());
            if (promise != Text(DROP)) {
                SetText(DROP, promise);
                drop.SetTextColor(promise.rfind(L"松开", 0) == 0 ? 0xD2000000 : 0x96000000);
            }
        }
        if (drop.IsVisible() != dragging) {
            drop.SetVisible(dragging);
        }

        // A message box runs a loop of its own, so failures wait for the end of this poll and for a shown window.
        if (!failures.empty() && !showingFailures && IsWindowVisible(GetHwnd())) {
            PostMessageW(GetHwnd(), SHOW_FAILURES, 0, 0);
        }
    }

    LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == SHOW_FAILURES) {
            showFailures();
            return 0;
        }
        if (msg == ASK_FOR_ACCOUNT) {
            showFailures();
            if (std::optional<std::pair<std::string, std::string>> wanted = std::exchange(accountWanted, {})) {
                showAccounts(GetHwnd(), wanted->first, wanted->second);
            }
            return 0;
        }
        if (msg == OPEN_LOG) {
            showLog();
            return 0;
        }
        if (msg == OPEN_MODPACK) {
            if (std::optional<std::pair<std::filesystem::path, b_launcher_base::modpack::pack_manifest>> pack =
                        std::exchange(droppedPack, {})) {
                showModpack(GetHwnd(), pack->first, pack->second);
            }
            return 0;
        }
        if (msg == WM_DESTROY) {
            Drops::detach();
        }
        return Form::HandleMessage(msg, wp, lp);
    }

    void MainWindow::openLog() {
        PostMessageW(GetHwnd(), OPEN_LOG, 0, 0);
    }

    bool MainWindow::takesDrops() const {
        return modalWindow(nullptr) == nullptr && !showingFailures;
    }

    void MainWindow::openModpack(const std::filesystem::path& file,
                                 const b_launcher_base::modpack::pack_manifest& manifest) {
        droppedPack = std::make_pair(file, manifest);
        PostMessageW(GetHwnd(), OPEN_MODPACK, 0, 0);
    }

    void MainWindow::showFailures() {
        // Failures that arrive meanwhile join the queue rather than open a second box.
        if (showingFailures) {
            return;
        }
        showingFailures = true;
        while (!failures.empty() && GetHwnd() != nullptr) {
            std::wstring text = std::move(failures.front());
            failures.erase(failures.begin());
            // Over the dialog that is open, if any: one under it would hide behind it and still take its input.
            message(modalWindow(GetHwnd()), text, L"提示：");
        }
        showingFailures = false;
    }

    void MainWindow::askForAccount(const std::string& login, const std::string& authServer) {
        // Once, however often the game asks before the manager opens.
        if (!accountWanted) {
            PostMessageW(GetHwnd(), ASK_FOR_ACCOUNT, 0, 0);
        }
        accountWanted = std::make_pair(login, authServer);
    }

    void MainWindow::OnAction(uint32_t id) {
        switch (id) {
            case LAUNCH: Game::launch(Instances::selected()); break;
            case FOLDER:
                if (const Instances::Entry* entry = Instances::find(Instances::selected())) {
                    Platform::open(entry->gameDirectory);
                }
                break;
            case ACCOUNT: showAccounts(GetHwnd()); break;
            case DOWNLOADS: showDownloads(GetHwnd()); break;
            case SETTINGS: showSettings(GetHwnd()); break;
        }
    }

    void MainWindow::OnSelection(uint32_t id, int index) {
        if (id == INSTANCE && index >= 0 && index < (int) instanceKeys.size()) {
            Instances::select(instanceKeys[index]);
        }
    }

    void MainWindow::showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs) {
        if (kind == Notices::Kind::Failed) {
            failures.push_back(text);
        } else {
            Control<NoticeLine>(NOTICE).show(text, durationMs);
        }
    }
}
