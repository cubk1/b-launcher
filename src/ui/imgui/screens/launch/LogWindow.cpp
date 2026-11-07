#include "ui/imgui/screens/launch/LogWindow.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/Appearance.h"
#if defined(_WIN32)
#include "ui/imgui/screens/shell/BorderlessWindow.h"
#else
#include "ui/imgui/screens/shell/SdlWindow.h"

#include <SDL3/SDL_timer.h>
#endif
#include "app/launch/Game.h"
#include "app/launch/GameLog.h"
#include "app/launch/LogViewModel.h"
#include "app/launch/LaunchDefaults.h"
#include "app/system/About.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/system/Platform.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiIme.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/IconButtonWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"

#include <core/base/strings.hpp>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#if defined(_WIN32)
#include "imgui_impl_win32.h"
#endif

#if defined(_WIN32)
#include <GL/gl.h>
#else
#include "ui/imgui/gl/UiGL.h"
#endif

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LogWindow {

    namespace {
        constexpr float START_W = 900.0f;
        constexpr float START_H = 560.0f;
        constexpr float TITLE_H = 40.0f;
        constexpr float BAR_H = 34.0f;
        constexpr float BAR_BUTTON = 28.0f;
        constexpr float BAR_PAD = 3.0f;
        constexpr float SEARCH_FIELD_W = 220.0f;
        constexpr float STATUS_H = 30.0f;
        constexpr float PAD = 12.0f;
        constexpr float ROW_H = 18.0f;
        constexpr float TEXT_SIZE = 12.5f;
        constexpr double TYPING_PAUSE = 0.2;

        const char* const SEARCH_OWNER = "log#search";

#if defined(_WIN32)
        BorderlessWindow* mainWindow = nullptr;
        BorderlessWindow window;
        ULONGLONG hiddenAt = 0;
        constexpr ULONGLONG RELEASE_AFTER = 10000;
#else
        SdlWindow* mainWindow = nullptr;
        SdlWindow window;
        uint64_t hiddenAt = 0;
        constexpr uint64_t RELEASE_AFTER = 10000;
#endif
        std::wstring logTitle = L"游戏日志 - " + Platform::widen(About::NAME);
        ImGuiContext* context = nullptr;
        bool created = false;
        bool shown = false;
        bool revealing = false;
        bool darkChrome = true;
        float layoutScale = 1.0f;
        NewUi::FrameState state;

        uint64_t seenGeneration = ~0ull;
        uint64_t scanned = 0;
        std::deque<uint64_t> rows;
        std::deque<uint64_t> starts;
        uint64_t visualEnd = 0;
        int layoutColumns = 0;
        bool wrapping = true;

        bool searchOpen = false;
        std::string searchText;
        LogViewModel::Search search;
        std::string typed;
        double typedAt = 0.0;
        bool refocus = false;

        constexpr int LINE_END = 1 << 30;

        struct Point {
            uint64_t row = 0;
            int column = 0;
        };

        struct Hit {
            uint64_t row = 0;
            float column = 0.0f;
            bool past = false;
        };

        enum class Unit { Character, Word, Line };

        bool selection = false;
        bool selecting = false;
        bool anchored = false;
        bool marking = false;
        bool block = false;
        Unit unit = Unit::Character;
        Point anchor;
        Point caret;
        Point unitFrom;
        Point unitTo;
        float asideCarry = 0.0f;
        int widest = 0;

        constexpr double FLASH_TIME = 0.6;
        constexpr double COPIED_TIME = 1.5;
        double copiedAt = -10.0;
        Point copiedFirst;
        Point copiedLast;
        bool copiedBlock = false;

        float scroll = 0.0f;
        bool following = true;
        int columnsAside = 0;

        constexpr uint64_t NONE = ~0ull;
        constexpr float GUTTER = 14.0f;
        constexpr size_t REPEAT_KEYS = 200;
        constexpr uint64_t REPEAT_TIMEOUT = 60000;

        struct Repeat {
            uint64_t row = 0;
            uint64_t seen = 0;
            int count = 1;
        };

        bool mergeRepeats = true;
        bool rebuilding = false;
        uint64_t head = NONE;
        std::map<uint64_t, uint32_t> folds;
        std::set<uint64_t> unfolded;
        std::map<uint64_t, int> repeated;
        std::unordered_map<std::string, Repeat> repeats;

        bool passes(GameLog::Level) {
            return true;
        }

        std::string maskNumbers(std::string_view text) {
            std::string out;
            out.reserve(text.size());
            bool inNumber = false;
            for (char c : text) {
                if ((c >= '0' && c <= '9') || (c == '.' && inNumber)) {
                    if (!inNumber) {
                        out += ' ';
                        inNumber = true;
                    }
                    continue;
                }
                inNumber = false;
                out += c;
            }
            return out;
        }

        bool divider(std::string_view text) {
            size_t characters = 0;
            for (size_t i = 0; i < text.size(); characters++) {
                if (text[i] == '-' || text[i] == '=') {
                    i++;
                } else if (text.compare(i, 3, "\xE2\x96\xAC") == 0) {
                    i += 3;
                } else {
                    return false;
                }
            }
            return characters > 5;
        }

        bool blank(std::string_view text) {
            return text.find_first_not_of(" \t\r") == std::string_view::npos;
        }

        size_t positionOf(uint64_t number) {
            return (size_t) (std::lower_bound(rows.begin(), rows.end(), number) - rows.begin());
        }

        uint64_t visualBase() {
            return starts.empty() ? visualEnd : starts.front();
        }

        float topOf(size_t index) {
            return (float) ((index < starts.size() ? starts[index] : visualEnd) - visualBase()) * ROW_H;
        }

        float contentHeight() {
            return (float) (visualEnd - visualBase()) * ROW_H;
        }

        size_t indexAt(float y) {
            if (rows.empty()) {
                return 0;
            }
            uint64_t line = visualBase() + (uint64_t) std::max(0.0f, std::floor(y / ROW_H));
            size_t index = (size_t) (std::upper_bound(starts.begin(), starts.end(), line) - starts.begin());
            return std::min(rows.size() - 1, index == 0 ? 0 : index - 1);
        }

        uint64_t spanOf(uint64_t number) {
            if (layoutColumns <= 0) {
                return 1;
            }
            return (uint64_t) LogViewModel::wrappedRows(GameLog::line(number).text, layoutColumns);
        }

        void dropRow(uint64_t number) {
            auto at = std::lower_bound(rows.begin(), rows.end(), number);
            if (at == rows.end() || *at != number) {
                return;
            }
            size_t index = (size_t) (at - rows.begin());
            float y = topOf(index);
            uint64_t span = (index + 1 < starts.size() ? starts[index + 1] : visualEnd) - starts[index];
            rows.erase(at);
            starts.erase(starts.begin() + (long long) index);
            for (size_t i = index; i < starts.size(); i++) {
                starts[i] -= span;
            }
            visualEnd -= span;
            if (!rebuilding && !following && y < scroll) {
                scroll = std::max(0.0f, scroll - (float) span * ROW_H);
            }
            search.erase(number);
            repeated.erase(number);
        }

        void merge(uint64_t number, const GameLog::Line& entry) {
            if (blank(entry.text) || divider(entry.text)) {
                return;
            }
            std::string key = maskNumbers(entry.text);
            auto found = repeats.find(key);
            if (found == repeats.end()) {
                if (repeats.size() >= REPEAT_KEYS) {
                    repeats.erase(std::min_element(repeats.begin(), repeats.end(), [](const auto& a, const auto& b) {
                        return a.second.seen < b.second.seen;
                    }));
                }
                repeats.emplace(std::move(key), Repeat{number, entry.at, 1});
                return;
            }
            Repeat& last = found->second;
            if (entry.at - last.seen >= REPEAT_TIMEOUT || unfolded.count(last.row) != 0) {
                last = Repeat{number, entry.at, 1};
                return;
            }
            dropRow(last.row);
            last = Repeat{number, entry.at, last.count + 1};
            repeated[number] = last.count;
        }

        void scan(uint64_t number) {
            const GameLog::Line& entry = GameLog::line(number);
            if (!passes(entry.level)) {
                return;
            }
            if (!entry.trace) {
                head = number;
                if (mergeRepeats) {
                    merge(number, entry);
                }
            } else if (head != NONE) {
                folds[head]++;
                if (unfolded.count(head) == 0) {
                    return;
                }
            }
            starts.push_back(visualEnd);
            visualEnd += spanOf(number);
            rows.push_back(number);
            search.scan(number, entry.text);
        }

        void restart() {
            rows.clear();
            starts.clear();
            visualEnd = 0;
            search.resetMatches();
            head = NONE;
            folds.clear();
            repeated.clear();
            repeats.clear();
            scanned = GameLog::first();
        }

        void update() {
            if (GameLog::generation() != seenGeneration) {
                seenGeneration = GameLog::generation();
                unfolded.clear();
                restart();
                selection = false;
                selecting = false;
                anchored = false;
                marking = false;
                scroll = 0.0f;
                following = true;
            }
            uint64_t first = GameLog::first();
            if (anchored && std::max(anchor.row, caret.row) < first) {
                selection = false;
                selecting = false;
                anchored = false;
                marking = false;
            }
            uint64_t removed = 0;
            while (!rows.empty() && rows.front() < first) {
                removed += (starts.size() > 1 ? starts[1] : visualEnd) - starts.front();
                rows.pop_front();
                starts.pop_front();
            }
            search.discardBefore(first);
            if (removed > 0 && !following) {
                scroll = std::max(0.0f, scroll - (float) removed * ROW_H);
            }
            folds.erase(folds.begin(), folds.lower_bound(first));
            unfolded.erase(unfolded.begin(), unfolded.lower_bound(first));
            repeated.erase(repeated.begin(), repeated.lower_bound(first));
            if (head != NONE && head < first) {
                head = NONE;
            }
            scanned = std::max(scanned, first);
            for (uint64_t n = scanned; n < GameLog::end(); n++) {
                scan(n);
            }
            scanned = GameLog::end();
        }

        void rebuildAround(uint64_t number) {
            float offset = topOf(positionOf(number)) - scroll;
            rebuilding = true;
            restart();
            update();
            rebuilding = false;
            if (!following) {
                scroll = std::max(0.0f, topOf(positionOf(number)) - offset);
            }
        }

        void relayout(int columns) {
            size_t top = indexAt(scroll);
            layoutColumns = columns;
            uint64_t line = 0;
            for (size_t i = 0; i < rows.size(); i++) {
                starts[i] = line;
                line += spanOf(rows[i]);
            }
            visualEnd = line;
            if (!following && !rows.empty()) {
                scroll = topOf(top);
            }
        }

        void toggleFold(uint64_t number) {
            UiSound::request(UiSound::TICK);
            if (unfolded.erase(number) == 0) {
                unfolded.insert(number);
            }
            rebuildAround(number);
        }

        void toggleRepeats() {
            uint64_t top = rows.empty() ? 0 : rows[indexAt(scroll)];
            mergeRepeats = !mergeRepeats;
            LaunchDefaults::setMergeLogRepeats(mergeRepeats);
            rebuildAround(top);
        }

        void toggleWrap() {
            wrapping = !wrapping;
            LaunchDefaults::setWrapLogLines(wrapping);
        }

        float viewHeight();

        void reveal(uint64_t number) {
            float y = topOf(positionOf(number));
            float view = viewHeight();
            if (y < scroll || y + ROW_H > scroll + view) {
                scroll = std::max(0.0f, y - view * 0.4f);
            }
            following = false;
        }

        void step(int direction) {
            uint64_t top = rows.empty() ? 0 : rows[indexAt(scroll)];
            if (auto number = search.step(direction, top)) {
                reveal(*number);
            }
        }

        void setQuery(const std::string& text) {
            if (!search.setQuery(text)) {
                return;
            }
            if (!search.query().empty()) {
                for (uint64_t n : rows) {
                    search.scan(n, GameLog::line(n).text);
                }
                step(1);
            }
        }

        std::string withCommas(uint64_t value) {
            std::string digits = std::to_string(value);
            std::string out;
            for (size_t i = 0; i < digits.size(); i++) {
                if (i > 0 && (digits.size() - i) % 3 == 0) {
                    out += ',';
                }
                out += digits[i];
            }
            return out;
        }

        bool before(const Point& a, const Point& b) {
            return a.row < b.row || (a.row == b.row && a.column < b.column);
        }

        Point selectionStart() {
            return before(caret, anchor) ? caret : anchor;
        }

        Point selectionEnd() {
            return before(caret, anchor) ? anchor : caret;
        }

        void refreshSelection() {
            selection = block ? anchor.column != caret.column : before(anchor, caret) || before(caret, anchor);
        }

        int lineColumns(uint64_t number) {
            const std::string& text = GameLog::line(number).text;
            return LogViewModel::columnsBefore(text, text.size());
        }

        int skippedOf(std::string_view text) {
            int skipped = 0;
            LogViewModel::columnsOf(text, columnsAside, 0, skipped);
            return LogViewModel::columnsBefore(text, text.size()) <= columnsAside ? columnsAside : skipped;
        }

        std::pair<Point, Point> unitAt(const Hit& hit, Unit kind) {
            const std::string& text = GameLog::line(hit.row).text;
            Point point{hit.row, 0};
            if (block) {
                point.column = std::max(0, (int) std::lround(hit.column));
            } else if (kind == Unit::Line) {
                return {point, Point{hit.row, LINE_END}};
            } else if (hit.past) {
                point.column = LogViewModel::columnsBefore(text, text.size());
            } else if (kind == Unit::Word) {
                auto [from, to] = LogViewModel::wordAround(text, (int) std::floor(std::max(0.0f, hit.column)));
                return {Point{hit.row, from}, Point{hit.row, to}};
            } else {
                point.column = LogViewModel::nearestColumn(text, hit.column);
            }
            return {point, point};
        }

        void extendTo(const Hit& hit) {
            auto [from, to] = unitAt(hit, unit);
            if (before(from, unitFrom)) {
                anchor = unitTo;
                caret = from;
            } else {
                anchor = unitFrom;
                caret = to;
            }
            refreshSelection();
        }

        std::string textBetween(const Point& first, const Point& last) {
            std::string text;
            for (size_t i = positionOf(first.row); i < rows.size() && rows[i] <= last.row; i++) {
                uint64_t number = rows[i];
                const std::string& line = GameLog::line(number).text;
                size_t start = number == first.row ? LogViewModel::byteAt(line, first.column) : 0;
                bool whole = number != last.row || last.column >= LINE_END;
                size_t end = whole ? line.size() : LogViewModel::byteAt(line, last.column);
                if (end > start) {
                    text.append(line, start, end - start);
                }
                if (!whole) {
                    break;
                }
                text += '\n';
                auto fold = folds.find(number);
                if (fold == folds.end() || unfolded.count(number) != 0) {
                    continue;
                }
                for (uint64_t hidden = number + 1; hidden <= number + fold->second; hidden++) {
                    text += GameLog::line(hidden).text;
                    text += '\n';
                }
            }
            return text;
        }

        std::string blockText() {
            int from = std::min(anchor.column, caret.column);
            int to = std::max(anchor.column, caret.column);
            uint64_t last = std::max(anchor.row, caret.row);
            size_t begin = positionOf(std::min(anchor.row, caret.row));
            std::string text;
            for (size_t i = begin; i < rows.size() && rows[i] <= last; i++) {
                std::string_view line = GameLog::line(rows[i]).text;
                size_t start = LogViewModel::byteAt(line, from);
                std::string_view piece = line.substr(start, LogViewModel::byteAt(line, to) - start);
                size_t kept = piece.find_last_not_of(" \t");
                if (i > begin) {
                    text += '\n';
                }
                if (kept != std::string_view::npos) {
                    text.append(piece.substr(0, kept + 1));
                }
            }
            return text;
        }

        void copySelection() {
            if (rows.empty()) {
                return;
            }
            copiedBlock = selection && block;
            if (!selection) {
                copiedFirst = Point{rows.front(), 0};
                copiedLast = Point{rows.back(), LINE_END};
            } else if (block) {
                copiedFirst = Point{std::min(anchor.row, caret.row), std::min(anchor.column, caret.column)};
                copiedLast = Point{std::max(anchor.row, caret.row), std::max(anchor.column, caret.column)};
            } else {
                copiedFirst = selectionStart();
                copiedLast = selectionEnd();
            }
            std::string text = copiedBlock ? blockText() : textBetween(copiedFirst, copiedLast);
            ImGui::SetClipboardText(text.c_str());
            copiedAt = ImGui::GetTime();
            selection = false;
            marking = false;
        }

        Point moved(const Point& from, ImGuiKey key, bool word) {
            size_t index = std::min(positionOf(from.row), rows.size() - 1);
            uint64_t number = rows[index];
            const std::string& text = GameLog::line(number).text;
            int total = LogViewModel::columnsBefore(text, text.size());
            int column = number == from.row ? std::min(from.column, total) : 0;
            long long page = std::max(1LL, (long long) (viewHeight() / ROW_H) - 1);
            auto toRow = [&](long long target) {
                uint64_t row = rows[(size_t) std::clamp(target, 0LL, (long long) rows.size() - 1)];
                return Point{row, LogViewModel::nearestColumn(GameLog::line(row).text, (float) column)};
            };
            switch (key) {
                case ImGuiKey_LeftArrow:
                    if (column > 0) {
                        return Point{number,
                                     word ? LogViewModel::wordAround(text, column - 1).first
                                          : LogViewModel::stepColumn(text, column, -1)};
                    }
                    return index == 0 ? Point{number, 0} : Point{rows[index - 1], lineColumns(rows[index - 1])};
                case ImGuiKey_RightArrow:
                    if (column < total) {
                        return Point{number,
                                     word ? LogViewModel::wordAround(text, column).second
                                          : LogViewModel::stepColumn(text, column, 1)};
                    }
                    return index + 1 >= rows.size() ? Point{number, total} : Point{rows[index + 1], 0};
                case ImGuiKey_UpArrow: return toRow((long long) index - 1);
                case ImGuiKey_DownArrow: return toRow((long long) index + 1);
                case ImGuiKey_PageUp: return toRow((long long) index - page);
                case ImGuiKey_PageDown: return toRow((long long) index + page);
                case ImGuiKey_Home: return word ? Point{rows.front(), 0} : Point{number, 0};
                case ImGuiKey_End: return word ? Point{rows.back(), lineColumns(rows.back())} : Point{number, total};
                default: return from;
            }
        }

        void exportAll() {
            std::string name = GameLog::title().empty() ? std::string("game") : GameLog::title();
            std::optional<std::filesystem::path> target =
                    Platform::pickSaveFile(Platform::userDirectory() / b_launcher_base::path_from_utf8(name + ".log"),
#if defined(_WIN32)
                                           L"导出游戏日志", window.hwnd);
#else
                                           L"导出游戏日志", window.window);
#endif
            if (!target) {
                return;
            }
            std::ofstream out(*target, std::ios::binary | std::ios::trunc);
            for (const GameLog::Line& entry : GameLog::lines()) {
                out << entry.text << '\n';
            }
            out.close();
            if (out) {
                Notifications::add(Notifications::DONE, "已导出日志到 " + Platform::display(*target), 3000);
            } else {
                Notifications::add(Notifications::FAILED, "无法写入 " + Platform::display(*target), 4000);
            }
        }

        float width() {
            return window.width / layoutScale;
        }

        float height() {
            return window.height / layoutScale;
        }

        float listTop() {
            return TITLE_H + 8.0f;
        }

        float listBottom() {
            return height() - STATUS_H;
        }

        float viewHeight() {
            return std::max(ROW_H, listBottom() - listTop() - 8.0f);
        }

        uint32_t amber() {
            return UiTheme::isLight() ? 0xFFC27803u : 0xFFE8A33Du;
        }

        uint32_t levelColor(GameLog::Level level) {
            switch (level) {
                case GameLog::Level::Fatal:
                case GameLog::Level::Error: return UiTheme::danger();
                case GameLog::Level::Warn: return amber();
                case GameLog::Level::Debug: return UiTheme::textMuted();
                case GameLog::Level::Launcher: return UiTheme::accentText();
                default: return UiTheme::textPrimary();
            }
        }

        void tip(const std::string& text, float x, float y, float w, float h) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
            NewUi::tooltipText = text;
            NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, width() - tipW - 6.0f);
            NewUi::tooltipY = y + h + 6.0f;
        }

        bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y, float size) {
            IconButtonWidget button(id, Ui::glyph(glyph), UiTheme::textSecondary(), UiTheme::textPrimary());
            button.bounds(x, y, size, size);
            button.paint();
            if (button.hovered()) {
                tip(hint, x, y, size, size);
            }
            return button.pressed;
        }

        std::vector<std::pair<float, float>> tabSpans;

        uint32_t liveColor(Game::State state) {
            if (state == Game::State::Preparing) {
                return UiTheme::accent();
            }
            return UiTheme::isLight() ? 0xFF2F9E5Cu : 0xFF5CC98Au;
        }

        void tabs(float x, float limit) {
            std::vector<GameLog::Tab> list = GameLog::tabs();
            std::vector<Game::Session> sessions = Game::sessions();
            const float tabH = 28.0f;
            const float gap = 4.0f;
            float top = (TITLE_H - tabH) * 0.5f;
            float each = std::clamp((limit - x - gap * (list.size() - 1)) / list.size(), 72.0f, 200.0f);
            for (const GameLog::Tab& tab : list) {
                if (x + 48.0f > limit) {
                    break;
                }
                float tabW = std::min(each, limit - x);
                Game::State state = Game::State::Idle;
                for (const Game::Session& session : sessions) {
                    if (session.log == tab.id && state != Game::State::Preparing) {
                        state = session.state;
                    }
                }
                bool live = state != Game::State::Idle;
                bool active = tab.id == GameLog::shown();
                bool over = NewUi::hit(x, top, tabW, tabH);
                std::string id = "log#tab#" + std::to_string(tab.id);
                float lit = UiState::motion(id, active ? 1.0f : (over ? 0.5f : 0.0f), 18.0f);
                if (lit > 0.001f) {
                    UiRender::roundRect(x, top, tabW, tabH, 8.0f,
                                        UiRender::mix(UiTheme::clear(), UiTheme::navHover(), lit));
                }
                float textX = x + 10.0f;
                if (live) {
                    UiRender::circle(textX + 3.0f, top + tabH * 0.5f, 3.0f, liveColor(state));
                    textX += 12.0f;
                }
                float closeRoom = live ? 0.0f : 20.0f;
                std::string text = UiFont::fitEnd(tab.title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                  std::max(0.0f, x + tabW - 8.0f - closeRoom - textX));
                UiRender::textY(text, textX, top, tabH,
                                UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), active ? 1.0f : lit),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (text != tab.title && over) {
                    tip(tab.title, x, top, tabW, tabH);
                }
                if (!live && over && NewUi::middleClick) {
                    NewUi::middleClick = false;
                    GameLog::close(tab.id);
                    tabSpans.emplace_back(x, tabW);
                    return;
                }
                float closeX = x + tabW - 22.0f;
                if (!live && (over || active)) {
                    bool closing = NewUi::hit(closeX, top + 4.0f, 18.0f, tabH - 8.0f);
                    UiWidgets::icon(Ui::glyph(0xf00d), closeX + 9.0f, top + tabH * 0.5f,
                                    closing ? UiTheme::textPrimary() : UiTheme::textMuted(), 8.0f);
                    if (NewUi::click(closeX, top + 4.0f, 18.0f, tabH - 8.0f)) {
                        GameLog::close(tab.id);
                        tabSpans.emplace_back(x, tabW);
                        return;
                    }
                }
                if (NewUi::click(x, top, tabW, tabH)) {
                    UiSound::request(UiSound::TICK);
                    GameLog::show(tab.id);
                }
                tabSpans.emplace_back(x, tabW);
                x += tabW + gap;
            }
        }

        void titleBar() {
            float w = width();
            Logo::draw(PAD + 2.0f, (TITLE_H - 18.0f) * 0.5f, 18.0f);
            float x = PAD + 28.0f;
            const char* title = "游戏日志";
            UiRender::textY(title, x, 0.0f, TITLE_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            x += UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 10.0f;
            tabSpans.clear();
            float end = w - (window.framed ? PAD : 130.0f);
            if (GameLog::tabs().size() > 1) {
                tabs(x, end);
            } else if (!GameLog::title().empty()) {
                UiRender::textY(UiFont::fitEnd(GameLog::title(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, end - x), x,
                                0.0f, TITLE_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            if (window.framed) {
                return;
            }
            float size = 26.0f;
            float y = (TITLE_H - size) * 0.5f;
            float bx = w - 14.0f - size;
            if (iconButton("log#close", 0xf00d, "关闭", bx, y, size)) {
                close();
            }
            bx -= size + 4.0f;
#if defined(_WIN32)
            bool zoomed = IsZoomed(window.hwnd) != FALSE;
            if (iconButton("log#maximize", zoomed ? 0xf2d2 : 0xf2d0, zoomed ? "还原" : "最大化", bx, y, size)) {
                ShowWindow(window.hwnd, zoomed ? SW_RESTORE : SW_MAXIMIZE);
            }
            bx -= size + 4.0f;
            if (iconButton("log#minimize", 0xf068, "最小化", bx, y, size)) {
                ShowWindow(window.hwnd, SW_MINIMIZE);
            }
#else
            bool zoomed = window.maximized();
            if (iconButton("log#maximize", zoomed ? 0xf2d2 : 0xf2d0, zoomed ? "还原" : "最大化", bx, y, size)) {
                window.toggleMaximize();
            }
            bx -= size + 4.0f;
            if (iconButton("log#minimize", 0xf068, "最小化", bx, y, size)) {
                window.minimize();
            }
#endif
        }

        void openSearch() {
            searchOpen = true;
            refocus = true;
        }

        void closeSearch() {
            searchOpen = false;
            if (NewUi::textEditing == SEARCH_OWNER) {
                NewUi::cancelTextEdit();
            }
        }

        std::shared_ptr<TextBinding> searchValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        SEARCH_OWNER, [] { return searchText; },
                        [](const std::string& text) {
                            searchText = text;
                            setQuery(text);
                            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                                step(ImGui::GetIO().KeyShift ? -1 : 1);
                                refocus = true;
                            }
                        },
                        "搜索日志");
                binding->limit = 200;
                return binding;
            }();
            return value;
        }

        struct Rect {
            float x = 0, y = 0, w = 0, h = 0;

            bool contains() const {
                return NewUi::hit(x, y, w, h);
            }
        };

        float searchPartWidth() {
            return SEARCH_FIELD_W + 6.0f + BAR_BUTTON * 2.0f + 2.0f + 8.0f;
        }

        Rect barBounds() {
            float actions = BAR_BUTTON * 7.0f + 2.0f * 6.0f;
            Rect bar;
            bar.w = BAR_PAD * 2.0f + actions + (searchOpen ? searchPartWidth() : 0.0f);
            bar.h = BAR_H;
            bar.x = width() - PAD - 14.0f - bar.w;
            bar.y = listTop() + 8.0f;
            return bar;
        }

        bool barButton(const std::string& id, int glyph, const std::string& hint, float x, float y, bool lit) {
            if (lit) {
                UiRender::roundRect(x, y, BAR_BUTTON, BAR_BUTTON, 7.0f, UiTheme::accentSoft(56));
            }
            IconButtonWidget button(id, Ui::glyph(glyph), lit ? UiTheme::accentText() : UiTheme::textSecondary(),
                                    lit ? UiTheme::accentText() : UiTheme::textPrimary());
            button.bounds(x, y, BAR_BUTTON, BAR_BUTTON);
            button.paint();
            if (button.hovered()) {
                tip(hint, x, y, BAR_BUTTON, BAR_BUTTON);
            }
            return button.pressed;
        }

        void searchPart(float x, float y) {
            float fieldY = y + (BAR_H - Page::FIELD_H) * 0.5f;
            if (refocus) {
                refocus = false;
                NewUi::beginTextEdit(searchValue());
                UiInput::moveCaret(NewUi::INPUT_TEXT, (int) Ui::length(searchText), false);
            }
            std::string found = search.query().empty() ? std::string()
                    : search.count() == 0 ? std::string("无匹配")
                                         : std::to_string(search.index() + 1) + "/" + withCommas(search.count());
            float foundW = found.empty() ? 0.0f : UiFont::width(found, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            TextFieldWidget field(searchValue());
            field.box(x, fieldY, SEARCH_FIELD_W, Page::FIELD_H).bounds(x, fieldY, SEARCH_FIELD_W, Page::FIELD_H);
            field.room(found.empty() ? 0.0f : foundW + 8.0f);
            field.paint();
            if (!found.empty()) {
                UiRender::textY(found, x + SEARCH_FIELD_W - 9.0f - foundW, fieldY, Page::FIELD_H,
                                search.count() == 0 ? UiTheme::danger() : UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
            }
            bool editing = NewUi::textEditing == SEARCH_OWNER;
            std::string now = editing ? UiInput::get(NewUi::INPUT_TEXT) : searchText;
            double clock = ImGui::GetTime();
            if (now != typed) {
                typed = now;
                typedAt = clock;
            }
            if (clock - typedAt >= TYPING_PAUSE) {
                setQuery(typed);
            }
            x += SEARCH_FIELD_W + 6.0f;
            float buttonY = y + (BAR_H - BAR_BUTTON) * 0.5f;
            if (barButton("log#previous", 0xf077, "上一个（Shift+Enter）", x, buttonY, false)) {
                step(-1);
            }
            x += BAR_BUTTON + 2.0f;
            if (barButton("log#next", 0xf078, "下一个（Enter）", x, buttonY, false)) {
                step(1);
            }
            x += BAR_BUTTON + 8.0f;
            UiRender::rect(x - 5.0f, y + 8.0f, 1.0f, BAR_H - 16.0f, UiTheme::outlineSoft());
        }

        void floatingBar() {
            Rect bar = barBounds();
            UiRender::shadow(bar.x, bar.y, bar.w, bar.h, 10.0f, 10.0f, 3.0f, UiTheme::shadow());
            UiRender::roundRect(bar.x, bar.y, bar.w, bar.h, 10.0f, UiTheme::surfaceRaised());
            UiRender::outline(bar.x, bar.y, bar.w, bar.h, 10.0f, UiTheme::outlineSoft());
            float x = bar.x + BAR_PAD;
            if (searchOpen) {
                searchPart(x + 4.0f, bar.y);
                x += searchPartWidth();
            }
            float y = bar.y + (BAR_H - BAR_BUTTON) * 0.5f;
            bool paused = GameLog::paused();
            if (barButton("log#search", 0xf002, searchOpen ? "关闭搜索（Esc）" : "搜索（Ctrl+F）", x, y, searchOpen)) {
                searchOpen ? closeSearch() : openSearch();
            }
            x += BAR_BUTTON + 2.0f;
            if (barButton("log#pause", paused ? 0xf04b : 0xf04c, paused ? "继续" : "暂停", x, y, paused)) {
                GameLog::setPaused(!paused);
                update();
            }
            x += BAR_BUTTON + 2.0f;
            if (barButton("log#repeats", 0xf5fd, mergeRepeats ? "逐条显示重复的日志" : "合并重复的日志", x, y,
                          mergeRepeats)) {
                toggleRepeats();
            }
            x += BAR_BUTTON + 2.0f;
            if (barButton("log#wrap", 0xf3be, wrapping ? "关闭自动换行" : "自动换行", x, y, wrapping)) {
                toggleWrap();
            }
            x += BAR_BUTTON + 2.0f;
            bool copied = ImGui::GetTime() - copiedAt < COPIED_TIME;
            if (copied) {
                UiState::keepAnimating();
            }
            std::string copyHint = copied ? std::string("已复制")
                    : selection           ? std::string("复制选中的内容（Ctrl+C）")
                                          : std::string("复制全部");
            if (barButton("log#copy", copied ? 0xf00c : 0xf0c5, copyHint, x, y, copied)) {
                copySelection();
            }
            x += BAR_BUTTON + 2.0f;
            if (barButton("log#export", 0xf56e, "导出", x, y, false)) {
                exportAll();
            }
            x += BAR_BUTTON + 2.0f;
            if (barButton("log#clear", 0xf2ed, "清空", x, y, false)) {
                GameLog::clear();
                update();
            }
        }

        std::vector<std::pair<Rect, uint64_t>> foldTargets;

        float pillWidth(const std::string& text) {
            return text.empty() ? 0.0f : UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
        }

        Rect pill(const std::string& text, float x, float y, bool action, bool hot) {
            Rect area{x, y + 2.0f, pillWidth(text), ROW_H - 4.0f};
            UiRender::roundRect(area.x, area.y, area.w, area.h, 5.0f,
                                action ? UiTheme::accentSoft(hot ? 90 : 50) : UiTheme::surfaceRaised());
            UiRender::outline(area.x, area.y, area.w, area.h, 5.0f, UiTheme::outlineSoft());
            UiRender::textY(text, x + 6.0f, y, ROW_H, action ? UiTheme::accentText() : UiTheme::textSecondary(),
                            UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return area;
        }

        void logList() {
            float left = PAD;
            float right = width() - PAD;
            float top = listTop();
            float bottom = listBottom();
            UiRender::roundRect(left, top, right - left, bottom - top, 10.0f, UiTheme::surface());
            UiRender::outline(left, top, right - left, bottom - top, 10.0f, UiTheme::outlineSoft());
            float viewTop = top + 4.0f;
            float view = viewHeight();
            float textX = left + 10.0f + GUTTER;
            float textRight = right - 14.0f;
            bool underBar = barBounds().contains();
            bool overFold = false;
            for (const auto& [area, number] : foldTargets) {
                if (!underBar && !selecting && area.contains()) {
                    overFold = true;
                    if (NewUi::click(area.x, area.y, area.w, area.h)) {
                        toggleFold(number);
                    }
                    break;
                }
            }
            foldTargets.clear();
            float charW = std::max(1.0f, UiFont::width("0", UiFont::CODE, TEXT_SIZE));
            int columns = (int) ((textRight - textX) / charW) + 2;
            int wanted = wrapping ? std::max(8, (int) std::floor((textRight - textX) / charW)) : 0;
            if (wanted != layoutColumns) {
                relayout(wanted);
            }
            float contentH = contentHeight();
            float maxScroll = std::max(0.0f, contentH - view);

            ImGuiIO& io = ImGui::GetIO();
            bool over = NewUi::hit(left, top, right - left, bottom - top);
            if (over && io.MouseWheel != 0.0f) {
                if (io.KeyShift && layoutColumns == 0) {
                    columnsAside = std::max(0, columnsAside - (int) std::lround(io.MouseWheel * 8.0f));
                } else {
                    scroll -= io.MouseWheel * ROW_H * 3.0f;
                    following = io.MouseWheel < 0.0f && scroll >= maxScroll;
                }
            }
            if (over && io.MouseWheelH != 0.0f && layoutColumns == 0) {
                columnsAside = std::max(0, columnsAside + (int) std::lround(io.MouseWheelH * 8.0f));
            }
            if (following) {
                scroll = maxScroll;
            }
            scroll = UiRender::clamp(scroll, 0.0f, maxScroll);

            bool overLines = !underBar && NewUi::hit(left, viewTop, right - left - 14.0f, view);
            if (overFold) {
                UiCursor::hand();
            } else if (overLines || selecting) {
                UiCursor::text();
            }
            auto hitAt = [&](float mx, float my) {
                Hit hit;
                float contentY = my - viewTop + scroll;
                size_t index = indexAt(contentY);
                hit.past = contentY >= contentH;
                hit.row = rows[index];
                const std::string& text = GameLog::line(hit.row).text;
                if (layoutColumns == 0) {
                    hit.column = (float) skippedOf(text) + (mx - textX) / charW;
                    return hit;
                }
                std::vector<int> breaks = LogViewModel::wrapStarts(text, layoutColumns);
                int segment =
                        std::clamp((int) std::floor((contentY - topOf(index)) / ROW_H), 0, (int) breaks.size() - 1);
                hit.column = (float) breaks[(size_t) segment] + (mx - textX) / charW;
                if (segment + 1 < (int) breaks.size()) {
                    hit.column = std::min(hit.column, (float) breaks[(size_t) segment + 1] - 0.01f);
                }
                return hit;
            };
            if (!rows.empty() && overLines && NewUi::pendingClick && !NewUi::clickConsumed) {
                NewUi::clickConsumed = true;
                Hit hit = hitAt(NewUi::mouseX, NewUi::mouseY);
                int kind = (std::max(1, ImGui::GetMouseClickedCount(ImGuiMouseButton_Left)) - 1) % 3;
                unit = kind == 0 ? Unit::Character : kind == 1 ? Unit::Word : Unit::Line;
                if (io.KeyShift && anchored) {
                    unitFrom = anchor;
                    unitTo = anchor;
                } else {
                    block = io.KeyAlt && unit == Unit::Character;
                    auto range = unitAt(hit, unit);
                    unitFrom = range.first;
                    unitTo = range.second;
                }
                extendTo(hit);
                anchored = true;
                marking = false;
                selecting = true;
                asideCarry = 0.0f;
                if (selection) {
                    following = false;
                }
            }
            if (selecting) {
                if (!NewUi::mouseDown || rows.empty()) {
                    selecting = false;
                } else {
                    float mx = NewUi::mouseX;
                    float my = NewUi::mouseY;
                    float dt = io.DeltaTime;
                    if (my < viewTop) {
                        scroll -= std::min(viewTop - my, 160.0f) * 10.0f * dt;
                        following = false;
                    } else if (my > viewTop + view) {
                        scroll += std::min(my - viewTop - view, 160.0f) * 10.0f * dt;
                    }
                    scroll = UiRender::clamp(scroll, 0.0f, maxScroll);
                    float sideways = layoutColumns > 0 ? 0.0f
                            : mx < textX               ? mx - textX
                            : mx > textRight           ? mx - textRight
                                                       : 0.0f;
                    if (sideways == 0.0f) {
                        asideCarry = 0.0f;
                    } else {
                        asideCarry += std::clamp(sideways, -160.0f, 160.0f) * 0.4f * dt;
                        int whole = (int) asideCarry;
                        asideCarry -= (float) whole;
                        columnsAside =
                                std::clamp(columnsAside + whole, 0, std::max(columnsAside, widest - columns + 4));
                    }
                    extendTo(hitAt(UiRender::clamp(mx, textX, textRight),
                                   UiRender::clamp(my, viewTop, viewTop + view - 1.0f)));
                    if (selection) {
                        following = false;
                    }
                }
            }
            if (overLines && selection && NewUi::rightClick) {
                NewUi::rightClick = false;
                copySelection();
            }

            Point first = selectionStart();
            Point last = selectionEnd();
            int blockFrom = std::min(anchor.column, caret.column);
            int blockTo = std::max(anchor.column, caret.column);
            uint32_t selectedInk = (UiTheme::accent() & 0x00FFFFFFu) | (UiTheme::isLight() ? 0x47000000u : 0x66000000u);
            float flash = std::clamp((float) (1.0 - (ImGui::GetTime() - copiedAt) / FLASH_TIME), 0.0f, 1.0f);
            if (flash > 0.0f) {
                UiState::keepAnimating();
            }
            float flashAlpha =
                    std::min(255.0f, (float) (selectedInk >> 24) * 1.6f) * flash * flash * (3.0f - 2.0f * flash);
            uint32_t flashInk = (selectedInk & 0x00FFFFFFu) | ((uint32_t) Ui::round(flashAlpha) << 24);
            int widestShown = 0;
            uint64_t currentLine = search.current().value_or(~0ull);
            UiRender::scissor(left + 1.0f, viewTop, right - left - 2.0f, view);
            float shownScroll = UiRender::snapLayout(scroll);
            for (size_t i = indexAt(shownScroll); i < rows.size(); i++) {
                float y = viewTop + topOf(i) - shownScroll;
                if (y >= viewTop + view) {
                    break;
                }
                uint64_t number = rows[i];
                const GameLog::Line& entry = GameLog::line(number);
                int total = LogViewModel::columnsBefore(entry.text, entry.text.size());
                widestShown = std::max(widestShown, total);
                std::vector<int> breaks = layoutColumns > 0 ? LogViewModel::wrapStarts(entry.text, layoutColumns)
                                                            : std::vector<int>{skippedOf(entry.text)};
                int segments = (int) breaks.size();
                auto paint = [&](int from, int to, uint32_t ink, bool fill, bool rounded) {
                    for (int s = 0; s < segments; s++) {
                        int start = breaks[(size_t) s];
                        int end = s + 1 < segments ? breaks[(size_t) s + 1] : LINE_END;
                        if (from >= end || to <= start) {
                            continue;
                        }
                        float x0 = std::max(textX, textX + (float) (from - start) * charW);
                        float x1 = fill && to >= end
                                ? textRight
                                : std::min(textRight, textX + (float) (std::min(to, end) - start) * charW);
                        if (x1 <= x0) {
                            continue;
                        }
                        float rowY = y + (float) s * ROW_H;
                        if (rounded) {
                            UiRender::roundRect(x0 - 1.0f, rowY + 1.0f, x1 - x0 + 2.0f, ROW_H - 2.0f, 3.0f, ink);
                        } else {
                            UiRender::rect(x0, rowY, x1 - x0, ROW_H, ink);
                        }
                    }
                };
                if (selection && number >= first.row && number <= last.row) {
                    if (block) {
                        paint(blockFrom, blockTo, selectedInk, false, false);
                    } else {
                        paint(number == first.row ? first.column : 0, number == last.row ? last.column : LINE_END,
                              selectedInk, true, false);
                    }
                } else if (!selection && flash > 0.0f && number >= copiedFirst.row && number <= copiedLast.row) {
                    if (copiedBlock) {
                        paint(copiedFirst.column, copiedLast.column, flashInk, false, false);
                    } else {
                        paint(number == copiedFirst.row ? copiedFirst.column : 0,
                              number == copiedLast.row ? copiedLast.column : LINE_END, flashInk, true, false);
                    }
                }
                auto fold = folds.find(number);
                bool foldable = fold != folds.end();
                bool open = foldable && unfolded.count(number) != 0;
                if (foldable) {
                    Rect area{left + 2.0f, y, textX - left - 4.0f, ROW_H};
                    bool hot = !underBar && area.contains();
                    UiWidgets::icon(Ui::glyph(open ? 0xf078 : 0xf054), left + 13.0f, y + ROW_H * 0.5f,
                                    hot ? UiTheme::textPrimary() : UiTheme::textMuted(), 8.0f);
                    foldTargets.emplace_back(area, number);
                } else if (entry.trace) {
                    UiRender::rect(left + 12.5f, y, 1.0f, ROW_H * (float) segments, UiTheme::outlineSoft());
                }
                auto repeat = repeated.find(number);
                std::string repeatText = repeat != repeated.end() ? std::to_string(repeat->second) + " 次" : "";
                std::string foldText = foldable && !open ? "+" + std::to_string(fold->second) + " 行" : "";
                float pillsW = pillWidth(repeatText) + pillWidth(foldText) +
                        (!repeatText.empty() && !foldText.empty() ? 4.0f : 0.0f);
                if (!search.query().empty() && search.contains(entry.text)) {
                    uint32_t tint = (amber() & 0x00FFFFFFu) | (number == currentLine ? 0x90000000u : 0x40000000u);
                    std::string lower = LogViewModel::Search::lowered(entry.text);
                    for (size_t at = lower.find(search.query()); at != std::string::npos;
                         at = lower.find(search.query(), at + search.query().size())) {
                        paint(LogViewModel::columnsBefore(entry.text, at),
                              LogViewModel::columnsBefore(entry.text, at + search.query().size()), tint, false, true);
                    }
                }
                float pillX = textX;
                float pillY = y + (float) (segments - 1) * ROW_H;
                if (layoutColumns > 0) {
                    for (int s = 0; s < segments; s++) {
                        size_t from = LogViewModel::byteAt(entry.text, breaks[(size_t) s]);
                        size_t to = s + 1 < segments ? LogViewModel::byteAt(entry.text, breaks[(size_t) s + 1])
                                                     : entry.text.size();
                        float rowY = y + (float) s * ROW_H;
                        if (to > from && rowY + ROW_H > viewTop) {
                            UiRender::textY(entry.text.substr(from, to - from), textX, rowY, ROW_H,
                                            levelColor(entry.level), UiFont::CODE, TEXT_SIZE);
                        }
                    }
                    pillX += (float) (total - breaks.back()) * charW + 8.0f;
                } else {
                    int skipped = 0;
                    std::string_view shownText = LogViewModel::columnsOf(entry.text, columnsAside, columns, skipped);
                    if (pillsW > 0.0f) {
                        int room = std::max(0, (int) ((textRight - textX - pillsW - 8.0f) / charW));
                        if (LogViewModel::columnsBefore(shownText, shownText.size()) > room) {
                            shownText = LogViewModel::columnsOf(entry.text, columnsAside, room, skipped);
                        }
                    }
                    if (!shownText.empty()) {
                        UiRender::textY(std::string(shownText), textX, y, ROW_H, levelColor(entry.level), UiFont::CODE,
                                        TEXT_SIZE);
                    }
                    pillX += LogViewModel::columnsBefore(shownText, shownText.size()) * charW + 8.0f;
                }
                if (!repeatText.empty()) {
                    pillX += pill(repeatText, pillX, pillY, false, false).w + 4.0f;
                }
                if (!foldText.empty()) {
                    Rect area{pillX, pillY, pillWidth(foldText), ROW_H};
                    pill(foldText, pillX, pillY, true, !underBar && area.contains());
                    foldTargets.emplace_back(area, number);
                }
            }
            widest = widestShown;
            if (rows.empty()) {
                const char* empty = GameLog::any() ? "没有日志" : "启动游戏后，这里会显示它的日志";
                float w = UiFont::width(empty, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(empty, left + (right - left - w) * 0.5f, viewTop, view, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            UiRender::unscissor();

            if (maxScroll > 0.0f) {
                float dragged = NewUi::listScrollbar("log#scrollbar", right - 9.0f, viewTop + 2.0f, view - 4.0f, view,
                                                     maxScroll, scroll, scroll);
                if (dragged != scroll) {
                    scroll = dragged;
                    following = scroll >= maxScroll - 0.5f;
                }
            }
        }

        void statusBar() {
            float y = height() - STATUS_H;
            std::string text = withCommas(rows.size()) + " 行";
            size_t total = GameLog::lines().size();
            if (rows.size() != total) {
                text += " / 共 " + withCommas(total) + " 行";
            }
            if (selection && !rows.empty()) {
                Point first = selectionStart();
                Point last = selectionEnd();
                if (first.row == last.row && !block) {
                    const std::string& line = GameLog::line(first.row).text;
                    size_t from = LogViewModel::byteAt(line, first.column);
                    size_t to = last.column >= LINE_END ? line.size() : LogViewModel::byteAt(line, last.column);
                    size_t characters = 0;
                    for (size_t i = from; i < to; i++) {
                        characters += ((unsigned char) line[i] & 0xC0) != 0x80 ? 1 : 0;
                    }
                    text += "，已选 " + withCommas(characters) + " 个字符";
                } else {
                    size_t from = positionOf(first.row);
                    size_t to = (size_t) (std::upper_bound(rows.begin(), rows.end(), last.row) - rows.begin());
                    text += "，已选 " + withCommas(to - std::min(from, to)) + " 行";
                }
            }
            if (GameLog::dropped() > 0) {
                text += "，已丢弃最早的 " + withCommas(GameLog::dropped()) + " 行";
            }
            UiRender::textY(text, PAD + 4.0f, y, STATUS_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float right = width() - PAD;
            if (GameLog::paused()) {
                std::string held = "已暂停，" + withCommas(GameLog::held()) + " 行未显示";
                float w = UiFont::width(held, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(held, right - w, y, STATUS_H, amber(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                right -= w + 14.0f;
            }
            if (!following && !rows.empty()) {
                std::string text = "回到底部";
                float w = Page::buttonWidth(text, 0xf063);
                if (Page::button("log#bottom", text, right - w, y + (STATUS_H - Page::FIELD_H) * 0.5f, w, false, false,
                                 0xf063)) {
                    following = true;
                }
            }
        }

        void keys() {
            ImGuiIO& io = ImGui::GetIO();
            bool editing = NewUi::textEditing == SEARCH_OWNER;
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                openSearch();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
                step(io.KeyShift ? -1 : 1);
            }
            if (editing) {
                return;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && !rows.empty()) {
                block = false;
                unit = Unit::Character;
                anchor = Point{rows.front(), 0};
                caret = Point{rows.back(), LINE_END};
                anchored = true;
                refreshSelection();
            }
            if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false))) {
                copySelection();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (searchOpen) {
                    closeSearch();
                } else {
                    selection = false;
                    marking = false;
                }
            }
            if ((selection || marking) && !block && io.KeyShift && anchored && !rows.empty()) {
                for (ImGuiKey key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                                     ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Home, ImGuiKey_End}) {
                    if (ImGui::IsKeyPressed(key)) {
                        caret = moved(caret, key, io.KeyCtrl);
                        unit = Unit::Character;
                        marking = true;
                        refreshSelection();
                        reveal(caret.row);
                    }
                }
                return;
            }
            float page = viewHeight() - ROW_H;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
                scroll -= page;
                following = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
                scroll += page;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                scroll = 0.0f;
                following = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
                following = true;
            }
        }

        void draw() {
            UiRender::reset();
            update();
            UiRender::rect(0.0f, 0.0f, width(), height(), UiTheme::panel());
            UiRender::hLine(0.0f, width(), TITLE_H, UiTheme::divider());
            keys();
            titleBar();
            logList();
            floatingBar();
            statusBar();
        }

        void create() {
            ImGuiContext* main = ImGui::GetCurrentContext();
            window.resizable = true;
            window.framed = Appearance::systemFrameRunning();
            window.quitOnDestroy = false;
            window.onClose = [] { close(); };
            window.isDragArea = [](int x, int y) {
                float lx = x / layoutScale;
                float ly = y / layoutScale;
                for (const std::pair<float, float>& span : tabSpans) {
                    if (lx >= span.first && lx <= span.first + span.second) {
                        return false;
                    }
                }
                return ly < TITLE_H && (window.framed || lx < width() - 110.0f);
            };
            int w = (int) (START_W * layoutScale + 0.5f);
            int h = (int) (START_H * layoutScale + 0.5f);
#if defined(_WIN32)
            if (!window.create(logTitle, w, h, mainWindow->glContext)) {
                wglMakeCurrent(mainWindow->deviceContext, mainWindow->glContext);
                return;
            }
#else
            if (!window.create(Platform::narrow(logTitle), w, h, mainWindow)) {
                mainWindow->makeCurrent();
                return;
            }
#endif
            mergeRepeats = LaunchDefaults::mergeLogRepeats();
            wrapping = LaunchDefaults::wrapLogLines();
            context = ImGui::CreateContext(ImGui::GetIO().Fonts);
            ImFont* font = ImGui::GetIO().FontDefault;
            ImGui::SetCurrentContext(context);
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().FontDefault = font;
#if defined(_WIN32)
            ImGui::GetIO().MouseDoubleClickTime = (float) GetDoubleClickTime() / 1000.0f;
#endif
#if defined(_WIN32)
            ImGui_ImplWin32_InitForOpenGL(window.hwnd);
            ImGui_ImplOpenGL3_Init("#version 130");
#else
            window.initImGui();
#endif
            window.imguiContext = context;
            window.setVsync(false);
            ImGui::SetCurrentContext(main);
#if defined(_WIN32)
            wglMakeCurrent(mainWindow->deviceContext, mainWindow->glContext);
#else
            mainWindow->makeCurrent();
#endif
            created = true;
#if !defined(_WIN32)
            window.setIcon(Logo::iconPixels(64, Appearance::systemFrameRunning()), 64);
#endif
            window.onModalFrame = [] { frame(layoutScale); };
        }
    }

    void setIcons(void* large, void* compact) {
#if defined(_WIN32)
        window.setIcons((HICON) large, (HICON) compact);
#else
        (void) large;
        (void) compact;
        window.setIcon(Logo::iconPixels(64, Appearance::systemFrameRunning()), 64);
#endif
    }

    void setTitle(const std::wstring& title) {
        logTitle = L"游戏日志 - " + title;
#if defined(_WIN32)
        window.setTitle(logTitle);
#else
        window.setTitle(Platform::narrow(logTitle));
#endif
    }

    void init(ShellWindow* main) {
        mainWindow = main;
    }

    void open() {
        if (mainWindow == nullptr) {
            return;
        }
        if (!created) {
            create();
            if (!created) {
                return;
            }
        }
        hiddenAt = 0;
        shown = true;
#if defined(_WIN32)
        if (IsIconic(window.hwnd)) {
            ShowWindow(window.hwnd, SW_RESTORE);
            SetForegroundWindow(window.hwnd);
        } else if (!IsWindowVisible(window.hwnd)) {
            revealing = true;
        } else {
            SetForegroundWindow(window.hwnd);
        }
#else
        if (!window.visible()) {
            revealing = true;
        } else {
            window.raise();
        }
#endif
    }

    void close() {
        if (!created || !shown) {
            return;
        }
        shown = false;
        revealing = false;
        if (NewUi::textEditing == SEARCH_OWNER) {
            NewUi::cancelTextEdit();
        }
#if defined(_WIN32)
        ShowWindow(window.hwnd, SW_HIDE);
        hiddenAt = GetTickCount64();
#else
        window.hide();
        hiddenAt = SDL_GetTicks();
#endif
    }

    bool isOpen() {
        return shown;
    }

    void frame(float scale) {
        layoutScale = scale;
#if defined(_WIN32)
        if (created && !shown && hiddenAt != 0 && GetTickCount64() - hiddenAt > RELEASE_AFTER) {
            hiddenAt = 0;
            shutdown();
            return;
        }
        if (!created || !shown || IsIconic(window.hwnd)) {
            return;
        }
        ImGuiContext* previous = ImGui::GetCurrentContext();
        HGLRC previousGl = wglGetCurrentContext();
        HDC previousDc = wglGetCurrentDC();
#else
        if (created && !shown && hiddenAt != 0 && SDL_GetTicks() - hiddenAt > RELEASE_AFTER) {
            hiddenAt = 0;
            shutdown();
            return;
        }
        if (!created || !shown || window.minimized()) {
            return;
        }
        ImGuiContext* previous = ImGui::GetCurrentContext();
#endif
        NewUi::FrameState mainState = NewUi::saveFrameState();
        NewUi::restoreFrameState(state);
        NewUi::overlaysEnabled = false;
        float savedUiScale = UiRender::uiScale;

        ImGui::SetCurrentContext(context);
#if defined(_WIN32)
        wglMakeCurrent(window.deviceContext, window.glContext);
#else
        window.makeCurrent();
#endif
        if (UiTheme::isLight() == darkChrome) {
            darkChrome = !UiTheme::isLight();
            window.setDarkMode(darkChrome);
        }
#if defined(_WIN32)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        if (window.preparingResize()) {
            ImGui::GetIO().DisplaySize = ImVec2((float) window.width, (float) window.height);
        }
#else
        window.newFrame();
#endif
        ImGui::NewFrame();
        ImGuiIO& logIo = ImGui::GetIO();
        if (logIo.MouseDelta.x != 0.0f || logIo.MouseDelta.y != 0.0f || logIo.MouseWheel != 0.0f ||
            logIo.InputQueueCharacters.Size > 0 || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UiState::keepAnimating();
        }
        UiRender::uiScale = scale / (float) UiRender::guiScale();
        UiRender::begin(ImGui::GetForegroundDrawList());
        NewUi::render(draw);
        UiRender::end();
        if (NewUi::caretFrame == NewUi::frameCounter) {
            window.setTextArea(NewUi::caretScreenX, NewUi::caretScreenY, NewUi::caretScreenH, UiIme::wanted());
        } else {
            window.clearTextArea();
        }
        ImGui::Render();
#if !defined(_WIN32)
        window.setTyping(!NewUi::textEditing.empty());
#endif
        uint32_t clear = UiTheme::panel();
#if defined(_WIN32)
        glViewport(0, 0, window.width, window.height);
        glClearColor(((clear >> 16) & 0xFF) / 255.0f, ((clear >> 8) & 0xFF) / 255.0f, (clear & 0xFF) / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
#else
        UiGL::Viewport(0, 0, window.width, window.height);
        UiGL::ClearColor(((clear >> 16) & 0xFF) / 255.0f, ((clear >> 8) & 0xFF) / 255.0f, (clear & 0xFF) / 255.0f,
                         1.0f);
        UiGL::Clear(UGL_COLOR_BUFFER_BIT);
#endif
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window.swap();
        if (revealing && !window.preparingResize()) {
            revealing = false;
#if defined(_WIN32)
            ShowWindow(window.hwnd, SW_SHOW);
            SetForegroundWindow(window.hwnd);
#else
            window.raise();
#endif
        }

        UiRender::uiScale = savedUiScale;
        state = NewUi::saveFrameState();
        NewUi::restoreFrameState(mainState);
        NewUi::overlaysEnabled = true;
        ImGui::SetCurrentContext(previous);
#if defined(_WIN32)
        wglMakeCurrent(previousDc, previousGl);
#else
        mainWindow->makeCurrent();
#endif
    }

    void shutdown() {
        if (!created) {
            return;
        }
        window.onModalFrame = nullptr;
        ImGuiContext* previous = ImGui::GetCurrentContext();
#if defined(_WIN32)
        HGLRC previousGl = wglGetCurrentContext();
        HDC previousDc = wglGetCurrentDC();
        ImGui::SetCurrentContext(context);
        wglMakeCurrent(window.deviceContext, window.glContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
#else
        ImGui::SetCurrentContext(context);
        window.makeCurrent();
        window.shutdownImGui();
#endif
        ImGui::DestroyContext(context);
        context = nullptr;
        window.imguiContext = nullptr;
        ImGui::SetCurrentContext(previous);
#if defined(_WIN32)
        wglMakeCurrent(previousDc, previousGl);
#else
        mainWindow->makeCurrent();
#endif
        window.destroy();
        created = false;
        shown = false;
    }
}
