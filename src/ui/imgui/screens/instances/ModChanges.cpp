#include "ui/imgui/screens/instances/ModChanges.h"
#include "app/instances/ModManager.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace ModChanges {

    namespace {
        constexpr float BAR_H = 46.0f;
        constexpr float LINE_H = 30.0f;
        constexpr float GAP = 8.0f;
        constexpr size_t MAX_LINES = 6;

        enum class Kind { Change, Issue, Problem, More };

        struct Line {
            Kind kind = Kind::Change;
            ModManager::Change change;
            ModManager::Issue issue;
            std::string text;
        };

        struct Layout {
            bool shown = false;
            std::vector<Line> lines;
            int errors = 0;
            int warnings = 0;
        };

        std::map<std::string, bool> collapsed;

        uint32_t warningInk() {
            return UiTheme::warningInk();
        }

        Layout layoutOf(const std::string& key) {
            Layout layout;
            const std::vector<ModManager::Change>& changes = ModManager::changes(key);
            const ModManager::Progress& progress = ModManager::progress(key);
            bool busy = ModManager::busy(key);
            layout.shown = !changes.empty() || busy || progress.phase == ModManager::Phase::Failed;
            if (!layout.shown) {
                return layout;
            }
            std::vector<ModManager::Issue> issues = busy ? std::vector<ModManager::Issue>() : ModManager::check(key);
            for (const ModManager::Issue& issue : issues) {
                (issue.severity == ModManager::Severity::Error ? layout.errors : layout.warnings)++;
            }
            if (!busy && !collapsed[key]) {
                for (const ModManager::Change& change : changes) {
                    Line line;
                    line.kind = Kind::Change;
                    line.change = change;
                    layout.lines.push_back(std::move(line));
                }
            }
            for (const ModManager::Issue& issue : issues) {
                Line line;
                line.kind = Kind::Issue;
                line.issue = issue;
                layout.lines.push_back(std::move(line));
            }
            if (!busy && progress.phase == ModManager::Phase::Failed) {
                for (const std::string& problem : progress.problems) {
                    Line line;
                    line.kind = Kind::Problem;
                    line.text = problem;
                    layout.lines.push_back(std::move(line));
                }
            }
            if (layout.lines.size() > MAX_LINES) {
                size_t hidden = layout.lines.size() - (MAX_LINES - 1);
                layout.lines.resize(MAX_LINES - 1);
                Line more;
                more.kind = Kind::More;
                more.text = "还有 " + std::to_string(hidden) + " 项";
                layout.lines.push_back(std::move(more));
            }
            return layout;
        }

        float heightOf(const Layout& layout) {
            if (!layout.shown) {
                return 0.0f;
            }
            float panel = layout.lines.empty() ? 0.0f : (float) layout.lines.size() * LINE_H + 8.0f + GAP;
            return panel + BAR_H;
        }

        bool line(const std::string& key, const Line& item, int index, float x, float y, float w, bool blocked) {
            float cy = y + LINE_H * 0.5f;
            float textX = x + 40.0f;
            float right = x + w - 12.0f;
            std::string id = "mods#changes#" + std::to_string(index);
            if (item.kind == Kind::Change) {
                const ModManager::Change& change = item.change;
                bool added = change.file.empty();
                UiWidgets::icon(Ui::glyph(added ? 0xf067 : 0xf021), x + 22.0f, cy, UiTheme::accentText(), 11.0f);
                float undoX = right - 26.0f;
                if (Page::iconButton(id + "#undo", 0xf00d, "撤销", undoX, y + 1.0f, blocked)) {
                    ModManager::unstage(key, change);
                    return true;
                }
                std::string detail = added
                        ? "新增 " + change.target.number
                        : (change.from.empty() ? std::string("?") : change.from) + " → " + change.target.number;
                float detailW =
                        std::min(UiFont::width(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL), (undoX - textX) * 0.5f);
                std::string name = UiFont::fitEnd(change.name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                  undoX - 12.0f - textX - detailW - 10.0f);
                float nameW = UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(name, textX, y, LINE_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                UiRender::textY(UiFont::fitEnd(detail, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, detailW),
                                textX + nameW + 10.0f, y, LINE_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                return false;
            }
            if (item.kind == Kind::More) {
                UiRender::textY(item.text, textX, y, LINE_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                return false;
            }
            bool error = item.kind == Kind::Problem || item.issue.severity == ModManager::Severity::Error;
            uint32_t ink = error ? UiTheme::danger() : warningInk();
            UiWidgets::icon(Ui::glyph(error ? 0xf06a : 0xf071), x + 22.0f, cy, ink, 11.0f);
            const std::string& text = item.kind == Kind::Problem ? item.text : item.issue.text;
            float limit = right;
            const std::string& missing = item.issue.missingProject;
            if (item.kind == Kind::Issue && !missing.empty()) {
                if (ModManager::adding(key, missing)) {
                    Page::spinner(right - 8.0f, cy, 6.0f, UiTheme::accent());
                    limit = right - 24.0f;
                } else {
                    std::string label = "添加前置";
                    float buttonW = Page::buttonWidth(label, 0xf067);
                    float buttonX = right - buttonW;
                    if (Page::button(id + "#add", label, buttonX, y + (LINE_H - Page::FIELD_H) * 0.5f, buttonW, false,
                                     blocked, 0xf067)) {
                        ModManager::addProject(key, missing);
                    }
                    limit = buttonX - 10.0f;
                }
            }
            UiRender::textY(UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, limit - textX), textX, y,
                            LINE_H, UiTheme::textSecondary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            return false;
        }

        void progressBlock(const ModManager::Progress& progress, size_t count, float x, float y, float w) {
            std::string status = progress.phase == ModManager::Phase::Verifying ? "正在检查新文件"
                    : progress.phase == ModManager::Phase::Replacing            ? "正在替换文件"
                                                                     : "正在下载 " + std::to_string(count) + " 个文件";
            UiRender::textY(status, x, y + 7.0f, 16.0f, UiTheme::textSecondary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_SMALL);
            float barY = y + 28.0f;
            UiRender::roundRect(x, barY, w, 6.0f, 3.0f, UiTheme::surfaceRaised());
            if (progress.fraction >= 0.0f) {
                float fill = std::max(6.0f, w * progress.fraction);
                UiRender::roundRect(x, barY, fill, 6.0f, 3.0f, UiTheme::accent());
            } else {
                UiState::keepAnimating();
                float t = (float) std::fmod(ImGui::GetTime() * 0.8, 1.0);
                float segment = w * 0.3f;
                float head = x - segment + (w + segment) * t;
                float from = std::max(x, head);
                float to = std::min(x + w, head + segment);
                if (to > from) {
                    UiRender::roundRect(from, barY, to - from, 6.0f, 3.0f, UiTheme::accent());
                }
            }
        }
    }

    float height(const std::string& key) {
        return heightOf(layoutOf(key));
    }

    void draw(const std::string& key, float x, float y, float w, bool blocked) {
        Layout layout = layoutOf(key);
        if (!layout.shown) {
            return;
        }
        if (!layout.lines.empty()) {
            float panelH = (float) layout.lines.size() * LINE_H + 8.0f;
            Page::card(x, y, w, panelH);
            for (size_t i = 0; i < layout.lines.size(); i++) {
                if (line(key, layout.lines[i], (int) i, x, y + 4.0f + (float) i * LINE_H, w, blocked)) {
                    break;
                }
            }
            y += panelH + GAP;
        }

        Page::card(x, y, w, BAR_H);
        const ModManager::Progress& progress = ModManager::progress(key);
        const std::vector<ModManager::Change>& changes = ModManager::changes(key);
        float buttonY = y + (BAR_H - Page::FIELD_H) * 0.5f;
        float right = x + w - 12.0f;
        if (ModManager::busy(key)) {
            bool cancellable =
                    progress.phase == ModManager::Phase::Downloading || progress.phase == ModManager::Phase::Verifying;
            float cancelW = Page::buttonWidth("取消");
            if (cancellable &&
                Page::button("mods#changes#cancel", "取消", right - cancelW, buttonY, cancelW, false, blocked)) {
                ModManager::cancel(key);
            }
            progressBlock(progress, changes.size(), x + 16.0f, y, right - cancelW - 16.0f - (x + 16.0f));
            return;
        }

        std::string apply = "应用更改";
        float applyW = Page::buttonWidth(apply, 0xf00c);
        float applyX = right - applyW;
        bool checking = ModManager::checking(key);
        bool ready = !changes.empty() && layout.errors == 0 && !checking;
        if (Page::button("mods#changes#apply", apply, applyX, buttonY, applyW, true, blocked || !ready, 0xf00c)) {
            ModManager::apply(key);
        }
        std::string discard = "放弃";
        float discardW = Page::buttonWidth(discard);
        float discardX = applyX - 8.0f - discardW;
        if (Page::button("mods#changes#discard", discard, discardX, buttonY, discardW, false, blocked)) {
            ModManager::discard(key);
            return;
        }

        float textX = x + 16.0f;
        if (!changes.empty()) {
            bool folded = collapsed[key];
            float toggleW = 26.0f +
                    UiFont::width(std::to_string(changes.size()) + " 项更改", UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            bool hovered = !blocked && NewUi::hit(x + 8.0f, y + 8.0f, toggleW + 12.0f, BAR_H - 16.0f);
            UiWidgets::icon(Ui::glyph(folded ? 0xf077 : 0xf078), textX + 6.0f, y + BAR_H * 0.5f,
                            hovered ? UiTheme::textPrimary() : UiTheme::textMuted(), 10.0f);
            if (!blocked && NewUi::click(x + 8.0f, y + 8.0f, toggleW + 12.0f, BAR_H - 16.0f)) {
                collapsed[key] = !folded;
            }
            textX += 22.0f;
        }
        std::string count = std::to_string(changes.size()) + " 项更改";
        UiRender::textY(count, textX, y, BAR_H, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        textX += UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 14.0f;
        float room = discardX - 12.0f - textX;
        if (progress.phase == ModManager::Phase::Failed) {
            UiRender::textY(UiFont::fitEnd(progress.error, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room), textX, y,
                            BAR_H, UiTheme::danger(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        } else if (checking) {
            Page::spinner(textX + 7.0f, y + BAR_H * 0.5f, 6.0f, UiTheme::accent());
            UiRender::textY("正在检查", textX + 20.0f, y, BAR_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        } else if (layout.errors > 0) {
            UiRender::textY(std::to_string(layout.errors) + " 个冲突", textX, y, BAR_H, UiTheme::danger(),
                            UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        } else if (layout.warnings > 0) {
            UiRender::textY(std::to_string(layout.warnings) + " 个提醒", textX, y, BAR_H, warningInk(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }
    }
}
