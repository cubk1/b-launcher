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
#include "app/launch/AskAi.h"
#include "app/launch/Crashes.h"
#include "app/launch/Game.h"
#include "app/launch/GameLog.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/system/Platform.h"
#include "ui/exui/Skins.h"
#include "ui/exui/windows/AccountsWindow.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/DownloadsWindow.h"
#include "ui/exui/windows/LogWindow.h"
#include "ui/exui/windows/NoticesWindow.h"
#include "ui/exui/windows/MultiplayerWindow.h"
#include "ui/exui/windows/SettingsWindow.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"

#include <algorithm>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        enum : uint32_t { LAUNCH = 1, DOWNLOADS, SETTINGS, INSTANCE, FOLDER, CREDIT, ACCOUNT, NOTICE, DROP, MULTIPLAYER };

        constexpr Gdiplus::ARGB CREDIT_COLOR = 0xFEC0C0C0;

        // The 2014 window, with 下载中心 in the place of 更新游戏 and the account across from the credit line,
        // as close to the right edge as the credit is to the left one.
        const exui::ControlSpec CONTROLS[] = {
                {LAUNCH, ControlKind::Button, {430, 310, 150, 75}, L"启动游戏"},
                {DOWNLOADS, ControlKind::Button, {590, 310, 100, 35}, L"下载中心"},
                {SETTINGS, ControlKind::Button, {590, 350, 100, 35}, L"选项"},
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
                {.id = MULTIPLAYER,
                 .kind = ControlKind::Hyperlink,
                 .bounds = {351, 423, 100, 25},
                 .caption = L"联机",
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
        constexpr UINT OPEN_NOTICES = WM_APP + 5;
        constexpr UINT SHOW_CRASH = WM_APP + 6;
        constexpr size_t CRASH_LINE = 60;

        std::string shortened(std::string_view text) {
            size_t at = 0;
            for (size_t points = 0; at < text.size() && points < CRASH_LINE; points++) {
                at++;
                while (at < text.size() && ((unsigned char) text[at] & 0xC0) == 0x80) {
                    at++;
                }
            }
            return at < text.size() ? std::string(text.substr(0, at)) + "…" : std::string(text);
        }

        std::string pickAi(HWND owner) {
            std::span<const AskAi::Service> services = AskAi::services();
            HMENU menu = CreatePopupMenu();
            for (size_t i = 0; i < services.size(); i++) {
                AppendMenuW(menu, MF_STRING, i + 1, Platform::widen(services[i].name).c_str());
            }
            POINT at{};
            GetCursorPos(&at);
            SetForegroundWindow(owner);
            UINT picked = (UINT) TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, at.x, at.y, 0,
                                                owner, nullptr);
            DestroyMenu(menu);
            return picked > 0 && picked <= services.size() ? std::string(services[picked - 1].key) : std::string();
        }

        std::wstring crashText(const Crashes::Report& report) {
            std::string text = "请不要给别人发送这个窗口，点击“导出日志”，使用导出的文件去求助。\n\n";
            if (report.causes.empty()) {
                text += "没有识别出具体原因。";
            } else {
                text += "可能的原因：";
                for (const Crashes::Cause& cause : report.causes) {
                    text += "\n" + cause.title;
                    size_t from = 0;
                    while (from <= cause.detail.size()) {
                        size_t end = cause.detail.find('\n', from);
                        text += "\n    " +
                                shortened(std::string_view(cause.detail)
                                                  .substr(from,
                                                          end == std::string::npos ? std::string::npos : end - from));
                        if (end == std::string::npos) {
                            break;
                        }
                        from = end + 1;
                    }
                }
            }
            return Platform::widen(text);
        }

        // Keeps the original white line; hovering and Tab reveal that it opens
        // the history. Long messages leave room for the count at the end.
        class NoticeLine : public exui::Hyperlink {
        public:
            NoticeLine(int x, int y, int width, int height)
                : Hyperlink(x, y, width, height, {}, {}, 0xFEFFFFFF) {
                SetColors(0xFEFFFFFF, 0xFEFFFFFF, 0xFEFFFFFF);
                SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
                SetAutoGlowColor();
                SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                SetVisible(false);
            }

            void show(const std::wstring& text, const std::wstring& suffix = {}) {
                if (text == fullText && suffix == tail) return;
                fullText = text;
                tail = suffix;
                SetTooltipText(text.empty() ? std::wstring() : text + L"\n点击查看全部消息");
                std::wstring caption = text.substr(0, 256);
                for (wchar_t& ch : caption) if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                Gdiplus::Bitmap scratch(1, 1, PixelFormat32bppPARGB);
                Gdiplus::Graphics g(&scratch);
                if (auto font = exui::widget_detail::MakeFont(*this, GetFontStyle())) {
                    Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsNoWrap);
                    auto fits = [&](const std::wstring& candidate) {
                        return exui::widget_detail::MeasureText(g, candidate, *font,
                                Gdiplus::RectF(0, 0, 10000, (float) GetHeight()), format).Width <= GetWidth() - 4;
                    };
                    if (caption.size() < text.size() || !fits(caption + suffix)) {
                        size_t left = 0, right = caption.size();
                        while (left < right) {
                            size_t middle = left + (right - left + 1) / 2;
                            if (fits(caption.substr(0, middle) + L"…" + suffix)) left = middle;
                            else right = middle - 1;
                        }
                        // Keep a UTF-16 surrogate pair together at the ellipsis.
                        if (left > 0 && caption[left - 1] >= 0xD800 && caption[left - 1] <= 0xDBFF) --left;
                        caption.resize(left);
                        caption += L"…";
                    }
                }
                SetCaption(caption + suffix);
                SetVisible(!text.empty());
            }

        private:
            std::wstring fullText, tail;
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
        Control<exui::Hyperlink>(MULTIPLAYER).SetColors(CREDIT_COLOR, 0xD20060C0, CREDIT_COLOR);
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
        const auto& room = Multiplayer::state();
        std::wstring online = L"联机";
        if (room.role != Multiplayer::Role::None && !room.code.empty()) online += L" - " + Platform::widen(room.code);
        if (Text(ACCOUNT) != account || Text(MULTIPLAYER) != online) {
            UpdateText(ACCOUNT, account);
            UpdateText(MULTIPLAYER, online);
            int accountWidth = std::clamp((int) std::ceil(exui::widget_detail::UnboundedTextWidth(*FindControl(ACCOUNT))) + 4, 60, 252);
            int onlineWidth = std::clamp((int) std::ceil(exui::widget_detail::UnboundedTextWidth(*FindControl(MULTIPLAYER))) + 4, 32, 100);
            FindControl(ACCOUNT)->SetBounds(713 - accountWidth, 423, accountWidth, 25);
            FindControl(MULTIPLAYER)->SetBounds(713 - accountWidth - 12 - onlineWidth, 423, onlineWidth, 25);
        }
        FindControl(MULTIPLAYER)->SetTooltipText(room.role == Multiplayer::Role::None ? L"创建或加入联机房间"
                                                                                   : Platform::widen(Multiplayer::stageText()));

        Game::State state = Game::state(Instances::selected());
        std::wstring caption = launchCaption(state);
        UpdateText(LAUNCH, caption);
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
        refreshNotice();

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
        if (msg == SHOW_CRASH) {
            presentCrash();
            return 0;
        }
        if (msg == OPEN_NOTICES) {
            if (!showingNotices && !showingFailures && modalWindow(nullptr) == nullptr &&
                !noticeHistory.entries().empty()) {
                showingNotices = true;
                const auto* current = noticeHistory.current();
                if (showNotices(GetHwnd(), noticeHistory, current != nullptr ? current->id : std::string())) {
                    failures.clear();
                }
                showingNotices = false;
                clearReadNotices();
            }
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

    void MainWindow::showCrash(std::shared_ptr<const Crashes::Report> report) {
        if (crash == nullptr) {
            PostMessageW(GetHwnd(), SHOW_CRASH, 0, 0);
        }
        crash = std::move(report);
    }

    void MainWindow::presentCrash() {
        std::shared_ptr<const Crashes::Report> report = std::exchange(crash, nullptr);
        if (report == nullptr) {
            return;
        }
        Platform::raiseWindow();
        std::string chosen = AskAi::chosen();
        std::wstring ask = chosen.empty() ? std::wstring(L"问 AI") : Platform::widen("问 " + AskAi::nameOf(chosen));
        int choice = message(modalWindow(GetHwnd()), crashText(*report), L"游戏崩溃", exui::MsgBoxButtons::YesNoCancel,
                             {L"关闭", L"查看日志", L"导出日志", ask});
        if (choice == 6) {
            GameLog::show(report->launch.log);
            showLog();
        } else if (choice == 5) {
            if (std::optional<std::filesystem::path> target = Platform::pickSaveAs(
                        Platform::desktopDirectory() / Platform::widen(Crashes::fileName(*report)))) {
                Crashes::save(report, *target);
            }
        } else if (choice == 3) {
            std::string key = chosen.empty() ? pickAi(GetHwnd()) : chosen;
            if (!key.empty()) {
                Notices::add(Notices::Kind::Info, "正在上传崩溃报告…", 3000);
                Crashes::ask(report, key);
            }
        }
    }

    void MainWindow::showFailures() {
        // Failures that arrive meanwhile join the queue rather than open a second box.
        if (showingFailures) {
            return;
        }
        showingFailures = true;
        while (!failures.empty() && GetHwnd() != nullptr) {
            Failure failure = std::move(failures.front());
            failures.erase(failures.begin());
            const auto* entry = noticeHistory.find(failure.id);
            if (entry != nullptr && entry->read) continue;
            // Over the dialog that is open, if any: one under it would hide behind it and still take its input.
            if (message(modalWindow(GetHwnd()), failure.text, L"提示：") == 0) {
                noticeHistory.markRead(failure.id);
                if (!showingNotices) clearReadNotices();
                else refreshNotice();
            }
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
            case MULTIPLAYER: showMultiplayer(GetHwnd()); break;
            case NOTICE: PostMessageW(GetHwnd(), OPEN_NOTICES, 0, 0); break;
        }
    }

    void MainWindow::OnSelection(uint32_t id, int index) {
        if (id == INSTANCE && index >= 0 && index < (int) instanceKeys.size()) {
            Instances::select(instanceKeys[index]);
        }
    }

    void MainWindow::showNotice(Notices::Kind kind, const std::wstring& text, long long durationMs) {
        std::string id = noticeHistory.add(kind, text, durationMs);
        if (kind == Notices::Kind::Failed) {
            failures.push_back({std::move(id), text});
        }
        refreshNotice();
    }

    std::string MainWindow::startNotice(Notices::Kind kind, const std::wstring& text) {
        std::string id = noticeHistory.live(kind, text);
        refreshNotice();
        return id;
    }

    void MainWindow::finishNotice(const std::string& id, Notices::Kind kind, const std::wstring& text,
                                  long long durationMs) {
        if (!noticeHistory.finish(id, kind, text, durationMs)) return;
        if (kind == Notices::Kind::Failed) failures.push_back({id, text});
        refreshNotice();
    }

    void MainWindow::refreshNotice() {
        if (showingNotices) {
            noticeHistory.markAllRead();
            // These errors were seen in the open history, including any older
            // entries evicted by its size limit.
            failures.clear();
        }
        auto& line = Control<NoticeLine>(NOTICE);
        size_t count = noticeHistory.runningCount() + noticeHistory.unreadCount();
        if (const auto* current = noticeHistory.current(); current != nullptr && !current->text.empty()) {
            line.show(current->text, count > 1 ? L"（另有 " + std::to_wstring(count - 1) + L" 条）" : std::wstring());
        } else {
            line.show(count > 0 ? L"消息记录（" + std::to_wstring(count) + L" 条）" : std::wstring());
        }
    }

    void MainWindow::clearReadNotices() {
        std::erase_if(failures, [&](const Failure& failure) {
            const auto* entry = noticeHistory.find(failure.id);
            return entry != nullptr && entry->read;
        });
        noticeHistory.clearRead();
        if (GetHwnd() != nullptr) refreshNotice();
    }
}
