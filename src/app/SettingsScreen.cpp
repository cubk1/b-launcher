#include "SettingsScreen.h"
#include "Appearance.h"
#include "Background.h"
#include "Catalog.h"
#include "Icons.h"
#include "Instances.h"
#include "LaunchDefaults.h"
#include "MainWindow.h"
#include "Page.h"
#include "Platform.h"

#include <core/base/strings.hpp>

#include "ui/menu/MenuItems.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiPageTransition.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"
#include "ui/value/Bind.h"
#include "ui/widget/IconButtonWidget.h"
#include "ui/widget/NavItemWidget.h"
#include "ui/widget/SelectWidget.h"
#include "ui/widget/SwatchWidget.h"
#include "ui/widget/TextFieldWidget.h"
#include "ui/widget/ToggleWidget.h"

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
        constexpr float MAIN_X = Page::LEFT + SIDE_W + 14.0f;

        float mainW() {
            return Page::right() - MAIN_X;
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
            if (!blocked && NewUi::hit(MAIN_X, rowY, labelRight - MAIN_X, Page::ROW_H)) {
                NewUi::markDoc(id, text, MAIN_X + 6.0f, rowY + Page::ROW_H + 10.0f);
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
                    .hitArea(MAIN_X, rowY, mainW(), Page::ROW_H)
                    .block(blocked);
            widget.paint();
        }

        void textRow(const std::string& id, const char* label, const char* doc, std::function<std::string()> read,
                     std::function<void(const std::string&)> write, float rowY, bool blocked) {
            Page::label(label, rowY);
            auto binding = std::make_shared<TextBinding>(
                    id, std::move(read), [write](const std::string& value) { write(Ui::trim(value)); }, "无");
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
            float labelRight = MAIN_X + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
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
            return Appearance::backgroundImage() ? 6 : 5;
        }

        void appearanceRows(float y, bool blocked) {
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
        }
    }

    namespace {
        constexpr int TAB_GAME = 0;
        constexpr int TAB_LAUNCHER = 1;
        constexpr int TAB_JAVA = 2;
        constexpr int TAB_ABOUT = 3;
        PageTransition sections(TAB_GAME);

        struct Tab {
            int section;
            int glyph;
            const char* label;
        };

        const Tab TABS[] = {
                {TAB_GAME, 0xf11b, "游戏设置"},
                {TAB_LAUNCHER, 0xf1de, "启动器设置"},
                {TAB_JAVA, 0xf0f4, "Java 管理"},
                {TAB_ABOUT, 0xf05a, "关于"},
        };

        void rail(bool covered) {
            float y = Page::TOP;
            for (const Tab& tab : TABS) {
                NavItemWidget item("settings#tab#" + std::to_string(tab.section), Ui::glyph(tab.glyph), tab.label);
                item.selected(sections.page == tab.section).bounds(Page::LEFT, y, SIDE_W, NAV_ITEM_H).block(covered);
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

        void launcherTab(bool covered) {
            int lookCount = appearanceCount();
            float contentH = Page::CAPTION_H + lookCount * Page::ROW_H;
            Page::Scroll body("settings#launcher", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            Page::caption("外观", y);
            Page::card(y + Page::CAPTION_H, lookCount);
            appearanceRows(y + Page::CAPTION_H, blocked);
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

        void javaTab(bool covered) {
            const Catalog::JavaList& javas = Catalog::java();
            bool loading = javas.status == Catalog::Status::Loading;
            std::string count = loading && javas.list.empty() ? std::string("正在查找…")
                                                              : "共 " + std::to_string(javas.list.size()) + " 个";
            UiRender::textY(count, MAIN_X + 2.0f, Page::TOP, NAV_ITEM_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            float refreshX = Page::right() - 28.0f;
            if (iconButton("settings#java#refresh", 0xf2f1, "重新查找", refreshX, Page::TOP + 1.0f,
                           covered || loading)) {
                Catalog::refreshJava();
            }
            std::string add = "添加";
            float addW = Page::buttonWidth(add, 0xf067);
            if (Page::button("settings#java#add", add, refreshX - 8.0f - addW, Page::TOP + 4.0f, addW, false, covered,
                             0xf067)) {
                if (std::optional<std::filesystem::path> picked = Platform::pickJava(std::filesystem::path())) {
                    Catalog::addJava(*picked);
                }
            }

            float top = Page::TOP + 40.0f;
            float bottom = MainWindow::height() - 14.0f;
            if (javas.list.empty()) {
                std::string text = loading ? "正在查找 Java…" : "没有找到 Java，点击“添加”选择 java.exe";
                float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float lead = loading ? 24.0f : 0.0f;
                float x = MAIN_X + (mainW() - w - lead) * 0.5f;
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
                bool hovered = !blocked && NewUi::hit(MAIN_X, rowY, mainW(), Page::ROW_H);
                Page::rowHover(id, rowY, hovered, (i == 0 ? 1 | 2 : 0) | (i == rows - 1 ? 4 | 8 : 0));
                UiWidgets::icon(Ui::glyph(0xf0f4), MAIN_X + 24.0f, rowY + Page::ROW_H * 0.5f, UiTheme::textMuted(),
                                12.0f);
                std::string name = "Java " + (java.version.empty() ? std::to_string(java.major) : java.version);
                float x = MAIN_X + 44.0f;
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

        struct Credit {
            const char* name;
            const char* license;
            const char* url;
        };

        const Credit CREDITS[] = {
                {"Dear ImGui", "MIT", "https://github.com/ocornut/imgui"},
                {"FreeType", "FreeType License", "https://freetype.org"},
                {"libcurl", "curl License", "https://curl.se"},
                {"zlib-ng", "zlib License", "https://github.com/zlib-ng/zlib-ng"},
                {"minizip-ng", "zlib License", "https://github.com/zlib-ng/minizip-ng"},
                {"simdjson", "Apache-2.0", "https://github.com/simdjson/simdjson"},
                {"JSON for Modern C++", "MIT", "https://github.com/nlohmann/json"},
                {"reproc", "MIT", "https://github.com/DaanDeMeyer/reproc"},
                {"stb", "MIT", "https://github.com/nothings/stb"},
                {"Font Awesome Free", "SIL OFL 1.1", "https://fontawesome.com"},
        };
        constexpr int CREDIT_COUNT = (int) (sizeof(CREDITS) / sizeof(CREDITS[0]));

        constexpr float HERO_H = 72.0f;
        constexpr float LOGO = 32.0f;

        void hero(float y) {
            UiRender::roundRect(MAIN_X, y, mainW(), HERO_H, 12.0f, UiTheme::surface());
            UiRender::outline(MAIN_X, y, mainW(), HERO_H, 12.0f, UiTheme::outlineSoft());
            UiRender::texture(Icons::logo(), MAIN_X + 20.0f, y + (HERO_H - LOGO) * 0.5f, LOGO, LOGO);
            const std::string name = "B Launcher\xE2\x84\xA2";
            float x = MAIN_X + 20.0f + LOGO + 14.0f;
            UiRender::textY(name, x, y, HERO_H, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            x += UiFont::width(name, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            tag(std::string("v") + B_LAUNCHER_VERSION, x, y + (HERO_H - Page::ROW_H) * 0.5f, UiTheme::accentSoft(40),
                UiTheme::accentText());
        }

        void creditRows(float y, bool blocked) {
            for (int i = 0; i < CREDIT_COUNT; i++) {
                const Credit& credit = CREDITS[i];
                float rowY = y + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                std::string id = std::string("settings#credit#") + credit.name;
                bool hovered = !blocked && NewUi::hit(MAIN_X, rowY, mainW(), Page::ROW_H);
                Page::rowHover(id, rowY, hovered, (i == 0 ? 1 | 2 : 0) | (i == CREDIT_COUNT - 1 ? 4 | 8 : 0));
                Page::label(credit.name, rowY);
                float amount = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 20.0f);
                float linkX = Page::controlRight() - 6.0f;
                UiWidgets::icon(Ui::glyph(0xf35d), linkX, rowY + Page::ROW_H * 0.5f,
                                UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), amount), 10.0f);
                float w = UiFont::width(credit.license, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY(credit.license, linkX - 18.0f - w, rowY, Page::ROW_H, UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                if (!blocked && NewUi::click(MAIN_X, rowY, mainW(), Page::ROW_H)) {
                    UiSound::request(UiSound::PAGE);
                    Platform::openUrl(credit.url);
                }
            }
        }

        void aboutTab(bool covered) {
            float contentH = HERO_H + Page::GAP + Page::CAPTION_H + CREDIT_COUNT * Page::ROW_H;
            Page::Scroll body("settings#about", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;

            if (Page::shown(y, HERO_H)) {
                hero(y);
            }
            y += HERO_H + Page::GAP;

            if (Page::shown(y, Page::CAPTION_H + CREDIT_COUNT * Page::ROW_H)) {
                Page::caption("开源组件", y);
                Page::card(y + Page::CAPTION_H, CREDIT_COUNT);
                creditRows(y + Page::CAPTION_H, blocked);
            }
            body.end();
        }
    }

    void draw() {
        bool covered = NewUi::overlayCoversMouse();
        rail(covered);
        sections.begin();
        Page::Column column(MAIN_X);
        switch (sections.page) {
            case TAB_LAUNCHER: launcherTab(covered); break;
            case TAB_JAVA: javaTab(covered); break;
            case TAB_ABOUT: aboutTab(covered); break;
            default: gameTab(covered); break;
        }
        sections.end();
    }
}
