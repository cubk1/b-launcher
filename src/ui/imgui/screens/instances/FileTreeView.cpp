#include "ui/imgui/screens/instances/FileTreeView.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include <core/base/strings.hpp>

#include <algorithm>
#include <vector>

namespace FileTreeView {

    namespace blb = b_launcher_base;

    namespace {
        constexpr float ROW = 30.0f;
        constexpr float INDENT = 18.0f;
        constexpr float PAD = 4.0f;
        constexpr float BOX = 16.0f;

        struct Row {
            const FileTree::Node* node = nullptr;
            int depth = 0;
        };

        void flatten(FileTree::Tree& tree, const std::string& path, int depth, std::vector<Row>& out) {
            for (const FileTree::Node& node : tree.children(path)) {
                out.push_back({&node, depth});
                if (node.directory && tree.open(node.path)) {
                    flatten(tree, node.path, depth + 1, out);
                }
            }
        }

        void checkbox(float x, float cy, FileTree::Mark mark, float hover) {
            float y = cy - BOX * 0.5f;
            if (mark == FileTree::Mark::OFF) {
                UiRender::outline(x, y, BOX, BOX, 4.5f, UiRender::mix(UiTheme::outline(), UiTheme::textMuted(), hover),
                                  0xF, 1.4f);
                return;
            }
            UiRender::roundRect(x, y, BOX, BOX, 4.5f,
                                UiRender::mix(UiTheme::accent(), UiTheme::accentHover(), 0.12f * hover));
            if (mark == FileTree::Mark::ON) {
                UiWidgets::icon(Ui::glyph(0xf00c), x + BOX * 0.5f, cy, UiTheme::onAccent(), 9.0f);
            } else {
                UiRender::roundRect(x + 4.0f, cy - 1.0f, BOX - 8.0f, 2.0f, 1.0f, UiTheme::onAccent());
            }
        }
    }

    Tag purpose(const FileTree::Node& node) {
        if (node.path.find('/') != std::string::npos) {
            return {};
        }
        std::string name = blb::to_lower(node.name);
        if (node.directory) {
            if (name == "mods") {
                return {"模组"};
            }
            if (name == "config") {
                return {"模组配置"};
            }
            if (name == "defaultconfigs") {
                return {"默认配置"};
            }
            if (name == "resourcepacks") {
                return {"资源包"};
            }
            if (name == "shaderpacks") {
                return {"光影包"};
            }
            if (name == "saves") {
                return {"存档"};
            }
            if (name == "screenshots") {
                return {"截图"};
            }
            return {};
        }
        if (name == "options.txt") {
            return {"游戏选项"};
        }
        if (name == "servers.dat") {
            return {"服务器列表"};
        }
        return {};
    }

    float height(FileTree::Tree& tree) {
        std::vector<Row> rows;
        flatten(tree, std::string(), 0, rows);
        return (float) std::max<size_t>(rows.size(), 1) * ROW + PAD * 2.0f;
    }

    bool draw(const std::string& id, FileTree::Tree& tree, FileTree::Rules& rules, float x, float y, float w,
              bool blocked, const Tagger& tagger, std::string* toggled) {
        std::vector<Row> rows;
        flatten(tree, std::string(), 0, rows);
        float h = (float) std::max<size_t>(rows.size(), 1) * ROW + PAD * 2.0f;
        Page::card(x, y, w, h);
        if (rows.empty()) {
            UiRender::textY("游戏目录里没有文件", x + 16.0f, y + PAD, ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            return false;
        }
        bool changed = false;
        float rowY = y + PAD;
        for (const Row& row : rows) {
            if (!Page::shown(rowY, ROW)) {
                rowY += ROW;
                continue;
            }
            const FileTree::Node& node = *row.node;
            std::string key = id + "#" + node.path;
            float rx = x + PAD;
            float rw = w - PAD * 2.0f;
            float ix = rx + 8.0f + row.depth * INDENT;
            float cy = rowY + ROW * 0.5f;
            bool hovered = !blocked && NewUi::hit(rx, rowY, rw, ROW);
            bool overBox = hovered && NewUi::hit(rx, rowY, ix + BOX + 6.0f - rx, ROW);
            float lit = UiState::motion(key + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
            if (lit > 0.001f) {
                UiRender::roundRect(rx, rowY, rw, ROW, 7.0f, UiTheme::overlay(Ui::round(12 * lit)));
            }
            FileTree::Mark mark = FileTree::mark(rules, node.path);
            checkbox(ix, cy, mark, UiState::motion(key + "#box", overBox ? 1.0f : 0.0f, 18.0f));
            float iconX = ix + BOX + 14.0f;
            if (node.directory) {
                bool open = tree.open(node.path);
                UiWidgets::icon(Ui::glyph(open ? 0xf078 : 0xf054), iconX, cy, UiTheme::textMuted(), 8.0f);
                UiWidgets::icon(Ui::glyph(open ? 0xf07c : 0xf07b), iconX + 18.0f, cy, UiTheme::accent(), 11.0f);
            } else {
                UiWidgets::icon(Ui::glyph(0xf15b), iconX + 18.0f, cy, UiTheme::textFaint(), 10.0f);
            }
            float textX = iconX + 30.0f;
            float right = rx + rw - 10.0f;
            Tag tag = tagger ? tagger(node) : Tag{};
            if (!tag.text.empty()) {
                float tagW = UiFont::width(tag.text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (tag.strong) {
                    float pillW = tagW + 12.0f;
                    UiRender::roundRect(right - pillW, cy - 9.0f, pillW, 18.0f, 9.0f, UiTheme::accentSoft(36));
                    UiRender::textY(tag.text, right - pillW + 6.0f, rowY, ROW, UiTheme::accentText(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    right -= pillW + 10.0f;
                } else {
                    UiRender::textY(tag.text, right - tagW, rowY, ROW, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                    right -= tagW + 10.0f;
                }
            }
            uint32_t ink = mark == FileTree::Mark::OFF ? UiTheme::textSecondary() : UiTheme::textPrimary();
            UiRender::textY(
                    UiFont::fitEnd(node.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, std::max(0.0f, right - textX)),
                    textX, rowY, ROW, ink, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (hovered) {
                UiCursor::hand();
                if (NewUi::click(rx, rowY, rw, ROW)) {
                    UiSound::request(UiSound::TICK);
                    if (node.directory && !overBox) {
                        tree.setOpen(node.path, !tree.open(node.path));
                    } else {
                        tree.toggle(rules, node.path);
                        changed = true;
                        if (toggled != nullptr) {
                            *toggled = node.path;
                        }
                    }
                }
            }
            rowY += ROW;
        }
        return changed;
    }
}
