#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ForgottenMelody.h"
#include "app/launch/Game.h"
#include "app/launch/GameLog.h"
#include "app/launch/ShortcutLaunch.h"
#include "app/system/Config.h"
#include "app/system/Interface.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "ui/exui/Resources.h"
#include "ui/exui/windows/MainWindow.h"

#include <ole2.h>

#include <optional>

namespace ForgottenMelody {

    namespace {
        // EXUI's own timers use addresses as ids, so a small number never collides with them.
        constexpr UINT_PTR POLL_TIMER = 1;
        constexpr UINT POLL_MS = 30;

        void connect(MainWindow& window) {
            Notices::show({
                    [&window](Notices::Kind kind, const std::string& text, long long durationMs) {
                        window.showNotice(kind, Platform::widen(text), durationMs);
                    },
                    [&window](Notices::Kind kind, const std::string& text) {
                        window.showNotice(kind, Platform::widen(text), 0);
                        return std::string("notice");
                    },
                    [&window](const std::string&, Notices::Kind kind, const std::string& text, long long durationMs) {
                        window.showNotice(kind, Platform::widen(text), durationMs);
                    },
            });
            Interface::connect({
                    [&window] { window.showNotice(Notices::Kind::Info, L"日志窗口还在开发中", 3000); },
                    [&window](const std::string&, const std::string&) {
                        window.showNotice(Notices::Kind::Failed, L"账号管理还在开发中，请先在默认界面添加账号", 5000);
                    },
            });
        }

        void disconnect() {
            Notices::show({});
            Interface::connect({});
        }

        void serve(MainWindow& window, const std::string& pendingLaunch, bool headless) {
            Platform::window = window.GetHwnd();
            Platform::claimLaunches();
            connect(window);
            Services::start();
            if (!pendingLaunch.empty()) {
                ShortcutLaunch::request(pendingLaunch, headless);
            }
            bool launchSent = false;
            bool launchFailed = false;
            window.OnWindowMessageFilter = [&](UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
                if (msg == WM_COPYDATA) {
                    const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
                    if (data == nullptr || data->lpData == nullptr || data->cbData < sizeof(wchar_t)) {
                        return false;
                    }
                    std::wstring text((const wchar_t*) data->lpData, data->cbData / sizeof(wchar_t));
                    while (!text.empty() && text.back() == L'\0') {
                        text.pop_back();
                    }
                    result = ShortcutLaunch::receive((unsigned long long) data->dwData, text) ? 1 : 0;
                    return true;
                }
                if (msg != WM_TIMER || wp != POLL_TIMER) {
                    return false;
                }
                GameLog::poll();
                Services::poll();
                if (std::optional<ShortcutLaunch::Outcome> outcome = ShortcutLaunch::take()) {
                    launchSent = outcome->launched;
                    launchFailed = !outcome->launched;
                    if (!outcome->launched && outcome->quiet && !headless) {
                        Platform::showWindow();
                    }
                }
                if (headless) {
                    if (launchSent && Game::state() == Game::State::Running) {
                        headless = false;
                        window.CloseWindow();
                    } else if (launchFailed || (launchSent && Game::state() == Game::State::Idle)) {
                        headless = false;
                        Game::setQuiet(false);
                        window.ShowWindowWithAnimation();
                    }
                }
                result = 0;
                return true;
            };
            // A window timer reaches the window through every message loop, the modal ones included, so the
            // services keep running under dialogs too.
            SetTimer(window.GetHwnd(), POLL_TIMER, POLL_MS, nullptr);
            if (!headless) {
                window.ShowWindowWithAnimation();
            }
            window.RunModalLoop();
            Platform::window = nullptr;
            disconnect();
            Config::save();
            Services::stop();
        }
    }

    int run(const std::string& pendingLaunch, bool headless) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        // The services run on WM_TIMER, whose exceptions Windows swallows by default; let them end the
        // process as they do in the default interface.
        BOOL suppress = FALSE;
        SetUserObjectInformationW(GetCurrentProcess(), UOI_TIMERPROC_EXCEPTION_SUPPRESSION, &suppress,
                                  sizeof(suppress));
        if (FAILED(OleInitialize(nullptr))) {
            return 1;
        }
        // 1 is the icon of app.rc.
        if (!exui::Initialize(MAKEINTRESOURCEW(1))) {
            OleUninitialize();
            return 1;
        }
        int code = 1;
        {
            MainWindow window;
            if (window.create()) {
                code = 0;
                serve(window, pendingLaunch, headless);
            }
        }
        exui::Shutdown();
        OleUninitialize();
        return code;
    }
}
