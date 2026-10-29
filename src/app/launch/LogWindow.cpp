#include "app/launch/LogWindow.h"
#include "app/shell/Logo.h"
#include "app/shell/BorderlessWindow.h"
#include "app/launch/GameLog.h"
#include "app/shell/Icons.h"
#include "app/shell/Page.h"
#include "app/system/Platform.h"

#include "ui/panel/NewUi.h"
#include "ui/panel/Notifications.h"
#include "ui/panel/UiCursor.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiInput.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"
#include "ui/value/Bind.h"
#include "ui/widget/IconButtonWidget.h"
#include "ui/widget/TextFieldWidget.h"

#include <core/base/strings.hpp>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

        BorderlessWindow* mainWindow = nullptr;
        BorderlessWindow window;
        std::wstring logTitle = L"游戏日志 - B Launcher";
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

        bool searchOpen = false;
        std::string searchText;
        std::string query;
        std::string typed;
        double typedAt = 0.0;
        bool refocus = false;
        std::deque<uint64_t> matches;
        int current = -1;

        bool selection = false;
        uint64_t anchor = 0;
        uint64_t caret = 0;
        bool selecting = false;

        float scroll = 0.0f;
        bool following = true;
        int columnsAside = 0;

        bool passes(GameLog::Level) {
            return true;
        }

        const GameLog::Line& line(uint64_t number) {
            static const GameLog::Line gone;
            if (number < GameLog::first() || number >= GameLog::end()) {
                return gone;
            }
            return GameLog::lines()[(size_t) (number - GameLog::first())];
        }

        bool containsQuery(const std::string& text) {
            if (query.empty()) {
                return false;
            }
            auto found = std::search(text.begin(), text.end(), query.begin(), query.end(),
                                     [](char a, char b) { return (char) std::tolower((unsigned char) a) == b; });
            return found != text.end();
        }

        void update() {
            if (GameLog::generation() != seenGeneration) {
                seenGeneration = GameLog::generation();
                rows.clear();
                matches.clear();
                current = -1;
                scanned = GameLog::first();
                selection = false;
                scroll = 0.0f;
                following = true;
            }
            uint64_t first = GameLog::first();
            size_t removed = 0;
            while (!rows.empty() && rows.front() < first) {
                rows.pop_front();
                removed++;
            }
            while (!matches.empty() && matches.front() < first) {
                matches.pop_front();
                current = std::max(-1, current - 1);
            }
            if (removed > 0 && !following) {
                scroll = std::max(0.0f, scroll - removed * ROW_H);
            }
            scanned = std::max(scanned, first);
            for (uint64_t n = scanned; n < GameLog::end(); n++) {
                const GameLog::Line& entry = line(n);
                if (passes(entry.level)) {
                    rows.push_back(n);
                    if (containsQuery(entry.text)) {
                        matches.push_back(n);
                    }
                }
            }
            scanned = GameLog::end();
        }

        void rebuild() {
            rows.clear();
            matches.clear();
            current = -1;
            scanned = GameLog::first();
            update();
        }

        size_t positionOf(uint64_t number) {
            return (size_t) (std::lower_bound(rows.begin(), rows.end(), number) - rows.begin());
        }

        float viewHeight();

        void reveal(uint64_t number) {
            float y = positionOf(number) * ROW_H;
            float view = viewHeight();
            if (y < scroll || y + ROW_H > scroll + view) {
                scroll = std::max(0.0f, y - view * 0.4f);
            }
            following = false;
        }

        void step(int direction) {
            if (matches.empty()) {
                return;
            }
            if (current < 0) {
                uint64_t top = rows.empty() ? 0 : rows[std::min(rows.size() - 1, (size_t) (scroll / ROW_H))];
                auto at = std::lower_bound(matches.begin(), matches.end(), top);
                current = (int) (at - matches.begin());
                if (direction < 0) {
                    current--;
                }
            } else {
                current += direction;
            }
            int count = (int) matches.size();
            current = ((current % count) + count) % count;
            reveal(matches[(size_t) current]);
        }

        void setQuery(const std::string& text) {
            std::string lower;
            for (char c : text) {
                lower += (char) std::tolower((unsigned char) c);
            }
            if (lower == query) {
                return;
            }
            query = lower;
            matches.clear();
            current = -1;
            if (!query.empty()) {
                for (uint64_t n : rows) {
                    if (containsQuery(line(n).text)) {
                        matches.push_back(n);
                    }
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

        void copySelection() {
            if (rows.empty()) {
                return;
            }
            uint64_t from = selection ? std::min(anchor, caret) : rows.front();
            uint64_t to = selection ? std::max(anchor, caret) : rows.back();
            std::string text;
            for (size_t i = positionOf(from); i < rows.size() && rows[i] <= to; i++) {
                text += line(rows[i]).text;
                text += '\n';
            }
            ImGui::SetClipboardText(text.c_str());
            size_t count = (size_t) std::count(text.begin(), text.end(), '\n');
            Notifications::add(Notifications::DONE, "已复制 " + std::to_string(count) + " 行日志", 2000);
        }

        void exportAll() {
            std::string name = GameLog::title().empty() ? std::string("game") : GameLog::title();
            std::optional<std::filesystem::path> target =
                    Platform::pickSaveFile(Platform::exeDirectory() / b_launcher_base::path_from_utf8(name + ".log"),
                                           L"导出游戏日志", window.hwnd);
            if (!target) {
                return;
            }
            std::ofstream out(*target, std::ios::binary | std::ios::trunc);
            for (const GameLog::Line& entry : GameLog::lines()) {
                out << entry.text << '\n';
            }
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

        void titleBar() {
            float w = width();
            Logo::draw(PAD + 2.0f, (TITLE_H - 18.0f) * 0.5f, 18.0f);
            float x = PAD + 28.0f;
            const char* title = "游戏日志";
            UiRender::textY(title, x, 0.0f, TITLE_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_BODY);
            x += UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_BODY) + 10.0f;
            if (!GameLog::title().empty()) {
                UiRender::textY(
                        UiFont::fitEnd(GameLog::title(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, w - 130.0f - x), x,
                        0.0f, TITLE_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
            float size = 26.0f;
            float y = (TITLE_H - size) * 0.5f;
            float bx = w - 14.0f - size;
            if (iconButton("log#close", 0xf00d, "关闭", bx, y, size)) {
                close();
            }
            bx -= size + 4.0f;
            bool zoomed = IsZoomed(window.hwnd) != FALSE;
            if (iconButton("log#maximize", zoomed ? 0xf2d2 : 0xf2d0, zoomed ? "还原" : "最大化", bx, y, size)) {
                ShowWindow(window.hwnd, zoomed ? SW_RESTORE : SW_MAXIMIZE);
            }
            bx -= size + 4.0f;
            if (iconButton("log#minimize", 0xf068, "最小化", bx, y, size)) {
                ShowWindow(window.hwnd, SW_MINIMIZE);
            }
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
            float actions = BAR_BUTTON * 5.0f + 2.0f * 4.0f;
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
            std::string found = query.empty() ? std::string()
                    : matches.empty()         ? std::string("无匹配")
                                              : std::to_string(current + 1) + "/" + withCommas(matches.size());
            float foundW = found.empty() ? 0.0f : UiFont::width(found, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            TextFieldWidget field(searchValue());
            field.box(x, fieldY, SEARCH_FIELD_W, Page::FIELD_H).bounds(x, fieldY, SEARCH_FIELD_W, Page::FIELD_H);
            field.room(found.empty() ? 0.0f : foundW + 8.0f);
            field.paint();
            if (!found.empty()) {
                UiRender::textY(found, x + SEARCH_FIELD_W - 9.0f - foundW, fieldY, Page::FIELD_H,
                                matches.empty() ? UiTheme::danger() : UiTheme::textMuted(), UiFont::MEDIUM,
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
            if (barButton("log#copy", 0xf0c5, selection ? "复制选中的行（Ctrl+C）" : "复制全部", x, y, false)) {
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

        std::string_view columnsOf(std::string_view text, int skip, int columns, int& skipped) {
            size_t i = 0;
            int column = 0;
            size_t start = std::string_view::npos;
            skipped = 0;
            while (i < text.size()) {
                unsigned char c = (unsigned char) text[i];
                size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
                int wide = length >= 3 ? 2 : 1;
                if (start == std::string_view::npos && column >= skip) {
                    start = i;
                    skipped = column;
                }
                if (start != std::string_view::npos && column - skipped >= columns) {
                    return text.substr(start, i - start);
                }
                column += wide;
                i += length;
            }
            return start == std::string_view::npos ? std::string_view() : text.substr(start);
        }

        int columnsBefore(std::string_view text, size_t bytes) {
            int column = 0;
            size_t i = 0;
            while (i < bytes && i < text.size()) {
                unsigned char c = (unsigned char) text[i];
                size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
                column += length >= 3 ? 2 : 1;
                i += length;
            }
            return column;
        }

        size_t rowAt(float y, float top) {
            float position = (y - top + scroll) / ROW_H;
            if (rows.empty()) {
                return 0;
            }
            return (size_t) std::clamp((long long) std::floor(position), 0LL, (long long) rows.size() - 1);
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
            float textX = left + 10.0f;
            float textRight = right - 14.0f;
            float charW = std::max(1.0f, UiFont::width("0", UiFont::CODE, TEXT_SIZE));
            int columns = (int) ((textRight - textX) / charW) + 2;
            float contentH = rows.size() * ROW_H;
            float maxScroll = std::max(0.0f, contentH - view);

            ImGuiIO& io = ImGui::GetIO();
            bool over = NewUi::hit(left, top, right - left, bottom - top);
            if (over && io.MouseWheel != 0.0f) {
                if (io.KeyShift) {
                    columnsAside = std::max(0, columnsAside - (int) std::lround(io.MouseWheel * 8.0f));
                } else {
                    scroll -= io.MouseWheel * ROW_H * 3.0f;
                    following = io.MouseWheel < 0.0f && scroll >= maxScroll;
                }
            }
            if (over && io.MouseWheelH != 0.0f) {
                columnsAside = std::max(0, columnsAside + (int) std::lround(io.MouseWheelH * 8.0f));
            }
            if (following) {
                scroll = maxScroll;
            }
            scroll = UiRender::clamp(scroll, 0.0f, maxScroll);

            bool underBar = barBounds().contains();
            bool overLines = !underBar && NewUi::hit(left, viewTop, right - left - 14.0f, view);
            if (overLines || selecting) {
                UiCursor::text();
            }
            if (!rows.empty() && overLines && NewUi::click(left, viewTop, right - left - 14.0f, view)) {
                uint64_t number = rows[rowAt(NewUi::mouseY, viewTop)];
                if (io.KeyShift && selection) {
                    caret = number;
                } else {
                    anchor = number;
                    caret = number;
                }
                selection = true;
                selecting = true;
            }
            if (selecting) {
                if (!NewUi::mouseDown || rows.empty()) {
                    selecting = false;
                } else {
                    if (NewUi::mouseY < viewTop) {
                        scroll = std::max(0.0f, scroll - ROW_H);
                        following = false;
                    } else if (NewUi::mouseY > viewTop + view) {
                        scroll = std::min(maxScroll, scroll + ROW_H);
                    }
                    caret = rows[rowAt(UiRender::clamp(NewUi::mouseY, viewTop, viewTop + view - 1.0f), viewTop)];
                }
            }

            uint64_t low = std::min(anchor, caret);
            uint64_t high = std::max(anchor, caret);
            uint64_t currentLine = current >= 0 && current < (int) matches.size() ? matches[(size_t) current] : ~0ull;
            UiRender::scissor(left + 1.0f, viewTop, right - left - 2.0f, view);
            float shownScroll = UiRender::snapLayout(scroll);
            size_t firstRow = (size_t) (shownScroll / ROW_H);
            float y = viewTop - std::fmod(shownScroll, ROW_H);
            for (size_t i = firstRow; i < rows.size() && y < viewTop + view; i++, y += ROW_H) {
                uint64_t number = rows[i];
                const GameLog::Line& entry = line(number);
                if (selection && number >= low && number <= high) {
                    UiRender::rect(left + 2.0f, y, right - left - 4.0f, ROW_H, UiTheme::accentSoft(90));
                    UiRender::rect(left + 2.0f, y, 3.0f, ROW_H, UiTheme::accent());
                }
                int skipped = 0;
                std::string_view shownText = columnsOf(entry.text, columnsAside, columns, skipped);
                if (!query.empty() && containsQuery(entry.text)) {
                    uint32_t tint = (amber() & 0x00FFFFFFu) | (number == currentLine ? 0x90000000u : 0x40000000u);
                    std::string lower;
                    lower.reserve(entry.text.size());
                    for (char c : entry.text) {
                        lower += (char) std::tolower((unsigned char) c);
                    }
                    for (size_t at = lower.find(query); at != std::string::npos;
                         at = lower.find(query, at + query.size())) {
                        int from = columnsBefore(entry.text, at) - skipped;
                        int to = columnsBefore(entry.text, at + query.size()) - skipped;
                        if (to <= 0 || from >= columns) {
                            continue;
                        }
                        float x0 = textX + std::max(0, from) * charW;
                        float x1 = textX + std::min(columns, to) * charW;
                        UiRender::roundRect(x0 - 1.0f, y + 1.0f, x1 - x0 + 2.0f, ROW_H - 2.0f, 3.0f, tint);
                    }
                }
                if (!shownText.empty()) {
                    UiRender::textY(std::string(shownText), textX, y, ROW_H, levelColor(entry.level), UiFont::CODE,
                                    TEXT_SIZE);
                }
            }
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
                size_t from = positionOf(std::min(anchor, caret));
                size_t to = positionOf(std::max(anchor, caret));
                text += "，已选 " + withCommas(to - from + 1) + " 行";
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
                anchor = rows.front();
                caret = rows.back();
                selection = true;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                copySelection();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (searchOpen) {
                    closeSearch();
                } else {
                    selection = false;
                }
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
            window.quitOnDestroy = false;
            window.onClose = [] { close(); };
            window.isDragArea = [](int x, int y) {
                float lx = x / layoutScale;
                float ly = y / layoutScale;
                return ly < TITLE_H && lx < width() - 110.0f;
            };
            int w = (int) (START_W * layoutScale + 0.5f);
            int h = (int) (START_H * layoutScale + 0.5f);
            if (!window.create(logTitle, w, h, mainWindow->glContext)) {
                wglMakeCurrent(mainWindow->deviceContext, mainWindow->glContext);
                return;
            }
            context = ImGui::CreateContext(ImGui::GetIO().Fonts);
            ImFont* font = ImGui::GetIO().FontDefault;
            ImGui::SetCurrentContext(context);
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().FontDefault = font;
            ImGui_ImplWin32_InitForOpenGL(window.hwnd);
            ImGui_ImplOpenGL3_Init("#version 130");
            window.imguiContext = context;
            window.setVsync(false);
            ImGui::SetCurrentContext(main);
            wglMakeCurrent(mainWindow->deviceContext, mainWindow->glContext);
            created = true;
        }
    }

    void setIcons(void* large, void* compact) {
        window.setIcons((HICON) large, (HICON) compact);
    }

    void setTitle(const std::wstring& title) {
        logTitle = L"游戏日志 - " + title;
        window.setTitle(logTitle);
    }

    void init(BorderlessWindow* main) {
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
        shown = true;
        if (IsIconic(window.hwnd)) {
            ShowWindow(window.hwnd, SW_RESTORE);
            SetForegroundWindow(window.hwnd);
        } else if (!IsWindowVisible(window.hwnd)) {
            revealing = true;
        } else {
            SetForegroundWindow(window.hwnd);
        }
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
        ShowWindow(window.hwnd, SW_HIDE);
    }

    bool isOpen() {
        return shown;
    }

    void frame(float scale) {
        layoutScale = scale;
        if (!created || !shown || IsIconic(window.hwnd)) {
            return;
        }
        ImGuiContext* previous = ImGui::GetCurrentContext();
        HGLRC previousGl = wglGetCurrentContext();
        HDC previousDc = wglGetCurrentDC();
        NewUi::FrameState mainState = NewUi::saveFrameState();
        NewUi::restoreFrameState(state);
        NewUi::overlaysEnabled = false;
        float savedUiScale = UiRender::uiScale;

        ImGui::SetCurrentContext(context);
        wglMakeCurrent(window.deviceContext, window.glContext);
        if (UiTheme::isLight() == darkChrome) {
            darkChrome = !UiTheme::isLight();
            window.setDarkMode(darkChrome);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
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
        ImGui::Render();
        glViewport(0, 0, window.width, window.height);
        uint32_t clear = UiTheme::panel();
        glClearColor(((clear >> 16) & 0xFF) / 255.0f, ((clear >> 8) & 0xFF) / 255.0f, (clear & 0xFF) / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window.swap();
        if (revealing) {
            revealing = false;
            ShowWindow(window.hwnd, SW_SHOW);
            SetForegroundWindow(window.hwnd);
        }

        UiRender::uiScale = savedUiScale;
        state = NewUi::saveFrameState();
        NewUi::restoreFrameState(mainState);
        NewUi::overlaysEnabled = true;
        ImGui::SetCurrentContext(previous);
        wglMakeCurrent(previousDc, previousGl);
    }

    void shutdown() {
        if (!created) {
            return;
        }
        ImGuiContext* previous = ImGui::GetCurrentContext();
        HGLRC previousGl = wglGetCurrentContext();
        HDC previousDc = wglGetCurrentDC();
        ImGui::SetCurrentContext(context);
        wglMakeCurrent(window.deviceContext, window.glContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(context);
        context = nullptr;
        window.imguiContext = nullptr;
        ImGui::SetCurrentContext(previous);
        wglMakeCurrent(previousDc, previousGl);
        window.destroy();
        created = false;
        shown = false;
    }
}
