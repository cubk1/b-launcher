#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/LogWindow.h"
#include "app/launch/GameLog.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ScrollBar.h"
#include "ui/exui/windows/Dialogs.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        constexpr int ROW_H = 18;
        // The lines' room inside the frame, their indent and the scroll bar's width.
        constexpr int INSET = 2, INDENT = 6, BAR_W = 10;
        // LiveForm's refresh timer is 1.
        constexpr UINT_PTR SEARCH_TIMER = 2;
        // The search waits for a pause in the typing.
        constexpr UINT SEARCH_DELAY_MS = 200;
        constexpr ULONGLONG FLASH_MS = 2500;

        // The default interface's colors, darkened for the light backing.
        Gdiplus::ARGB levelColor(GameLog::Level level) {
            switch (level) {
                case GameLog::Level::Fatal:
                case GameLog::Level::Error: return 0xD2C00000;
                case GameLog::Level::Warn: return 0xD2A05A00;
                case GameLog::Level::Debug: return 0x8C000000;
                case GameLog::Level::Launcher: return 0xD20060C0;
                default: return 0xD2000000;
            }
        }

        const GameLog::Line& lineAt(uint64_t number) {
            static const GameLog::Line gone;
            if (number < GameLog::first() || number >= GameLog::end()) {
                return gone;
            }
            return GameLog::lines()[(size_t) (number - GameLog::first())];
        }

        std::string lowered(std::string_view text) {
            std::string out(text);
            for (char& c : out) {
                c = (char) std::tolower((unsigned char) c);
            }
            return out;
        }

        // UTF-8 text in columns, as 宋体 draws it: CJK characters take two, the others one.
        size_t lengthOf(unsigned char lead) {
            return lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
        }

        int columnsBefore(std::string_view text, size_t bytes) {
            int column = 0;
            for (size_t i = 0; i < bytes && i < text.size();) {
                size_t length = lengthOf((unsigned char) text[i]);
                column += length >= 3 ? 2 : 1;
                i += length;
            }
            return column;
        }

        // The part of text from column skip on that fills columns; skipped is the column it starts at.
        std::string_view columnsOf(std::string_view text, int skip, int columns, int& skipped) {
            size_t i = 0;
            int column = 0;
            size_t start = std::string_view::npos;
            skipped = 0;
            while (i < text.size()) {
                size_t length = lengthOf((unsigned char) text[i]);
                if (start == std::string_view::npos && column >= skip) {
                    start = i;
                    skipped = column;
                }
                if (start != std::string_view::npos && column - skipped >= columns) {
                    return text.substr(start, i - start);
                }
                column += length >= 3 ? 2 : 1;
                i += length;
            }
            return start == std::string_view::npos ? std::string_view() : text.substr(start);
        }

        std::wstring withCommas(uint64_t value) {
            std::wstring digits = std::to_wstring(value);
            std::wstring out;
            for (size_t i = 0; i < digits.size(); i++) {
                if (i > 0 && (digits.size() - i) % 3 == 0) {
                    out += L',';
                }
                out += digits[i];
            }
            return out;
        }

        bool writeClipboard(HWND owner, const std::wstring& text) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
            if (memory == nullptr) {
                return false;
            }
            auto* data = static_cast<wchar_t*>(GlobalLock(memory));
            if (data == nullptr) {
                GlobalFree(memory);
                return false;
            }
            std::copy(text.begin(), text.end(), data);
            data[text.size()] = L'\0';
            GlobalUnlock(memory);
            bool done = false;
            if (OpenClipboard(owner)) {
                done = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                CloseClipboard();
            }
            if (!done) {
                GlobalFree(memory);
            }
            return done;
        }

        // GameLog's lines, each in the color of its level, as the default interface shows them: a click selects a
        // line, Shift or a drag a range, Shift and the wheel move sideways, and the list follows new lines while it
        // is scrolled to the bottom. Lines are known by their number in the log.
        class LogView : public exui::Widget {
        public:
            std::function<void()> OnCopy;

            LogView(int x, int y, int width, int height) {
                SetBounds(x, y, width, height);
                SetTabStop(true);
                bar = &Add<exui::ScrollBar>();
                bar->SetVisible(false);
                bar->SetLineStep(ROW_H);
                bar->OnScroll = [this](int position) {
                    scroll = position;
                    following = scroll >= maxScroll();
                    Invalidate();
                };
            }

            // Takes in the lines that arrived and lets go of those the log dropped.
            void update() {
                bool changed = false;
                if (GameLog::generation() != seenGeneration) {
                    seenGeneration = GameLog::generation();
                    matches.clear();
                    current = -1;
                    selection = false;
                    scroll = 0;
                    aside = 0;
                    following = true;
                    scanned = shownFirst = GameLog::first();
                    changed = true;
                }
                uint64_t first = GameLog::first();
                if (first > shownFirst) {
                    if (!following) {
                        scroll = (int) std::max<long long>(0, scroll - (long long) (first - shownFirst) * ROW_H);
                    }
                    shownFirst = first;
                    while (!matches.empty() && matches.front() < first) {
                        matches.pop_front();
                        current = std::max(-1, current - 1);
                    }
                    changed = true;
                }
                scanned = std::max(scanned, first);
                if (scanned < GameLog::end()) {
                    for (uint64_t number = scanned; number < GameLog::end(); number++) {
                        if (contains(lineAt(number).text)) {
                            matches.push_back(number);
                        }
                    }
                    scanned = GameLog::end();
                    changed = true;
                }
                if (changed) {
                    layout();
                    Invalidate();
                }
            }

            uint64_t count() const {
                return GameLog::end() - GameLog::first();
            }

            bool isFollowing() const {
                return following;
            }

            void follow() {
                following = true;
                layout();
                Invalidate();
            }

            // How many lines are selected, of those still in the log.
            uint64_t selected() const {
                uint64_t from, to;
                return range(from, to) && selection ? to - from + 1 : 0;
            }

            bool searching() const {
                return !query.empty();
            }

            size_t matchCount() const {
                return matches.size();
            }

            int currentMatch() const {
                return current;
            }

            void setQuery(const std::string& text) {
                std::string lower = lowered(text);
                if (lower == query) {
                    return;
                }
                query = lower;
                matches.clear();
                current = -1;
                if (!query.empty()) {
                    for (uint64_t number = GameLog::first(); number < GameLog::end(); number++) {
                        if (contains(lineAt(number).text)) {
                            matches.push_back(number);
                        }
                    }
                    step(1);
                }
                Invalidate();
            }

            // To the next or the previous match; the first step goes from the top of the view.
            void step(int direction) {
                if (matches.empty()) {
                    return;
                }
                if (current < 0) {
                    uint64_t top = GameLog::first() + (uint64_t) (scroll / ROW_H);
                    current = (int) (std::lower_bound(matches.begin(), matches.end(), top) - matches.begin());
                    if (direction < 0) {
                        current--;
                    }
                } else {
                    current += direction;
                }
                int total = (int) matches.size();
                current = ((current % total) + total) % total;
                reveal(matches[(size_t) current]);
            }

            // The selected lines, or all of them, one per line of text.
            std::wstring text(uint64_t& lines) const {
                uint64_t from, to;
                lines = 0;
                if (!range(from, to)) {
                    return {};
                }
                if (!selection) {
                    from = GameLog::first();
                    to = GameLog::end() - 1;
                }
                std::string text;
                for (uint64_t number = from; number <= to; number++) {
                    text += lineAt(number).text;
                    text += "\r\n";
                }
                lines = to - from + 1;
                return Platform::widen(text);
            }

            void OnPaint(Gdiplus::Graphics& g) override {
                using namespace Gdiplus;
                layout();
                SolidBrush backing(Color(0xD2FFFFFF));
                g.FillRectangle(&backing, 0, 0, GetWidth(), GetHeight());
                Pen border(Color(0x50000000));
                g.DrawRectangle(&border, 0, 0, GetWidth() - 1, GetHeight() - 1);

                const Rect view(INSET, INSET, viewWidth(), viewHeight());
                auto font = exui::widget_detail::MakeFont(*this, m_fontStyle);
                StringFormat format(StringFormat::GenericTypographic());
                format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
                format.SetLineAlignment(StringAlignmentCenter);
                g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                if (cell <= 0) {
                    RectF box;
                    g.MeasureString(L"0000000000", 10, font.get(), PointF(0, 0), &format, &box);
                    cell = std::max(1.0f, box.Width / 10);
                }
                const auto saved = g.Save();
                g.SetClip(view, CombineModeIntersect);
                if (count() == 0) {
                    StringFormat centered;
                    centered.SetAlignment(StringAlignmentCenter);
                    centered.SetLineAlignment(StringAlignmentCenter);
                    SolidBrush muted(Color(0x96000000));
                    g.DrawString(GameLog::any() ? L"没有日志" : L"启动游戏后，这里会显示它的日志", -1, font.get(),
                                 RectF((REAL) view.X, (REAL) view.Y, (REAL) view.Width, (REAL) view.Height), &centered,
                                 &muted);
                    g.Restore(saved);
                    return;
                }
                const int columns = (int) ((view.Width - INDENT) / cell) + 2;
                const float textX = (float) (view.X + INDENT);
                uint64_t low = 0, high = 0;
                bool selecting = selection && range(low, high);
                uint64_t matched = current >= 0 && current < (int) matches.size() ? matches[(size_t) current] : ~0ull;
                uint64_t row = (uint64_t) (scroll / ROW_H);
                for (int y = view.Y - scroll % ROW_H; row < count() && y < view.GetBottom(); row++, y += ROW_H) {
                    uint64_t number = GameLog::first() + row;
                    const GameLog::Line& entry = lineAt(number);
                    if (selecting && number >= low && number <= high) {
                        SolidBrush fill(Color(0x300060C0));
                        g.FillRectangle(&fill, view.X, y, view.Width, ROW_H);
                        SolidBrush edge(Color(0xB40060C0));
                        g.FillRectangle(&edge, view.X, y, 3, ROW_H);
                    }
                    int skipped = 0;
                    std::string_view shown = columnsOf(entry.text, aside, columns, skipped);
                    if (!query.empty() && contains(entry.text)) {
                        SolidBrush tint(Color(number == matched ? 0xA0FFB400 : 0x50FFB400));
                        std::string lower = lowered(entry.text);
                        for (size_t at = lower.find(query); at != std::string::npos;
                             at = lower.find(query, at + query.size())) {
                            int from = columnsBefore(entry.text, at) - skipped;
                            int to = columnsBefore(entry.text, at + query.size()) - skipped;
                            if (to <= 0 || from >= columns) {
                                continue;
                            }
                            float x0 = textX + std::max(0, from) * cell;
                            float x1 = textX + std::min(columns, to) * cell;
                            g.FillRectangle(&tint, x0 - 1, (REAL) y + 1, x1 - x0 + 2, (REAL) ROW_H - 2);
                        }
                    }
                    if (!shown.empty()) {
                        std::wstring wide = Platform::widen(std::string(shown));
                        SolidBrush ink(Color(levelColor(entry.level)));
                        g.DrawString(wide.c_str(), (INT) wide.size(), font.get(),
                                     RectF(textX, (REAL) y, (REAL) view.GetRight() - textX, (REAL) ROW_H), &format,
                                     &ink);
                    }
                }
                g.Restore(saved);
            }

            bool OnMouseDown(int, int y, UINT button) override {
                if (button != VK_LBUTTON || count() == 0) {
                    return true;
                }
                uint64_t number = numberAt(y);
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && selection) {
                    caret = number;
                } else {
                    anchor = caret = number;
                }
                selection = true;
                dragging = true;
                dragY = y;
                // Dragging past the top or the bottom scrolls.
                StartTimer(40);
                Invalidate();
                return true;
            }

            bool OnMouseMove(int, int y, UINT) override {
                if (dragging && count() > 0) {
                    dragY = y;
                    caret = numberAt(std::clamp(y, INSET, INSET + viewHeight() - 1));
                    Invalidate();
                }
                return true;
            }

            bool OnMouseUp(int, int, UINT button) override {
                if (button == VK_LBUTTON) {
                    dragging = false;
                    StopTimer();
                }
                return true;
            }

            void OnMouseCancel() override {
                dragging = false;
                StopTimer();
                Widget::OnMouseCancel();
            }

            void OnTimer() override {
                if (!dragging || count() == 0) {
                    StopTimer();
                    return;
                }
                if (dragY < INSET) {
                    scroll -= ROW_H;
                    following = false;
                } else if (dragY >= INSET + viewHeight()) {
                    scroll += ROW_H;
                } else {
                    return;
                }
                layout();
                caret = numberAt(std::clamp(dragY, INSET, INSET + viewHeight() - 1));
                Invalidate();
            }

            bool OnMouseWheel(int delta, int, int) override {
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    aside = std::max(0, aside - delta * 8 / WHEEL_DELTA);
                } else {
                    scroll -= delta * 3 * ROW_H / WHEEL_DELTA;
                    following = delta < 0 && scroll >= maxScroll();
                    layout();
                }
                Invalidate();
                return true;
            }

            bool OnKeyDown(UINT vk, UINT) override {
                bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                int page = std::max(ROW_H, viewHeight() - ROW_H);
                switch (vk) {
                    case 'A':
                        if (!control || count() == 0) {
                            return false;
                        }
                        anchor = GameLog::first();
                        caret = GameLog::end() - 1;
                        selection = true;
                        break;
                    case 'C':
                        if (!control) {
                            return false;
                        }
                        if (OnCopy) {
                            OnCopy();
                        }
                        return true;
                    case VK_ESCAPE:
                        if (!selection) {
                            return false;
                        }
                        selection = false;
                        break;
                    case VK_UP:
                    case VK_PRIOR:
                        scroll -= vk == VK_UP ? ROW_H : page;
                        following = false;
                        break;
                    case VK_DOWN:
                    case VK_NEXT:
                        scroll += vk == VK_DOWN ? ROW_H : page;
                        following = scroll >= maxScroll();
                        break;
                    case VK_HOME:
                        scroll = 0;
                        following = false;
                        break;
                    case VK_END: following = true; break;
                    default: return false;
                }
                layout();
                Invalidate();
                return true;
            }

        private:
            exui::ScrollBar* bar = nullptr;
            uint64_t seenGeneration = ~0ull;
            // The first line still in the log, and the end of the lines searched.
            uint64_t shownFirst = 0, scanned = 0;
            std::string query;
            std::deque<uint64_t> matches;
            int current = -1;
            bool selection = false, dragging = false;
            uint64_t anchor = 0, caret = 0;
            int dragY = 0;
            int scroll = 0;
            // Columns scrolled sideways.
            int aside = 0;
            bool following = true;
            // The width of a column in the font.
            float cell = 0;

            int viewWidth() const {
                return std::max(0, GetWidth() - 2 * INSET - (bar->IsVisible() ? BAR_W : 0));
            }

            int viewHeight() const {
                return std::max(ROW_H, GetHeight() - 2 * INSET);
            }

            int maxScroll() const {
                return (int) std::max<long long>(0, (long long) count() * ROW_H - viewHeight());
            }

            void layout() {
                int most = maxScroll();
                if (following) {
                    scroll = most;
                }
                scroll = std::clamp(scroll, 0, most);
                bar->SetVisible(most > 0);
                bar->SetBounds(GetWidth() - INSET - BAR_W, INSET, BAR_W, viewHeight());
                bar->SetRange(0, most, viewHeight());
                bar->SetPosition(scroll);
            }

            // The selection as it is in the log now, from its first line to its last.
            bool range(uint64_t& from, uint64_t& to) const {
                if (count() == 0) {
                    return false;
                }
                from = std::max(std::min(anchor, caret), GameLog::first());
                to = std::min(std::max(anchor, caret), GameLog::end() - 1);
                return from <= to;
            }

            uint64_t numberAt(int y) const {
                long long row = ((long long) y - INSET + scroll) / ROW_H;
                return GameLog::first() + (uint64_t) std::clamp<long long>(row, 0, (long long) count() - 1);
            }

            bool contains(const std::string& text) const {
                if (query.empty()) {
                    return false;
                }
                auto found = std::search(text.begin(), text.end(), query.begin(), query.end(),
                                         [](char a, char b) { return (char) std::tolower((unsigned char) a) == b; });
                return found != text.end();
            }

            void reveal(uint64_t number) {
                int y = (int) (number - GameLog::first()) * ROW_H;
                if (y < scroll || y + ROW_H > scroll + viewHeight()) {
                    scroll = std::max(0, y - viewHeight() * 2 / 5);
                }
                following = false;
                layout();
                Invalidate();
            }
        };

        // The default interface's log window in the size and look of the instance manager: the lines above, the
        // search and what to do with them below.
        class LogWindow : public LiveForm {
        public:
            enum : uint32_t {
                VIEW = 1,
                STATUS,
                BOTTOM,
                SEARCH_LABEL,
                SEARCH,
                PREVIOUS,
                NEXT,
                PAUSE,
                COPY,
                EXPORT,
                CLEAR
            };

            bool create() {
                static const exui::ControlSpec CONTROLS[] = {
                        {STATUS, ControlKind::Label, {20, 370, 598, 26}},
                        {.id = BOTTOM,
                         .kind = ControlKind::Button,
                         .bounds = {624, 369, 76, 28},
                         .caption = L"回到底部",
                         .visible = false},
                        {SEARCH_LABEL, ControlKind::Label, {18, 406, 32, 26}, L"搜索"},
                        {SEARCH, ControlKind::Edit, {52, 406, 146, 26}},
                        {PREVIOUS, ControlKind::Button, {204, 402, 70, 35}, L"上一个"},
                        {NEXT, ControlKind::Button, {280, 402, 70, 35}, L"下一个"},
                        {PAUSE, ControlKind::Button, {378, 402, 76, 35}, L"暂停"},
                        {COPY, ControlKind::Button, {460, 402, 76, 35}, L"复制"},
                        {EXPORT, ControlKind::Button, {542, 402, 76, 35}, L"导出"},
                        {CLEAR, ControlKind::Button, {624, 402, 76, 35}, L"清空"},
                };
                if (!createDialog(*this, nullptr, L"游戏日志", 720, 450,
                                  exui::CaptionButtonMask::Minimize | exui::CaptionButtonMask::Close)) {
                    return false;
                }
                SetIndependent(true);
                auto lines = std::make_shared<LogView>(18, 35, 682, 330);
                view = lines.get();
                view->OnCopy = [this] { copy(); };
                AddControl(VIEW, lines);
                AddControls(CONTROLS);
                Control<exui::Label>(STATUS).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::EditBox>(SEARCH).OnTextChanged = [this](const std::wstring&) {
                    SetTimer(GetHwnd(), SEARCH_TIMER, SEARCH_DELAY_MS, nullptr);
                };
                FindControl(SEARCH)->SetTooltipText(L"搜索日志（Ctrl+F）");
                FindControl(PREVIOUS)->SetTooltipText(L"上一个（Shift+Enter）");
                FindControl(NEXT)->SetTooltipText(L"下一个（Enter）");
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                view->update();
                std::wstring title =
                        GameLog::title().empty() ? L"游戏日志" : L"游戏日志 - " + Platform::widen(GameLog::title());
                if (GetTitle() != title) {
                    SetTitle(title);
                }

                uint64_t count = view->count();
                std::wstring status = withCommas(count) + L" 行";
                if (uint64_t selected = view->selected(); selected > 0) {
                    status += L"，已选 " + withCommas(selected) + L" 行";
                }
                if (GameLog::dropped() > 0) {
                    status += L"，已丢弃最早的 " + withCommas(GameLog::dropped()) + L" 行";
                }
                if (view->searching()) {
                    size_t matches = view->matchCount();
                    status += matches == 0 ? std::wstring(L"，没有找到")
                            : view->currentMatch() < 0
                            ? L"，找到 " + withCommas(matches) + L" 处"
                            : L"，第 " + withCommas(view->currentMatch() + 1) + L" / " + withCommas(matches) + L" 处";
                }
                if (GameLog::paused()) {
                    status += L"，已暂停，" + withCommas(GameLog::held()) + L" 行未显示";
                }
                if (GetTickCount64() < flashUntil) {
                    status = flash;
                }
                setText(STATUS, status);
                FindControl(BOTTOM)->SetVisible(!view->isFollowing() && count > 0);
                setText(PAUSE, GameLog::paused() ? L"继续" : L"暂停");
                FindControl(COPY)->SetTooltipText(view->selected() > 0 ? L"复制选中的行（Ctrl+C）" : L"复制全部");
                FindControl(COPY)->SetEnabled(count > 0);
                FindControl(EXPORT)->SetEnabled(count > 0);
                FindControl(PREVIOUS)->SetEnabled(view->matchCount() > 0);
                FindControl(NEXT)->SetEnabled(view->matchCount() > 0);
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_TIMER && wp == SEARCH_TIMER) {
                    search();
                    refresh();
                    return 0;
                }
                if (msg == WM_KEYDOWN) {
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool inSearch = GetFocusedWidget() == FindControl(SEARCH);
                    if (wp == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                        SetFocusedWidget(FindControl(SEARCH));
                        Control<exui::EditBox>(SEARCH).SelectAll();
                        return 0;
                    }
                    if (wp == VK_F3 || (wp == VK_RETURN && inSearch)) {
                        search();
                        view->step(shift ? -1 : 1);
                        refresh();
                        return 0;
                    }
                    if (wp == VK_ESCAPE && inSearch) {
                        SetText(SEARCH, L"");
                        search();
                        refresh();
                        return 0;
                    }
                }
                return LiveForm::HandleMessage(msg, wp, lp);
            }

            void OnAction(uint32_t id) override {
                switch (id) {
                    case BOTTOM: view->follow(); break;
                    case PREVIOUS:
                    case NEXT:
                        search();
                        view->step(id == NEXT ? 1 : -1);
                        break;
                    case PAUSE: GameLog::setPaused(!GameLog::paused()); break;
                    case COPY: copy(); break;
                    case EXPORT: exportLog(); break;
                    case CLEAR: GameLog::clear(); break;
                }
                refresh();
            }

        private:
            LogView* view = nullptr;
            // What was just done, shown instead of the counts for a moment.
            std::wstring flash;
            ULONGLONG flashUntil = 0;

            void setText(uint32_t id, const std::wstring& text) {
                if (Text(id) != text) {
                    SetText(id, text);
                }
            }

            void say(const std::wstring& text) {
                flash = text;
                flashUntil = GetTickCount64() + FLASH_MS;
                refresh();
            }

            // Applies what is typed at once, without waiting for the pause.
            void search() {
                KillTimer(GetHwnd(), SEARCH_TIMER);
                view->setQuery(Platform::narrow(Text(SEARCH)));
            }

            void copy() {
                uint64_t lines = 0;
                std::wstring text = view->text(lines);
                if (lines > 0 && writeClipboard(GetHwnd(), text)) {
                    say(L"已复制 " + withCommas(lines) + L" 行日志");
                }
            }

            void exportLog() {
                std::string name = GameLog::title().empty() ? std::string("game") : GameLog::title();
                std::optional<std::filesystem::path> target = Platform::pickSaveFile(
                        Platform::exeDirectory() / Platform::widen(name + ".log"), L"导出游戏日志", GetHwnd());
                if (!target) {
                    return;
                }
                std::ofstream out(*target, std::ios::binary | std::ios::trunc);
                for (const GameLog::Line& entry : GameLog::lines()) {
                    out << entry.text << '\n';
                }
                std::wstring where = Platform::widen(Platform::display(*target));
                if (out) {
                    say(L"已导出日志到 " + where);
                } else {
                    message(GetHwnd(), L"无法写入 " + where, L"游戏日志");
                }
            }
        };

        std::unique_ptr<LogWindow> current;
    }

    void showLog() {
        if (current != nullptr && current->GetHwnd() != nullptr && !current->IsClosing()) {
            HWND hwnd = current->GetHwnd();
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
            return;
        }
        // One still closing goes at once, so that its object can go with it.
        closeLog();
        current = std::make_unique<LogWindow>();
        if (!current->create()) {
            current.reset();
            return;
        }
        // A step down and right from the main window, which it would cover otherwise.
        RECT main{};
        if (Platform::window != nullptr && GetWindowRect((HWND) Platform::window, &main)) {
            SetWindowPos(current->GetHwnd(), nullptr, main.left + 40, main.top + 40, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        current->ShowWindowWithAnimation();
    }

    void closeLog() {
        if (current != nullptr && current->GetHwnd() != nullptr) {
            current->CloseWindow(false);
        }
        current.reset();
    }
}
