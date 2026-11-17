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
#include "app/multiplayer/TerracottaGuest.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/ScrollPanel.h"
#include "ui/exui/windows/Dialogs.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace ForgottenMelody {
    namespace {
        using Multiplayer::Role;
        using Multiplayer::Stage;
        constexpr RECT PAGE_PADDING{15, 10, 3, 10};
        constexpr int GROUP_GAP = 15;

        std::wstring trimWide(std::wstring_view value) {
            constexpr wchar_t WHITESPACE[] = L" \t\r\n\v\f";
            size_t begin = value.find_first_not_of(WHITESPACE);
            if (begin == std::wstring_view::npos) return {};
            size_t end = value.find_last_not_of(WHITESPACE);
            return std::wstring(value.substr(begin, end - begin + 1));
        }

        std::wstring firstTrimmedWideLine(std::wstring_view value) {
            std::wstring result = trimWide(value);
            size_t lineBreak = result.find_first_of(L"\r\n");
            if (lineBreak != std::wstring::npos) {
                result = trimWide(std::wstring_view(result).substr(0, lineBreak));
            }
            return result;
        }

        std::wstring readTrimmedClipboardLine(HWND owner) {
            if (!OpenClipboard(owner)) return {};
            std::wstring result;
            if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                    size_t limit = GlobalSize(data) / sizeof(wchar_t);
                    size_t length = 0;
                    while (length < limit && text[length] != L'\0') ++length;
                    result.assign(text, length);
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
            return firstTrimmedWideLine(result);
        }

        int measureLabelHeight(const exui::Label& label, const std::wstring& text, int availableWidth, int minHeight) {
            if (text.empty()) return minHeight;
            Gdiplus::Bitmap scratch(1, 1, PixelFormat32bppPARGB);
            Gdiplus::Graphics g(&scratch);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            auto font = exui::widget_detail::MakeFont(label, label.GetFontStyle());
            if (!font) return minHeight;
            Gdiplus::StringFormat format;
            Gdiplus::RectF bounds(0, 0, static_cast<float>(std::max(120, availableWidth - 4)), 2000.0f);
            Gdiplus::RectF measured;
            if (g.MeasureString(text.c_str(), static_cast<INT>(text.size()), font.get(), bounds, &format, &measured) !=
                Gdiplus::Ok) {
                return minHeight;
            }
            return std::max(minHeight, static_cast<int>(std::ceil(measured.Height)) + 6);
        }

        float measureUnwrappedWidth(Gdiplus::Graphics& g, const Gdiplus::Font& font, const std::wstring& text) {
            if (text.empty()) return 0.0f;
            Gdiplus::StringFormat format;
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces |
                                  Gdiplus::StringFormatFlagsNoFitBlackBox);
            Gdiplus::RectF bounds;
            if (g.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, Gdiplus::PointF(0, 0), &format,
                                &bounds) != Gdiplus::Ok) {
                return 0.0f;
            }
            return bounds.Width;
        }

        void prepareTerracottaError(const exui::EditBox& box, const std::wstring& raw, int availableWidth,
                                    bool& outMultiline, int& outHeight, std::wstring& outDisplay) {
            outMultiline = false;
            outHeight = 32;
            outDisplay.clear();
            if (raw.empty()) return;

            std::vector<std::wstring> paragraphs;
            size_t pos = 0;
            while (pos <= raw.size()) {
                size_t next = raw.find(L'\n', pos);
                std::wstring line = raw.substr(pos, next == std::wstring::npos ? std::wstring::npos : next - pos);
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                paragraphs.push_back(std::move(line));
                if (next == std::wstring::npos) break;
                pos = next + 1;
            }

            Gdiplus::Bitmap scratch(1, 1, PixelFormat32bppPARGB);
            Gdiplus::Graphics g(&scratch);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            auto font = exui::widget_detail::MakeFont(box, box.GetFontStyle());
            if (!font) {
                outMultiline = paragraphs.size() > 1;
                outHeight = outMultiline ? std::clamp(static_cast<int>(paragraphs.size()) * 16 + 20, 54, 136) : 32;
                for (size_t i = 0; i < paragraphs.size(); ++i) {
                    if (i > 0) outDisplay += L"\r\n";
                    outDisplay += paragraphs[i];
                }
                return;
            }

            float singleLineMax = static_cast<float>(std::max(160, availableWidth - 8));
            if (paragraphs.size() == 1 && measureUnwrappedWidth(g, *font, paragraphs.front()) <= singleLineMax) {
                outMultiline = false;
                outHeight = 32;
                outDisplay = paragraphs.front();
                return;
            }

            float wrapMax = static_cast<float>(std::max(140, availableWidth - 22));
            std::vector<std::wstring> lines;
            for (const std::wstring& para : paragraphs) {
                if (para.empty()) {
                    lines.emplace_back();
                    continue;
                }
                if (measureUnwrappedWidth(g, *font, para) <= wrapMax) {
                    lines.push_back(para);
                    continue;
                }
                size_t start = 0;
                while (start < para.size()) {
                    size_t low = 1;
                    size_t high = para.size() - start;
                    while (low < high) {
                        size_t mid = low + (high - low + 1) / 2;
                        if (measureUnwrappedWidth(g, *font, para.substr(start, mid)) <= wrapMax) low = mid;
                        else high = mid - 1;
                    }
                    if (low < para.size() - start && para[start + low - 1] >= 0xD800 &&
                        para[start + low - 1] <= 0xDBFF) {
                        if (low > 1) --low;
                        else if (start + low < para.size()) ++low;
                    }
                    if (start + low < para.size() && low > 4) {
                        for (size_t cut = low; cut > low / 2; --cut) {
                            wchar_t ch = para[start + cut - 1];
                            if (ch == L' ' || ch == L'/' || ch == L'-' || ch == L'：' || ch == L'，' || ch == L'。') {
                                low = cut;
                                break;
                            }
                        }
                    }
                    lines.push_back(para.substr(start, low));
                    start += low;
                    while (start < para.size() && para[start] == L' ') ++start;
                }
            }

            Gdiplus::StringFormat format;
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces |
                                  Gdiplus::StringFormatFlagsNoFitBlackBox);
            Gdiplus::RectF glyphRect;
            g.MeasureString(L"中", 1, font.get(), Gdiplus::PointF(0, 0), &format, &glyphRect);
            int pitch = std::max(15, static_cast<int>(std::ceil(glyphRect.Height)));
            outMultiline = true;
            outHeight = std::clamp(static_cast<int>(lines.size()) * pitch + 20, 54, 136);
            for (size_t i = 0; i < lines.size(); ++i) {
                if (i > 0) outDisplay += L"\r\n";
                outDisplay += lines[i];
            }
        }

        class PillLabel final : public exui::Label {
        public:
            PillLabel() { SetFont(L"宋体", 12.0f); }

            void SetPillBorder(Gdiplus::ARGB color) {
                m_pillBorder = color;
                Invalidate();
            }

            void OnPaint(Gdiplus::Graphics& g) override {
                exui::Label::OnPaint(g);
                if (m_pillBorder) {
                    const Gdiplus::RectF whole(0, 0, static_cast<float>(GetWidth()), static_cast<float>(GetHeight()));
                    exui::DrawBorder(g, whole, m_pillBorder);
                }
            }

        private:
            Gdiplus::ARGB m_pillBorder = 0;
        };

        class MultiplayerWindow : public SectionedForm, public std::enable_shared_from_this<MultiplayerWindow> {
        public:
            ~MultiplayerWindow() override { CloseWindow(false); }

            enum : uint32_t {
                HOME = FIRST_CONTROL, ROOM, SERVICE, HOST_GROUP, HOST_NOTE, WORLDS, HOST, CANCEL_HOST,
                JOIN_GROUP, JOIN_NOTE, JOIN_LABEL, JOIN_CODE, JOIN_STATUS, JOIN,
                ROOM_GROUP, CODE_LABEL, CODE, COPY_CODE, STATE_LABEL, STATE, TARGET_LABEL, TARGET,
                ADDRESS_LABEL, ADDRESS, COPY_ADDRESS, GAME_LABEL, GAME, PLAY,
                ERROR_GROUP, ERROR_TEXT, LEAVE, RETRY, BACK,
                MEMBERS_GROUP, MEMBERS, KICK, BAN,
                TERRACOTTA, TC_OPEN, TC_HOME_GROUP, TC_HOME_NOTE,
                TC_GROUP, TC_NOTE, TC_CODE_LABEL, TC_CODE, TC_PASTE, TC_COPY_CODE,
                TC_STATUS_LABEL, TC_STATUS, TC_ADDRESS_LABEL, TC_ADDRESS, TC_COPY,
                TC_GAME_LABEL, TC_GAME, TC_GAME_STATE, TC_PLAY,
                TC_ERROR_GROUP, TC_ERROR, TC_COPY_ERROR, TC_MEMBERS_GROUP, TC_MEMBERS,
                TC_JOIN, TC_LEAVE, TC_RETRY, TC_BACK, TC_FEEDBACK,
            };

            bool create(HWND parent) {
                Frame frame;
                frame.width = 760;
                frame.listWidth = 0;
                if (!createFrame(parent, L"联机", L"", {{L"联机", HOME}, {L"联机房间", ROOM},
                        {L"HMCL / 陶瓦联机", TERRACOTTA}}, frame)) return false;
                SetIndependent(true);
                using exui::ControlKind;
                const exui::ControlSpec controls[] = {
                        {SERVICE, ControlKind::Label, {}},
                        {HOST_GROUP, ControlKind::Group, {}, L"创建房间"},
                        {HOST_NOTE, ControlKind::Label, {}},
                        {WORLDS, ControlKind::List, {}},
                        {HOST, ControlKind::Button, {}, L"创建房间"},
                        {CANCEL_HOST, ControlKind::Button, {}, L"取消"},
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
                        {LEAVE, ControlKind::Button, {}, L"退出房间"},
                        {RETRY, ControlKind::Button, {}, L"重试"},
                        {BACK, ControlKind::Button, {}, L"返回"},
                        {MEMBERS_GROUP, ControlKind::Group, {}, L"成员"},
                        {MEMBERS, ControlKind::List, {}},
                        {KICK, ControlKind::Button, {}, L"移出房间"},
                        {BAN, ControlKind::Button, {}, L"封禁"},
                        {TC_HOME_GROUP, ControlKind::Group, {}, L"HMCL / 陶瓦联机"},
                        {TC_HOME_NOTE, ControlKind::Label, {}, L"粘贴好友从 HMCL 或陶瓦联机分享的房间码。"},
                        {TC_OPEN, ControlKind::Button, {}, L"加入 HMCL / 陶瓦房间"},
                        {TC_GROUP, ControlKind::Group, {}, L"加入 HMCL / 陶瓦房间"},
                        {TC_NOTE, ControlKind::Label, {}, L"粘贴好友从 HMCL 或陶瓦联机分享的完整房间码。"},
                        {TC_CODE_LABEL, ControlKind::Label, {}, L"房间码"},
                        {TC_CODE, ControlKind::Edit, {}},
                        {TC_PASTE, ControlKind::Button, {}, L"粘贴"},
                        {TC_COPY_CODE, ControlKind::Button, {}, L"复制房间码"},
                        {TC_STATUS_LABEL, ControlKind::Label, {}, L"状态"},
                        {TC_STATUS, ControlKind::Label, {}},
                        {TC_ADDRESS_LABEL, ControlKind::Label, {}, L"连接地址"},
                        {TC_ADDRESS, ControlKind::Edit, {}},
                        {TC_COPY, ControlKind::Button, {}, L"复制地址"},
                        {TC_GAME_LABEL, ControlKind::Label, {}, L"当前实例"},
                        {TC_GAME, ControlKind::Label, {}},
                        {TC_PLAY, ControlKind::Button, {}, L"启动并加入"},
                        {TC_ERROR_GROUP, ControlKind::Group, {}, L"加入失败"},
                        {TC_ERROR, ControlKind::Edit, {}},
                        {TC_COPY_ERROR, ControlKind::Button, {}, L"复制错误信息"},
                        {TC_MEMBERS_GROUP, ControlKind::Group, {}, L"联机成员"},
                        {TC_MEMBERS, ControlKind::List, {}},
                        {TC_JOIN, ControlKind::Button, {}, L"加入房间"},
                        {TC_LEAVE, ControlKind::Button, {}, L"退出房间"},
                        {TC_RETRY, ControlKind::Button, {}, L"重试"},
                        {TC_BACK, ControlKind::Button, {}, L"返回"},
                };
                AddControls(controls);
                AddControl(TC_GAME_STATE, std::make_shared<PillLabel>());
                AddControl(TC_FEEDBACK, std::make_shared<PillLabel>());
                auto& homePanel = *FindControl(HOME);
                auto& roomPanel = *FindControl(ROOM);
                home = &homePanel.Add<exui::ScrollPanel>(0, 0, homePanel.GetWidth(), homePanel.GetHeight());
                room = &roomPanel.Add<exui::ScrollPanel>(0, 0, roomPanel.GetWidth(), roomPanel.GetHeight());
                auto& tcPanel = *FindControl(TERRACOTTA);
                terracotta = &tcPanel.Add<exui::ScrollPanel>(0, 0, tcPanel.GetWidth(), tcPanel.GetHeight());
                MoveInto(home->GetContent(), {SERVICE, HOST_GROUP, HOST_NOTE, WORLDS, HOST, CANCEL_HOST,
                                             JOIN_GROUP, JOIN_NOTE, JOIN_LABEL, JOIN_CODE, JOIN_STATUS, JOIN,
                                             TC_HOME_GROUP, TC_HOME_NOTE, TC_OPEN});
                MoveInto(room->GetContent(), {ROOM_GROUP, CODE_LABEL, CODE, COPY_CODE, STATE_LABEL, STATE,
                                             TARGET_LABEL, TARGET, ADDRESS_LABEL, ADDRESS, COPY_ADDRESS,
                                             GAME_LABEL, GAME, PLAY, ERROR_GROUP, ERROR_TEXT,
                                             MEMBERS_GROUP, MEMBERS, KICK, BAN});
                MoveInto(terracotta->GetContent(), {TC_GROUP, TC_NOTE, TC_CODE_LABEL, TC_CODE,
                        TC_PASTE, TC_COPY_CODE, TC_STATUS_LABEL, TC_STATUS, TC_ADDRESS_LABEL, TC_ADDRESS, TC_COPY,
                        TC_GAME_LABEL, TC_GAME, TC_GAME_STATE, TC_PLAY, TC_ERROR_GROUP, TC_ERROR, TC_COPY_ERROR,
                        TC_MEMBERS_GROUP, TC_MEMBERS});
                Control<exui::EditBox>(TC_ADDRESS).SetReadOnly(true);
                auto& tcError = Control<exui::EditBox>(TC_ERROR);
                tcError.SetReadOnly(true);
                tcError.SetBackgroundColor(0xE8FFF7F7);
                tcError.SetBorderColor(0x68C84A4A);
                tcError.SetTextColor(0xE68B1A1A);
                Control<exui::Label>(TC_STATUS).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::Label>(TC_GAME).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                auto& tcGameState = Control<exui::Label>(TC_GAME_STATE);
                tcGameState.SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
                tcGameState.SetFont(L"宋体", 11.0f);
                auto& tcFeedback = Control<exui::Label>(TC_FEEDBACK);
                tcFeedback.SetAlignment(Gdiplus::StringAlignmentCenter, Gdiplus::StringAlignmentCenter);
                tcFeedback.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::Label>(TC_NOTE).SetMultiline(true);
                FindControl(TC_PASTE)->SetTooltipText(L"从剪贴板读取第一行房间码并填入");
                FindControl(TC_ADDRESS)->SetTooltipText(L"在游戏的多人游戏中选择陶瓦局域网入口，或直接连接这个地址");
                FindControl(TC_PLAY)->SetTooltipWhenDisabled(true);
                UpdateText(TC_CODE, Platform::widen(TerracottaGuest::state().code));
                auto& tcCode = Control<exui::EditBox>(TC_CODE);
                tcCode.PasteTransform = [](std::wstring_view raw) { return firstTrimmedWideLine(raw); };
                tcCode.OnTextChanged = [this](const std::wstring&) { refresh(); };
                auto& tcMembers = Control<exui::ListView>(TC_MEMBERS);
                tcMembers.SetRowHeight(24);
                tcMembers.SetHeaderHeight(26);
                tcMembers.InsertColumn(L"玩家", 500);
                tcMembers.InsertColumn(L"身份", 145);
                for (uint32_t id : {TC_JOIN, TC_LEAVE, TC_RETRY, TC_BACK, TC_FEEDBACK})
                    FindControl(id)->SetVisible(false);
                for (uint32_t id : {SERVICE, JOIN_STATUS, STATE, TARGET, GAME})
                    Control<exui::Label>(id).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::EditBox>(JOIN_CODE).SetInputMode(exui::EditInputMode::Digits);
                for (uint32_t id : {CODE, ADDRESS, ERROR_TEXT}) Control<exui::EditBox>(id).SetReadOnly(true);
                Control<exui::EditBox>(ERROR_TEXT).SetMultiline(true);
                FindControl(ADDRESS)->SetTooltipText(L"也可以在游戏的多人游戏里选择直接连接，输入这个地址");
                Control<exui::EditBox>(JOIN_CODE).OnTextChanged = [this](const std::wstring&) {
                    cancelLookup();
                    UpdateText(JOIN_STATUS, L"");
                    refresh();
                };
                auto& worlds = Control<exui::ListView>(WORLDS);
                worlds.InsertColumn(L"世界", 410);
                worlds.InsertColumn(L"版本", 150);
                worlds.InsertColumn(L"端口", 85);
                auto& members = Control<exui::ListView>(MEMBERS);
                members.SetRowHeight(24);
                members.SetHeaderHeight(26);
                members.InsertColumn(L"成员", 265);
                members.InsertColumn(L"身份", 110);
                members.InsertColumn(L"状态", 80);
                members.InsertColumn(L"地址", 190);
                OnDestroying = [this] { cancelLookup(); };
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                if (terracottaPage || TerracottaGuest::state().stage != TerracottaGuest::Stage::Idle) {
                    waiting = false;
                    cancelLookup();
                    refreshTerracotta();
                    return;
                }
                bool available = Multiplayer::available();
                if (!available || (Multiplayer::state().role == Role::None && shownShape > 0)) waiting = false;
                if (available) {
                    Multiplayer::warmUp();
                    if (Multiplayer::state().role == Role::None || Multiplayer::state().stage == Stage::Failed)
                        Multiplayer::watchService();
                    if (Multiplayer::state().role == Role::None && waiting) {
                        Multiplayer::watchWorlds();
                    }
                }
                if (Multiplayer::state().role == Role::None) {
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
                    tcShape = -1;
                    tcErrorHeight = -1;
                    tcNoteHeight = -1;
                    tcFooterBuilt = false;
                    clearTerracottaFeedback();
                    for (uint32_t id : {TC_JOIN, TC_LEAVE, TC_RETRY, TC_BACK, TC_FEEDBACK})
                        FindControl(id)->SetVisible(false);
                    showSection(inRoom ? 1 : 0);
                    layoutRoom();
                    FindControl(LEAVE)->SetVisible(inRoom && !failed);
                    FindControl(RETRY)->SetVisible(failed);
                    FindControl(BACK)->SetVisible(failed);
                    std::vector<exui::Layout> footer{exui::Layout::Row({})};
                    if (failed) { footer.push_back(cell(BACK, 80)); footer.push_back(cell(RETRY, 80)); }
                    else if (inRoom) footer.push_back(cell(LEAVE, 100));
                    setFooterActions(std::move(footer));
                }
                revealFocus(inRoom ? *room : *home);
            }

            void OnSelection(uint32_t id, int index) override {
                if (id == WORLDS || id == MEMBERS) refresh();
                else SectionedForm::OnSelection(id, index);
            }

            void OnAction(uint32_t id) override {
                // A modeless window may be closed and reopened while the ban confirmation is open.
                auto lifetime = weak_from_this().lock();
                if (id < FIRST_CONTROL) { SectionedForm::OnAction(id); return; }
                const auto& state = Multiplayer::state();
                if (id >= TC_OPEN) {
                    bool focusCode = false;
                    switch (id) {
                        case TC_OPEN:
                            if (!Multiplayer::active() && state.role == Role::None) {
                                terracottaPage = true;
                                focusCode = !TerracottaGuest::active();
                            }
                            break;
                        case TC_PASTE:
                            if (!TerracottaGuest::active()) {
                                std::wstring clip = readTrimmedClipboardLine(GetHwnd());
                                if (clip.empty()) {
                                    setTerracottaFeedback(L"剪贴板为空", false, 2200);
                                } else {
                                    if (Text(TC_CODE) != clip) {
                                        Control<exui::EditBox>(TC_CODE).SetText(clip, true);
                                    }
                                    if (TerracottaGuest::state().stage == TerracottaGuest::Stage::Failed &&
                                        Platform::narrow(clip) != TerracottaGuest::state().code) {
                                        TerracottaGuest::dismiss();
                                    }
                                    setTerracottaFeedback(L"已粘贴房间码", true, 1800);
                                }
                                focusCode = true;
                            }
                            break;
                        case TC_COPY_CODE:
                            if (std::string code = Platform::narrow(Text(TC_CODE)); !code.empty()) {
                                if (Platform::setClipboardText(code)) setTerracottaFeedback(L"已复制房间码", true, 2200);
                                else setTerracottaFeedback(L"复制房间码失败", false, 2200);
                            }
                            break;
                        case TC_COPY_ERROR:
                            if (TerracottaGuest::state().stage == TerracottaGuest::Stage::Failed) {
                                const auto& tcState = TerracottaGuest::state();
                                std::string copyText = tcState.error;
                                if (!tcState.detail.empty()) {
                                    if (!copyText.empty()) copyText += "\r\n";
                                    copyText += tcState.detail;
                                }
                                if (!copyText.empty()) {
                                    if (Platform::setClipboardText(copyText)) {
                                        setTerracottaFeedback(L"已复制错误信息", true, 2500);
                                    } else {
                                        setTerracottaFeedback(L"复制错误信息失败", false, 2500);
                                    }
                                }
                            }
                            break;
                        case TC_JOIN:
                        case TC_RETRY:
                            if (!Multiplayer::active() && state.role == Role::None && !TerracottaGuest::active() &&
                                    !trimmed(Text(TC_CODE)).empty()) {
                                clearTerracottaFeedback();
                                TerracottaGuest::join(trimmed(Text(TC_CODE)));
                            }
                            break;
                        case TC_LEAVE:
                            if (TerracottaGuest::active() &&
                                TerracottaGuest::state().stage != TerracottaGuest::Stage::Closing) {
                                clearTerracottaFeedback();
                                TerracottaGuest::leave();
                            }
                            break;
                        case TC_BACK:
                            if (!TerracottaGuest::active()) {
                                clearTerracottaFeedback();
                                TerracottaGuest::dismiss();
                                terracottaPage = false;
                                shownShape = -1;
                                tcShape = -1;
                                tcErrorHeight = -1;
                                tcNoteHeight = -1;
                                tcFooterBuilt = false;
                            }
                            break;
                        case TC_COPY:
                            if (std::string address = Platform::narrow(Text(TC_ADDRESS)); !address.empty()) {
                                if (Platform::setClipboardText(address)) {
                                    setTerracottaFeedback(L"已复制连接地址", true, 2200);
                                } else {
                                    setTerracottaFeedback(L"复制连接地址失败", false, 2200);
                                }
                            }
                            break;
                        case TC_PLAY:
                            if (TerracottaGuest::state().stage == TerracottaGuest::Stage::Ready &&
                                !TerracottaGuest::state().address.empty()) {
                                const auto* entry = Instances::find(Instances::selected());
                                if (entry && Game::state(entry->key) == Game::State::Idle)
                                    Game::launch(entry->key, false, {}, TerracottaGuest::state().address);
                            }
                            break;
                    }
                    if (GetHwnd()) refresh();
                    if (focusCode && GetHwnd() && !TerracottaGuest::active()) {
                        if (auto* codeCtrl = FindControl(TC_CODE)) {
                            if (codeCtrl->IsVisible()) SetFocusedWidget(codeCtrl);
                        }
                    }
                    return;
                }
                if (TerracottaGuest::active()) return;
                switch (id) {
                    case HOST:
                        if (state.role == Role::None && Multiplayer::available() && Multiplayer::canHost() && pendingCode.empty()) {
                            if (!waiting) {
                                waiting = true;
                                firstScan = Multiplayer::worldsSerial();
                                Control<exui::ListView>(WORLDS).SetSelectedRow(-1);
                            } else {
                                startHosting(selectedWorld());
                            }
                        }
                        break;
                    case CANCEL_HOST: waiting = false; break;
                    case JOIN:
                        if (state.role == Role::None && Multiplayer::available() && !waiting) {
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
                    case KICK: manageMember(false); break;
                    case BAN: manageMember(true); break;
                }
                if (GetHwnd()) refresh();
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                auto lifetime = weak_from_this().lock();
                if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { CloseWindow(); return 0; }
                if (msg == WM_KEYDOWN && wp == VK_RETURN && Multiplayer::state().role == Role::None) {
                    auto* focus = GetFocusedWidget();
                    if (focus == FindControl(TC_CODE) && !TerracottaGuest::active()) {
                        Click(TerracottaGuest::state().stage == TerracottaGuest::Stage::Failed ? TC_RETRY : TC_JOIN);
                        return 0;
                    }
                    if (focus == FindControl(JOIN_CODE)) { Click(JOIN); return 0; }
                    if (focus == FindControl(WORLDS)) { Click(HOST); return 0; }
                }
                return SectionedForm::HandleMessage(msg, wp, lp);
            }

        private:
            exui::ScrollPanel *home = nullptr, *room = nullptr, *terracotta = nullptr;
            int shownShape = -1, homeShape = -1, firstScan = 0;
            bool waiting = false;
            Rows shownWorlds;
            std::vector<std::string> worldKeys;
            std::string pendingCode;
            Rows shownMembers;
            std::vector<std::string> memberKeys;
            bool managing = false;
            bool terracottaPage = false;
            int tcShape = -1;
            int tcErrorHeight = -1;
            int tcNoteHeight = -1;
            bool tcWasActive = false;
            int tcStyledGameState = -1;
            bool tcFooterBuilt = false;
            bool tcFooterIdle = false;
            bool tcFooterFailed = false;
            bool tcFooterHasFeedback = false;
            bool tcFooterPositive = true;
            std::wstring tcFooterFeedback;
            std::wstring tcFeedbackText;
            bool tcFeedbackPositive = true;
            ULONGLONG tcFeedbackUntil = 0;
            Rows shownTerracottaMembers;

            void setTerracottaFeedback(std::wstring text, bool positive = true, ULONGLONG durationMs = 2400) {
                tcFeedbackText = std::move(text);
                tcFeedbackPositive = positive;
                tcFeedbackUntil = GetTickCount64() + durationMs;
            }

            void clearTerracottaFeedback() {
                tcFeedbackText.clear();
                tcFeedbackUntil = 0;
                if (auto* ctrl = FindControl(TC_FEEDBACK)) ctrl->SetVisible(false);
            }

            void layoutTerracottaFooter(bool isIdle, bool isFailed, bool showFeedback) {
                if (tcFooterBuilt && tcFooterIdle == isIdle && tcFooterFailed == isFailed &&
                    tcFooterHasFeedback == showFeedback &&
                    (!showFeedback ||
                     (tcFooterFeedback == tcFeedbackText && tcFooterPositive == tcFeedbackPositive))) {
                    return;
                }
                tcFooterBuilt = true;
                tcFooterIdle = isIdle;
                tcFooterFailed = isFailed;
                tcFooterHasFeedback = showFeedback;
                tcFooterFeedback = showFeedback ? tcFeedbackText : std::wstring();
                tcFooterPositive = tcFeedbackPositive;

                FindControl(TC_FEEDBACK)->SetVisible(showFeedback);
                std::vector<exui::Layout> footer;
                if (isIdle || isFailed) {
                    footer.push_back(cell(TC_BACK, 80));
                }
                if (showFeedback) {
                    auto& label = Control<PillLabel>(TC_FEEDBACK);
                    label.SetTextColor(tcFeedbackPositive ? 0xD21F7A47 : 0xD29A5B00);
                    label.SetBackgroundColor(tcFeedbackPositive ? 0xEAF2FAF5 : 0xEAFFF6E8);
                    label.SetPillBorder(tcFeedbackPositive ? 0x801F7A47 : 0x80C88220);
                    UpdateText(TC_FEEDBACK, tcFeedbackText);
                    int width = std::clamp(
                            static_cast<int>(std::ceil(exui::widget_detail::UnboundedTextWidth(label))) + 24, 96, 320);
                    footer.push_back(cell(TC_FEEDBACK, width, 24));
                }
                footer.push_back(exui::Layout::Row({}));
                if (isIdle || isFailed) {
                    footer.push_back(isFailed ? cell(TC_RETRY, 85) : cell(TC_JOIN, 100));
                } else {
                    footer.push_back(cell(TC_LEAVE, 100));
                }
                setFooterActions(std::move(footer));
            }

            void layoutTerracotta(int shape, int errorHeight, int noteHeight) {
                if (shape == tcShape && (shape != 1 || errorHeight == tcErrorHeight) && noteHeight == tcNoteHeight &&
                    shownShape == 10) {
                    return;
                }
                tcShape = shape;
                tcErrorHeight = errorHeight;
                tcNoteHeight = noteHeight;
                bool isIdle = shape == 0;
                bool isFailed = shape == 1;
                bool isWorking = shape == 2;
                bool isReady = shape == 3 || shape == 4;
                bool gameBusy = shape == 4;
                bool isActive = isWorking || isReady;

                FindControl(TC_NOTE)->SetVisible(isIdle || isFailed || isWorking || isReady);
                FindControl(TC_PASTE)->SetVisible(isIdle || isFailed);
                FindControl(TC_COPY_CODE)->SetVisible(isActive);
                FindControl(TC_STATUS_LABEL)->SetVisible(isActive);
                FindControl(TC_STATUS)->SetVisible(isActive);
                FindControl(TC_ADDRESS_LABEL)->SetVisible(isReady);
                FindControl(TC_ADDRESS)->SetVisible(isReady);
                FindControl(TC_COPY)->SetVisible(isReady);
                FindControl(TC_GAME_LABEL)->SetVisible(isReady);
                FindControl(TC_GAME)->SetVisible(isReady);
                FindControl(TC_GAME_STATE)->SetVisible(gameBusy);
                FindControl(TC_PLAY)->SetVisible(isReady);
                FindControl(TC_ERROR_GROUP)->SetVisible(isFailed);
                FindControl(TC_ERROR)->SetVisible(isFailed);
                FindControl(TC_COPY_ERROR)->SetVisible(isFailed);
                FindControl(TC_MEMBERS_GROUP)->SetVisible(isActive);
                FindControl(TC_MEMBERS)->SetVisible(isActive);

                FindControl(TC_BACK)->SetVisible(isIdle || isFailed);
                FindControl(TC_JOIN)->SetVisible(isIdle);
                FindControl(TC_RETRY)->SetVisible(isFailed);
                FindControl(TC_LEAVE)->SetVisible(isActive);

                using exui::Layout;
                std::vector<Layout> rows;
                if (isIdle || isFailed) {
                    rows.push_back(cell(TC_NOTE, 22));
                    rows.push_back(row(TC_CODE_LABEL, {cell(TC_CODE), cell(TC_PASTE, 75)}, 30, 85));
                } else if (isWorking) {
                    rows.push_back(row(TC_CODE_LABEL, {cell(TC_CODE), cell(TC_COPY_CODE, 100)}, 30, 85));
                    rows.push_back(row(TC_STATUS_LABEL, {cell(TC_STATUS)}, 26, 85));
                    rows.push_back(cell(TC_NOTE, noteHeight));
                } else {
                    rows.push_back(row(TC_CODE_LABEL, {cell(TC_CODE), cell(TC_COPY_CODE, 100)}, 30, 85));
                    rows.push_back(row(TC_STATUS_LABEL, {cell(TC_STATUS)}, 26, 85));
                    rows.push_back(row(TC_ADDRESS_LABEL, {cell(TC_ADDRESS), cell(TC_COPY, 100)}, 30, 85));
                    std::vector<Layout> gameCells{cell(TC_GAME)};
                    if (gameBusy) gameCells.push_back(cell(TC_GAME_STATE, 62, 22));
                    gameCells.push_back(cell(TC_PLAY, 105));
                    rows.push_back(row(TC_GAME_LABEL, std::move(gameCells), 30, 85));
                    rows.push_back(cell(TC_NOTE, noteHeight));
                }

                constexpr int BLOCK_GAP = 12;
                Layout topGroup = group(TC_GROUP, std::move(rows));
                std::vector<Layout> blocks{std::move(topGroup)};
                if (isFailed) {
                    blocks.push_back(group(
                            TC_ERROR_GROUP,
                            {
                                    cell(TC_ERROR, errorHeight),
                                    Layout::Row({Layout::Row({}), cell(TC_COPY_ERROR, 112)}, 0, 28),
                            },
                            {20, 25, 20, 14}, 9));
                } else if (isActive) {
                    std::vector<Layout> members{cell(TC_MEMBERS)};
                    int panelHeight = terracotta->GetHeight() - PAGE_PADDING.top - PAGE_PADDING.bottom;
                    int usedHeight = blocks[0].Measure().Height + BLOCK_GAP +
                            group(TC_MEMBERS_GROUP, members).Measure().Height;
                    members.front() = cell(TC_MEMBERS, std::max(140, panelHeight - usedHeight));
                    blocks.push_back(group(TC_MEMBERS_GROUP, std::move(members)));
                }
                terracotta->SetContentLayout(Layout::Column(std::move(blocks), BLOCK_GAP), PAGE_PADDING);
            }

            void refreshTerracotta() {
                using TCStage = TerracottaGuest::Stage;
                const auto& state = TerracottaGuest::state();
                bool active = TerracottaGuest::active();
                bool failed = state.stage == TCStage::Failed;
                bool ready = state.stage == TCStage::Ready;
                bool closing = state.stage == TCStage::Closing;
                if (tcWasActive && !active) {
                    clearTerracottaFeedback();
                }
                tcWasActive = active;

                const auto* entry = Instances::find(Instances::selected());
                Game::State gameState = entry ? Game::state(entry->key) : Game::State::Idle;
                bool gameBusy = ready && entry && gameState != Game::State::Idle;
                int shape = !active ? (failed ? 1 : 0) : (!ready ? 2 : (gameBusy ? 4 : 3));

                std::wstring note;
                if (ready) {
                    if (entry && gameState != Game::State::Idle) {
                        note = gameState == Game::State::Preparing
                                ? L"当前实例正在准备启动；若在加入房间前已发起启动，需重启游戏以加载兼容补丁。"
                                : L"当前实例已在运行：请在游戏「多人游戏」选陶瓦入口或直连上方地址；若在加入前已启动，需重启游戏。";
                    } else {
                        note = L"在游戏「多人游戏」选择陶瓦局域网入口，或直接连接上方地址；已运行的游戏需重启生效。";
                    }
                } else if (closing) {
                    note = L"正在断开联机通道并关闭本地联机组件，关闭完成前请勿强制退出启动器。";
                } else if (active) {
                    note = state.detail.empty() ? L"正在建立联机通道" : Platform::widen(state.detail);
                } else {
                    note = L"粘贴好友从 HMCL 或陶瓦联机分享的完整房间码。";
                }

                int panelW = terracotta->GetWidth() > 0 ? terracotta->GetWidth() : 730;
                int groupInnerW = std::max<int>(
                        240, panelW - 12 - static_cast<int>(PAGE_PADDING.left + PAGE_PADDING.right) - 40);
                int noteHeight = active ? measureLabelHeight(Control<exui::Label>(TC_NOTE), note, groupInnerW, 32) : 22;

                bool errMultiline = false;
                int errHeight = 32;
                std::wstring errDisplay;
                if (failed) {
                    std::wstring rawError =
                            Platform::widen(state.error + (!state.detail.empty() ? "\r\n" + state.detail : ""));
                    prepareTerracottaError(Control<exui::EditBox>(TC_ERROR), rawError, groupInnerW, errMultiline,
                                           errHeight, errDisplay);
                }

                if (shownShape != 10) {
                    shownShape = 10;
                    tcShape = -1;
                    tcErrorHeight = -1;
                    tcNoteHeight = -1;
                    tcFooterBuilt = false;
                    showSection(2);
                    for (uint32_t id : {LEAVE, RETRY, BACK}) FindControl(id)->SetVisible(false);
                }
                layoutTerracotta(shape, errHeight, noteHeight);

                bool showFeedback = !tcFeedbackText.empty() && GetTickCount64() < tcFeedbackUntil;
                if (!showFeedback && !tcFeedbackText.empty()) {
                    tcFeedbackText.clear();
                    tcFeedbackUntil = 0;
                }
                layoutTerracottaFooter(shape == 0, shape == 1, showFeedback);

                UpdateText(TC_GROUP, active ? L"HMCL / 陶瓦房间" : L"加入 HMCL / 陶瓦房间");
                if (active && !state.code.empty()) {
                    UpdateText(TC_CODE, Platform::widen(state.code));
                }
                auto& codeEdit = Control<exui::EditBox>(TC_CODE);
                bool becameReadOnly = active && !codeEdit.IsReadOnly();
                codeEdit.SetReadOnly(active);
                if (becameReadOnly && GetFocusedWidget() == &codeEdit) {
                    SetFocusedWidget(nullptr);
                }

                std::string status = ready ? "联机通道已就绪，请在游戏内加入" : TerracottaGuest::stageText();
                auto& statusLabel = Control<exui::Label>(TC_STATUS);
                Gdiplus::ARGB statusColor = ready ? 0xD21F7A47 : closing ? 0xD2666666 : 0xD2000000;
                if (statusLabel.GetTextColor() != statusColor) {
                    statusLabel.SetTextColor(statusColor);
                }
                UpdateText(TC_STATUS, Platform::widen(status));
                FindControl(TC_STATUS)->SetTooltipText(Platform::widen(
                        !state.detail.empty() && !failed ? status + " · " + state.detail : status));

                UpdateText(TC_NOTE, note);
                FindControl(TC_NOTE)->SetTooltipText(note);
                UpdateText(TC_ADDRESS, Platform::widen(state.address));

                auto& errorBox = Control<exui::EditBox>(TC_ERROR);
                if (errorBox.IsMultiline() != errMultiline) {
                    errorBox.SetMultiline(errMultiline);
                }
                UpdateText(TC_ERROR, errDisplay);

                std::wstring playTip;
                if (!entry) {
                    UpdateText(TC_GAME, L"请在主窗口选择实例");
                    FindControl(TC_GAME)->SetTooltipText(L"请先在主窗口选择或创建一个游戏实例");
                    UpdateText(TC_PLAY, L"启动并加入");
                    playTip = L"请先在主窗口选择或创建一个游戏实例";
                    tcStyledGameState = -1;
                } else {
                    std::wstring instanceName = Platform::widen(entry->info.id);
                    UpdateText(TC_GAME, instanceName);
                    std::wstring gameTip = instanceName;
                    if (gameState == Game::State::Preparing) {
                        gameTip += L"（准备中）\n实例正在准备启动；若在加入房间前已发起启动，请重启游戏以加载兼容补丁";
                        playTip = L"当前实例正在准备启动；若在加入房间前已发起启动，请重启游戏以加载兼容补丁";
                        UpdateText(TC_PLAY, L"准备中…");
                        if (tcStyledGameState != 1) {
                            tcStyledGameState = 1;
                            auto& stateTag = Control<PillLabel>(TC_GAME_STATE);
                            stateTag.SetTextColor(0xD29A5B00);
                            stateTag.SetBackgroundColor(0xFFFFF5E6);
                            stateTag.SetPillBorder(0x68C88220);
                        }
                        UpdateText(TC_GAME_STATE, L"准备中");
                    } else if (gameState == Game::State::Running) {
                        gameTip += L"（运行中）\n游戏已在运行；若在加入房间前已启动，请重启游戏以加载兼容补丁";
                        playTip = L"游戏已在运行；若在加入房间前已启动，请重启游戏以加载兼容补丁";
                        UpdateText(TC_PLAY, L"运行中");
                        if (tcStyledGameState != 2) {
                            tcStyledGameState = 2;
                            auto& stateTag = Control<PillLabel>(TC_GAME_STATE);
                            stateTag.SetTextColor(0xD24A5568);
                            stateTag.SetBackgroundColor(0xFFEDF1F5);
                            stateTag.SetPillBorder(0x608090A0);
                        }
                        UpdateText(TC_GAME_STATE, L"运行中");
                    } else {
                        gameTip += L"\n点击右侧“启动并加入”会自动启动该实例并连接房间";
                        playTip = state.address.empty() ? L"正在等待联机通道地址就绪"
                                                        : L"启动当前选中的实例并自动连接房间";
                        UpdateText(TC_PLAY, L"启动并加入");
                        tcStyledGameState = 0;
                    }
                    FindControl(TC_GAME)->SetTooltipText(gameTip);
                    FindControl(TC_GAME_STATE)->SetTooltipText(gameTip);
                }
                FindControl(TC_PLAY)->SetTooltipText(playTip);
                FindControl(TC_PLAY)->SetEnabled(ready && !state.address.empty() && entry &&
                        gameState == Game::State::Idle);

                FindControl(TC_COPY_CODE)->SetEnabled(active && !state.code.empty());
                FindControl(TC_COPY)->SetEnabled(ready && !state.address.empty());
                FindControl(TC_COPY_ERROR)->SetEnabled(failed && (!state.error.empty() || !state.detail.empty()));
                bool hasCode = !trimmed(Text(TC_CODE)).empty();
                bool sameFailedCode = trimmed(Text(TC_CODE)) == state.code;
                UpdateText(TC_RETRY, sameFailedCode ? L"重试" : L"加入房间");
                FindControl(TC_JOIN)->SetEnabled(!active && !failed && hasCode && !Multiplayer::active());
                FindControl(TC_RETRY)->SetEnabled(failed && hasCode && !Multiplayer::active());
                FindControl(TC_LEAVE)->SetEnabled(active && !closing);
                UpdateText(TC_LEAVE, closing ? L"正在退出…" : L"退出房间");

                Rows rows;
                for (const auto& member : state.members) {
                    std::wstring name = trimWide(Platform::widen(member.name));
                    if (name.empty()) name = L"未知成员";
                    std::wstring kind = member.kind == "HOST" ? L"房主" : member.kind == "LOCAL" ? L"你" : L"成员";
                    rows.push_back({std::move(name), std::move(kind)});
                }
                if (rows.empty()) rows.push_back({closing ? L"正在退出房间…" : ready ? L"暂无其他成员" : L"正在同步联机成员…", L""});
                fill(Control<exui::ListView>(TC_MEMBERS), shownTerracottaMembers, std::move(rows));
                UpdateText(TC_MEMBERS_GROUP, L"联机成员（" + std::to_wstring(state.members.size()) + L"）");
                revealFocus(*terracotta);
            }

            void cancelLookup() {
                if (pendingCode.empty()) return;
                pendingCode.clear();
                Multiplayer::forgetLookup();
            }

            void startHosting(int port) {
                if (TerracottaGuest::active() || !waiting || port <= 0 || Multiplayer::state().role != Role::None ||
                    !Multiplayer::available() || !Multiplayer::canHost()) return;
                waiting = false;
                Multiplayer::pick(port);
                Multiplayer::type(0);
                Multiplayer::host();
            }

            int selectedWorld() const {
                if (!waiting || Multiplayer::worldsSerial() == firstScan) return 0;
                std::string key = selectedKey(Control<exui::ListView>(WORLDS), worldKeys);
                for (const auto& world : Multiplayer::worlds())
                    if (key == std::to_string(world.port)) return world.port;
                return 0;
            }

            void layoutHome(bool multiple) {
                int shape = !waiting ? 0 : multiple ? 2 : 1;
                if (shape == homeShape) return;
                homeShape = shape;
                FindControl(HOST_NOTE)->SetVisible(waiting);
                FindControl(WORLDS)->SetVisible(waiting && multiple);
                FindControl(HOST)->SetVisible(!waiting || multiple);
                FindControl(CANCEL_HOST)->SetVisible(waiting);
                for (uint32_t id : {JOIN_GROUP, JOIN_NOTE, JOIN_LABEL, JOIN_CODE, JOIN_STATUS, JOIN})
                    FindControl(id)->SetVisible(!waiting);
                using exui::Layout;
                std::vector<Layout> host;
                if (waiting) host.push_back(cell(HOST_NOTE, 24));
                if (multiple) host.push_back(cell(WORLDS, 130));
                std::vector<Layout> actions{Layout::Row({})};
                if (!waiting || multiple) actions.push_back(cell(HOST, 120));
                if (waiting) actions.push_back(cell(CANCEL_HOST, 80));
                host.push_back(Layout::Row(std::move(actions), 10, 30));
                std::vector<Layout> groups{cell(SERVICE, 24), group(HOST_GROUP, std::move(host))};
                if (!waiting) groups.push_back(group(JOIN_GROUP, {cell(JOIN_NOTE, 24),
                        row(JOIN_LABEL, {cell(JOIN_CODE, 130), cell(JOIN_STATUS), cell(JOIN, 100)}, 30, 85)}));
                for (uint32_t id : {TC_HOME_GROUP, TC_HOME_NOTE, TC_OPEN})
                    FindControl(id)->SetVisible(!waiting);
                FindControl(TC_OPEN)->SetEnabled(!Multiplayer::active() && !TerracottaGuest::active());
                if (!waiting) {
                    groups.push_back(
                            group(TC_HOME_GROUP, {Layout::Row({cell(TC_HOME_NOTE), cell(TC_OPEN, 165)}, 10, 30)}));
                }
                home->SetContentLayout(Layout::Column(std::move(groups), GROUP_GAP), PAGE_PADDING);
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
                bool fresh = waiting && Multiplayer::worldsSerial() != firstScan;
                if (available && fresh) {
                    if (Multiplayer::worlds().size() == 1 && Multiplayer::canHost()) {
                        startHosting(Multiplayer::worlds().front().port);
                        return;
                    }
                    for (const auto& world : Multiplayer::worlds()) {
                        rows.push_back({Platform::widen(world.name.empty() ? "局域网世界" : world.name),
                                        Platform::widen(world.version), std::to_wstring(world.port)});
                        keys.push_back(std::to_string(world.port));
                    }
                }
                bool multiple = keys.size() > 1;
                auto& worlds = Control<exui::ListView>(WORLDS);
                fill(worlds, shownWorlds, worldKeys, std::move(rows), std::move(keys));
                FindControl(JOIN_CODE)->SetEnabled(available && !waiting);
                FindControl(WORLDS)->SetEnabled(available);
                FindControl(HOST)->SetEnabled(available && Multiplayer::canHost() && pendingCode.empty() &&
                        (!waiting || selectedWorld() > 0));
                UpdateText(HOST, waiting ? L"分享选中世界" : L"创建房间");
                UpdateText(HOST_NOTE, multiple ? L"检测到多个世界，请选择要分享的世界。" : L"请在游戏的单人世界里对局域网开放。");
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
                FindControl(JOIN)->SetEnabled(available && !waiting && pendingCode.empty() && RoomApi::validCode(trimmed(Text(JOIN_CODE))));
                FindControl(TC_OPEN)->SetEnabled(!Multiplayer::active() && !TerracottaGuest::active());
                UpdateText(JOIN, pendingCode.empty() ? L"加入房间" : L"正在查找…");
                layoutHome(multiple);
            }

            void layoutRoom() {
                using exui::Layout;
                bool host = Multiplayer::state().role == Role::Host;
                bool failed = Multiplayer::state().stage == Stage::Failed;
                for (uint32_t id : {TARGET_LABEL, TARGET}) FindControl(id)->SetVisible(host);
                for (uint32_t id : {ADDRESS_LABEL, ADDRESS, COPY_ADDRESS, GAME_LABEL, GAME, PLAY}) FindControl(id)->SetVisible(!host);
                FindControl(ERROR_GROUP)->SetVisible(failed);
                FindControl(ERROR_TEXT)->SetVisible(failed);
                for (uint32_t id : {MEMBERS_GROUP, MEMBERS}) FindControl(id)->SetVisible(!failed);
                for (uint32_t id : {KICK, BAN}) FindControl(id)->SetVisible(host && !failed);
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
                else {
                    std::vector<Layout> members{cell(MEMBERS)};
                    if (host) members.push_back(Layout::Row({Layout::Row({}), cell(KICK, 100), cell(BAN, 80)}, 10, 30));
                    int roomHeight = room->GetHeight() - PAGE_PADDING.top - PAGE_PADDING.bottom;
                    int listHeight = roomHeight - groups.front().Measure().Height - GROUP_GAP -
                            group(MEMBERS_GROUP, members).Measure().Height;
                    members.front() = cell(MEMBERS, std::max(150, listHeight));
                    groups.push_back(group(MEMBERS_GROUP, std::move(members)));
                }
                room->SetContentLayout(Layout::Column(std::move(groups), GROUP_GAP), PAGE_PADDING);
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
                refreshMembers();
            }

            void refreshMembers() {
                const auto& state = Multiplayer::state();
                Rows rows;
                std::vector<std::string> keys;
                for (const auto& member : state.members) {
                    std::wstring role = member.host ? L"房主" : L"成员";
                    if (member.self) role += L" - 你";
                    rows.push_back({Platform::widen(member.name.empty() ? "设备 " + member.device : member.name),
                                    role, member.online ? L"在线" : L"离线", Platform::widen(member.address)});
                    keys.push_back(member.device);
                }
                if (rows.empty()) { rows.push_back({L"还没有成员", L"", L"", L""}); keys.emplace_back(); }
                fill(Control<exui::ListView>(MEMBERS), shownMembers, memberKeys, std::move(rows), std::move(keys));
                UpdateText(MEMBERS_GROUP, L"成员（" + std::to_wstring(state.members.size()) + L"）");
                bool enabled = manageable(selectedKey(Control<exui::ListView>(MEMBERS), memberKeys)) && !managing;
                FindControl(KICK)->SetEnabled(enabled);
                FindControl(BAN)->SetEnabled(enabled);
            }

            bool manageable(const std::string& device) const {
                const auto& state = Multiplayer::state();
                return state.role == Role::Host && state.stage == Stage::Ready && !device.empty() &&
                        std::any_of(state.members.begin(), state.members.end(), [&](const auto& member) {
                            return member.device == device && !member.self && !member.host;
                        });
            }

            void manageMember(bool ban) {
                std::string device = selectedKey(Control<exui::ListView>(MEMBERS), memberKeys);
                if (managing || !manageable(device)) return;
                std::string code = Multiplayer::state().code;
                if (ban) {
                    auto member = std::find_if(Multiplayer::state().members.begin(), Multiplayer::state().members.end(),
                                              [&](const auto& value) { return value.device == device; });
                    std::wstring name = Platform::widen(member->name.empty() ? member->device : member->name);
                    managing = true;
                    refreshMembers();
                    int answer = message(GetHwnd(), L"要在当前房间封禁 “" + name + L"” 吗？\n被封禁的设备将无法再次加入。",
                                         L"封禁成员", exui::MsgBoxButtons::OkCancel, {L"取消", L"封禁"});
                    managing = false;
                    if (answer != 0) return;
                }
                if (GetHwnd() && Multiplayer::state().code == code && manageable(device)) Multiplayer::kick(device, ban);
            }

            void copy(uint32_t id) {
                auto& edit = Control<exui::EditBox>(id);
                edit.SelectAll();
                edit.CopyToClipboard();
            }
        };

        std::shared_ptr<MultiplayerWindow> multiplayerWindow;
    }

    void showMultiplayer(HWND parent) {
        if (!multiplayerWindow || !IsWindow(multiplayerWindow->GetHwnd())) {
            multiplayerWindow = std::make_shared<MultiplayerWindow>();
            if (!multiplayerWindow->create(parent)) { multiplayerWindow.reset(); return; }
        }
        multiplayerWindow->ShowWindowWithAnimation();
        SetForegroundWindow(multiplayerWindow->GetHwnd());
    }

    void closeMultiplayer() {
        auto closing = std::move(multiplayerWindow);
        if (closing) closing->CloseWindow(false);
    }
}
