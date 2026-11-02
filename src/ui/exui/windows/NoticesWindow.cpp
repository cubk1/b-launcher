#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/NoticesWindow.h"
#include "ui/exui/windows/NoticeHistory.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/ListView.h"

#include <algorithm>
#include <ctime>
#include <iterator>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        const wchar_t* statusOf(const NoticeHistory::Entry& entry) {
            if (entry.running) return L"进行中";
            if (entry.kind == Notices::Kind::Failed) return L"失败";
            return entry.kind == Notices::Kind::Done ? L"已完成" : L"提示";
        }

        std::wstring timeOf(std::time_t time) {
            std::tm local{};
            wchar_t text[16]{};
            if (localtime_s(&local, &time) == 0) std::wcsftime(text, std::size(text), L"%H:%M:%S", &local);
            return text;
        }

        class NoticesWindow : public LiveForm {
        public:
            enum : uint32_t { LIST = 1, CONTENT_LABEL, CONTENT, COUNT, COPY, CLOSE };

            NoticesWindow(NoticeHistory& history, std::string selected)
                : history(history), initial(std::move(selected)) {}

            bool create(HWND parent) {
                const exui::ControlSpec controls[] = {
                        {LIST, ControlKind::List, {18, 42, 604, 184}},
                        {CONTENT_LABEL, ControlKind::Label, {18, 236, 180, 22}, L"消息内容："},
                        {CONTENT, ControlKind::Edit, {18, 260, 604, 70}},
                        {COUNT, ControlKind::Label, {18, 351, 360, 25}},
                        {COPY, ControlKind::Button, {422, 350, 95, 30}, L"复制内容"},
                        {CLOSE, ControlKind::Button, {527, 350, 95, 30}, L"关闭"},
                };
                if (!createDialog(*this, parent, L"消息记录", 640, 390,
                                  exui::CaptionButtonMask::Title | exui::CaptionButtonMask::Close)) return false;
                AddControls(controls);
                auto& list = Control<exui::ListView>(LIST);
                list.SetSingleSelection(true);
                list.SetHeaderHeight(24);
                list.SetRowHeight(28);
                list.InsertColumn(L"状态", 78);
                list.InsertColumn(L"消息", 424);
                list.InsertColumn(L"时间", 88);
                auto& content = Control<exui::EditBox>(CONTENT);
                content.SetMultiline(true);
                content.SetReadOnly(true);
                FindControl(COUNT)->SetTooltipText(L"窗口打开期间，已有和新增的已完成消息都视为已读，关闭时清除。进行中的任务会保留。");
                refresh();
                startRefreshing();
                return true;
            }

        protected:
            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
                    CloseWindow();
                    return 0;
                }
                if (msg == WM_SHOWWINDOW && wp && loaded && !opened) {
                    opened = true;
                    LRESULT result = LiveForm::HandleMessage(msg, wp, lp);
                    history.markAllRead();
                    refresh();
                    return result;
                }
                if (msg == WM_DESTROY && opened) {
                    // Include results received since the last refresh, or
                    // during the closing animation.
                    history.markAllRead();
                    opened = false;
                }
                return LiveForm::HandleMessage(msg, wp, lp);
            }

            void refresh() override {
                if (loaded && revision == history.revision()) return;
                if (opened) history.markAllRead();
                auto& list = Control<exui::ListView>(LIST);
                int scroll = list.GetScrollPosition();
                int rowHeight = list.GetRowHeight();
                size_t top = (size_t) (scroll / rowHeight);
                std::string anchor = top < keys.size() ? keys[top] : std::string();
                Rows rows;
                std::vector<std::string> nextKeys;
                for (auto at = history.entries().rbegin(); at != history.entries().rend(); ++at) {
                    std::wstring line = at->text;
                    for (wchar_t& ch : line) if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                    rows.push_back({statusOf(*at), std::move(line), timeOf(at->time)});
                    nextKeys.push_back(at->id);
                }
                fill(list, shown, keys, std::move(rows), std::move(nextKeys));
                if (list.GetSelectedRow() < 0 && !keys.empty()) {
                    auto at = std::find(keys.begin(), keys.end(), initial);
                    list.SetSelectedRow(at == keys.end() ? 0 : (int) (at - keys.begin()));
                    list.EnsureRowVisible(list.GetSelectedRow());
                } else if (!anchor.empty()) {
                    auto at = std::find(keys.begin(), keys.end(), anchor);
                    list.ScrollTo(at == keys.end() ? scroll : (int) (at - keys.begin()) * rowHeight + scroll % rowHeight);
                }
                initial.clear();
                UpdateText(COUNT, L"共 " + std::to_wstring(keys.size()) + L" 条消息，" +
                                          std::to_wstring(history.runningCount()) + L" 项进行中");
                revision = history.revision();
                loaded = true;
                selection();
            }

            void OnSelection(uint32_t id, int) override {
                if (id == LIST) selection();
            }

            void OnAction(uint32_t id) override {
                if (id == CLOSE) {
                    CloseWindow();
                } else if (id == COPY) {
                    refresh();
                    auto& content = Control<exui::EditBox>(CONTENT);
                    auto selected = content.GetSelection();
                    size_t caret = content.GetCaretPosition();
                    size_t anchor = caret == selected.first ? selected.second : selected.first;
                    content.SelectAll();
                    content.CopyToClipboard();
                    content.SetSelection(anchor, caret);
                }
            }

        private:
            NoticeHistory& history;
            std::string initial;
            std::vector<std::string> keys;
            Rows shown;
            uint64_t revision = 0;
            bool loaded = false;
            bool opened = false;

            void selection() {
                std::string id = selectedKey(Control<exui::ListView>(LIST), keys);
                const auto* entry = history.find(id);
                UpdateText(CONTENT, exui::TextBuffer::Normalize(entry != nullptr ? entry->text : std::wstring(), true));
                FindControl(COPY)->SetEnabled(entry != nullptr && !entry->text.empty());
            }
        };
    }

    bool showNotices(HWND parent, NoticeHistory& history, const std::string& selected) {
        NoticesWindow window(history, selected);
        if (!window.create(parent)) return false;
        runModal(window, window.FindControl(NoticesWindow::LIST));
        return true;
    }
}
