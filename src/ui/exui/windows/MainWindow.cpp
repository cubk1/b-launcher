#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/MainWindow.h"
#include "app/accounts/Accounts.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"
#include "ui/exui/Skins.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"

#include <algorithm>

#ifndef B_LAUNCHER_VERSION
#define B_LAUNCHER_VERSION "0.0.0"
#endif

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        enum : uint32_t { LAUNCH = 1, DOWNLOADS, SETTINGS, INSTANCE, FOLDER, CREDIT, ACCOUNT, NOTICE };

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
                 .caption = L"B Launcher " B_LAUNCHER_VERSION L" by cubk",
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
        if (!CreateHostWindow(nullptr, L"B Launcher", 720, 450, exui::SkinBytes(exui::Skin::MainBackground), 0xFFFFFF,
                              7, exui::CaptionButtonMask::Minimize | exui::CaptionButtonMask::Close, false)) {
            return false;
        }
        // The main window stretches its skin; the edge fade into the base color is for dialogs.
        SetBackgroundMode(exui::SkinBackgroundMode::Stretch);
        SetWindowAnimationTypes(exui::WindowAnimationKind::ZoomFade, exui::WindowAnimationKind::ZoomFade, false);
        AddControls(CONTROLS);
        FindControl(LAUNCH)->SetFont(L"微软雅黑", 25, Gdiplus::FontStyleBold);
        Control<exui::Hyperlink>(CREDIT).SetTargetUrl(L"https://github.com/cubk1/b-launcher");
        auto& account = Control<exui::Hyperlink>(ACCOUNT);
        account.SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
        // It opens the account settings rather than a page, so it never turns to the visited color.
        account.SetColors(CREDIT_COLOR, 0xD20060C0, CREDIT_COLOR);
        // Notices go above 启动游戏, centered over it and 下载中心; failures get a message box.
        AddControl(NOTICE, std::make_shared<NoticeLine>(430, 283, 260, 25));
        refresh();
        return true;
    }

    void MainWindow::refresh() {
        const std::vector<Instances::Entry>& entries = Instances::list();
        auto& combo = Control<exui::ComboBox>(INSTANCE);
        if (!std::equal(entries.begin(), entries.end(), instanceKeys.begin(), instanceKeys.end(),
                        [](const Instances::Entry& entry, const std::string& key) { return entry.key == key; })) {
            instanceKeys.clear();
            combo.ClearItems();
            for (const Instances::Entry& entry : entries) {
                instanceKeys.push_back(entry.key);
                combo.AddItem(Platform::widen(entry.info.id));
            }
        }
        auto selected = std::find(instanceKeys.begin(), instanceKeys.end(), Instances::selected());
        int index = selected == instanceKeys.end() ? -1 : (int) (selected - instanceKeys.begin());
        if (index >= 0 && index != combo.GetSelectedIndex()) {
            combo.SetSelectedIndex(index);
        }

        const auto* current = Accounts::current();
        std::wstring account = current == nullptr ? std::wstring(L"还没有账号")
                                                  : Platform::widen(current->name + " · " + Accounts::kindOf(*current));
        if (account != Text(ACCOUNT)) {
            SetText(ACCOUNT, account);
        }

        Game::State state = Game::state();
        std::wstring caption = launchCaption(state);
        if (caption != Text(LAUNCH)) {
            SetText(LAUNCH, caption);
        }
        bool launchable = state == Game::State::Idle && index >= 0;
        if (FindControl(LAUNCH)->IsEnabled() != launchable) {
            FindControl(LAUNCH)->SetEnabled(launchable);
        }

        // A message box runs a loop of its own, so failures wait for the end of this poll and for a shown window.
        if (!failures.empty() && !showingFailures && IsWindowVisible(GetHwnd())) {
            PostMessageW(GetHwnd(), SHOW_FAILURES, 0, 0);
        }
    }

    LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg != SHOW_FAILURES) {
            return Form::HandleMessage(msg, wp, lp);
        }
        // Failures that arrive meanwhile join the queue rather than open a second box.
        if (!showingFailures) {
            showingFailures = true;
            while (!failures.empty() && GetHwnd() != nullptr) {
                std::wstring text = std::move(failures.front());
                failures.erase(failures.begin());
                message(GetHwnd(), text, L"提示：");
            }
            showingFailures = false;
        }
        return 0;
    }

    void MainWindow::OnAction(uint32_t id) {
        switch (id) {
            case LAUNCH: Game::launch(Instances::selected()); break;
            case FOLDER:
                if (const Instances::Entry* entry = Instances::find(Instances::selected())) {
                    Platform::open(entry->gameDirectory);
                }
                break;
            case ACCOUNT: showNotice(Notices::Kind::Info, L"账号设置还在开发中", 3000); break;
            case DOWNLOADS: showNotice(Notices::Kind::Info, L"下载中心还在开发中", 3000); break;
            case SETTINGS:
                // 忘却的旋律 has no settings yet, so this is the way back.
                if (message(GetHwnd(), L"设置窗口还在开发中，要切换回默认界面吗？\n切换后启动器会重启。", L"设置",
                            exui::MsgBoxButtons::OkCancel, {L"取消", L"切换"}) != 0) {
                    break;
                }
                if (Services::busy()) {
                    showNotice(Notices::Kind::Failed, L"有游戏、安装或下载正在进行，完成后再切换", 4000);
                    break;
                }
                UiStyles::set("default");
                if (!Platform::restart()) {
                    showNotice(Notices::Kind::Failed, L"重启失败，请手动重新打开启动器", 5000);
                }
                break;
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
