#include "ui/imgui/screens/settings/SettingsScreen.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "app/system/Catalog.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "app/system/Updates.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "app/instances/Instances.h"
#include "app/launch/JavaDownloads.h"
#include "app/launch/LaunchDefaults.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/launch/Patches.h"
#include "ui/imgui/screens/home/QuickActions.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"

#include <core/base/strings.hpp>

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/IconButtonWidget.h"
#include "ui/imgui/widget/NavItemWidget.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/SwatchWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef B_LAUNCHER_VERSION
#define B_LAUNCHER_VERSION "0.0.0"
#endif

namespace SettingsScreen {

    namespace blb = b_launcher_base;

    namespace {
        using Page::FIELD_H;
        constexpr float SELECT_W = 180.0f;
        constexpr float WIDE_FIELD = 300.0f;
        constexpr float SIDE_W = 140.0f;
        constexpr float NAV_ITEM_H = 30.0f;
        constexpr float NAV_STEP = 32.0f;
        float mainX() {
            return Page::left() + SIDE_W + 14.0f;
        }

        float mainW() {
            return Page::right() - mainX();
        }

        struct Mode {
            blb::instance::isolation_mode mode;
            const char* label;
        };

        const Mode MODES[] = {
                {blb::instance::isolation_mode::none, "不隔离"},
                {blb::instance::isolation_mode::modded, "仅隔离 Mod 实例"},
                {blb::instance::isolation_mode::all, "全部隔离"},
        };

        struct Language {
            LaunchDefaults::Language language;
            const char* label;
        };

        const Language LANGUAGES[] = {
                {LaunchDefaults::Language::None, "不自动设置"},
                {LaunchDefaults::Language::Chinese, "自动设置为中文"},
                {LaunchDefaults::Language::English, "自动设置为英文"},
        };

        struct Collector {
            blb::launch::gc_mode mode;
            const char* label;
        };

        const Collector COLLECTORS[] = {
                {blb::launch::gc_mode::zgc, "尽量使用 ZGC"},
                {blb::launch::gc_mode::generational_zgc, "尽量使用分代 ZGC"},
                {blb::launch::gc_mode::g1, "标准 G1GC"},
                {blb::launch::gc_mode::tuned_g1, "调优 G1GC"},
                {blb::launch::gc_mode::none, "不指定（可自定义）"},
        };

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

        void explain(const std::string& id, const std::string& text, float rowY, float labelRight, bool blocked) {
            if (!blocked && NewUi::hit(mainX(), rowY, labelRight - mainX(), Page::ROW_H)) {
                NewUi::markDoc(id, text, mainX() + 6.0f, rowY + Page::ROW_H + 10.0f);
            }
        }

        void select(const std::string& id, const std::shared_ptr<Choice>& choice, float rowY, bool blocked) {
            SelectWidget widget(id, choice);
            widget.bounds(Page::controlRight() - SELECT_W, rowY + Page::FIELD_Y, SELECT_W, FIELD_H).block(blocked);
            widget.paint();
        }

        void toggle(const std::string& id, std::function<bool()> read, std::function<void(bool)> write, float rowY,
                    bool blocked) {
            auto flag = std::make_shared<FlagBinding>(id, std::move(read), std::move(write));
            ToggleWidget widget(id, flag, true);
            widget.bounds(Page::controlRight() - 30.0f, rowY + (Page::ROW_H - 18.0f) * 0.5f, 30.0f, 18.0f)
                    .hitArea(mainX(), rowY, mainW(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void textRow(const std::string& id, const char* label, const char* doc, std::function<std::string()> read,
                     std::function<void(const std::string&)> write, float rowY, bool blocked, const char* hint = "无") {
            Page::label(label, rowY);
            auto binding = std::make_shared<TextBinding>(
                    id, std::move(read), [write](const std::string& value) { write(Ui::trim(value)); }, hint);
            binding->limit = 1024;
            float x = Page::controlRight() - WIDE_FIELD;
            TextFieldWidget field(binding);
            field.box(x, rowY + Page::FIELD_Y, WIDE_FIELD, FIELD_H)
                    .bounds(x, rowY + Page::FIELD_Y, WIDE_FIELD, FIELD_H)
                    .block(blocked);
            field.paint();
            explain(id, doc, rowY, x - 8.0f, blocked);
        }

        std::string fitTail(const std::string& text, float available) {
            return UiFont::fitStart(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, available);
        }

        void instanceRows(float y, bool blocked) {
            Page::label("默认版本隔离", y);
            select("settings#isolation",
                   std::make_shared<ModeBinding>(
                           "settings#isolation",
                           [] {
                               for (const Mode& mode : MODES) {
                                   if (mode.mode == Instances::isolation()) {
                                       return std::string(mode.label);
                                   }
                               }
                               return std::string();
                           },
                           [](const std::string& option) {
                               for (const Mode& mode : MODES) {
                                   if (option == mode.label) {
                                       Instances::setIsolation(mode.mode);
                                   }
                               }
                           },
                           [] {
                               std::vector<std::string> options;
                               for (const Mode& mode : MODES) {
                                   options.push_back(mode.label);
                               }
                               return options;
                           }),
                   y, blocked);
            y += Page::ROW_H;

            Page::label("实例列表", y);
            std::string count = Instances::loading() ? std::string("正在读取…")
                                                     : std::to_string(Instances::list().size()) + " 个实例";
            float countW = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float refreshX = Page::controlRight() - 27.0f;
            UiRender::textY(count, refreshX - 8.0f - countW, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (iconButton("settings#refresh", 0xf2f1, "重新读取", refreshX, y + 6.0f, blocked)) {
                Instances::refresh();
            }
        }

        void launchRows(float y, bool blocked) {
            Page::label("游戏语言", y);
            select("settings#language",
                   std::make_shared<ModeBinding>(
                           "settings#language",
                           [] {
                               for (const Language& language : LANGUAGES) {
                                   if (language.language == LaunchDefaults::language()) {
                                       return std::string(language.label);
                                   }
                               }
                               return std::string();
                           },
                           [](const std::string& option) {
                               for (const Language& language : LANGUAGES) {
                                   if (option == language.label) {
                                       LaunchDefaults::setLanguage(language.language);
                                   }
                               }
                           },
                           [] {
                               std::vector<std::string> options;
                               for (const Language& language : LANGUAGES) {
                                   options.push_back(language.label);
                               }
                               return options;
                           }),
                   y, blocked);
            explain("settings#language",
                    "启动时，如果游戏目录里还没有设置过语言（比如新实例第一次启动），就把游戏语言设为中文或英文；之后在"
                    "游戏里改的语言不会被覆盖。",
                    y, Page::controlRight() - SELECT_W - 8.0f, blocked);
            y += Page::ROW_H;

            Page::label("内存管理", y);
            select("settings#gc",
                   std::make_shared<ModeBinding>(
                           "settings#gc",
                           [] {
                               for (const Collector& collector : COLLECTORS) {
                                   if (collector.mode == LaunchDefaults::gc()) {
                                       return std::string(collector.label);
                                   }
                               }
                               return std::string();
                           },
                           [](const std::string& option) {
                               for (const Collector& collector : COLLECTORS) {
                                   if (option == collector.label) {
                                       LaunchDefaults::setGc(collector.mode);
                                   }
                               }
                           },
                           [] {
                               std::vector<std::string> options;
                               for (const Collector& collector : COLLECTORS) {
                                   options.push_back(collector.label);
                               }
                               return options;
                           }),
                   y, blocked);
            explain("settings#gc",
                    "游戏用的垃圾回收器。ZGC 停顿更短，需要 Java 15 以上，分代 ZGC 需要 Java 21 以上，Java "
                    "版本不够时改用标准 G1GC；调优 G1GC 是启动器默认的一组 G1 参数；不指定则不加回收器参数，可以在 JVM "
                    "参数里自己写。实例在高级设置里关闭默认 JVM 参数时，这里不生效。",
                    y, Page::controlRight() - SELECT_W - 8.0f, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#jvmArgs", "JVM 参数",
                    "所有实例都会加上，排在实例自己的 JVM 参数前面。空格分隔，含空格的参数用双引号括起来。",
                    [] { return LaunchDefaults::jvmArgs(); },
                    [](const std::string& value) { LaunchDefaults::setJvmArgs(value); }, y, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#gameArgs", "游戏参数",
                    "所有实例都会加上，排在实例自己的游戏参数前面。空格分隔，含空格的参数用双引号括起来。",
                    [] { return LaunchDefaults::gameArgs(); },
                    [](const std::string& value) { LaunchDefaults::setGameArgs(value); }, y, blocked);
            y += Page::ROW_H;

            Page::label("显示游戏日志", y);
            toggle(
                    "settings#showLogs", [] { return LaunchDefaults::showLogs(); },
                    [](bool on) { LaunchDefaults::setShowLogs(on); }, y, blocked);
            explain("settings#showLogs",
                    "游戏启动时打开日志窗口；实例可以在版本设置里另行设置，实例菜单的“测试游戏”总会打开。不显示时，游戏"
                    "异常退出也会自动打开。",
                    y, Page::controlRight() - 40.0f, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#preLaunch", "启动前命令",
                    "每个实例启动前通过 cmd "
                    "运行，在实例自己的启动前命令之前，启动器等它结束再启动游戏；工作目录是游戏目录，可用 "
                    "%INST_NAME%、%INST_DIR%、%INST_MC_DIR%、%INST_JAVA% 等环境变量。",
                    [] { return LaunchDefaults::preLaunch(); },
                    [](const std::string& value) { LaunchDefaults::setPreLaunch(value); }, y, blocked);
        }

        void backgroundFileRow(float y, bool blocked) {
            const std::string label = "背景图片文件";
            Page::label(label, y);
            std::string path = Background::path();
            float pickX = Page::controlRight() - 27.0f;
            float right = pickX - 8.0f;
            if (!path.empty()) {
                float resetX = pickX - 30.0f;
                if (iconButton("settings#backgroundReset", 0xf2ea, "恢复默认图片", resetX, y + 6.0f, blocked)) {
                    Background::setPath(std::string());
                }
                right = resetX - 8.0f;
            }
            if (iconButton("settings#backgroundPick", 0xf07c, "选择图片", pickX, y + 6.0f, blocked)) {
                std::filesystem::path start = path.empty() ? Platform::exeDirectory() : blb::path_from_utf8(path);
                if (std::optional<std::filesystem::path> picked = Platform::pickImage(start)) {
                    Background::setPath(blb::path_to_utf8(*picked));
                }
            }
            float labelRight = mainX() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            float room = std::max(0.0f, right - labelRight - (Background::preparing() ? 24.0f : 0.0f));
            std::string shown = path.empty() ? std::string("默认图片")
                                             : fitTail(Platform::display(blb::path_from_utf8(path)), room);
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (Background::preparing()) {
                Page::spinner(right - shownW - 16.0f, y + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
            }
        }

        int appearanceCount() {
            return Appearance::backgroundImage() ? 9 : 8;
        }

        void uiStyleRow(float y, bool blocked) {
            Page::label("界面风格", y);
            auto uiStyle = std::make_shared<ModeBinding>(
                    "settings#uiStyle", [] { return std::string(UiStyles::current().label); },
                    [](const std::string& option) {
                        for (const UiStyles::Style& candidate : UiStyles::all()) {
                            if (option == candidate.label) {
                                UiStyles::set(candidate.key);
                            }
                        }
                    },
                    [] {
                        std::vector<std::string> options;
                        for (const UiStyles::Style& candidate : UiStyles::all()) {
                            options.push_back(candidate.label);
                        }
                        return options;
                    });
            select("settings#uiStyle", uiStyle, y, blocked);
            float labelRight = Page::controlRight() - SELECT_W - 8.0f;
            if (&UiStyles::current() != &UiStyles::running()) {
                bool busy = Services::busy();
                std::string text = "立即重启";
                float w = Page::buttonWidth(text, 0xf2f1);
                float x = labelRight - w;
                if (Page::button("settings#restart", text, x, y + Page::FIELD_Y, w, true, blocked || busy, 0xf2f1) &&
                    !Platform::restart()) {
                    Notices::add(Notices::Kind::Failed, "重启失败，请手动重新打开启动器", 5000);
                }
                std::string hint = busy ? "任务进行中，暂不能重启" : "重启后生效";
                float hintW = UiFont::width(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                labelRight = x - 12.0f - hintW;
                UiRender::textY(hint, labelRight, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                labelRight -= 8.0f;
            }
            explain("settings#uiStyle", "忘却的旋律，开发中", y, labelRight, blocked);
        }

        void appearanceRows(float y, bool blocked) {
            uiStyleRow(y, blocked);
            y += Page::ROW_H;

            Page::label("主题", y);
            auto theme = std::make_shared<ModeBinding>(
                    "settings#theme", [] { return std::string(Appearance::theme().label); },
                    [](const std::string& option) {
                        for (const Appearance::Theme& candidate : Appearance::themes()) {
                            if (option == candidate.label) {
                                Appearance::setTheme(candidate.key);
                            }
                        }
                    },
                    [] {
                        std::vector<std::string> options;
                        for (const Appearance::Theme& candidate : Appearance::themes()) {
                            options.push_back(candidate.label);
                        }
                        return options;
                    });
            float themeX = Page::controlRight() - SELECT_W;
            select("settings#theme", theme, y, blocked);
            auto accent = std::make_shared<ColorBinding>(
                    "settings#accent", [] { return Appearance::accent(); },
                    [](uint32_t argb) { Appearance::setAccent(argb); });
            accent->resetTo(Appearance::defaultAccent());
            float swatchX = themeX - 12.0f - 15.0f;
            SwatchWidget swatch(accent);
            swatch.swatchAt(swatchX, y + Page::SWATCH_Y)
                    .picker(swatchX - 214.0f - 12.0f, y - 120.0f)
                    .hoverRing()
                    .block(blocked);
            swatch.paint();
            y += Page::ROW_H;

            Page::label("界面缩放", y);
            select("settings#zoom",
                   std::make_shared<ModeBinding>(
                           "settings#zoom", [] { return std::to_string(Appearance::zoom()) + "%"; },
                           [](const std::string& option) { Appearance::setZoom(std::atoi(option.c_str())); },
                           [] {
                               std::vector<std::string> options;
                               for (int percent : Appearance::zooms()) {
                                   options.push_back(std::to_string(percent) + "%");
                               }
                               return options;
                           }),
                   y, blocked);
            y += Page::ROW_H;

            Page::label("背景图片", y);
            toggle(
                    "settings#background", [] { return Appearance::backgroundImage(); },
                    [](bool on) { Appearance::setBackgroundImage(on); }, y, blocked);
            y += Page::ROW_H;

            if (Appearance::backgroundImage()) {
                backgroundFileRow(y, blocked);
                y += Page::ROW_H;
            }

            Page::label("界面音效", y);
            toggle(
                    "settings#sounds", [] { return Appearance::uiSounds(); },
                    [](bool on) { Appearance::setUiSounds(on); }, y, blocked);
            y += Page::ROW_H;

            Page::label("记住窗口大小", y);
            toggle(
                    "settings#rememberWindow", [] { return Appearance::rememberWindow(); },
                    [](bool on) { Appearance::setRememberWindow(on); }, y, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#windowTitle", "窗口标题", "任务栏和窗口切换时显示的名字，留空使用默认标题。",
                    [] { return Appearance::customWindowTitle(); },
                    [](const std::string& value) { Appearance::setWindowTitle(value); }, y, blocked,
                    Appearance::defaultWindowTitle());
            y += Page::ROW_H;

            textRow(
                    "settings#brandTitle", "左上角标题",
                    "侧栏左上角标志旁边的文字，留空使用默认标题；",
                    [] { return Appearance::customBrandTitle(); },
                    [](const std::string& value) { Appearance::setBrandTitle(value); }, y, blocked,
                    Appearance::defaultBrandTitle());
        }
    }

    namespace {
        constexpr int TAB_GAME = 0;
        constexpr int TAB_LAUNCHER = 1;
        constexpr int TAB_JAVA = 2;
        constexpr int TAB_ABOUT = 3;
        constexpr int TAB_PATCH = 4;
        PageTransition sections(TAB_GAME);

        struct Tab {
            int section;
            int glyph;
            const char* label;
        };

        const Tab TABS[] = {
                {TAB_GAME, 0xf11b, "游戏设置"},  {TAB_PATCH, 0xf462, "补丁"}, {TAB_LAUNCHER, 0xf1de, "启动器设置"},
                {TAB_JAVA, 0xf0f4, "Java 管理"}, {TAB_ABOUT, 0xf05a, "关于"},
        };

        void rail(bool covered) {
            float y = Page::TOP;
            for (const Tab& tab : TABS) {
                NavItemWidget item("settings#tab#" + std::to_string(tab.section), Ui::glyph(tab.glyph), tab.label);
                item.selected(sections.page == tab.section).bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
                item.paint();
                if (item.pressed) {
                    sections.go(tab.section);
                }
                y += NAV_STEP;
            }
        }

        void gameTab(bool covered) {
            const int instanceCount = 2;
            const int launchCount = 6;
            float contentH = 2 * Page::CAPTION_H + (instanceCount + launchCount) * Page::ROW_H + Page::GAP;
            Page::Scroll body("settings#game", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;

            if (Page::shown(y, Page::CAPTION_H + instanceCount * Page::ROW_H)) {
                Page::caption("实例", y);
                Page::card(y + Page::CAPTION_H, instanceCount);
                instanceRows(y + Page::CAPTION_H, blocked);
            }
            y += Page::CAPTION_H + instanceCount * Page::ROW_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + launchCount * Page::ROW_H)) {
                Page::caption("启动", y);
                Page::card(y + Page::CAPTION_H, launchCount);
                launchRows(y + Page::CAPTION_H, blocked);
            }
            body.end();
        }

        int homepageCount() {
            int kind = Homepage::source();
            return kind == Homepage::SOURCE_FILE || kind == Homepage::SOURCE_URL || kind == Homepage::SOURCE_NONE ? 2
                                                                                                                  : 1;
        }

        void homepageFileRow(float y, bool blocked) {
            const std::string label = "主页文件";
            Page::label(label, y);
            std::string path = Homepage::file();
            float pickX = Page::controlRight() - 27.0f;
            if (iconButton("settings#homepagePick", 0xf07c, "选择 XAML 文件", pickX, y + 6.0f, blocked)) {
                std::filesystem::path start =
                        path.empty() ? Platform::exeDirectory() : blb::path_from_utf8(path).parent_path();
                std::vector<std::filesystem::path> picked =
                        Platform::pickFiles(start, L"选择主页文件", L"XAML 文件", L"*.xaml;*.xml;*.txt");
                if (!picked.empty()) {
                    Homepage::setFile(blb::path_to_utf8(picked.front()));
                }
            }
            float right = pickX - 8.0f;
            float labelRight = mainX() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            std::string shown = path.empty()
                    ? std::string("未选择")
                    : fitTail(Platform::display(blb::path_from_utf8(path)), std::max(0.0f, right - labelRight));
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void homepageRows(float y, bool blocked) {
            Page::label("主页内容", y);
            select("settings#homepage",
                   std::make_shared<ModeBinding>(
                           "settings#homepage", [] { return Homepage::choice(); },
                           [](const std::string& value) { Homepage::choose(value); },
                           [] { return Homepage::choices(); }),
                   y, blocked);
            y += Page::ROW_H;
            if (Homepage::source() == Homepage::SOURCE_NONE) {
                Page::label("快捷操作", y);
                select("settings#quickActions", QuickActions::choice(), y, blocked);
                explain("settings#quickActions",
                        "显示在主页标志下方，可以多选，拖动右侧的手柄调整顺序；全部取消就不显示。", y,
                        Page::controlRight() - SELECT_W - 8.0f, blocked);
            } else if (Homepage::source() == Homepage::SOURCE_FILE) {
                homepageFileRow(y, blocked);
            } else if (Homepage::source() == Homepage::SOURCE_URL) {
                textRow(
                        "settings#homepageUrl", "主页网址", "PCL2 联网主页的地址，返回 XAML 内容",
                        [] { return Homepage::url(); }, [](const std::string& value) { Homepage::setUrl(value); }, y,
                        blocked);
            }
        }

        void launcherTab(bool covered) {
            int lookCount = appearanceCount();
            int homeCount = homepageCount();
            float contentH =
                    Page::CAPTION_H + lookCount * Page::ROW_H + Page::GAP + Page::CAPTION_H + homeCount * Page::ROW_H;
            Page::Scroll body("settings#launcher", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            Page::caption("外观", y);
            Page::card(y + Page::CAPTION_H, lookCount);
            appearanceRows(y + Page::CAPTION_H, blocked);
            y += Page::CAPTION_H + lookCount * Page::ROW_H + Page::GAP;
            Page::caption("主页", y);
            Page::card(y + Page::CAPTION_H, homeCount);
            homepageRows(y + Page::CAPTION_H, blocked);
            body.end();
        }

        const char* archName(blb::cpu_arch arch) {
            switch (arch) {
                case blb::cpu_arch::x86: return "x86";
                case blb::cpu_arch::x86_64: return "x64";
                case blb::cpu_arch::arm32: return "ARM32";
                case blb::cpu_arch::arm64: return "ARM64";
                default: return "";
            }
        }

        float tag(const std::string& text, float x, float rowY, uint32_t fill, uint32_t ink) {
            float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 12.0f;
            float y = rowY + (Page::ROW_H - 18.0f) * 0.5f;
            UiRender::roundRect(x, y, w, 18.0f, 6.0f, fill);
            UiRender::textY(text, x + 6.0f, y, 18.0f, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            return w;
        }

        void javaMenu(const blb::java::java_installation& java, float x, float y) {
            std::filesystem::path executable = java.executable;
            std::filesystem::path home = java.home;
            NewUi::openMenu(
                    "settings#java#menu",
                    [executable, home] {
                        MenuItems items;
                        items.push_back(
                                ActionMenuItem::make("打开所在文件夹", 0xf07c, [home] { Platform::open(home); }));
                        items.push_back(ActionMenuItem::make("从列表中移除", 0xf2ed,
                                                             [executable] { Catalog::removeJava(executable); })
                                                ->dangerous()
                                                ->confirmFirst());
                        return items;
                    },
                    x, y, 170.0f);
        }

        PageTransition javaViews(0);
        int javaMajor = 21;

        void javaList(bool covered) {
            const Catalog::JavaList& javas = Catalog::java();
            bool loading = javas.status == Catalog::Status::Loading;
            std::string count = loading && javas.list.empty() ? std::string("正在查找…")
                                                              : "共 " + std::to_string(javas.list.size()) + " 个";
            UiRender::textY(count, mainX() + 2.0f, Page::TOP, NAV_ITEM_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float refreshX = Page::right() - 28.0f;
            if (iconButton("settings#java#refresh", 0xf2f1, "重新查找", refreshX, Page::TOP + 1.0f,
                           covered || loading)) {
                Catalog::refreshJava();
            }
            std::string add = "添加";
            float addW = Page::buttonWidth(add, 0xf067);
            float addX = refreshX - 8.0f - addW;
            if (Page::button("settings#java#add", add, addX, Page::TOP + 4.0f, addW, false, covered, 0xf067)) {
                if (std::optional<std::filesystem::path> picked = Platform::pickJava(std::filesystem::path())) {
                    Catalog::addJava(*picked);
                }
            }
            std::string download = "下载";
            float downloadW = Page::buttonWidth(download, 0xf019);
            if (Page::button("settings#java#download", download, addX - 8.0f - downloadW, Page::TOP + 4.0f, downloadW,
                             true, covered, 0xf019)) {
                javaViews.go(1);
            }

            float top = Page::TOP + 40.0f;
            float bottom = MainWindow::height() - 14.0f;
            if (javas.list.empty()) {
                std::string text = loading ? "正在查找 Java…" : "没有找到 Java，点击“添加”选择 java.exe";
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float lead = loading ? 24.0f : 0.0f;
                float x = mainX() + (mainW() - w - lead) * 0.5f;
                if (loading) {
                    Page::spinner(x + 8.0f, top + 82.0f, 8.0f, UiTheme::accent());
                }
                UiRender::textY(text, x + lead, top + 70.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return;
            }
            int rows = (int) javas.list.size();
            Page::Scroll body("settings#java", top, bottom, rows * Page::ROW_H);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y0 = body.top - body.offset;
            Page::card(y0, rows);
            std::vector<blb::java::java_installation> list = javas.list;
            for (int i = 0; i < rows; i++) {
                float rowY = y0 + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const blb::java::java_installation& java = list[i];
                std::string id = "settings#java#" + std::to_string(i);
                bool hovered = !blocked && NewUi::hit(mainX(), rowY, mainW(), Page::ROW_H);
                Page::rowHover(id, rowY, hovered, (i == 0 ? 1 | 2 : 0) | (i == rows - 1 ? 4 | 8 : 0));
                UiWidgets::icon(Ui::glyph(0xf0f4), mainX() + 24.0f, rowY + Page::ROW_H * 0.5f, UiTheme::textMuted(),
                                12.0f);
                std::string name = "Java " + (java.version.empty() ? std::to_string(java.major) : java.version);
                float x = mainX() + 44.0f;
                UiRender::textY(name, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                x += UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                x += tag(java.jdk ? "JDK" : "JRE", x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                std::string arch = archName(java.arch);
                if (!arch.empty()) {
                    x += tag(arch, x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                }
                if (Catalog::addedByHand(java.executable)) {
                    x += tag("手动添加", x, rowY, UiTheme::accentSoft(40), UiTheme::accentText()) + 4.0f;
                }
                float menuX = Page::controlRight() - 28.0f;
                std::string where = Platform::display(java.home);
                float room = menuX - 12.0f - (x + 8.0f);
                std::string shown =
                        UiFont::fitStart(where, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, std::max(0.0f, room));
                float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float whereX = menuX - 12.0f - shownW;
                UiRender::textY(shown, whereX, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_SMALL);
                if (shown != where && !blocked && NewUi::hit(whereX, rowY + 8.0f, shownW, Page::ROW_H - 16.0f)) {
                    tip(where, whereX, rowY + 8.0f, shownW, Page::ROW_H - 16.0f);
                }
                if (iconButton(id + "#menu", 0xf141, "更多操作", menuX, rowY + (Page::ROW_H - 28.0f) * 0.5f, blocked)) {
                    javaMenu(java, menuX - 142.0f, rowY + Page::ROW_H - 4.0f);
                }
            }
            body.end();
        }

        void javaRing(float cx, float cy, float fraction) {
            const float tau = 6.28318531f;
            float r = UiRender::transformW(7.0f);
            float stroke = UiRender::transformW(1.7f);
            float x = UiRender::transformX(cx);
            float y = UiRender::transformY(cy);
            ShaderUtil::arc(x, y, r, stroke, 0.0f, tau,
                            UiRender::applyAlpha((UiTheme::accent() & 0x00FFFFFFu) | 0x40000000u));
            if (fraction > 0.001f) {
                ShaderUtil::arc(x, y, r, stroke, -tau * 0.25f, -tau * 0.25f + tau * std::min(1.0f, fraction),
                                UiRender::applyAlpha(UiTheme::accent()));
            }
        }

        std::shared_ptr<ModeBinding> majorChoice(const std::vector<JavaDownloads::Package>& packages) {
            std::vector<std::string> options = {"全部版本"};
            for (const JavaDownloads::Package& package : packages) {
                std::string label = "Java " + std::to_string(package.major);
                if (std::find(options.begin(), options.end(), label) == options.end()) {
                    options.push_back(label);
                }
            }
            return std::make_shared<ModeBinding>(
                    "settings#java#major",
                    [] { return javaMajor == 0 ? std::string("全部版本") : "Java " + std::to_string(javaMajor); },
                    [](const std::string& value) {
                        javaMajor = value.rfind("Java ", 0) == 0 ? std::atoi(value.c_str() + 5) : 0;
                    },
                    [options] { return options; });
        }

        void javaDownloads(bool covered) {
            const JavaDownloads::List& list = JavaDownloads::list();
            bool loading = list.status == JavaDownloads::Status::Loading;
            if (iconButton("settings#java#back", 0xf053, "返回", mainX() - 4.0f, Page::TOP + 1.0f, covered)) {
                javaViews.go(0);
            }
            UiRender::textY("下载 Java", mainX() + 30.0f, Page::TOP, NAV_ITEM_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
            float refreshX = Page::right() - 28.0f;
            if (iconButton("settings#java#reload", 0xf2f1, "刷新列表", refreshX, Page::TOP + 1.0f,
                           covered || loading)) {
                JavaDownloads::refresh();
            }
            SelectWidget major("settings#java#major", majorChoice(list.packages));
            major.bounds(refreshX - 8.0f - SELECT_W, Page::TOP + 4.0f, SELECT_W, FIELD_H).block(covered || loading);
            major.paint();

            float top = Page::TOP + 40.0f;
            float bottom = MainWindow::height() - 14.0f;
            std::vector<const JavaDownloads::Package*> shown;
            for (const JavaDownloads::Package& package : list.packages) {
                if (javaMajor == 0 || package.major == javaMajor) {
                    shown.push_back(&package);
                }
            }
            if (shown.empty()) {
                std::string text = loading                             ? "正在获取 Java 列表…"
                        : list.status == JavaDownloads::Status::Failed ? "获取 Java 列表失败：" + list.error
                        : list.packages.empty()                        ? "列表是空的"
                                                                       : "没有这个版本的 Java";
                text = UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, mainW() - 40.0f);
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float lead = loading ? 24.0f : 0.0f;
                float x = mainX() + (mainW() - w - lead) * 0.5f;
                if (loading) {
                    Page::spinner(x + 8.0f, top + 82.0f, 8.0f, UiTheme::accent());
                }
                UiRender::textY(text, x + lead, top + 70.0f, 24.0f, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return;
            }
            int rows = (int) shown.size();
            Page::Scroll body("settings#java#downloads", top, bottom, rows * Page::ROW_H);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y0 = body.top - body.offset;
            Page::card(y0, rows);
            for (int i = 0; i < rows; i++) {
                float rowY = y0 + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const JavaDownloads::Package& package = *shown[i];
                std::string id = "settings#java#get#" + package.folder;
                bool hovered = !blocked && NewUi::hit(mainX(), rowY, mainW(), Page::ROW_H);
                Page::rowHover(id, rowY, hovered, (i == 0 ? 1 | 2 : 0) | (i == rows - 1 ? 4 | 8 : 0));
                float cy = rowY + Page::ROW_H * 0.5f;
                UiWidgets::icon(Ui::glyph(0xf0f4), mainX() + 24.0f, cy, UiTheme::textMuted(), 12.0f);

                JavaDownloads::State state = JavaDownloads::state(package.folder);
                bool done = state == JavaDownloads::State::Done || JavaDownloads::installed(package);
                float right = Page::controlRight();
                float actionX = right;
                if (state == JavaDownloads::State::Running) {
                    javaRing(right - 10.0f, cy, JavaDownloads::progress(package.folder));
                    actionX = right - 24.0f;
                } else if (done) {
                    std::string text = "已安装";
                    float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    UiRender::textY(text, right - w, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    UiWidgets::check(right - w - 12.0f, cy, UiTheme::accent(), 9.0f);
                    actionX = right - w - 22.0f;
                } else {
                    std::string text = "安装";
                    float w = Page::buttonWidth(text, 0xf019);
                    if (Page::button(id + "#install", text, right - w, rowY + Page::FIELD_Y, w, false, blocked,
                                     0xf019)) {
                        JavaDownloads::install(package);
                    }
                    actionX = right - w;
                }
                std::string size = package.archiveSize > 0
                        ? std::to_string((package.archiveSize + 500000) / 1000000) + " MB"
                        : std::string();
                float sizeW = size.empty() ? 0.0f : UiFont::width(size, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                if (!size.empty()) {
                    UiRender::textY(size, actionX - 12.0f - sizeW, rowY, Page::ROW_H, UiTheme::textMuted(),
                                    UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                }
                float x = mainX() + 44.0f;
                float tags = UiFont::width(package.version, UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 16.0f +
                        (package.preview ? UiFont::width("预览", UiFont::MEDIUM, UiWidgets::TEXT_SMALL) + 16.0f : 0.0f);
                float room = std::max(40.0f, actionX - 24.0f - sizeW - x - tags);
                std::string name =
                        UiFont::fitEnd(JavaDownloads::name(package), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room);
                UiRender::textY(name, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                x += UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                x += tag(package.version, x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                if (package.preview) {
                    tag("预览", x, rowY, UiTheme::accentSoft(40), UiTheme::accentText());
                }
            }
            body.end();
        }

        void javaTab(bool covered) {
            javaViews.begin();
            if (javaViews.page == 1) {
                javaDownloads(covered);
            } else {
                javaList(covered);
            }
            javaViews.end();
        }

        struct Link {
            const char* name;
            const char* value;
            const char* url;
        };

        const Link PROJECT[] = {
                {"作者", "cubk", "https://cubk.cn/"},
                {"GitHub", "cubk1/b-launcher", "https://github.com/cubk1/b-launcher"},
        };

        const Link THANKS[] = {
                {"BMCLAPI", "提供下载源", "https://www.bangbang93.com/"},
                {"PCL", "提供主页系统", "https://github.com/Meloong-Git/PCL"},
        };

        const Link CREDITS[] = {
                {"Dear ImGui", "MIT", "https://github.com/ocornut/imgui"},
                {"FreeType", "FreeType License", "https://freetype.org"},
                {"libcurl", "curl License", "https://curl.se"},
                {"zlib-ng", "zlib License", "https://github.com/zlib-ng/zlib-ng"},
                {"minizip-ng", "zlib License", "https://github.com/zlib-ng/minizip-ng"},
                {"simdjson", "Apache-2.0", "https://github.com/simdjson/simdjson"},
                {"JSON for Modern C++", "MIT", "https://github.com/nlohmann/json"},
                {"reproc", "MIT", "https://github.com/DaanDeMeyer/reproc"},
                {"stb", "MIT", "https://github.com/nothings/stb"},
                {"nanosvg", "zlib License", "https://github.com/memononen/nanosvg"},
                {"Font Awesome Free", "SIL OFL 1.1", "https://fontawesome.com"},
        };

        constexpr int PROJECT_COUNT = (int) (sizeof(PROJECT) / sizeof(PROJECT[0]));
        constexpr int THANKS_COUNT = (int) (sizeof(THANKS) / sizeof(THANKS[0]));
        constexpr int CREDIT_COUNT = (int) (sizeof(CREDITS) / sizeof(CREDITS[0]));

        constexpr float HERO_H = 72.0f;
        const char* const REPOSITORY = "https://github.com/cubk1/b-launcher";
        constexpr float LOGO = 32.0f;

        void quietButton(float x, float y, float w, const std::string& text, bool spinning) {
            UiRender::roundRect(x, y, w, Page::FIELD_H, 7.0f, UiTheme::surfaceRaised());
            UiRender::outline(x, y, w, Page::FIELD_H, 7.0f, UiTheme::outlineSoft());
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float left = x + (w - (12.0f + 6.0f + textW)) * 0.5f;
            float cy = y + Page::FIELD_H * 0.5f;
            if (spinning) {
                Page::spinner(left + 6.0f, cy, 5.0f, UiTheme::accent());
            } else {
                UiWidgets::check(left + 6.0f, cy, UiTheme::accent(), 8.0f);
            }
            UiRender::textY(text, left + 18.0f, y, Page::FIELD_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
        }

        void updateControl(float y, float leftEnd, bool blocked) {
            Updates::State state = Updates::state();
            float by = y + (HERO_H - Page::FIELD_H) * 0.5f;
            float right = mainX() + mainW() - 20.0f;
            if (state == Updates::State::Checking || state == Updates::State::Latest) {
                std::string text = state == Updates::State::Checking ? "正在检查" : "已是最新版本";
                float w = Page::buttonWidth(text, 0xf00c);
                quietButton(right - w, by, w, text, state == Updates::State::Checking);
                return;
            }
            bool available = state == Updates::State::Available;
            std::string label = available ? "下载 v" + Updates::latest() : std::string("检查更新");
            int glyph = available ? 0xf019 : 0xf2f1;
            float w = Page::buttonWidth(label, glyph);
            float x = right - w;
            if (Page::button("settings#update", label, x, by, w, available, blocked, glyph)) {
                if (available) {
                    Updates::openDownload();
                } else {
                    Updates::check(false, true);
                }
            }
            if (state != Updates::State::Failed) {
                return;
            }
            std::string status = "检查失败";
            float statusW = UiFont::width(status, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float statusX = x - 12.0f - statusW;
            if (statusX >= leftEnd + 12.0f) {
                UiRender::textY(status, statusX, y, HERO_H, UiTheme::danger(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            }
        }

        void tagline(float x, float y, bool blocked) {
            const float h = 16.0f;
            const std::string lead = "an ";
            const std::string link = "open-source";
            const std::string rest = " minecraft launcher";
            UiRender::textY(lead, x, y, h, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float linkX = x + UiFont::width(lead, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            float linkW = UiFont::width(link, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            bool hovered = !blocked && NewUi::hit(linkX, y, linkW, h);
            float lit = UiState::motion("settings#about#source", hovered ? 1.0f : 0.0f, 20.0f);
            uint32_t ink = UiRender::mix(UiTheme::accentText(), UiTheme::accent(), lit);
            UiRender::textY(link, linkX, y, h, ink, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (lit > 0.001f) {
                float saved = UiRender::alphaMul;
                UiRender::alphaMul = saved * lit;
                UiRender::hLine(linkX, linkX + linkW, y + h - 1.0f, ink);
                UiRender::alphaMul = saved;
            }
            UiRender::textY(rest, linkX + linkW, y, h, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            if (hovered) {
                UiCursor::hand();
            }
            if (!blocked && NewUi::click(linkX, y, linkW, h)) {
                UiSound::request(UiSound::PAGE);
                Platform::openUrl(REPOSITORY);
            }
        }

        void hero(float y, bool blocked) {
            UiRender::roundRect(mainX(), y, mainW(), HERO_H, 12.0f, UiTheme::surface());
            UiRender::outline(mainX(), y, mainW(), HERO_H, 12.0f, UiTheme::outlineSoft());
            Logo::draw(mainX() + 20.0f, y + (HERO_H - LOGO) * 0.5f, LOGO);
            const std::string name = "B Launcher\xE2\x84\xA2";
            float textX = mainX() + 20.0f + LOGO + 14.0f;
            float titleY = y + (HERO_H - 24.0f - 2.0f - 16.0f) * 0.5f;
            float x = textX;
            UiRender::textY(name, x, titleY, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            x += UiFont::width(name, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            x += tag(std::string("v") + B_LAUNCHER_VERSION, x, titleY + 12.0f - Page::ROW_H * 0.5f,
                     UiTheme::accentSoft(40), UiTheme::accentText());
            tagline(textX, titleY + 26.0f, blocked);
            updateControl(y, x, blocked);
        }

        void linkRows(const char* prefix, const Link* links, int count, float y, bool blocked) {
            for (int i = 0; i < count; i++) {
                const Link& link = links[i];
                float rowY = y + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                std::string id = std::string(prefix) + link.name;
                bool hovered = !blocked && NewUi::hit(mainX(), rowY, mainW(), Page::ROW_H);
                Page::rowHover(id, rowY, hovered, (i == 0 ? 1 | 2 : 0) | (i == count - 1 ? 4 | 8 : 0));
                Page::label(link.name, rowY);
                float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
                float linkX = Page::controlRight() - 6.0f;
                UiWidgets::icon(Ui::glyph(0xf35d), linkX, rowY + Page::ROW_H * 0.5f,
                                UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), amount), 10.0f);
                float w = UiFont::width(link.value, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(link.value, linkX - 18.0f - w, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                if (!blocked && NewUi::click(mainX(), rowY, mainW(), Page::ROW_H)) {
                    UiSound::request(UiSound::PAGE);
                    Platform::openUrl(link.url);
                }
            }
        }

        float linkCard(const char* caption, const char* prefix, const Link* links, int count, float y, bool blocked) {
            if (Page::shown(y, Page::CAPTION_H + count * Page::ROW_H)) {
                Page::caption(caption, y);
                Page::card(y + Page::CAPTION_H, count);
                linkRows(prefix, links, count, y + Page::CAPTION_H, blocked);
            }
            return y + Page::CAPTION_H + count * Page::ROW_H + Page::GAP;
        }

        struct PatchOption {
            const char* value;
            const char* label;
        };

        const std::vector<PatchOption> SKIN_MODES = {
                {"fallback", "只补全没有皮肤的玩家"},
                {"override", "强制使用自定义皮肤"},
        };

        const std::vector<PatchOption> SKIN_SOURCES = {
                {"mojang", "Mojang"},
                {"littleskin", "LittleSkin"},
                {"custom", "自定义"},
        };

        std::string patchId(const char* key) {
            return std::string("settings#patch#") + key;
        }

        void patchToggle(const char* label, const char* key, const char* doc, float y, bool blocked) {
            Page::label(label, y);
            std::string id = patchId(key);
            toggle(id, [key] { return Patches::flag(key); }, [key](bool on) { Patches::setFlag(key, on); }, y, blocked);
            explain(id, doc, y, Page::controlRight() - 40.0f, blocked);
        }

        void patchSelect(const char* label, const char* key, const std::vector<PatchOption>& options, const char* doc,
                         float y, bool blocked) {
            Page::label(label, y);
            std::string id = patchId(key);
            select(id,
                   std::make_shared<ModeBinding>(
                           id,
                           [key, &options] {
                               std::string current = Patches::value(key);
                               for (const PatchOption& option : options) {
                                   if (current == option.value) {
                                       return std::string(option.label);
                                   }
                               }
                               return std::string(options.front().label);
                           },
                           [key, &options](const std::string& picked) {
                               for (const PatchOption& option : options) {
                                   if (picked == option.label) {
                                       Patches::setValue(key, option.value);
                                   }
                               }
                           },
                           [&options] {
                               std::vector<std::string> labels;
                               for (const PatchOption& option : options) {
                                   labels.push_back(option.label);
                               }
                               return labels;
                           }),
                   y, blocked);
            explain(id, doc, y, Page::controlRight() - SELECT_W - 8.0f, blocked);
        }

        float patchText(const char* label, const char* key, const char* hint, const char* doc, float y, bool blocked,
                        float lead, int limit) {
            Page::label(label, y);
            std::string id = patchId(key);
            auto binding = std::make_shared<TextBinding>(
                    id, [key] { return Patches::value(key); },
                    [key](const std::string& text) { Patches::setValue(key, Ui::trim(text)); }, hint);
            binding->limit = limit;
            float x = Page::controlRight() - WIDE_FIELD;
            TextFieldWidget field(binding);
            field.box(x, y + Page::FIELD_Y, WIDE_FIELD, FIELD_H)
                    .bounds(x, y + Page::FIELD_Y, WIDE_FIELD, FIELD_H)
                    .block(blocked);
            field.paint();
            explain(id, doc, y, x - lead - 8.0f, blocked);
            return x;
        }

        void pathRow(const char* label, const char* key, const char* hint, const char* doc, bool png, float y,
                     bool blocked) {
            float x = patchText(label, key, hint, doc, y, blocked, 36.0f, 1024);
            if (!iconButton(patchId(key) + "#pick", 0xf07c, "选择图片", x - 36.0f, y + 6.0f, blocked)) {
                return;
            }
            std::string current = Patches::value(key);
            bool local = !current.empty() && current.find(',') == std::string::npos &&
                    current.find("://") == std::string::npos;
            std::filesystem::path start = local ? blb::path_from_utf8(current).parent_path() : Platform::exeDirectory();
            std::optional<std::filesystem::path> picked;
            if (png) {
                std::vector<std::filesystem::path> files =
                        Platform::pickFiles(start, L"选择图片", L"PNG 图片", L"*.png");
                if (!files.empty()) {
                    picked = files.front();
                }
            } else {
                picked = Platform::pickImage(start);
            }
            if (picked) {
                Patches::setValue(key, blb::path_to_utf8(*picked));
            }
        }

        int skinCount() {
            if (!Patches::flag("skin.authlib")) {
                return 2;
            }
            std::string source = Patches::value("skin.source");
            return source == "custom" ? 7 : source == "littleskin" ? 5 : 6;
        }

        void enableRows(float y, bool blocked) {
            Page::label("为所有实例启用", y);
            toggle(
                    "settings#patch#all", [] { return Patches::all(); }, [](bool on) { Patches::setAll(on); }, y,
                    blocked);
            explain("settings#patch#all",
                    "关闭时，只有在版本设置里把“启动器补丁”设为启用的实例会加载补丁；开启后，实例也可以单独设为不启用。"
                    "补丁和 authlib-injector、自己在 JVM 参数里添加的 -javaagent 可以同时使用。",
                    y, Page::controlRight() - 40.0f, blocked);
        }

        void skinRows(float y, bool blocked) {
            patchToggle("修复旧版皮肤", "skin.legacy",
                        "1.7.5 及以下版本使用的 s3.amazonaws.com、skins.minecraft.net 皮肤地址已经失效，改为从皮肤来源"
                        "加载。",
                        y, blocked);
            y += Page::ROW_H;
            patchToggle("自定义皮肤", "skin.authlib",
                        "通过 authlib 从皮肤来源加载皮肤和披风，适用于 1.7.6 及以上版本。", y, blocked);
            y += Page::ROW_H;
            if (!Patches::flag("skin.authlib")) {
                return;
            }
            patchSelect("皮肤优先级", "skin.mode", SKIN_MODES,
                        "只补全没有皮肤的玩家：正版玩家自带的皮肤不变，只给没有皮肤的玩家（如离线玩家）使用皮肤来源。"
                        "强制使用自定义皮肤：先查皮肤来源，皮肤来源没有的材质再用玩家原有的。",
                        y, blocked);
            y += Page::ROW_H;
            patchSelect("皮肤来源", "skin.source", SKIN_SOURCES,
                        "Mojang：从正版服务器获取皮肤；LittleSkin：按玩家名从 LittleSkin 皮肤站获取；自定义：使用选择的"
                        "皮肤和披风图片。",
                        y, blocked);
            y += Page::ROW_H;
            std::string source = Patches::value("skin.source");
            if (source == "custom") {
                pathRow("皮肤图片", "skin.template.skin", "不使用",
                        "PNG 皮肤图片，也可以填写网址；手臂粗细会自动识别。", true, y, blocked);
                y += Page::ROW_H;
                pathRow("披风图片", "skin.template.cape", "不使用", "PNG 披风图片，也可以填写网址。", true, y, blocked);
                y += Page::ROW_H;
            } else if (source != "littleskin") {
                patchText("正版玩家名", "skin.player", "留空则按玩家自己的名字查询",
                          "填写后使用这个正版玩家的皮肤和披风，启动游戏时获取；留空则按每个玩家自己的名字查询。", y,
                          blocked, 0.0f, 16);
                y += Page::ROW_H;
            }
            patchToggle("标记为已签名", "skin.trusted",
                        "1.20.3 起游戏只显示其他玩家已签名的材质，开启后自定义材质会被当作已签名。", y, blocked);
        }

        void fixRows(float y, bool blocked) {
            patchToggle("Log4Shell 漏洞修复", "log4j.enabled", "修复 log4j2 的远程代码执行漏洞（CVE-2021-44228）。", y,
                        blocked);
            y += Page::ROW_H;
            patchToggle("中文输入修复", "input.enabled", "修复 1.12.2 及以下版本（LWJGL 2）无法输入中文的问题。", y,
                        blocked);
            y += Page::ROW_H;
            patchToggle("LWJGL 内存访问", "lwjgl.unsafe",
                        "在 Java 25 以上运行 LWJGL 3 时，让 MemoryUtil 改用 jdk.internal.misc.Unsafe，避开 "
                        "sun.misc.Unsafe 的弃用路径；LWJGL 3.4 会启用自带的对应后端。",
                        y, blocked);
        }

        void windowRows(float y, bool blocked) {
            patchText("窗口标题", "window.title", "不修改", "{title} 代表游戏原来的标题，例如 {title} - 我的服务器。",
                      y, blocked, 0.0f, 1024);
            y += Page::ROW_H;
            pathRow("窗口图标", "window.icon", "不修改",
                    "图片路径，多个不同尺寸的图片用英文逗号分隔；支持 PNG、ICO、BMP、GIF 等格式。", false, y, blocked);
        }

        void debugRows(float y, bool blocked) {
            patchToggle("输出调试日志", "debug", "在游戏日志里输出每个被修改的类和每次皮肤查询。", y, blocked);
        }

        float patchCard(const char* title, int rows, float y, const std::function<void(float)>& paint) {
            if (Page::shown(y, Page::CAPTION_H + rows * Page::ROW_H)) {
                Page::caption(title, y);
                Page::card(y + Page::CAPTION_H, rows);
                paint(y + Page::CAPTION_H);
            }
            return y + Page::CAPTION_H + rows * Page::ROW_H + Page::GAP;
        }

        void patchTab(bool covered) {
            int skins = skinCount();
            float contentH = 5 * Page::CAPTION_H + (1 + skins + 3 + 2 + 1) * Page::ROW_H + 4 * Page::GAP;
            Page::Scroll body("settings#patch", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            y = patchCard("启用", 1, y, [blocked](float top) { enableRows(top, blocked); });
            y = patchCard("皮肤", skins, y, [blocked](float top) { skinRows(top, blocked); });
            y = patchCard("修复", 3, y, [blocked](float top) { fixRows(top, blocked); });
            y = patchCard("窗口", 2, y, [blocked](float top) { windowRows(top, blocked); });
            patchCard("调试", 1, y, [blocked](float top) { debugRows(top, blocked); });
            body.end();
        }

        int aboutFrame = -10;

        void aboutTab(bool covered) {
            int frame = ImGui::GetFrameCount();
            if (frame - aboutFrame > 1 && Updates::state() != Updates::State::Checking) {
                Updates::check(false, false);
            }
            aboutFrame = frame;
            float contentH = HERO_H + Page::GAP + 3 * Page::CAPTION_H +
                    (PROJECT_COUNT + THANKS_COUNT + CREDIT_COUNT) * Page::ROW_H + 2 * Page::GAP;
            Page::Scroll body("settings#about", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;

            if (Page::shown(y, HERO_H)) {
                hero(y, blocked);
            }
            y += HERO_H + Page::GAP;
            y = linkCard("项目", "settings#project#", PROJECT, PROJECT_COUNT, y, blocked);
            y = linkCard("鸣谢", "settings#thanks#", THANKS, THANKS_COUNT, y, blocked);
            linkCard("开源组件", "settings#credit#", CREDITS, CREDIT_COUNT, y, blocked);
            body.end();
        }
    }

    void draw() {
        bool covered = NewUi::overlayCoversMouse();
        rail(covered);
        sections.begin();
        Page::Column column(mainX());
        switch (sections.page) {
            case TAB_LAUNCHER: launcherTab(covered); break;
            case TAB_JAVA: javaTab(covered); break;
            case TAB_ABOUT: aboutTab(covered); break;
            case TAB_PATCH: patchTab(covered); break;
            default: gameTab(covered); break;
        }
        sections.end();
    }
}
