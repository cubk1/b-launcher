#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/MultiplayerWindow.h"
#include "app/instances/Instances.h"
#include "app/launch/Game.h"
#include "app/multiplayer/Multiplayer.h"
#include "app/multiplayer/RoomApi.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/ScrollPanel.h"
#include "ui/exui/windows/Dialogs.h"

#include <algorithm>
#include <charconv>
#include <memory>

namespace ForgottenMelody {
    namespace {
        using Multiplayer::Role;
        using Multiplayer::Stage;

        class MultiplayerWindow : public SectionedForm {
        public:
            ~MultiplayerWindow() override { CloseWindow(false); }

            enum : uint32_t {
                HOME = FIRST_CONTROL, ROOM, SERVICE, HOST_GROUP, HOST_NOTE, WORLDS, PORT_LABEL, PORT, PORT_NOTE, HOST,
                JOIN_GROUP, JOIN_NOTE, JOIN_LABEL, JOIN_CODE, JOIN_STATUS, JOIN,
                ROOM_GROUP, CODE_LABEL, CODE, COPY_CODE, STATE_LABEL, STATE, TARGET_LABEL, TARGET,
                ADDRESS_LABEL, ADDRESS, COPY_ADDRESS, GAME_LABEL, GAME, PLAY,
                ERROR_GROUP, ERROR_TEXT, FOOTER_NOTE, LEAVE, RETRY, BACK,
            };

            bool create(HWND parent) {
                Frame frame;
                frame.width = 760;
                frame.listWidth = 0;
                if (!createFrame(parent, L"联机", L"", {{L"联机", HOME}, {L"联机房间", ROOM}}, frame)) return false;
                SetIndependent(true);
                using exui::ControlKind;
                const exui::ControlSpec controls[] = {
                        {SERVICE, ControlKind::Label, {}},
                        {HOST_GROUP, ControlKind::Group, {}, L"创建房间"},
                        {HOST_NOTE, ControlKind::Label, {}, L"请先在游戏的单人世界里对局域网开放，然后选择下方的世界。"},
                        {WORLDS, ControlKind::List, {}},
                        {PORT_LABEL, ControlKind::Label, {}, L"手动端口"},
                        {PORT, ControlKind::Edit, {}},
                        {PORT_NOTE, ControlKind::Label, {}, L"留空时使用选中的世界"},
                        {HOST, ControlKind::Button, {}, L"创建房间"},
                        {JOIN_GROUP, ControlKind::Group, {}, L"加入房间"},
                        {JOIN_NOTE, ControlKind::Label, {}, L"输入好友发来的 6 位房间码。"},
                        {JOIN_LABEL, ControlKind::Label, {}, L"房间码"},
                        {JOIN_CODE, ControlKind::Edit, {}},
                        {JOIN_STATUS, ControlKind::Label, {}},
                        {JOIN, ControlKind::Button, {}, L"加入房间"},
                        {ROOM_GROUP, ControlKind::Group, {}, L"房间"},
                        {CODE_LABEL, ControlKind::Label, {}, L"房间码"},
                        {CODE, ControlKind::Edit, {}},
                        {COPY_CODE, ControlKind::Button, {}, L"复制房间码"},
                        {STATE_LABEL, ControlKind::Label, {}, L"状态"},
                        {STATE, ControlKind::Label, {}},
                        {TARGET_LABEL, ControlKind::Label, {}, L"本地端口"},
                        {TARGET, ControlKind::Label, {}},
                        {ADDRESS_LABEL, ControlKind::Label, {}, L"连接地址"},
                        {ADDRESS, ControlKind::Edit, {}},
                        {COPY_ADDRESS, ControlKind::Button, {}, L"复制地址"},
                        {GAME_LABEL, ControlKind::Label, {}, L"当前实例"},
                        {GAME, ControlKind::Label, {}},
                        {PLAY, ControlKind::Button, {}, L"启动并加入"},
                        {ERROR_GROUP, ControlKind::Group, {}, L"连接失败"},
                        {ERROR_TEXT, ControlKind::Edit, {}},
                        {FOOTER_NOTE, ControlKind::Label, {}},
                        {LEAVE, ControlKind::Button, {}, L"退出房间"},
                        {RETRY, ControlKind::Button, {}, L"重试"},
                        {BACK, ControlKind::Button, {}, L"返回"},
                };
                AddControls(controls);
                auto& homePanel = *FindControl(HOME);
                auto& roomPanel = *FindControl(ROOM);
                home = &homePanel.Add<exui::ScrollPanel>(0, 0, homePanel.GetWidth(), homePanel.GetHeight());
                room = &roomPanel.Add<exui::ScrollPanel>(0, 0, roomPanel.GetWidth(), roomPanel.GetHeight());
                MoveInto(home->GetContent(), {SERVICE, HOST_GROUP, HOST_NOTE, WORLDS, PORT_LABEL, PORT, PORT_NOTE, HOST,
                                             JOIN_GROUP, JOIN_NOTE, JOIN_LABEL, JOIN_CODE, JOIN_STATUS, JOIN});
                MoveInto(room->GetContent(), {ROOM_GROUP, CODE_LABEL, CODE, COPY_CODE, STATE_LABEL, STATE,
                                             TARGET_LABEL, TARGET, ADDRESS_LABEL, ADDRESS, COPY_ADDRESS,
                                             GAME_LABEL, GAME, PLAY, ERROR_GROUP, ERROR_TEXT});
                for (uint32_t id : {SERVICE, JOIN_STATUS, STATE, TARGET, GAME})
                    Control<exui::Label>(id).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                for (uint32_t id : {PORT, JOIN_CODE}) Control<exui::EditBox>(id).SetInputMode(exui::EditInputMode::Digits);
                for (uint32_t id : {CODE, ADDRESS, ERROR_TEXT}) Control<exui::EditBox>(id).SetReadOnly(true);
                Control<exui::EditBox>(ERROR_TEXT).SetMultiline(true);
                FindControl(PORT)->SetTooltipText(L"游戏聊天栏里显示的局域网端口，范围为 1 - 65535");
                FindControl(ADDRESS)->SetTooltipText(L"也可以在游戏的多人游戏里选择直接连接，输入这个地址");
                Control<exui::EditBox>(PORT).OnTextChanged = [this](const std::wstring&) { refresh(); };
                Control<exui::EditBox>(JOIN_CODE).OnTextChanged = [this](const std::wstring&) {
                    cancelLookup();
                    UpdateText(JOIN_STATUS, L"");
                    refresh();
                };
                auto& worlds = Control<exui::ListView>(WORLDS);
                worlds.InsertColumn(L"世界", 410);
                worlds.InsertColumn(L"版本", 150);
                worlds.InsertColumn(L"端口", 85);
                firstScan = Multiplayer::worldsSerial();
                using exui::Layout;
                home->SetContentLayout(Layout::Column({
                        cell(SERVICE, 24),
                        group(HOST_GROUP, {cell(HOST_NOTE, 24), cell(WORLDS, 130),
                                row(PORT_LABEL, {cell(PORT, 110), cell(PORT_NOTE), cell(HOST, 100)}, 30, 85)}),
                        group(JOIN_GROUP, {cell(JOIN_NOTE, 24),
                                row(JOIN_LABEL, {cell(JOIN_CODE, 130), cell(JOIN_STATUS), cell(JOIN, 100)}, 30, 85)}),
                }, 15), {15, 10, 3, 10});
                OnDestroying = [this] { cancelLookup(); };
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                bool available = Multiplayer::available();
                if (available) {
                    Multiplayer::warmUp();
                    if (Multiplayer::state().role == Role::None || Multiplayer::state().stage == Stage::Failed)
                        Multiplayer::watchService();
                    if (Multiplayer::state().role == Role::None) {
                        Multiplayer::watchWorlds();
                    }
                }
                if (Multiplayer::state().role == Role::None) {
                    if (shownShape > 0) firstScan = Multiplayer::worldsSerial();
                    refreshHome(available);
                }
                const auto& state = Multiplayer::state();
                if (state.role != Role::None) refreshRoom();
                bool inRoom = state.role != Role::None;
                bool failed = inRoom && state.stage == Stage::Failed;
                int shape = !inRoom ? 0 : state.role == Role::Host ? 1 : 2;
                if (failed) shape += 2;
                if (shape != shownShape) {
                    shownShape = shape;
                    showSection(inRoom ? 1 : 0);
                    layoutRoom();
                    FindControl(LEAVE)->SetVisible(inRoom && !failed);
                    FindControl(RETRY)->SetVisible(failed);
                    FindControl(BACK)->SetVisible(failed);
                    std::vector<exui::Layout> footer{cell(FOOTER_NOTE)};
                    if (failed) { footer.push_back(cell(BACK, 80)); footer.push_back(cell(RETRY, 80)); }
                    else if (inRoom) footer.push_back(cell(LEAVE, 100));
                    setFooterActions(std::move(footer));
                }
                UpdateText(FOOTER_NOTE, failed ? L"连接已中断，可重试或返回。"
                        : inRoom ? L"关闭窗口后仍保持联机。" : L"联机窗口可与主窗口同时使用。");
                revealFocus(inRoom ? *room : *home);
            }

            void OnSelection(uint32_t id, int index) override {
                if (id == WORLDS) refresh();
                else SectionedForm::OnSelection(id, index);
            }

            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) { SectionedForm::OnAction(id); return; }
                const auto& state = Multiplayer::state();
                switch (id) {
                    case HOST:
                        if (state.role == Role::None && Multiplayer::available() && Multiplayer::canHost()) {
                            int port = manualPort();
                            int picked = selectedWorld();
                            if (port > 0 || (trimmed(Text(PORT)).empty() && picked > 0)) {
                                cancelLookup();
                                Multiplayer::pick(port > 0 ? 0 : picked);
                                Multiplayer::type(port);
                                Multiplayer::host();
                            }
                        }
                        break;
                    case JOIN:
                        if (state.role == Role::None && Multiplayer::available()) {
                            std::string code = trimmed(Text(JOIN_CODE));
                            if (RoomApi::validCode(code) && pendingCode.empty()) {
                                pendingCode = code;
                                Multiplayer::check(code);
                            }
                        }
                        break;
                    case COPY_CODE: copy(CODE); break;
                    case COPY_ADDRESS: copy(ADDRESS); break;
                    case PLAY:
                        if (state.role == Role::Guest && state.stage == Stage::Ready && !state.address.empty()) {
                            const auto* entry = Instances::find(Instances::selected());
                            if (entry && Game::state(entry->key) == Game::State::Idle)
                                Game::launch(entry->key, false, {}, state.address);
                        }
                        break;
                    case LEAVE: if (state.stage != Stage::Closing) Multiplayer::leave(); break;
                    case RETRY: if (retryable()) Multiplayer::retry(); break;
                    case BACK: if (Multiplayer::canRetry()) Multiplayer::dismiss(); break;
                }
                refresh();
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { CloseWindow(); return 0; }
                if (msg == WM_KEYDOWN && wp == VK_RETURN && Multiplayer::state().role == Role::None) {
                    auto* focus = GetFocusedWidget();
                    if (focus == FindControl(JOIN_CODE)) { Click(JOIN); return 0; }
                    if (focus == FindControl(PORT) || focus == FindControl(WORLDS)) { Click(HOST); return 0; }
                }
                return SectionedForm::HandleMessage(msg, wp, lp);
            }

        private:
            exui::ScrollPanel *home = nullptr, *room = nullptr;
            int shownShape = -1, firstScan = 0;
            Rows shownWorlds;
            std::vector<std::string> worldKeys;
            std::string pendingCode;

            void cancelLookup() {
                if (pendingCode.empty()) return;
                pendingCode.clear();
                Multiplayer::forgetLookup();
            }

            int manualPort() const {
                std::string text = trimmed(Text(PORT));
                int port = 0;
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
                return error == std::errc() && end == text.data() + text.size() && port > 0 && port <= 65535 ? port : 0;
            }

            int selectedWorld() const {
                if (Multiplayer::worldsSerial() == firstScan) return 0;
                std::string key = selectedKey(Control<exui::ListView>(WORLDS), worldKeys);
                for (const auto& world : Multiplayer::worlds())
                    if (key == std::to_string(world.port)) return world.port;
                return 0;
            }

            void refreshHome(bool available) {
                const auto& service = Multiplayer::service();
                std::wstring status = !available ? L"这个版本没有包含联机组件"
                        : !service.known ? L"正在查询服务器状态…"
                        : !service.reachable ? L"联机服务器暂时连不上"
                        : service.maintenance ? (service.message.empty() ? L"联机服务器维护中" : Platform::widen(service.message))
                        : L"联机服务器可用性 " + std::to_wstring(service.available) + L"/" + std::to_wstring(service.total);
                UpdateText(SERVICE, status);
                FindControl(SERVICE)->SetTooltipText(status);
                Rows rows;
                std::vector<std::string> keys;
                bool fresh = Multiplayer::worldsSerial() != firstScan;
                if (available && fresh) {
                    for (const auto& world : Multiplayer::worlds()) {
                        rows.push_back({Platform::widen(world.name.empty() ? "局域网世界" : world.name),
                                        Platform::widen(world.version), std::to_wstring(world.port)});
                        keys.push_back(std::to_string(world.port));
                    }
                }
                bool onlyWorld = keys.size() == 1;
                if (rows.empty()) {
                    rows.push_back({!available ? L"联机组件不可用" : !fresh ? L"正在查找局域网世界…" : L"尚未发现世界，请在游戏中对局域网开放", L"", L""});
                    keys.emplace_back();
                }
                auto& worlds = Control<exui::ListView>(WORLDS);
                fill(worlds, shownWorlds, worldKeys, std::move(rows), std::move(keys));
                if (onlyWorld && worlds.GetSelectedRow() < 0) worlds.SetSelectedRow(0);
                FindControl(PORT)->SetEnabled(available);
                FindControl(JOIN_CODE)->SetEnabled(available);
                FindControl(WORLDS)->SetEnabled(available);
                FindControl(HOST)->SetEnabled(available && Multiplayer::canHost() && pendingCode.empty() &&
                        (trimmed(Text(PORT)).empty() ? selectedWorld() > 0 : manualPort() > 0));
                if (!pendingCode.empty()) {
                    const auto& lookup = Multiplayer::lookup();
                    if (lookup.code == pendingCode && lookup.done) {
                        std::string code = pendingCode;
                        bool exists = lookup.exists && lookup.error.empty();
                        std::wstring problem = lookup.error.empty() ? L"房间不存在或已关闭" : Platform::widen(lookup.error);
                        cancelLookup();
                        if (exists && code == trimmed(Text(JOIN_CODE))) Multiplayer::join(code);
                        else UpdateText(JOIN_STATUS, problem);
                    } else UpdateText(JOIN_STATUS, L"正在查找房间…");
                }
                FindControl(JOIN_STATUS)->SetTooltipText(Text(JOIN_STATUS));
                FindControl(JOIN)->SetEnabled(available && pendingCode.empty() && RoomApi::validCode(trimmed(Text(JOIN_CODE))));
                UpdateText(JOIN, pendingCode.empty() ? L"加入房间" : L"正在查找…");
            }

            void layoutRoom() {
                using exui::Layout;
                bool host = Multiplayer::state().role == Role::Host;
                bool failed = Multiplayer::state().stage == Stage::Failed;
                for (uint32_t id : {TARGET_LABEL, TARGET}) FindControl(id)->SetVisible(host);
                for (uint32_t id : {ADDRESS_LABEL, ADDRESS, COPY_ADDRESS, GAME_LABEL, GAME, PLAY}) FindControl(id)->SetVisible(!host);
                FindControl(ERROR_GROUP)->SetVisible(failed);
                FindControl(ERROR_TEXT)->SetVisible(failed);
                std::vector<Layout> rows{
                        row(CODE_LABEL, {cell(CODE), cell(COPY_CODE, 100)}, 30, 85),
                        row(STATE_LABEL, {cell(STATE)}, 30, 85),
                };
                if (host) rows.push_back(row(TARGET_LABEL, {cell(TARGET)}, 30, 85));
                else {
                    rows.push_back(row(ADDRESS_LABEL, {cell(ADDRESS), cell(COPY_ADDRESS, 100)}, 30, 85));
                    rows.push_back(row(GAME_LABEL, {cell(GAME), cell(PLAY, 100)}, 30, 85));
                }
                std::vector<Layout> groups{group(ROOM_GROUP, std::move(rows))};
                if (failed) groups.push_back(group(ERROR_GROUP, {cell(ERROR_TEXT, 110)}));
                room->SetContentLayout(Layout::Column(std::move(groups), 15), {15, 10, 3, 10});
            }

            bool retryable() const {
                return Multiplayer::canRetry() && (Multiplayer::state().role != Role::Host || Multiplayer::canHost());
            }

            void refreshRoom() {
                const auto& state = Multiplayer::state();
                UpdateText(ROOM_GROUP, state.role == Role::Host ? L"你的房间" : L"加入的房间");
                UpdateText(CODE, Platform::widen(state.code));
                FindControl(COPY_CODE)->SetEnabled(!state.code.empty());
                std::wstring status = Platform::widen(Multiplayer::stageText());
                if (state.role == Role::Host && state.stage == Stage::Ready && state.published)
                    status += state.links > 0 ? L"，" + std::to_wstring(state.links) + L" 个连接" : L"，等待好友加入";
                UpdateText(STATE, status);
                FindControl(STATE)->SetTooltipText(status);
                UpdateText(TARGET, state.target > 0 ? std::to_wstring(state.target) : L"等待世界开放");
                UpdateText(ADDRESS, Platform::widen(state.address));
                FindControl(COPY_ADDRESS)->SetEnabled(!state.address.empty());
                const auto* entry = Instances::find(Instances::selected());
                UpdateText(GAME, entry ? Platform::widen(entry->info.id) : L"请在主窗口选择实例");
                FindControl(GAME)->SetTooltipText(Text(GAME));
                FindControl(PLAY)->SetEnabled(state.role == Role::Guest && state.stage == Stage::Ready &&
                        !state.address.empty() && entry && Game::state(entry->key) == Game::State::Idle);
                UpdateText(ERROR_TEXT, Platform::widen(state.error + (state.detail.empty() ? "" : "\r\n\r\n" + state.detail)));
                FindControl(RETRY)->SetEnabled(retryable());
                FindControl(BACK)->SetEnabled(Multiplayer::canRetry());
                bool host = state.role == Role::Host, closing = state.stage == Stage::Closing;
                UpdateText(LEAVE, closing ? (host ? L"正在关闭…" : L"正在退出…") : host ? L"关闭房间" : L"退出房间");
                FindControl(LEAVE)->SetEnabled(!closing);
            }

            void copy(uint32_t id) {
                auto& edit = Control<exui::EditBox>(id);
                edit.SelectAll();
                edit.CopyToClipboard();
            }
        };

        std::unique_ptr<MultiplayerWindow> multiplayerWindow;
    }

    void showMultiplayer(HWND parent) {
        if (!multiplayerWindow || !IsWindow(multiplayerWindow->GetHwnd())) {
            multiplayerWindow = std::make_unique<MultiplayerWindow>();
            if (!multiplayerWindow->create(parent)) { multiplayerWindow.reset(); return; }
        }
        multiplayerWindow->ShowWindowWithAnimation();
        SetForegroundWindow(multiplayerWindow->GetHwnd());
    }

    void closeMultiplayer() {
        multiplayerWindow.reset();
    }
}
