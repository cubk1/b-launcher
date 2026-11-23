#include "ui/imgui/screens/instances/InstancePackUpdate.h"
#include "app/downloads/Projects.h"
#include "app/instances/PackUpdates.h"
#include "app/launch/Game.h"
#include "app/system/Platform.h"
#include "ui/imgui/screens/downloads/DownloadsParts.h"
#include "ui/imgui/screens/instances/InstanceScreenshots.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace InstancePackUpdate {

    using PackUpdates::Change;
    using PackUpdates::Phase;

    namespace {
        constexpr float LIST_ROW = 34.0f;
        constexpr float FOLDER_ROW = 30.0f;
        constexpr float INDENT = 16.0f;
        constexpr float HEAD_H = 30.0f;
        constexpr float SIZE_W = 60.0f;
        constexpr float GUTTER_W = 14.0f;
        constexpr float PATH_H = 36.0f;
        constexpr float KEEP_W = 48.0f;
        constexpr float BOX = 16.0f;
        constexpr float ICON = 24.0f;
        constexpr float STEP_H = 46.0f;

        std::string chosenKey;
        std::string chosenId;
        bool wantPlan = false;
        bool wasApplying = false;

        struct Node {
            std::string name;
            std::string path;
            std::optional<PackUpdates::Item> item;
            std::map<std::string, Node> dirs;
            std::map<std::string, Node> files;
            int added = 0;
            int updated = 0;
            int removed = 0;
            int modified = 0;
            uint64_t bytes = 0;
            std::vector<std::string> local;
        };

        struct Folder {
            const Node* node;
            int depth;
        };

        enum Kind { ADD, UPDATE, REMOVE, MODIFIED };
        enum class Mark { Off, Partial, On };

        Node tree;
        uint64_t builtSerial = 0;
        std::map<std::string, const Node*> folderIndex;
        std::set<std::string> opened;
        std::set<std::string> kept;
        std::string selected;
        std::string measuredFolder;
        uint64_t measuredSerial = 0;
        float measuredChange = 0.0f;
        float slotWidths[4] = {};

        bool release(const Addons::Version& version) {
            return version.type.empty() || version.type == "release";
        }

        std::string labelOf(const Addons::Version& version) {
            return version.number.empty() ? version.name : version.number;
        }

        const Addons::Version* newest(const Addons::Versions& versions, const PackUpdates::Origin& origin) {
            bool stable = true;
            for (const Addons::Version& version : versions.list) {
                if (version.id == origin.version) {
                    stable = release(version);
                }
            }
            const Addons::Version* best = nullptr;
            for (const Addons::Version& version : versions.list) {
                if ((!stable || release(version)) && (best == nullptr || version.published > best->published)) {
                    best = &version;
                }
            }
            return best;
        }

        bool mine(const std::string& key) {
            return PackUpdates::state().instanceKey == key;
        }

        uint32_t tint(uint32_t ink) {
            return (ink & 0x00FFFFFFu) | 0x2E000000u;
        }

        std::string joined(const std::vector<std::string>& parts) {
            std::string out;
            for (const std::string& part : parts) {
                out += (out.empty() ? "" : "、") + part;
            }
            return out;
        }

        std::string fileName(const std::string& path) {
            auto slash = path.rfind('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::string parentOf(const std::string& path) {
            auto slash = path.rfind('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }

        std::string fitMiddle(const std::string& text, float available) {
            if (UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) <= available) {
                return text;
            }
            std::vector<size_t> starts;
            for (size_t i = 0; i < text.size(); i++) {
                if (((unsigned char) text[i] & 0xC0) != 0x80) {
                    starts.push_back(i);
                }
            }
            size_t count = starts.size();
            starts.push_back(text.size());
            const std::string dots = "…";
            float room = available - UiFont::width(dots, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (room <= 0.0f || count == 0) {
                return dots;
            }
            auto measure = [](const std::string& part) {
                return UiFont::width(part, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            };
            size_t tail = 0;
            while (tail < count && measure(text.substr(starts[count - tail - 1])) <= room * 0.45f) {
                tail++;
            }
            std::string back = text.substr(starts[count - tail]);
            float backW = measure(back);
            size_t head = 0;
            while (head < count - tail && measure(text.substr(0, starts[head + 1])) + backW <= room) {
                head++;
            }
            return text.substr(0, starts[head]) + dots + back;
        }

        uint32_t inkOf(int kind) {
            switch (kind) {
                case ADD: return UiTheme::accentText();
                case UPDATE: return UiTheme::textSecondary();
                case REMOVE: return UiTheme::danger();
                default: return UiTheme::warningInk();
            }
        }

        float pillWidth(const std::string& text) {
            return UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 14.0f;
        }

        int kindOf(const PackUpdates::Item& item) {
            return item.modified                    ? MODIFIED
                    : item.change == Change::Add    ? ADD
                    : item.change == Change::Update ? UPDATE
                                                    : REMOVE;
        }

        const char* labelOf(int kind) {
            return kind == MODIFIED ? "本地有修改" : kind == ADD ? "新增" : kind == UPDATE ? "更新" : "删除";
        }

        int glyphOf(int kind) {
            return kind == ADD ? 0xf067 : kind == UPDATE ? 0xf021 : kind == REMOVE ? 0xf068 : 0xf044;
        }

        float glyphSize(int kind) {
            return kind == UPDATE ? 9.0f : kind == MODIFIED ? 8.5f : 10.0f;
        }

        float countWidth(int amount) {
            return UiFont::width(std::to_string(amount), UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 30.0f;
        }

        float countPill(int kind, int amount, float x, float cy, bool blocked) {
            constexpr float h = 20.0f;
            float w = countWidth(amount);
            float top = cy - h * 0.5f;
            uint32_t ink = inkOf(kind);
            std::string text = std::to_string(amount);
            UiRender::roundRect(x, top, w, h, 6.0f, tint(ink));
            UiWidgets::iconBeside(Ui::glyph(glyphOf(kind)), x + 12.0f, text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, top,
                                  h, ink, glyphSize(kind));
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(text, x + w - 8.0f - textW, top, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (!blocked && NewUi::hit(x, top, w, h)) {
                Page::tooltip(std::string(labelOf(kind)) + " " + std::to_string(amount) + " 个文件", x, cy - h * 0.5f,
                              w, h);
            }
            return w;
        }

        float countsWidth(const Node& node) {
            const int amounts[] = {node.added, node.updated, node.removed, node.modified};
            float used = 0.0f;
            for (int amount : amounts) {
                if (amount > 0) {
                    used += countWidth(amount) + 4.0f;
                }
            }
            return std::max(0.0f, used - 4.0f);
        }

        void counts(const Node& node, float x, float end, float cy, bool blocked) {
            const int amounts[] = {node.added, node.updated, node.removed, node.modified};
            for (int kind = ADD; kind <= MODIFIED; kind++) {
                if (amounts[kind] > 0 && x + countWidth(amounts[kind]) <= end) {
                    x += countPill(kind, amounts[kind], x, cy, blocked) + 4.0f;
                }
            }
        }

        constexpr float SLOT_GAP = 14.0f;

        float cellWidth(int amount) {
            return UiFont::width(std::to_string(amount), UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 16.0f;
        }

        void cell(int kind, int amount, float x, float cy, bool blocked) {
            constexpr float h = 20.0f;
            float top = cy - h * 0.5f;
            uint32_t ink = inkOf(kind);
            std::string text = std::to_string(amount);
            UiWidgets::iconBeside(Ui::glyph(glyphOf(kind)), x + 5.0f, text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, top,
                                  h, ink, glyphSize(kind));
            UiRender::textY(text, x + 16.0f, top, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float w = cellWidth(amount);
            if (!blocked && NewUi::hit(x - 4.0f, top, w + 8.0f, h)) {
                Page::tooltip(std::string(labelOf(kind)) + " " + text + " 个文件", x, top, w, h);
            }
        }

        void slotted(const Node& node, float x, float end, float cy, bool blocked) {
            const int amounts[] = {node.added, node.updated, node.removed, node.modified};
            for (int kind = ADD; kind <= MODIFIED; kind++) {
                if (slotWidths[kind] <= 0.0f) {
                    continue;
                }
                if (x + slotWidths[kind] > end) {
                    break;
                }
                if (amounts[kind] > 0) {
                    cell(kind, amounts[kind], x, cy, blocked);
                }
                x += slotWidths[kind] + SLOT_GAP;
            }
        }

        float pill(const std::string& text, float x, float cy, int kind) {
            constexpr float h = 20.0f;
            float w = pillWidth(text);
            uint32_t ink = inkOf(kind);
            UiRender::roundRect(x, cy - h * 0.5f, w, h, 6.0f, tint(ink));
            UiRender::textY(text, x + 7.0f, cy - h * 0.5f, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        void checkbox(float x, float cy, Mark mark, float hover) {
            float y = cy - BOX * 0.5f;
            if (mark == Mark::Off) {
                UiRender::outline(x, y, BOX, BOX, 4.5f, UiRender::mix(UiTheme::outline(), UiTheme::textMuted(), hover),
                                  0xF, 1.4f);
                return;
            }
            UiRender::roundRect(x, y, BOX, BOX, 4.5f,
                                UiRender::mix(UiTheme::accent(), UiTheme::accentHover(), 0.12f * hover));
            if (mark == Mark::On) {
                UiWidgets::icon(Ui::glyph(0xf00c), x + BOX * 0.5f, cy, UiTheme::onAccent(), 9.0f);
            } else {
                UiRender::roundRect(x + 4.0f, cy - 1.0f, BOX - 8.0f, 2.0f, 1.0f, UiTheme::onAccent());
            }
        }

        Mark markOf(const std::vector<std::string>& paths) {
            size_t on = 0;
            for (const std::string& path : paths) {
                on += kept.contains(path) ? 1 : 0;
            }
            return on == 0 ? Mark::Off : on == paths.size() ? Mark::On : Mark::Partial;
        }

        void flip(const std::vector<std::string>& paths) {
            bool keep = markOf(paths) != Mark::On;
            for (const std::string& path : paths) {
                if (keep) {
                    kept.insert(path);
                } else {
                    kept.erase(path);
                }
            }
        }

        bool pending(const PackUpdates::Summary& summary) {
            if (summary.added + summary.changed + summary.removed > 0 || summary.loadersChanged) {
                return true;
            }
            return std::any_of(summary.items.begin(), summary.items.end(), [](const PackUpdates::Item& item) {
                return item.modified && !kept.contains(item.path);
            });
        }

        uint64_t downloadBytes(const PackUpdates::Summary& summary) {
            uint64_t bytes = summary.bytes;
            for (const PackUpdates::Item& item : summary.items) {
                if (item.modified && item.change != Change::Remove && !kept.contains(item.path)) {
                    bytes += item.bytes;
                }
            }
            return bytes;
        }

        void tally(Node& node, const PackUpdates::Item& item) {
            node.bytes += item.change == Change::Remove ? 0 : item.bytes;
            if (item.modified) {
                node.modified++;
                node.local.push_back(item.path);
            } else if (item.change == Change::Add) {
                node.added++;
            } else if (item.change == Change::Update) {
                node.updated++;
            } else {
                node.removed++;
            }
        }

        void index(const Node& node) {
            folderIndex[node.path] = &node;
            for (const auto& [name, dir] : node.dirs) {
                index(dir);
            }
        }

        void rebuild(const PackUpdates::State& state) {
            tree = Node{};
            folderIndex.clear();
            opened = {std::string()};
            kept.clear();
            selected.clear();
            builtSerial = state.serial;
            for (const PackUpdates::Item& item : state.summary.items) {
                if (item.modified && item.change == Change::Remove) {
                    kept.insert(item.path);
                }
                Node* at = &tree;
                tally(*at, item);
                size_t start = 0;
                for (;;) {
                    size_t slash = item.path.find('/', start);
                    if (slash == std::string::npos) {
                        Node& leaf = at->files[item.path.substr(start)];
                        leaf.name = item.path.substr(start);
                        leaf.path = item.path;
                        leaf.item = item;
                        break;
                    }
                    std::string name = item.path.substr(start, slash - start);
                    Node& dir = at->dirs[name];
                    dir.name = name;
                    dir.path = item.path.substr(0, slash);
                    tally(dir, item);
                    at = &dir;
                    start = slash + 1;
                }
            }
            index(tree);
        }

        void select(const std::string& path) {
            selected = path;
            std::string at = path;
            opened.insert(std::string());
            while (!at.empty()) {
                opened.insert(at);
                at = parentOf(at);
            }
        }

        void folders(const Node& node, int depth, std::vector<Folder>& out) {
            for (const auto& [name, dir] : node.dirs) {
                out.push_back({&dir, depth});
                if (opened.contains(dir.path)) {
                    folders(dir, depth + 1, out);
                }
            }
        }

        int changesOf(const Node& node) {
            return node.added + node.updated + node.removed + node.modified;
        }

        bool rowHover(const std::string& id, float x, float y, float w, float h, float top, bool blocked, int corners,
                      uint32_t base = 0) {
            bool hovered = !blocked && NewUi::hit(x, std::max(y, top), w, h - std::max(0.0f, top - y));
            float lit = UiState::motion(id, hovered ? 1.0f : 0.0f, 20.0f);
            if (base != 0) {
                UiRender::roundRect(x, y, w, h, Page::CARD_RADIUS, base, corners);
            }
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, w, h, Page::CARD_RADIUS, UiTheme::overlay(Ui::round(12 * lit)), corners);
            }
            return hovered;
        }

        void folderPane(float x, float top, float bottom, float w, const std::string& key, bool blocked) {
            std::vector<Folder> rows{{&tree, 0}};
            if (opened.contains(std::string())) {
                folders(tree, 1, rows);
            }
            int count = (int) rows.size();
            float gutter = count * FOLDER_ROW > bottom - top ? GUTTER_W : 0.0f;
            Page::Scroll scroll("packupdate#folders#" + key, top, bottom, count * FOLDER_ROW, x, w - gutter);
            bool outside = scroll.begin();
            for (int i = 0; i < count; i++) {
                float rowY = scroll.top - scroll.offset + i * FOLDER_ROW;
                float rowW = gutter > 0.0f ? w - gutter + 4.0f : w;
                if (rowY + FOLDER_ROW < top || rowY > bottom) {
                    continue;
                }
                const Node& node = *rows[i].node;
                bool root = rows[i].node == &tree;
                bool current = node.path == selected;
                float ix = x + 8.0f + rows[i].depth * INDENT;
                float cy = rowY + FOLDER_ROW * 0.5f;
                bool hovered = rowHover("packupdate#folder#" + node.path, x, rowY, rowW, FOLDER_ROW, top,
                                        blocked || outside, 0, current ? UiTheme::accentSoft(48) : 0u);
                bool open = opened.contains(node.path);
                bool nested = !node.dirs.empty();
                if (nested) {
                    UiWidgets::icon(Ui::glyph(open ? 0xf078 : 0xf054), ix + 6.0f, cy, UiTheme::textMuted(), 8.0f);
                }
                UiWidgets::icon(Ui::glyph(open && nested ? 0xf07c : 0xf07b), ix + 24.0f, cy, UiTheme::accent(), 12.0f);
                std::string amount = std::to_string(changesOf(node));
                float amountW = UiFont::width(amount, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(amount, x + rowW - 12.0f - amountW, rowY, FOLDER_ROW, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                std::string full = root ? std::string("全部文件") : node.name;
                std::string name = fitMiddle(full, std::max(20.0f, x + rowW - 22.0f - amountW - ix - 38.0f));
                UiRender::textY(name, ix + 38.0f, rowY, FOLDER_ROW,
                                current ? UiTheme::accentText() : UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (hovered && name != full) {
                    Page::tooltip(full, ix + 38.0f, rowY, UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL),
                                  FOLDER_ROW);
                }
                if (hovered) {
                    UiCursor::hand();
                    if (NewUi::click(x, std::max(rowY, top), rowW, FOLDER_ROW)) {
                        if (nested && NewUi::mouseX < ix + 16.0f) {
                            if (!opened.erase(node.path)) {
                                opened.insert(node.path);
                            }
                        } else {
                            select(node.path);
                        }
                    }
                }
            }
            scroll.end();
        }

        struct Columns {
            float nameX;
            float nameRight;
            float sizeRight;
            float changeX;
            float changeEnd;
            float keepCx;
            bool keep;
        };

        float changeWidth(const Node& folder) {
            if (measuredFolder == folder.path && measuredSerial == builtSerial) {
                return measuredChange;
            }
            float need = UiFont::width("变化", UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            std::fill(std::begin(slotWidths), std::end(slotWidths), 0.0f);
            for (const auto& [name, dir] : folder.dirs) {
                const int amounts[] = {dir.added, dir.updated, dir.removed, dir.modified};
                for (int kind = ADD; kind <= MODIFIED; kind++) {
                    if (amounts[kind] > 0) {
                        slotWidths[kind] = std::max(slotWidths[kind], cellWidth(amounts[kind]));
                    }
                }
            }
            float slots = 0.0f;
            for (float slot : slotWidths) {
                slots += slot > 0.0f ? slot + SLOT_GAP : 0.0f;
            }
            need = std::max(need, slots - SLOT_GAP);
            for (const auto& [name, file] : folder.files) {
                need = std::max(need, pillWidth(labelOf(kindOf(*file.item))));
            }
            measuredFolder = folder.path;
            measuredSerial = builtSerial;
            measuredChange = need;
            return need;
        }

        float insetOf(float gutter) {
            return gutter > 0.0f ? gutter + 8.0f : 16.0f;
        }

        Columns columns(float x, float w, bool keep, float change, float gutter) {
            Columns c;
            float right = x + w - insetOf(gutter);
            c.keep = keep;
            c.keepCx = right - KEEP_W * 0.5f;
            c.changeEnd = keep ? right - KEEP_W : right;
            c.changeX = c.changeEnd - std::min(change, w * 0.34f);
            c.sizeRight = c.changeX - 16.0f;
            c.nameX = x + 16.0f;
            c.nameRight = c.sizeRight - SIZE_W - 12.0f;
            return c;
        }

        bool keepBox(const std::string& id, const std::vector<std::string>& paths, float cx, float cy, float h,
                     bool blocked) {
            float x = cx - BOX * 0.5f;
            bool hovered = !blocked && NewUi::hit(cx - KEEP_W * 0.5f, cy - h * 0.5f, KEEP_W, h);
            checkbox(x, cy, markOf(paths), UiState::motion(id, hovered ? 1.0f : 0.0f, 18.0f));
            if (!hovered) {
                return false;
            }
            UiCursor::hand();
            if (NewUi::click(cx - KEEP_W * 0.5f, cy - h * 0.5f, KEEP_W, h)) {
                flip(paths);
            }
            return true;
        }

        void sizeCell(uint64_t bytes, const Columns& c, float rowY) {
            if (bytes == 0) {
                return;
            }
            std::string size = InstanceScreenshots::sizeText(bytes);
            float sizeW = UiFont::width(size, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY(size, c.sizeRight - sizeW, rowY, LIST_ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
        }

        float nameCell(int glyph, uint32_t iconInk, const std::string& text, uint32_t ink, const Columns& c, float rowY,
                       bool hot = false, const std::string& extra = {}) {
            float cy = rowY + LIST_ROW * 0.5f;
            UiWidgets::icon(Ui::glyph(glyph), c.nameX + 8.0f, cy, iconInk, 12.0f);
            std::string shown = fitMiddle(text, std::max(20.0f, c.nameRight - c.nameX - 26.0f));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, c.nameX + 26.0f, rowY, LIST_ROW, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            bool cut = shown != text;
            bool hidden = !extra.empty() && c.nameX + 26.0f + shownW + 50.0f >= c.nameRight;
            if (hot && (cut || hidden) && NewUi::hit(c.nameX, rowY, c.nameRight - c.nameX, LIST_ROW)) {
                Page::tooltip(hidden ? text + "，" + extra : text, c.nameX + 26.0f, rowY, shownW, LIST_ROW);
            }
            return c.nameX + 26.0f + shownW;
        }

        void pathBar(float x, float y, float w, float reserve, bool blocked) {
            std::vector<std::pair<std::string, std::string>> parts{{"全部文件", std::string()}};
            size_t start = 0;
            while (!selected.empty() && start <= selected.size()) {
                size_t slash = selected.find('/', start);
                size_t end = slash == std::string::npos ? selected.size() : slash;
                parts.push_back({selected.substr(start, end - start), selected.substr(0, end)});
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }
            constexpr float GAP = 22.0f;
            float limit = w - 32.0f - reserve;
            size_t first = parts.size() - 1;
            float used = UiFont::width(parts.back().first, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            while (first > 0) {
                float next = UiFont::width(parts[first - 1].first, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + GAP;
                if (used + next > limit) {
                    break;
                }
                used += next;
                first--;
            }
            float px = x + 16.0f;
            float cy = y + PATH_H * 0.5f;
            if (first > 0) {
                UiRender::textY("…", px, y, PATH_H, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                px += UiFont::width("…", UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                UiWidgets::icon(Ui::glyph(0xf054), px + 3.0f, cy, UiTheme::textFaint(), 7.0f);
                px += 14.0f;
            }
            for (size_t i = first; i < parts.size(); i++) {
                bool last = i + 1 == parts.size();
                std::string label = last ? fitMiddle(parts[i].first, std::max(20.0f, x + w - 16.0f - reserve - px))
                                         : parts[i].first;
                float labelW = UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                bool hovered = !last && !blocked && NewUi::hit(px - 4.0f, y + 4.0f, labelW + 8.0f, PATH_H - 8.0f);
                UiRender::textY(label, px, y, PATH_H,
                                last ? UiTheme::textPrimary()
                                     : (hovered ? UiTheme::accentText() : UiTheme::textSecondary()),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (hovered) {
                    UiCursor::hand();
                    if (NewUi::click(px - 4.0f, y + 4.0f, labelW + 8.0f, PATH_H - 8.0f)) {
                        select(parts[i].second);
                    }
                }
                px += labelW;
                if (!last) {
                    UiWidgets::icon(Ui::glyph(0xf054), px + 11.0f, cy, UiTheme::textFaint(), 7.0f);
                    px += GAP;
                }
            }
        }

        void listPane(float x, float top, float bottom, float w, const PackUpdates::Summary& summary,
                      const std::string& key, bool blocked) {
            auto found = folderIndex.find(selected);
            if (found == folderIndex.end()) {
                select(std::string());
                found = folderIndex.find(selected);
            }
            const Node& folder = *found->second;
            bool keepColumn = !folder.local.empty();
            bool up = !selected.empty();
            int lead = up ? 1 : 0;
            int count = lead + (int) folder.dirs.size() + (int) folder.files.size();
            float gutter = count * LIST_ROW > bottom - (top + PATH_H + HEAD_H + 1.0f) ? GUTTER_W : 0.0f;
            Columns c = columns(x, w, keepColumn, changeWidth(folder), gutter);

            float chipsW = countsWidth(folder);
            float chipsRight = x + w - insetOf(gutter);
            counts(folder, chipsRight - chipsW, chipsRight, top + PATH_H * 0.5f, blocked);
            pathBar(x, top, w, chipsW + insetOf(gutter) - 4.0f, blocked);
            float headY = top + PATH_H;
            UiRender::hLine(x, x + w, headY, UiTheme::rowDivider(UiTheme::outlineSoft()));
            UiRender::textY("名称", c.nameX, headY, HEAD_H, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float sizeW = UiFont::width("大小", UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            UiRender::textY("大小", c.sizeRight - sizeW, headY, HEAD_H, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            UiRender::textY("变化", c.changeX, headY, HEAD_H, UiTheme::textFaint(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            if (keepColumn) {
                float keepW = UiFont::width("保留", UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY("保留", c.keepCx - BOX * 0.5f - 6.0f - keepW, headY, HEAD_H, UiTheme::textFaint(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                keepBox("packupdate#keep#folder", folder.local, c.keepCx, headY + HEAD_H * 0.5f, HEAD_H, blocked);
            }
            float listTop = headY + HEAD_H;
            UiRender::hLine(x, x + w, listTop, UiTheme::rowDivider(UiTheme::outlineSoft()));
            listTop += 1.0f;

            Page::Scroll scroll("packupdate#list#" + key + "#" + selected, listTop, bottom, count * LIST_ROW, x,
                                w - gutter);
            bool outside = scroll.begin();
            bool inactive = blocked || outside;
            std::vector<const Node*> dirs;
            for (const auto& [name, dir] : folder.dirs) {
                dirs.push_back(&dir);
            }
            std::vector<const Node*> files;
            for (const auto& [name, file] : folder.files) {
                files.push_back(&file);
            }
            std::string navigate;
            bool navigateUp = false;
            for (int i = 0; i < count; i++) {
                float rowY = scroll.top - scroll.offset + i * LIST_ROW;
                if (rowY + LIST_ROW < listTop || rowY > bottom) {
                    continue;
                }
                float cy = rowY + LIST_ROW * 0.5f;
                if (i > 0) {
                    UiRender::hLine(x + 16.0f, x + w - 16.0f, rowY, UiTheme::rowDivider(UiTheme::outlineSoft()));
                }
                if (up && i == 0) {
                    bool hovered = rowHover("packupdate#up", x, rowY, w, LIST_ROW, listTop, inactive, 0);
                    nameCell(0xf062, UiTheme::textMuted(), "上一级", UiTheme::textSecondary(), c, rowY);
                    if (hovered) {
                        UiCursor::hand();
                        navigateUp = NewUi::click(x, std::max(rowY, listTop), w, LIST_ROW) || navigateUp;
                    }
                    continue;
                }
                int at = i - lead;
                if (at < (int) dirs.size()) {
                    const Node& dir = *dirs[at];
                    bool overBox = keepColumn && !dir.local.empty() &&
                            keepBox("packupdate#keep#" + dir.path, dir.local, c.keepCx, cy, LIST_ROW, inactive);
                    bool hovered = rowHover("packupdate#row#" + dir.path, x, rowY, w, LIST_ROW, listTop,
                                            inactive || overBox, 0);
                    nameCell(0xf07b, UiTheme::accent(), dir.name, UiTheme::textPrimary(), c, rowY, !inactive);
                    sizeCell(dir.bytes, c, rowY);
                    slotted(dir, c.changeX, c.changeEnd, cy, inactive);
                    if (hovered) {
                        UiCursor::hand();
                        if (NewUi::click(x, std::max(rowY, listTop), w, LIST_ROW)) {
                            navigate = dir.path;
                        }
                    }
                    continue;
                }
                const Node& file = *files[at - (int) dirs.size()];
                const PackUpdates::Item& item = *file.item;
                if (keepColumn && item.modified) {
                    keepBox("packupdate#keep#" + file.path, {file.path}, c.keepCx, cy, LIST_ROW, inactive);
                }
                int kind = kindOf(item);
                bool faded = item.change == Change::Remove && !(item.modified && kept.contains(item.path));
                std::string replaced = item.from.empty() ? std::string() : "替换 " + fileName(item.from);
                float end =
                        nameCell(0xf15b, UiTheme::textMuted(), file.name,
                                 faded ? UiTheme::textMuted() : UiTheme::textPrimary(), c, rowY, !inactive, replaced);
                if (!item.from.empty() && end + 50.0f < c.nameRight) {
                    std::string from =
                            UiFont::fitEnd(replaced, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, c.nameRight - end - 10.0f);
                    UiRender::textY(from, end + 10.0f, rowY, LIST_ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                sizeCell(item.change == Change::Remove ? 0 : item.bytes, c, rowY);
                pill(labelOf(kind), c.changeX, cy, kind);
            }
            scroll.end();
            if (navigateUp) {
                select(parentOf(selected));
            } else if (!navigate.empty()) {
                select(navigate);
            }
        }

        const Addons::Version* versionOf(const Addons::Versions& versions, const std::string& id) {
            for (const Addons::Version& version : versions.list) {
                if (version.id == id) {
                    return &version;
                }
            }
            return nullptr;
        }

        std::shared_ptr<ModeBinding> versionChoice(const PackUpdates::Origin& origin,
                                                   const Addons::Versions& versions) {
            auto options = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
            std::set<std::string> used;
            for (const Addons::Version& version : versions.list) {
                std::string label = labelOf(version);
                if (!release(version)) {
                    label += version.type == "alpha" ? "（预览版）" : "（测试版）";
                }
                if (version.id == origin.version) {
                    label += "（当前）";
                }
                std::string unique = label;
                for (int n = 2; used.contains(unique); n++) {
                    unique = label + " " + std::to_string(n);
                }
                used.insert(unique);
                options->push_back({unique, version.id});
            }
            return std::make_shared<ModeBinding>(
                    "packupdate#version",
                    [options] {
                        for (const auto& [label, id] : *options) {
                            if (id == chosenId) {
                                return label;
                            }
                        }
                        return std::string();
                    },
                    [options](const std::string& picked) {
                        for (const auto& [label, id] : *options) {
                            if (label == picked && id != chosenId) {
                                chosenId = id;
                                wantPlan = true;
                            }
                        }
                    },
                    [options] {
                        std::vector<std::string> labels;
                        for (const auto& [label, id] : *options) {
                            labels.push_back(label);
                        }
                        return labels;
                    });
        }

        void packIcon(const PackUpdates::Origin& origin, float x, float cy, bool blocked) {
            const Projects::Brief* brief = Projects::brief(origin.source, origin.project);
            float y = cy - ICON * 0.5f;
            DownloadsParts::projectIcon(brief != nullptr ? brief->iconUrl : std::string(), x, y, ICON,
                                        Addons::Kind::Modpack);
            std::string slug = brief != nullptr && !brief->slug.empty() ? brief->slug : std::string();
            bool curseforge = origin.source == Addons::Source::CurseForge;
            std::string url = curseforge
                    ? (slug.empty() ? std::string() : "https://www.curseforge.com/minecraft/modpacks/" + slug)
                    : "https://modrinth.com/modpack/" + (slug.empty() ? origin.project : slug);
            if (url.empty() || blocked || !NewUi::hit(x, y, ICON, ICON)) {
                return;
            }
            UiCursor::hand();
            Page::tooltip(curseforge ? "在 CurseForge 上查看" : "在 Modrinth 上查看", x, y, ICON, ICON);
            if (NewUi::click(x, y, ICON, ICON)) {
                Platform::openUrl(url);
            }
        }

        bool toolbar(const Instances::Entry& entry, const PackUpdates::State& state, const PackUpdates::Origin& origin,
                     const Addons::Versions& versions, bool covered) {
            const std::string& key = entry.key;
            bool applying = PackUpdates::running();
            float cy = Page::top() + 12.0f;
            bool back = Page::back("packupdate#back", "返回", Page::left() - 4.0f, cy - 14.0f, covered);
            float x = Page::left() + 30.0f;
            packIcon(origin, x, cy, covered);
            x += ICON + 10.0f;
            float selectW = std::clamp(Page::width() * 0.3f, 200.0f, 300.0f);
            bool ready = versions.status == Addons::Status::Ready && !versions.list.empty();
            SelectWidget select("packupdate#version", versionChoice(origin, versions));
            select.bounds(x, cy - Page::FIELD_H * 0.5f, selectW, Page::FIELD_H).block(covered || applying || !ready);
            select.paint();
            x += selectW + 14.0f;

            bool here = applying && mine(key);
            bool planned = state.phase == Phase::Planned && mine(key);
            std::string label = here ? "取消" : "更改";
            float buttonW = std::max(88.0f, Page::buttonWidth(label, here ? 0 : 0xf2f1));
            float buttonX = Page::right() - buttonW;
            bool gameBusy = Game::state(key) != Game::State::Idle;
            std::string note;
            uint32_t noteInk = UiTheme::textMuted();
            if (here) {
                note = PackUpdates::progress();
                noteInk = UiTheme::textSecondary();
            } else if (applying) {
                note = "另一个实例正在更改版本";
            } else if (gameBusy) {
                note = "游戏运行中";
            } else if (planned && pending(state.summary)) {
                note = "需要下载 " + InstanceScreenshots::sizeText(downloadBytes(state.summary));
                noteInk = UiTheme::textSecondary();
            }
            float noteW = UiFont::width(note, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float noteX = buttonX - 14.0f - noteW;
            if (!note.empty()) {
                UiRender::textY(note, noteX, cy - 12.0f, 24.0f, noteInk, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (here) {
                    Page::spinner(noteX - 14.0f, cy, 6.0f, UiTheme::accent());
                    noteX -= 28.0f;
                }
            }
            bool actionable = planned && pending(state.summary);
            if (Page::button("packupdate#go", label, buttonX, cy - Page::FIELD_H * 0.5f, buttonW, !here,
                             covered || (!here && (!actionable || gameBusy)), here ? 0 : 0xf2f1)) {
                if (here) {
                    PackUpdates::cancel();
                } else {
                    PackUpdates::apply(kept);
                }
            }

            float limit = noteX - 16.0f;
            if (planned && state.summary.loadersChanged && x + 80.0f < limit) {
                std::vector<std::string> parts;
                if (state.summary.minecraft != entry.info.minecraft_version) {
                    parts.push_back("Minecraft " + state.summary.minecraft);
                }
                parts.insert(parts.end(), state.summary.loaders.begin(), state.summary.loaders.end());
                if (!parts.empty()) {
                    UiRender::textY(UiFont::fitEnd("将切换到 " + joined(parts), UiFont::MEDIUM, UiWidgets::TEXT_SMALL,
                                                   limit - x),
                                    x, cy - 12.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                }
            }
            return back;
        }

        void identifyView(const Instances::Entry& entry, bool covered) {
            using PackUpdates::Search;
            const std::string& key = entry.key;
            const PackUpdates::Identification& search = PackUpdates::identification();
            if (search.instanceKey != key) {
                PackUpdates::identify(key);
            }
            float y = Page::top() + 40.0f;
            Page::caption("识别整合包", y);
            y += Page::CAPTION_H;
            if (search.phase == Search::Running || search.phase == Search::Binding || search.instanceKey != key) {
                Page::card(y, 1);
                Page::spinner(Page::left() + 24.0f, y + Page::ROW_H * 0.5f, 6.0f, UiTheme::accent());
                UiRender::textY(search.phase == Search::Binding ? "正在关联整合包" : "正在查找这个整合包",
                                Page::left() + 40.0f, y, Page::ROW_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return;
            }
            if (search.phase == Search::Failed || search.candidates.empty()) {
                Page::card(y, 1);
                std::string text = search.phase == Search::Failed ? "识别失败：" + search.error
                                                                  : std::string("没有找到对应的整合包");
                std::string retry = "重试";
                float retryW = Page::buttonWidth(retry);
                text = UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::width() - retryW - 48.0f);
                UiRender::textY(text, Page::left() + 16.0f, y, Page::ROW_H,
                                search.phase == Search::Failed ? UiTheme::danger() : UiTheme::textSecondary(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (Page::button("packupdate#identify#retry", retry, Page::controlRight() - retryW, y + Page::FIELD_Y,
                                 retryW, false, covered)) {
                    PackUpdates::identify(key);
                }
                return;
            }
            int count = (int) search.candidates.size();
            float x = Page::left();
            float w = Page::width();
            Page::listCard(x, y, w, Page::ROW_H, count);
            for (int i = 0; i < count; i++) {
                const PackUpdates::Candidate& candidate = search.candidates[i];
                float rowY = y + i * Page::ROW_H;
                bool hovered = !covered && NewUi::hit(x, rowY, w, Page::ROW_H);
                float lit = UiState::motion("packupdate#candidate#" + std::to_string(i), hovered ? 1.0f : 0.0f, 20.0f);
                if (lit > 0.001f) {
                    UiRender::roundRect(x, rowY, w, Page::ROW_H, Page::CARD_RADIUS,
                                        UiTheme::overlay(Ui::round(14 * lit)), Page::rowCorners(i, count));
                }
                std::string match = Addons::sourceName(candidate.source) + std::string("，") +
                        std::to_string(candidate.matched) + "/" + std::to_string(candidate.total) + " 个文件吻合";
                float matchW = UiFont::width(match, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float right = x + w - 16.0f;
                UiRender::textY(match, right - matchW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                float textX = x + 16.0f;
                float room = std::max(40.0f, right - matchW - 16.0f - textX);
                std::string title =
                        UiFont::fitEnd(candidate.title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room * 0.6f);
                UiRender::textY(title, textX, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                float versionX = textX + UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 10.0f;
                std::string version = UiFont::fitEnd(candidate.version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL,
                                                     std::max(0.0f, textX + room - versionX));
                UiRender::textY(version, versionX, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                if (!covered && NewUi::click(x, rowY, w, Page::ROW_H)) {
                    PackUpdates::choose((size_t) i);
                }
            }
        }

        void centred(const std::string& text, float x, float w, float cy, uint32_t ink) {
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(text, x + (w - textW) * 0.5f, cy - 12.0f, 24.0f, ink, UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void loading(const std::string& text, float x, float w, float cy) {
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            Page::spinner(x + (w - textW) * 0.5f - 14.0f, cy, 6.0f, UiTheme::accent());
            centred(text, x + 10.0f, w, cy, UiTheme::textSecondary());
        }

        void stepsView(float x, float w, float cy) {
            std::vector<PackUpdates::Step> steps = PackUpdates::steps();
            if (steps.empty()) {
                steps.push_back({"准备", PackUpdates::StepState::Active});
            }
            float boxW = std::min(440.0f, w - 64.0f);
            float bx = x + (w - boxW) * 0.5f;
            float top = cy - steps.size() * STEP_H * 0.5f;
            for (size_t i = 0; i < steps.size(); i++) {
                const PackUpdates::Step& step = steps[i];
                float rowY = top + i * STEP_H;
                float lineCy = rowY + 12.0f;
                bool active = step.state == PackUpdates::StepState::Active;
                bool done = step.state == PackUpdates::StepState::Done;
                if (done) {
                    UiWidgets::icon(Ui::glyph(0xf00c), bx + 7.0f, lineCy, UiTheme::accent(), 11.0f);
                } else if (active) {
                    Page::spinner(bx + 7.0f, lineCy, 6.0f, UiTheme::accent());
                } else {
                    UiRender::outline(bx + 2.0f, lineCy - 5.0f, 10.0f, 10.0f, 5.0f, UiTheme::textFaint(), 0xF, 1.4f);
                }
                float textX = bx + 26.0f;
                std::string detail;
                if (active && step.total > 0) {
                    detail = std::to_string(step.done) + " / " + std::to_string(step.total);
                }
                float detailW = UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(UiFont::fitEnd(active ? "正在" + step.label + "..." : step.label, UiFont::MEDIUM,
                                               UiWidgets::TEXT_CONTROL, bx + boxW - textX - detailW - 12.0f),
                                textX, rowY, 24.0f,
                                active         ? UiTheme::textPrimary()
                                        : done ? UiTheme::textSecondary()
                                               : UiTheme::textFaint(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (!detail.empty()) {
                    UiRender::textY(detail, bx + boxW - detailW, rowY, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
                if (!active) {
                    continue;
                }
                float barY = rowY + 30.0f;
                float barW = bx + boxW - textX;
                UiRender::roundRect(textX, barY, barW, 4.0f, 2.0f, UiTheme::overlay(18));
                if (step.total > 0) {
                    float fraction = std::clamp((float) step.done / (float) step.total, 0.0f, 1.0f);
                    UiRender::roundRect(textX, barY, std::max(4.0f, barW * fraction), 4.0f, 2.0f, UiTheme::accent());
                } else {
                    float phase = (float) std::fmod(ImGui::GetTime() * 0.7, 1.0);
                    float segW = barW * 0.3f;
                    float from = textX - segW + (barW + segW) * phase;
                    float left = std::max(from, textX);
                    float right = std::min(from + segW, textX + barW);
                    if (right > left) {
                        UiRender::roundRect(left, barY, right - left, 4.0f, 2.0f, UiTheme::accent());
                    }
                }
            }
        }

        bool browserView(const Instances::Entry& entry, const PackUpdates::Origin& origin, bool covered) {
            const std::string& key = entry.key;
            const Addons::Versions& versions = Addons::versions(origin.source, origin.project);
            bool applying = PackUpdates::running();
            if (wasApplying && !applying) {
                wantPlan = true;
            }
            wasApplying = applying;
            if (chosenKey != key) {
                chosenKey = key;
                chosenId.clear();
                wantPlan = true;
            }
            bool ready = versions.status == Addons::Status::Ready && !versions.list.empty();
            if (ready && versionOf(versions, chosenId) == nullptr) {
                const Addons::Version* best = newest(versions, origin);
                chosenId = best != nullptr ? best->id : versions.list.front().id;
                wantPlan = true;
            }
            if (ready && wantPlan && !applying) {
                wantPlan = false;
                const Addons::Version* version = versionOf(versions, chosenId);
                if (version->file() == nullptr) {
                    PackUpdates::reset();
                } else {
                    PackUpdates::plan(key, *version);
                }
            }
            const PackUpdates::State& state = PackUpdates::state();
            bool shown = mine(key) && (state.phase == Phase::Planned || (applying && state.phase == Phase::Applying));
            if (shown && builtSerial != state.serial) {
                rebuild(state);
            }
            bool back = toolbar(entry, state, origin, versions, covered);

            MainWindow::Rect panel = MainWindow::panel();
            float x = panel.x;
            float w = panel.w;
            float bodyTop = Page::top() + 36.0f;
            float bodyBottom = panel.y + panel.h - 10.0f;
            bool blocked = covered || applying;
            uint32_t line = UiTheme::rowDivider(UiTheme::outlineSoft());
            UiRender::hLine(x, x + w, bodyTop, line);
            float cy = (bodyTop + bodyBottom) * 0.5f;

            if (versions.status == Addons::Status::Loading) {
                loading("正在获取版本列表", x, w, cy);
            } else if (versions.status == Addons::Status::Failed) {
                centred(UiFont::fitEnd("无法获取版本列表：" + versions.error, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                       w - 48.0f),
                        x, w, cy - 20.0f, UiTheme::danger());
                std::string retry = "重试";
                float retryW = Page::buttonWidth(retry);
                if (Page::button("packupdate#versions#retry", retry, x + (w - retryW) * 0.5f, cy + 8.0f, retryW, false,
                                 covered)) {
                    Addons::refreshVersions(origin.source, origin.project);
                }
            } else if (versions.list.empty()) {
                centred("没有可用的版本", x, w, cy, UiTheme::textMuted());
            } else if (applying && !mine(key)) {
                centred("另一个实例正在更改整合包版本", x, w, cy, UiTheme::textMuted());
            } else if (!mine(key) || state.phase == Phase::Idle) {
                if (wantPlan) {
                    loading("正在准备", x, w, cy);
                } else {
                    centred("这个版本没有可下载的文件", x, w, cy, UiTheme::textMuted());
                }
            } else if (state.phase == Phase::Planning) {
                stepsView(x, w, cy);
            } else if (state.phase == Phase::Failed) {
                centred(UiFont::fitEnd("无法比较文件：" + state.error, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                       w - 48.0f),
                        x, w, cy - 20.0f, UiTheme::danger());
                std::string retry = "重试";
                float retryW = Page::buttonWidth(retry);
                if (Page::button("packupdate#changes#retry", retry, x + (w - retryW) * 0.5f, cy + 8.0f, retryW, false,
                                 blocked)) {
                    wantPlan = true;
                }
            } else if (state.summary.items.empty()) {
                bool same = chosenId == origin.version;
                centred(state.summary.loadersChanged ? "只有加载器需要更新"
                                : same               ? "本地文件和这个版本一致"
                                                     : "没有文件变化",
                        x, w, cy, UiTheme::textMuted());
            } else {
                float leftW = std::clamp(w * 0.24f, 180.0f, 260.0f);
                UiRender::vLine(x + leftW, bodyTop, panel.y + panel.h, line);
                folderPane(x, bodyTop + 6.0f, bodyBottom, leftW, key, blocked);
                listPane(x + leftW + 1.0f, bodyTop + 1.0f, bodyBottom, w - leftW - 1.0f, state.summary, key, blocked);
            }
            return back;
        }
    }

    std::string rowValue(const std::string& instanceKey) {
        const PackUpdates::Origin* origin = PackUpdates::origin(instanceKey);
        if (origin == nullptr) {
            return PackUpdates::identifiable(instanceKey) ? std::string("需要识别") : std::string();
        }
        const Addons::Versions& versions = Addons::versions(origin->source, origin->project);
        if (versions.status == Addons::Status::Ready) {
            const Addons::Version* latest = newest(versions, *origin);
            if (latest != nullptr && latest->id != origin->version) {
                return "可更新到 " + labelOf(*latest);
            }
        }
        return origin->versionName;
    }

    void reset(const std::string& instanceKey) {
        if (PackUpdates::running() && mine(instanceKey)) {
            return;
        }
        chosenKey = instanceKey;
        chosenId.clear();
        wantPlan = true;
        PackUpdates::reset();
    }

    bool draw(const Instances::Entry& entry) {
        bool covered = NewUi::overlayCoversMouse();
        const PackUpdates::Origin* origin = PackUpdates::origin(entry.key);
        if (origin != nullptr) {
            return browserView(entry, *origin, covered);
        }
        bool back = Page::back("packupdate#back", "返回", Page::left() - 4.0f, Page::top() - 2.0f, covered);
        Page::title("整合包版本", Page::left() + 30.0f, Page::top());
        if (!PackUpdates::identifiable(entry.key)) {
            return true;
        }
        identifyView(entry, covered);
        return back;
    }
}
