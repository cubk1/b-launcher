#include "ui/imgui/screens/instances/InstanceExport.h"
#include "app/instances/Exports.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/IconButtonWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include <algorithm>
#include <memory>
#include <string>

namespace InstanceExport {

    using Options = Exports::Options;

    namespace {
        constexpr float WIDE_FIELD = 300.0f;

        void tip(const std::string& text, float x, float y, float w, float h) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22;
            NewUi::tooltipText = text;
            NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
            NewUi::tooltipY = y + h + 6.0f;
        }

        bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y, bool blocked) {
            IconButtonWidget button(id, Ui::glyph(glyph), UiTheme::textSecondary(), UiTheme::textPrimary());
            button.bounds(x, y, 28, 28).block(blocked);
            button.paint();
            if (!blocked && button.hovered()) {
                tip(hint, x, y, 28, 28);
            }
            return button.pressed;
        }

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
                       bool blocked, const char* doc = nullptr) {
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
            if (doc != nullptr && !blocked &&
                NewUi::hit(Page::left(), rowY, Page::controlRight() - 60.0f - Page::left(), Page::ROW_H)) {
                NewUi::markDoc(id + "#doc", doc, Page::left() + 6.0f, rowY + Page::ROW_H + 10.0f);
            }
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
                    Exports::start(entry.key);
                }
            }
        }
    }

    bool draw(const Instances::Entry& entry) {
        const std::string& key = entry.key;
        bool covered = NewUi::overlayCoversMouse();
        bool back = iconButton("export#back", 0xf053, "返回", Page::left() - 4.0f, Page::TOP - 2.0f, covered);
        const char* title = "导出整合包";
        Page::title(title, Page::left() + 30.0f, Page::TOP);
        float nameX = Page::left() + 30.0f + UiFont::width(title, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
        UiRender::textY(UiFont::fitEnd(entry.info.id, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::right() - nameX),
                        nameX, Page::TOP, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        Exports::options(key);

        const int packCount = 4;
        const int contentCount = 8;
        const int fileCount = 1;
        float contentH = 3 * Page::CAPTION_H + (packCount + contentCount + fileCount) * Page::ROW_H + 2 * Page::GAP;
        float buttonTop = MainWindow::height() - 28.0f - Page::FIELD_H;
        Page::Scroll body("export#body#" + key, Page::TOP + 40.0f, buttonTop - 14.0f, contentH);
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

        if (Page::shown(y, Page::CAPTION_H + contentCount * Page::ROW_H)) {
            Page::caption("包含的内容", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, contentCount);
            toggleRow(key, "mods", "模组", &Options::mods, rowY, blocked);
            toggleRow(key, "disabledMods", "已禁用的模组", &Options::disabled_mods, rowY + Page::ROW_H, blocked);
            toggleRow(key, "modSettings", "模组配置文件", &Options::mod_settings, rowY + 2 * Page::ROW_H, blocked,
                      "config 等文件夹里模组的设置。");
            toggleRow(key, "gameSettings", "游戏设置", &Options::game_settings, rowY + 3 * Page::ROW_H, blocked,
                      "options.txt 里的按键、视频等设置。");
            toggleRow(key, "resourcePacks", "资源包", &Options::resource_packs, rowY + 4 * Page::ROW_H, blocked);
            toggleRow(key, "shaderPacks", "光影包", &Options::shader_packs, rowY + 5 * Page::ROW_H, blocked);
            toggleRow(key, "saves", "存档", &Options::saves, rowY + 6 * Page::ROW_H, blocked);
            toggleRow(key, "servers", "服务器列表", &Options::servers, rowY + 7 * Page::ROW_H, blocked);
        }
        y += Page::CAPTION_H + contentCount * Page::ROW_H + Page::GAP;

        if (Page::shown(y, Page::CAPTION_H + fileCount * Page::ROW_H)) {
            Page::caption("文件", y);
            float rowY = y + Page::CAPTION_H;
            Page::card(rowY, fileCount);
            toggleRow(key, "bundle", "全部打包进整合包", &Options::bundle_files, rowY, blocked,
                      "关闭时，能在 Modrinth "
                      "上找到的模组和资源只记下载地址，整合包更小，安装时再下载；打开则把文件都放进整合包。");
        }
        body.end();

        exportButton(entry, covered);
        return back;
    }
}
