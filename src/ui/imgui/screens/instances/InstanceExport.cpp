#include "ui/imgui/screens/instances/InstanceExport.h"
#include "app/instances/Exports.h"
#include "ui/imgui/screens/instances/FileTreeView.h"
#include "ui/imgui/screens/settings/LauncherBuilderPage.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <algorithm>
#include <memory>
#include <string>

namespace InstanceExport {

    using Options = Exports::Options;

    namespace {
        constexpr int VIEW_EXPORT = 0;
        constexpr int VIEW_LAUNCHER = 1;
        PageTransition views(VIEW_EXPORT);

        std::string launcherName() {
            const LauncherBuilder::Project& custom = LauncherBuilder::project();
            return !custom.windowTitle.empty() ? custom.windowTitle : custom.brandTitle;
        }

        constexpr float WIDE_FIELD = 300.0f;

        void textRow(const std::string& key, const char* field, const char* label, const char* placeholder, float w,
                     std::string Options::* member, float rowY, bool blocked) {
            Page::label(label, rowY);
            auto value = std::make_shared<TextBinding>(
                    std::string("export#") + field + "#" + key, [key, member] { return Exports::options(key).*member; },
                    [key, member](const std::string& text) {
                        Exports::options(key).*member = Ui::trim(text);
                        Exports::save(key);
                    },
                    placeholder);
            value->limit = 200;
            float x = Page::controlRight() - w;
            float y = rowY + Page::FIELD_Y;
            TextFieldWidget widget(value);
            widget.box(x, y, w, Page::FIELD_H).bounds(x, y, w, Page::FIELD_H).block(blocked);
            widget.paint();
        }

        void toggleRow(const std::string& key, const char* field, const char* label, bool Options::* member, float rowY,
                       bool blocked) {
            Page::label(label, rowY);
            std::string id = std::string("export#") + field + "#" + key;
            auto flag = std::make_shared<FlagBinding>(
                    id, [key, member] { return Exports::options(key).*member; },
                    [key, member](bool on) {
                        Exports::options(key).*member = on;
                        Exports::save(key);
                    });
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::left(), rowY, Page::width(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void flagRow(const std::string& key, const char* field, const char* label, bool (*read)(const std::string&),
                     void (*write)(const std::string&, bool), float rowY, bool blocked) {
            Page::label(label, rowY);
            std::string id = std::string("export#") + field + "#" + key;
            auto flag = std::make_shared<FlagBinding>(
                    id, [key, read] { return read(key); }, [key, write](bool on) { write(key, on); });
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(Page::left(), rowY, Page::width(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void exportButton(const Instances::Entry& entry, bool blocked) {
            float y = MainWindow::height() - 28.0f - Page::FIELD_H;
            bool running = Exports::running();
            bool mine = running && Exports::runningKey() == entry.key;
            std::string label = mine ? "取消" : "导出";
            float w = std::max(88.0f, Page::buttonWidth(label, mine ? 0 : 0xf56e));
            float x = Page::right() - w;
            if (mine) {
                std::string now = Exports::status();
                float nowW = UiFont::width(now, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(now, x - 14.0f - nowW, y, Page::FIELD_H, UiTheme::textSecondary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                Page::spinner(x - 14.0f - nowW - 14.0f, y + Page::FIELD_H * 0.5f, 6.0f, UiTheme::accent());
            } else if (running) {
                std::string busy = "另一个实例正在导出";
                float busyW = UiFont::width(busy, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(busy, x - 14.0f - busyW, y, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (Page::button("export#go", label, x, y, w, !mine, blocked || (running && !mine), mine ? 0 : 0xf56e)) {
                if (mine) {
                    Exports::cancel();
                } else {
                    std::optional<LauncherBuilder::ThemeFiles> launcher;
                    if (Exports::withLauncher(entry.key)) {
                        launcher = LauncherBuilderPage::themeFiles();
                    }
                    Exports::start(entry.key, std::move(launcher));
                }
            }
        }
    }

    void reset() {
        views.page = VIEW_EXPORT;
        views.mix = 1.0f;
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        views.begin();
        if (views.page == VIEW_LAUNCHER) {
            if (LauncherBuilderPage::draw(covered, true)) {
                views.go(VIEW_EXPORT);
            }
            views.end();
            return false;
        }
        bool back = Page::iconButton("export#back", 0xf053, "返回", Page::left() - 4.0f, Page::top() - 2.0f, covered);
        const char* title = "导出整合包";
        Page::title(title, Page::left() + 30.0f, Page::top());
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::right() - nameX),
                        nameX, Page::top(), 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        Exports::options(key);
        FileTree::Tree& tree = Exports::tree(key);

        const int packCount = 4;
        const bool launcher = Exports::withLauncher(key);
        const int optionCount = launcher ? 4 : 2;
        float treeH = FileTreeView::height(tree);
        float contentH = 3 * Page::CAPTION_H + (packCount + optionCount) * Page::ROW_H + treeH + 2 * Page::GAP;
        float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
        Page::Scroll body("export#body#" + key, Page::top() + 40.0f, buttonTop - 14.0f, contentH);
        bool outside = body.begin();
        bool blocked = outside || covered || (Exports::running() && Exports::runningKey() == key);
        float y = body.top - body.offset;

        if (Page::shown(y, Page::CAPTION_H + packCount * Page::ROW_H)) {
            Page::caption("整合包", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, packCount);
            textRow(key, "name", "名称", "整合包名称", WIDE_FIELD, &Options::name, rowY, blocked);
            textRow(key, "version", "版本", "1.0.0", 140.0f, &Options::version, rowY + Page::ROW_H, blocked);
            textRow(key, "author", "作者", "无", 200.0f, &Options::author, rowY + 2 * Page::ROW_H, blocked);
            textRow(key, "summary", "简介", "无", WIDE_FIELD, &Options::summary, rowY + 3 * Page::ROW_H, blocked);
        }
        y += Page::CAPTION_H + packCount * Page::ROW_H + Page::GAP;

        Page::caption("文件", y);
        if (FileTreeView::draw("export#tree#" + key, tree, Exports::options(key).picks, Page::left(),
                               y + Page::CAPTION_H, Page::width(), blocked, FileTreeView::purpose)) {
            Exports::save(key);
        }
        y += Page::CAPTION_H + treeH + Page::GAP;

        if (Page::shown(y, Page::CAPTION_H + optionCount * Page::ROW_H)) {
            Page::caption("选项", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, optionCount);
            toggleRow(key, "bundle", "全部打包进整合包", &Options::bundle_files, rowY, blocked);
            flagRow(key, "launcher", "附带自定义启动器", Exports::withLauncher, Exports::setWithLauncher,
                    rowY + Page::ROW_H, blocked);
            if (launcher) {
                flagRow(key, "single", "单实例模式", Exports::single, Exports::setSingle, rowY + 2 * Page::ROW_H,
                        blocked);
                if (Page::navRow("export#builder", "自定义启动器设置", launcherName(), rowY + 3 * Page::ROW_H, blocked,
                                 Page::rowCorners(3, 4), 0xf552)) {
                    views.go(VIEW_LAUNCHER);
                }
            }
        }
        body.end();

        exportButton(entry, covered);
        views.end();
        return back;
    }
}
