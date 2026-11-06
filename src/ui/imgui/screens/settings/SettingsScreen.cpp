#include "ui/imgui/screens/settings/SettingsScreen.h"
#include "ui/imgui/screens/shell/Logo.h"
#include "ui/imgui/screens/shell/Appearance.h"
#include "ui/imgui/screens/shell/Background.h"
#include "ui/imgui/screens/shell/Dialog.h"
#include "app/system/About.h"
#include "app/system/Catalog.h"
#include "app/system/Config.h"
#include "app/downloads/DownloadSources.h"
#include "app/downloads/ModNames.h"
#include "app/system/DiskScan.h"
#include "ui/imgui/screens/home/HomeWidgets.h"
#include "ui/imgui/screens/home/Homepage.h"
#include "app/system/Updates.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "app/instances/Instances.h"
#include "app/launch/GameOptions.h"
#include "app/launch/JavaDownloads.h"
#include "app/launch/LaunchDefaults.h"
#include "ui/imgui/screens/shell/MainWindow.h"
#include "ui/imgui/screens/onboarding/Onboarding.h"
#include "ui/imgui/screens/shell/Page.h"
#include "app/launch/Patches.h"
#include "app/launch/ServerList.h"
#include "ui/imgui/screens/home/QuickActions.h"
#include "ui/imgui/screens/settings/GameOptionRows.h"
#include "ui/imgui/screens/settings/KeyBindEditor.h"
#include "ui/imgui/screens/settings/ToolboxTab.h"
#include "ui/imgui/screens/shell/Textures.h"
#include "app/system/Network.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"

#include <core/base/strings.hpp>

#include "ui/imgui/menu/MenuItems.h"
#include "ui/imgui/overlay/UiOverlays.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiPageTransition.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/NavItemWidget.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/SwatchWidget.h"
#include "ui/imgui/widget/TextFieldWidget.h"
#include "ui/imgui/widget/ToggleWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

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

        bool dangerButton(const std::string& id, const std::string& text, float x, float y, float w, int glyph,
                          bool blocked) {
            bool hovered = !blocked && NewUi::hit(x, y, w, FIELD_H);
            float lit = UiState::motion(id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
            uint32_t red = UiTheme::danger() & 0x00FFFFFFu;
            UiRender::roundRect(x, y, w, FIELD_H, 7.0f, red | ((uint32_t) Ui::round(72.0f + 40.0f * lit) << 24));
            UiRender::outline(x, y, w, FIELD_H, 7.0f, red | ((uint32_t) Ui::round(140.0f + 70.0f * lit) << 24));
            float textW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (glyph != 0) {
                float contentW = 10.0f + 6.0f + textW;
                float start = x + (w - contentW) * 0.5f;
                UiWidgets::icon(Ui::glyph(glyph), start + 5.0f, y + FIELD_H * 0.5f, UiTheme::danger(), 10.0f);
                UiRender::textY(text, start + 16.0f, y, FIELD_H, UiTheme::danger(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            } else {
                UiRender::textY(text, x + (w - textW) * 0.5f, y, FIELD_H, UiTheme::danger(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
            if (!blocked && NewUi::click(x, y, w, FIELD_H)) {
                UiSound::request(UiSound::forLabel(text));
                return true;
            }
            return false;
        }

        void textRow(const std::string& id, const char* label, std::function<std::string()> read,
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
        }

        std::string fitTail(const std::string& text, float available) {
            return UiFont::fitStart(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, available);
        }

        void instanceRows(float y, bool blocked) {
            Page::label("默认版本隔离", y);
            static auto isolation = ModeBinding::mapped<blb::instance::isolation_mode>(
                    "settings#isolation", Instances::isolation, Instances::setIsolation,
                    {{blb::instance::isolation_mode::none, "不隔离"},
                     {blb::instance::isolation_mode::modded, "仅隔离 Mod 实例"},
                     {blb::instance::isolation_mode::all, "全部隔离"}});
            select("settings#isolation", isolation, y, blocked);
            y += Page::ROW_H;

            Page::label("实例列表", y);
            std::string count = Instances::loading() ? std::string("正在读取…")
                                                     : std::to_string(Instances::list().size()) + " 个实例";
            float countW = UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            float refreshX = Page::controlRight() - 27.0f;
            UiRender::textY(count, refreshX - 8.0f - countW, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (Page::iconButton("settings#refresh", 0xf2f1, "重新读取", refreshX, y + 6.0f, blocked)) {
                Instances::refresh();
            }
        }

        void launchRows(float y, bool blocked) {
            Page::label("默认游戏语言", y);
            static auto language = ModeBinding::mapped<LaunchDefaults::Language>(
                    "settings#language", LaunchDefaults::language, LaunchDefaults::setLanguage,
                    {{LaunchDefaults::Language::None, "不设置"},
                     {LaunchDefaults::Language::Chinese, "中文"},
                     {LaunchDefaults::Language::English, "英文"}});
            select("settings#language", language, y, blocked);
            y += Page::ROW_H;

            Page::label("垃圾回收器", y);
            static auto gc = ModeBinding::mapped<blb::launch::gc_mode>(
                    "settings#gc", LaunchDefaults::gc, LaunchDefaults::setGc,
                    {{blb::launch::gc_mode::zgc, "尽量使用 ZGC"},
                     {blb::launch::gc_mode::generational_zgc, "尽量使用分代 ZGC"},
                     {blb::launch::gc_mode::g1, "标准 G1GC"},
                     {blb::launch::gc_mode::tuned_g1, "调优 G1GC"},
                     {blb::launch::gc_mode::none, "不指定（可自定义）"}});
            select("settings#gc", gc, y, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#jvmArgs", "JVM 参数", [] { return LaunchDefaults::jvmArgs(); },
                    [](const std::string& value) { LaunchDefaults::setJvmArgs(value); }, y, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#gameArgs", "游戏参数", [] { return LaunchDefaults::gameArgs(); },
                    [](const std::string& value) { LaunchDefaults::setGameArgs(value); }, y, blocked);
            y += Page::ROW_H;

            Page::label("显示游戏日志", y);
            toggle(
                    "settings#showLogs", [] { return LaunchDefaults::showLogs(); },
                    [](bool on) { LaunchDefaults::setShowLogs(on); }, y, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#preLaunch", "启动前命令", [] { return LaunchDefaults::preLaunch(); },
                    [](const std::string& value) { LaunchDefaults::setPreLaunch(value); }, y, blocked);
        }

        struct ImageFile {
            const char* id;
            const char* label;
            const char* fallback;
            const char* resetTip;
            std::string path;
            bool busy;
            std::optional<std::filesystem::path> (*pick)(const std::filesystem::path&);
            void (*apply)(const std::string&);
        };

        void imageFileRow(const ImageFile& file, float y, bool blocked) {
            const std::string label = file.label;
            const std::string id = file.id;
            Page::label(label, y);
            float pickX = Page::controlRight() - 27.0f;
            float right = pickX - 8.0f;
            if (!file.path.empty()) {
                float resetX = pickX - 30.0f;
                if (Page::iconButton(id + "Reset", 0xf2ea, file.resetTip, resetX, y + 6.0f, blocked)) {
                    file.apply(std::string());
                }
                right = resetX - 8.0f;
            }
            if (Page::iconButton(id + "Pick", 0xf07c, "选择图片", pickX, y + 6.0f, blocked)) {
                std::filesystem::path start =
                        file.path.empty() ? Platform::exeDirectory() : blb::path_from_utf8(file.path);
                if (std::optional<std::filesystem::path> picked = file.pick(start)) {
                    file.apply(blb::path_to_utf8(*picked));
                }
            }
            float labelRight = mainX() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            float room = std::max(0.0f, right - labelRight - (file.busy ? 24.0f : 0.0f));
            std::string shown = file.path.empty() ? std::string(file.fallback)
                                                  : fitTail(Platform::display(blb::path_from_utf8(file.path)), room);
            float shownW = UiFont::width(shown, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            UiRender::textY(shown, right - shownW, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                            UiWidgets::TEXT_CONTROL);
            if (file.busy) {
                Page::spinner(right - shownW - 16.0f, y + Page::ROW_H * 0.5f, 7.0f, UiTheme::accent());
            }
        }

        void backgroundFileRow(float y, bool blocked) {
            imageFileRow({"settings#background", "背景图片文件", "默认图片", "恢复默认图片", Background::path(),
                          Background::preparing(), Platform::pickImage, Background::setPath},
                         y, blocked);
        }

        void logoFileRow(float y, bool blocked) {
            imageFileRow({"settings#logo", "启动器标志", "默认标志", "恢复默认标志", Logo::customPath(),
                          Logo::preparing(), Platform::pickLogo, Logo::setCustomPath},
                         y, blocked);
        }

        int lookCount() {
#if defined(_WIN32)
            int count = 6;
#else
            int count = 5;
#endif
            return Appearance::backgroundImage() ? count + 1 : count;
        }

        int windowCount() {
            return Appearance::systemFrameRunning() ? 4 : 5;
        }

        float restartControls(const std::string& id, float right, float labelEnd, float y, bool blocked,
                              std::string& unshown) {
            bool busy = Services::busy();
            std::string text = "立即重启";
            float w = Page::buttonWidth(text, 0xf2f1);
            float x = right - w;
            if (Page::button(id, text, x, y + Page::FIELD_Y, w, true, blocked || busy, 0xf2f1) &&
                !Platform::restart()) {
                Notices::add(Notices::Kind::Failed, "重启失败，请手动重新打开启动器", 5000);
            }
            std::string hint = busy ? "任务进行中，暂不能重启" : "重启后生效";
            float hintX = x - 12.0f - UiFont::width(hint, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
            if (hintX >= labelEnd) {
                UiRender::textY(hint, hintX, y, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                return hintX - 8.0f;
            }
            unshown = hint;
            return x - 8.0f;
        }

        void systemFrameRow(float y, bool blocked) {
            const std::string label = "使用系统窗口边框";
            if (Appearance::systemFrame() != Appearance::systemFrameRunning()) {
                float labelEnd =
                        mainX() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 12.0f;
                std::string unshown;
                restartControls("settings#frameRestart", Page::controlRight() - 30.0f - 12.0f, labelEnd, y, blocked,
                                unshown);
            }
            Page::label(label, y);
            toggle(
                    "settings#systemFrame", [] { return Appearance::systemFrame(); },
                    [](bool on) { Appearance::setSystemFrame(on); }, y, blocked);
        }

#if defined(_WIN32)
        void uiStyleRow(float y, bool blocked) {
            const std::string label = "界面风格";
            Page::label(label, y);
            float labelEnd = mainX() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 12.0f;
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
            if (&UiStyles::current() != &UiStyles::running()) {
                std::string unshown;
                restartControls("settings#restart", Page::controlRight() - SELECT_W - 8.0f, labelEnd, y, blocked,
                                unshown);
            }
        }
#endif

        void lookRows(float y, bool blocked) {
#if defined(_WIN32)
            uiStyleRow(y, blocked);
            y += Page::ROW_H;
#endif

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

            Page::label("模组名称", y);
            select("settings#modNames",
                   std::make_shared<ModeBinding>(
                           "settings#modNames", [] { return std::string(ModNames::original() ? "原名" : "中文名"); },
                           [](const std::string& value) { ModNames::setOriginal(value == "原名"); },
                           [] { return std::vector<std::string>{"中文名", "原名"}; }),
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
        }

        void windowSettingRows(float y, bool blocked) {
            Page::label("记住窗口大小", y);
            toggle(
                    "settings#rememberWindow", [] { return Appearance::rememberWindow(); },
                    [](bool on) { Appearance::setRememberWindow(on); }, y, blocked);
            y += Page::ROW_H;

            systemFrameRow(y, blocked);
            y += Page::ROW_H;

            textRow(
                    "settings#windowTitle", "窗口标题", [] { return Appearance::customWindowTitle(); },
                    [](const std::string& value) { Appearance::setWindowTitle(value); }, y, blocked,
                    Appearance::defaultWindowTitle());
            y += Page::ROW_H;

            logoFileRow(y, blocked);
            y += Page::ROW_H;

            if (!Appearance::systemFrameRunning()) {
                textRow(
                        "settings#brandTitle", "侧栏标题", [] { return Appearance::customBrandTitle(); },
                        [](const std::string& value) { Appearance::setBrandTitle(value); }, y, blocked,
                        Appearance::defaultBrandTitle());
            }
        }

        void cardPage(const std::string& id, int rows, bool covered, const std::function<void(float, bool)>& paint) {
            Page::Scroll body(id, Page::TOP, MainWindow::height() - 14.0f, rows * Page::ROW_H);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            Page::card(y, rows);
            paint(y, blocked);
            body.end();
        }
    }

    namespace {
        constexpr int SECTION_LAUNCH = 0;
        constexpr int SECTION_OPTIONS = 1;
        constexpr int SECTION_KEYS = 2;
        constexpr int SECTION_PATCH = 3;
        constexpr int SECTION_FOLDERS = 4;
        constexpr int SECTION_LOOK = 5;
        constexpr int SECTION_WINDOW = 6;
        constexpr int SECTION_HOME = 7;
        constexpr int SECTION_NETWORK = 8;
        constexpr int SECTION_RESET = 9;
        constexpr int SECTION_JAVA = 10;
        constexpr int SECTION_TOOLS = 11;
        constexpr int SECTION_ABOUT = 12;
        PageTransition sections(SECTION_LAUNCH);

        constexpr int GROUP_NONE = -1;
        constexpr int GROUP_GAME = 0;
        constexpr int GROUP_LAUNCHER = 1;

        struct Group {
            int glyph;
            const char* label;
        };

        const Group GROUPS[] = {{0xf11b, "游戏设置"}, {0xf108, "启动器选项"}};

        struct Tab {
            int section;
            int glyph;
            const char* label;
            int group;
        };

        const Tab TABS[] = {
                {SECTION_LAUNCH, 0xf04b, "全局启动项", GROUP_GAME},
                {SECTION_OPTIONS, 0xf1de, "全局游戏选项", GROUP_GAME},
                {SECTION_KEYS, 0xf11c, "按键绑定", GROUP_GAME},
                {SECTION_PATCH, 0xf462, "补丁", GROUP_GAME},
                {SECTION_FOLDERS, 0xf07b, "游戏目录", GROUP_GAME},
                {SECTION_LOOK, 0xf53f, "外观", GROUP_LAUNCHER},
                {SECTION_WINDOW, 0xf2d0, "窗口", GROUP_LAUNCHER},
                {SECTION_HOME, 0xf015, "主页", GROUP_LAUNCHER},
                {SECTION_NETWORK, 0xf019, "下载与网络", GROUP_LAUNCHER},
                {SECTION_RESET, 0xf2f1, "引导与重置", GROUP_LAUNCHER},
                {SECTION_JAVA, 0xf0f4, "Java 管理", GROUP_NONE},
                {SECTION_TOOLS, 0xf0ad, "工具箱", GROUP_NONE},
                {SECTION_ABOUT, 0xf05a, "关于", GROUP_NONE},
        };

        int groupOf(int section) {
            for (const Tab& tab : TABS) {
                if (tab.section == section) {
                    return tab.group;
                }
            }
            return GROUP_NONE;
        }

        constexpr float SEARCH_H = 28.0f;
        const char* const SEARCH_OWNER = "settings#search";
        std::string searchText;

        std::shared_ptr<TextBinding> searchValue() {
            static std::shared_ptr<TextBinding> value = [] {
                auto binding = std::make_shared<TextBinding>(
                        SEARCH_OWNER, [] { return searchText; }, [](const std::string& text) { searchText = text; },
                        "搜索选项");
                binding->limit = 40;
                return binding;
            }();
            return value;
        }

        std::string typedSearch() {
            return NewUi::textEditing == SEARCH_OWNER ? UiInput::get(NewUi::INPUT_TEXT) : searchText;
        }

        struct Entry {
            int section;
            const char* label;
            const char* keywords;
        };

        const Entry ENTRIES[] = {
                {SECTION_LAUNCH, "默认游戏语言", "语言 中文 英文"},
                {SECTION_LAUNCH, "垃圾回收器", "GC ZGC G1 内存"},
                {SECTION_LAUNCH, "JVM 参数", "Java 虚拟机"},
                {SECTION_LAUNCH, "游戏参数", nullptr},
                {SECTION_LAUNCH, "显示游戏日志", nullptr},
                {SECTION_LAUNCH, "启动前命令", "cmd 脚本"},
                {SECTION_KEYS, nullptr, "快捷键 键位"},
                {SECTION_PATCH, "应用给默认设置的实例", "启用"},
                {SECTION_PATCH, "修复旧版皮肤", nullptr},
                {SECTION_PATCH, "自定义皮肤", "authlib 披风"},
                {SECTION_PATCH, "皮肤优先级", nullptr},
                {SECTION_PATCH, "皮肤来源", "Mojang LittleSkin"},
                {SECTION_PATCH, "皮肤图片", nullptr},
                {SECTION_PATCH, "披风图片", nullptr},
                {SECTION_PATCH, "正版玩家名", nullptr},
                {SECTION_PATCH, "自定义材质标记为已签名", nullptr},
                {SECTION_PATCH, "Log4Shell 漏洞修复", "log4j 安全"},
                {SECTION_PATCH, "中文输入修复", "输入法 IME"},
                {SECTION_PATCH, "Java 25 LWJGL 兼容", "Unsafe"},
                {SECTION_PATCH, "窗口标题", nullptr},
                {SECTION_PATCH, "窗口图标", nullptr},
                {SECTION_PATCH, "离线加入局域网", "LAN 联机"},
                {SECTION_PATCH, "输出调试日志", "debug"},
                {SECTION_FOLDERS, "默认版本隔离", "隔离"},
                {SECTION_FOLDERS, "实例列表", nullptr},
#if defined(_WIN32)
                {SECTION_FOLDERS, "搜索硬盘上的 .minecraft", "扫描 游戏目录"},
                {SECTION_LOOK, "界面风格", "EXUI"},
#endif
                {SECTION_LOOK, "主题", "强调色 颜色 深色 浅色"},
                {SECTION_LOOK, "界面缩放", "缩放 大小"},
                {SECTION_LOOK, "模组名称", "中文名 原名"},
                {SECTION_LOOK, "背景图片", nullptr},
                {SECTION_LOOK, "背景图片文件", nullptr},
                {SECTION_LOOK, "界面音效", "声音"},
                {SECTION_WINDOW, "记住窗口大小", nullptr},
                {SECTION_WINDOW, "使用系统窗口边框", "标题栏 边框"},
                {SECTION_WINDOW, "窗口标题", "任务栏"},
                {SECTION_WINDOW, "启动器标志", "图标 logo"},
                {SECTION_WINDOW, "侧栏标题", nullptr},
                {SECTION_HOME, "主页内容", "PCL XAML"},
                {SECTION_HOME, "主页组件", nullptr},
                {SECTION_HOME, "组件位置", nullptr},
                {SECTION_HOME, "快捷操作", nullptr},
                {SECTION_HOME, "已保存的服务器", nullptr},
                {SECTION_HOME, "服务器地址", nullptr},
                {SECTION_HOME, "服务器名称", nullptr},
                {SECTION_HOME, "主页文件", "XAML"},
                {SECTION_HOME, "联网主页网址", "XAML"},
                {SECTION_NETWORK, "Minecraft", "下载源 镜像 BMCLAPI"},
                {SECTION_NETWORK, "资源文件", "下载源 镜像 BMCLAPI"},
                {SECTION_NETWORK, "Modrinth", "下载源 镜像 MCIM"},
                {SECTION_NETWORK, "CurseForge", "下载源 镜像 MCIM"},
                {SECTION_NETWORK, "使用系统代理", "代理 proxy"},
                {SECTION_RESET, "使用启动引导", "引导"},
                {SECTION_RESET, "重置启动器选项", "恢复默认"},
                {SECTION_JAVA, nullptr, "JDK JRE 运行时"},
                {SECTION_TOOLS, nullptr, "自定义下载 PCL 主题"},
                {SECTION_ABOUT, nullptr, "版本 更新 鸣谢 开源"},
        };

        struct Hit {
            const Tab* tab;
            std::string label;
            bool row;
        };

        std::unordered_map<int, std::vector<std::string>> collectedLabels;
        bool labelsCollected = false;

        const char* keywordsOf(int section, const std::string& label) {
            for (const Entry& entry : ENTRIES) {
                if (entry.section == section && entry.label != nullptr && label == entry.label) {
                    return entry.keywords;
                }
            }
            return nullptr;
        }

        std::vector<std::string> labelsOf(int section) {
            std::vector<std::string> labels;
            auto add = [&](const std::string& label) {
                if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
                    labels.push_back(label);
                }
            };
            if (auto found = collectedLabels.find(section); found != collectedLabels.end()) {
                for (const std::string& label : found->second) {
                    add(label);
                }
            }
            for (const Entry& entry : ENTRIES) {
                if (entry.section == section && entry.label != nullptr) {
                    add(entry.label);
                }
            }
            if (section == SECTION_OPTIONS) {
                for (const GameOptions::OptionSpec& option : GameOptions::OPTIONS) {
                    add(option.label);
                }
            }
            return labels;
        }

        std::vector<Hit> hitsFor(const std::string& query) {
            auto matches = [&](const char* text) { return text != nullptr && blb::icontains(text, query); };
            std::vector<Hit> hits;
            for (const Tab& tab : TABS) {
                bool whole = matches(tab.label) || (tab.group != GROUP_NONE && matches(GROUPS[tab.group].label));
                for (const Entry& entry : ENTRIES) {
                    whole = whole ||
                            (entry.section == tab.section && entry.label == nullptr && matches(entry.keywords));
                }
                if (whole) {
                    hits.push_back({&tab, tab.label, false});
                }
                for (const std::string& label : labelsOf(tab.section)) {
                    if (matches(label.c_str()) || matches(keywordsOf(tab.section, label))) {
                        hits.push_back({&tab, label, true});
                    }
                }
            }
            return hits;
        }

        std::string pathOf(const Hit& hit) {
            if (hit.tab->group == GROUP_NONE) {
                return std::string();
            }
            std::string group = GROUPS[hit.tab->group].label;
            return hit.row ? group + " › " + hit.tab->label : group;
        }

        void searchField(bool covered) {
            float x = Page::left();
            float y = Page::TOP;
            bool filled = !typedSearch().empty();
            float clearX = x + SIDE_W - 14.0f;
            bool clearOver = filled && !covered && NewUi::hit(clearX - 10.0f, y, 20.0f, SEARCH_H);
            if (filled && !covered && NewUi::click(clearX - 10.0f, y, 20.0f, SEARCH_H)) {
                UiSound::request(UiSound::RELEASE);
                searchText.clear();
                if (NewUi::textEditing == SEARCH_OWNER) {
                    UiInput::set(NewUi::INPUT_TEXT, "");
                }
                filled = false;
            }
            TextFieldWidget field(searchValue());
            field.lead(22.0f)
                    .radius(8.0f)
                    .room(filled ? 18.0f : 0.0f)
                    .box(x, y, SIDE_W, SEARCH_H)
                    .bounds(x, y, filled ? SIDE_W - 24.0f : SIDE_W, SEARCH_H)
                    .block(covered);
            field.paint();
            UiWidgets::icon(Ui::glyph(0xf002), x + 16.0f, y + SEARCH_H * 0.5f, UiTheme::textFaint(), 11.0f);
            if (filled) {
                UiWidgets::icon(Ui::glyph(0xf00d), clearX, y + SEARCH_H * 0.5f,
                                clearOver ? UiTheme::textPrimary() : UiTheme::textFaint(), 9.0f);
            }
        }

        void openHit(const Hit& hit) {
            searchText.clear();
            if (NewUi::textEditing == SEARCH_OWNER) {
                NewUi::cancelTextEdit();
            }
            sections.go(hit.tab->section);
            if (hit.row) {
                Page::reveal(hit.label);
            }
        }

        void emptyState(int glyph, const std::string& text) {
            float cx = Page::columnLeft() + Page::columnWidth() * 0.5f;
            float cy = Page::TOP + (MainWindow::height() - 14.0f - Page::TOP) * 0.38f;
            UiWidgets::icon(Ui::glyph(glyph), cx, cy - 16.0f, UiTheme::textFaint(), 22.0f);
            Page::centredText(
                    UiFont::fitEnd(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, Page::columnWidth() - 32.0f), cx,
                    cy + 6.0f, 24.0f, UiTheme::textMuted(), UiWidgets::TEXT_CONTROL);
        }

        void resultsView(const std::string& query, bool covered) {
            std::vector<Hit> hits = hitsFor(query);
            if (hits.empty()) {
                emptyState(0xf002, "没有找到“" + query + "”");
                return;
            }
            int count = (int) hits.size();
            Page::Scroll body("settings#results", Page::TOP, MainWindow::height() - 14.0f, count * Page::ROW_H);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            Page::card(y, count);
            for (int i = 0; i < count; i++) {
                float rowY = y + i * Page::ROW_H;
                if (!Page::shown(rowY)) {
                    continue;
                }
                const Hit& hit = hits[i];
                if (Page::navRow("settings#hit#" + std::to_string(i), hit.label, pathOf(hit), rowY, blocked,
                                 Page::rowCorners(i, count), hit.tab->glyph)) {
                    openHit(hit);
                    break;
                }
            }
            body.end();
        }

        void tabItem(const Tab& tab, float y, bool blocked) {
            NavItemWidget item("settings#tab#" + std::to_string(tab.section), Ui::glyph(tab.glyph), tab.label);
            item.selected(sections.page == tab.section)
                    .nested(tab.group != GROUP_NONE)
                    .bounds(Page::left(), y, SIDE_W, NAV_ITEM_H)
                    .block(blocked);
            item.paint();
            if (item.pressed) {
                sections.go(tab.section);
            }
        }

        void rail(bool covered) {
            searchField(covered);
            float y = Page::TOP + SEARCH_H + 10.0f;
            int open = groupOf(sections.page);
            size_t count = std::size(TABS);
            size_t i = 0;
            while (i < count) {
                const Tab& first = TABS[i];
                if (first.group == GROUP_NONE) {
                    tabItem(first, y, covered);
                    y += NAV_STEP;
                    i++;
                    continue;
                }
                size_t end = i;
                while (end < count && TABS[end].group == first.group) {
                    end++;
                }
                std::string key = "settings#group#" + std::to_string(first.group);
                const Group& group = GROUPS[first.group];
                NavItemWidget header(key, Ui::glyph(group.glyph), group.label);
                header.bounds(Page::left(), y, SIDE_W, NAV_ITEM_H).block(covered);
                header.paint();
                if (header.pressed && open != first.group) {
                    sections.go(first.section);
                }
                y += NAV_STEP;
                float shown = UiState::motion(key + "#open", open == first.group ? 1.0f : 0.0f, 16.0f);
                float height = UiRender::snapLayout((float) (end - i) * NAV_STEP * shown);
                if (shown > 0.001f) {
                    UiRender::scissor(Page::left() - 4.0f, y, SIDE_W + 8.0f, height);
                    float savedAlpha = UiRender::alphaMul;
                    UiRender::alphaMul = savedAlpha * shown;
                    for (size_t k = i; k < end; k++) {
                        tabItem(TABS[k], y + (float) (k - i) * NAV_STEP, covered || shown < 0.999f);
                    }
                    UiRender::alphaMul = savedAlpha;
                    UiRender::unscissor();
                }
                y += height;
                i = end;
            }
        }

        std::string optionsSource() {
            if (const Instances::Entry* entry = Instances::find(Instances::selected())) {
                return entry->source;
            }
            for (const Instances::Source& source : Instances::sources()) {
                if (source.exists) {
                    return source.key;
                }
            }
            return std::string();
        }

        constexpr float SCAN_BAR_H = 4.0f;
        constexpr double SCAN_SWEEP = 1.4;

#if defined(_WIN32)
        bool scanning(DiskScan::Kind kind) {
            const DiskScan::Status& scan = DiskScan::status();
            return scan.state == DiskScan::State::Running && scan.kind == kind;
        }

        void startScan(DiskScan::Kind kind, const std::string& barId) {
            UiState::remove(barId);
            DiskScan::start(kind);
        }
#endif

        void scanBar(const std::string& id, float x, float y, float w) {
            const DiskScan::Status& scan = DiskScan::status();
            UiRender::roundRect(x, y, w, SCAN_BAR_H, SCAN_BAR_H * 0.5f, UiTheme::outline());
            if (scan.waiting) {
                UiState::keepAnimating();
                float t = (float) (std::fmod(ImGui::GetTime(), SCAN_SWEEP) / SCAN_SWEEP);
                float segment = w * 0.3f;
                float head = x - segment + (w + segment) * t;
                float from = std::max(x, head);
                float to = std::min(x + w, head + segment);
                if (to - from > 0.5f) {
                    float width = std::max(SCAN_BAR_H, to - from);
                    UiRender::roundRect(std::min(from, x + w - width), y, width, SCAN_BAR_H, SCAN_BAR_H * 0.5f,
                                        UiTheme::accent());
                }
                return;
            }
            float shown = UiState::motion(id, scan.progress, 10.0f);
            if (shown > 0.001f) {
                UiRender::roundRect(x, y, std::max(SCAN_BAR_H, w * shown), SCAN_BAR_H, SCAN_BAR_H * 0.5f,
                                    UiTheme::accent());
            }
        }

        bool sourceAdded(const std::filesystem::path& folder) {
            for (const Instances::Source& source : Instances::sources()) {
                std::error_code error;
                if (source.root == folder || std::filesystem::equivalent(source.root, folder, error)) {
                    return true;
                }
            }
            return false;
        }

        constexpr float DRIVE_W = 120.0f;

#if defined(_WIN32)
        std::string sizeText(uint64_t bytes) {
            double gigabytes = (double) bytes / 1e9;
            char text[32];
            if (gigabytes >= 1000.0) {
                std::snprintf(text, sizeof(text), "%.1f TB", gigabytes / 1000.0);
            } else {
                std::snprintf(text, sizeof(text), "%.0f GB", gigabytes);
            }
            return text;
        }

        class DriveChoice final : public Choice {
        public:
            DriveChoice() : Choice("settings#drives") {}

            bool multi() override {
                return true;
            }

            std::vector<std::string> options() override {
                std::vector<std::string> out;
                for (const DiskScan::Drive& drive : DiskScan::drives()) {
                    out.push_back(nameOf(drive));
                }
                return out;
            }

            bool selected(const std::string& option) override {
                const DiskScan::Drive* drive = find(option);
                return drive != nullptr && DiskScan::driveSelected(drive->letter);
            }

            void choose(const std::string& option) override {
                if (const DiskScan::Drive* drive = find(option)) {
                    DiskScan::selectDrive(drive->letter, !DiskScan::driveSelected(drive->letter));
                }
            }

            std::string label() override {
                const std::vector<DiskScan::Drive>& all = DiskScan::drives();
                if (all.empty()) {
                    return DiskScan::drivesReady() ? "没有 NTFS 磁盘" : "正在读取磁盘…";
                }
                std::vector<std::string> picked;
                for (const DiskScan::Drive& drive : all) {
                    if (DiskScan::driveSelected(drive.letter)) {
                        picked.push_back(std::string(1, (char) drive.letter) + ":");
                    }
                }
                if (picked.empty()) {
                    return "未选择磁盘";
                }
                return picked.size() == all.size() ? std::string("全部磁盘") : blb::join(picked, "、");
            }

            std::string optionIcon(const std::string& option) override {
                const DiskScan::Drive* drive = find(option);
                return drive != nullptr ? Ui::glyph(drive->removable ? 0xf287 : 0xf0a0) : std::string();
            }

            std::string optionDetail(const std::string& option) override {
                const DiskScan::Drive* drive = find(option);
                return drive != nullptr && drive->size > 0 ? sizeText(drive->size) : std::string();
            }

        private:
            static std::string nameOf(const DiskScan::Drive& drive) {
                std::string name = std::string(1, (char) drive.letter) + ":";
                return drive.label.empty() ? name : name + " " + drive.label;
            }

            static const DiskScan::Drive* find(const std::string& option) {
                for (const DiskScan::Drive& drive : DiskScan::drives()) {
                    if (nameOf(drive) == option) {
                        return &drive;
                    }
                }
                return nullptr;
            }
        };

        void driveSelect(const std::string& id, float x, float y, bool blocked) {
            static auto choice = std::make_shared<DriveChoice>();
            SelectWidget widget(id, choice);
            widget.bounds(x, y, DRIVE_W, FIELD_H).block(blocked || DiskScan::drives().empty());
            widget.paint();
        }

        void folderRows(float y, bool blocked) {
            const DiskScan::Status& scan = DiskScan::status();
            const std::string label = "搜索硬盘上的 .minecraft";
            Page::label(label, y);
            std::string action = "全盘搜索";
            float actionW = Page::buttonWidth(action, 0xf0a0);
            float actionX = Page::controlRight() - actionW;
            bool running = scan.state == DiskScan::State::Running;
            if (Page::button("settings#folders#scan", action, actionX, y + Page::FIELD_Y, actionW, false,
                             blocked || running || !DiskScan::anySelected(), 0xf0a0)) {
                startScan(DiskScan::Kind::Folders, "settings#folders#bar");
            }
            float labelEnd = mainX() + 16.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 16.0f;
            float right = actionX - 16.0f;
            if (!scanning(DiskScan::Kind::Folders)) {
                float selectX = actionX - 8.0f - DRIVE_W;
                driveSelect("settings#folders#drives", selectX, y + Page::FIELD_Y, blocked || running);
                right = selectX - 12.0f;
            }
            if (scanning(DiskScan::Kind::Folders)) {
                float w = std::min(220.0f, right - labelEnd);
                if (w >= 60.0f) {
                    float top = y + (Page::ROW_H - 18.0f - 5.0f - SCAN_BAR_H) * 0.5f;
                    UiRender::textY(UiFont::fitEnd(scan.step, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, w), right - w, top,
                                    18.0f, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    scanBar("settings#folders#bar", right - w, top + 23.0f, w);
                }
            } else {
                bool failed = scan.state == DiskScan::State::Failed && scan.kind == DiskScan::Kind::Folders;
                bool done = scan.state == DiskScan::State::Done && scan.kind == DiskScan::Kind::Folders;
                std::string note = failed ? scan.error
                        : done            ? "找到 " + std::to_string(scan.folders.size()) + " 个"
                                          : std::string("需要管理员权限");
                note = UiFont::fitEnd(note, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, std::max(0.0f, right - labelEnd));
                float noteW = UiFont::width(note, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                UiRender::textY(note, right - noteW, y, Page::ROW_H, failed ? UiTheme::danger() : UiTheme::textMuted(),
                                UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
            }
        }
#endif

        void foundRows(float y, bool blocked) {
            const DiskScan::Status& scan = DiskScan::status();
            for (size_t i = 0; i < scan.folders.size(); i++) {
                float rowY = y + (float) i * Page::ROW_H;
                const std::filesystem::path& folder = scan.folders[i];
                float textX = mainX() + 16.0f;
                UiWidgets::icon(Ui::glyph(0xf07b), textX + 6.0f, rowY + Page::ROW_H * 0.5f, UiTheme::textMuted(),
                                12.0f);
                float end = Page::controlRight();
                if (sourceAdded(folder)) {
                    std::string added = "已添加";
                    float addedW = UiFont::width(added, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    UiRender::textY(added, end - addedW, rowY, Page::ROW_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    UiWidgets::check(end - addedW - 12.0f, rowY + Page::ROW_H * 0.5f, UiTheme::accent(), 9.0f);
                    end -= addedW + 22.0f;
                } else {
                    std::string add = "添加";
                    float addW = Page::buttonWidth(add, 0xf067);
                    if (Page::button("settings#folders#add#" + std::to_string(i), add, end - addW, rowY + Page::FIELD_Y,
                                     addW, false, blocked, 0xf067)) {
                        std::string problem = Instances::addFolder(folder);
                        if (!problem.empty()) {
                            Instances::notify(problem, true);
                        }
                    }
                    end -= addW;
                }
                float pathX = textX + 22.0f;
                std::string where = UiFont::fitStart(Platform::display(folder), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL,
                                                     std::max(0.0f, end - 12.0f - pathX));
                UiRender::textY(where, pathX, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
            }
        }

        void noFolder() {
            emptyState(0xf07b, "还没有游戏目录");
        }

        void optionsSection(bool covered) {
            std::string source = optionsSource();
            std::filesystem::path shared = GameOptions::sharedFile(source);
            if (shared.empty()) {
                noFolder();
                return;
            }
            const Instances::Source* found = Instances::source(source);
            std::string title = found != nullptr ? found->label : std::string("游戏目录");
            float contentH = Page::CAPTION_H + GameOptionRows::COUNT * Page::ROW_H;
            Page::Scroll body("settings#optionsPage", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            GameOptionRows::caption(title, shared, "版本独立的实例在实例设置里修改", y);
            Page::card(y + Page::CAPTION_H, GameOptionRows::COUNT);
            GameOptionRows::draw("settings#options", shared, y + Page::CAPTION_H, blocked);
            body.end();
        }

        void foldersSection(bool covered) {
#if defined(_WIN32)
            const int instanceCount = 3;
#else
            const int instanceCount = 2;
#endif
            const int foundCount = (int) DiskScan::status().folders.size();
            float contentH = Page::CAPTION_H + instanceCount * Page::ROW_H;
            if (foundCount > 0) {
                contentH += Page::GAP + Page::CAPTION_H + foundCount * Page::ROW_H;
            }
            Page::Scroll body("settings#folders", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;

            if (Page::shown(y, Page::CAPTION_H + instanceCount * Page::ROW_H)) {
                Page::caption("实例", y);
                Page::card(y + Page::CAPTION_H, instanceCount);
                instanceRows(y + Page::CAPTION_H, blocked);
#if defined(_WIN32)
                folderRows(y + Page::CAPTION_H + 2 * Page::ROW_H, blocked);
#endif
            }
            y += Page::CAPTION_H + instanceCount * Page::ROW_H + Page::GAP;

            if (foundCount > 0 && Page::shown(y, Page::CAPTION_H + foundCount * Page::ROW_H)) {
                Page::caption("找到的游戏目录", y);
                Page::card(y + Page::CAPTION_H, foundCount);
                foundRows(y + Page::CAPTION_H, blocked);
            }
            body.end();
        }

        void keysTab(bool covered) {
            std::string source = optionsSource();
            std::filesystem::path shared = GameOptions::sharedFile(source);
            if (shared.empty()) {
                noFolder();
                return;
            }
            const Instances::Source* found = Instances::source(source);
            std::string note = (found != nullptr ? found->label : std::string("游戏目录")) +
                    " 里共用的按键，版本独立的实例在实例设置里修改";
            KeyBindEditor::draw("settings#keys", shared, false, note, Page::TOP, covered);
        }

        class SavedServers final : public Choice {
        public:
            SavedServers() : Choice("settings#savedServers") {}

            std::vector<std::string> options() override {
                sync();
                return keys;
            }

            bool selected(const std::string& option) override {
                const ServerList::Server* server = find(option);
                return server != nullptr && matches(*server);
            }

            void choose(const std::string& option) override {
                if (const ServerList::Server* server = find(option)) {
                    HomeWidgets::setServerAddress(server->address);
                    HomeWidgets::setServerName(server->name);
                }
            }

            std::string label() override {
                if (const ServerList::Server* server = find(blb::to_lower(Ui::trim(HomeWidgets::serverAddress())))) {
                    return server->name.empty() ? server->address : server->name;
                }
                return ServerList::loading() ? "正在读取…" : keys.empty() ? "没有保存的服务器" : "选择服务器";
            }

            std::string optionLabel(const std::string& option) override {
                const ServerList::Server* server = find(option);
                return server != nullptr ? (server->name.empty() ? server->address : server->name) : option;
            }

            uint32_t optionImage(const std::string& option) override {
                const ServerList::Server* server = find(option);
                return server != nullptr ? Textures::of(server->icon) : 0;
            }

            std::string optionDetail(const std::string& option) override {
                const ServerList::Server* server = find(option);
                return server != nullptr && !server->name.empty() ? server->address : std::string();
            }

        private:
            bool cached = false;
            unsigned seenRevision = 0;
            std::vector<std::string> keys;
            std::unordered_map<std::string, ServerList::Server> entries;

            void sync() {
                const auto& servers = ServerList::servers();
                unsigned revision = ServerList::revision();
                if (cached && seenRevision == revision) {
                    return;
                }
                keys.clear();
                entries.clear();
                keys.reserve(servers.size());
                entries.reserve(servers.size());
                for (const ServerList::Server& server : servers) {
                    std::string key = blb::to_lower(server.address);
                    if (entries.emplace(key, server).second) {
                        keys.push_back(std::move(key));
                    }
                }
                seenRevision = revision;
                cached = true;
            }

            const ServerList::Server* find(const std::string& option) {
                sync();
                auto found = entries.find(option);
                return found != entries.end() ? &found->second : nullptr;
            }

            static bool matches(const ServerList::Server& server) {
                return blb::to_lower(server.address) == blb::to_lower(Ui::trim(HomeWidgets::serverAddress()));
            }
        };

        void savedServerRow(float y, bool blocked) {
            static auto saved = std::make_shared<SavedServers>();
            Page::label("已保存的服务器", y);
            select("settings#savedServers", saved, y, blocked || ServerList::servers().empty());
        }

        int homepageCount() {
            int kind = Homepage::source();
            if (kind == Homepage::SOURCE_NONE) {
                return 2 + (HomeWidgets::enabled(HomeWidgets::LAUNCH) ? 1 : 0) +
                        (HomeWidgets::enabled(HomeWidgets::QUICK) ? 1 : 0) +
                        (HomeWidgets::enabled(HomeWidgets::SERVER) ? 3 : 0);
            }
            return kind == Homepage::SOURCE_FILE || kind == Homepage::SOURCE_URL ? 2 : 1;
        }

        void homepageFileRow(float y, bool blocked) {
            const std::string label = "主页文件";
            Page::label(label, y);
            std::string path = Homepage::file();
            float pickX = Page::controlRight() - 27.0f;
            if (Page::iconButton("settings#homepagePick", 0xf07c, "选择 XAML 文件", pickX, y + 6.0f, blocked)) {
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
                Page::label("主页组件", y);
                select("settings#homeWidgets", HomeWidgets::choice(), y, blocked);
                if (HomeWidgets::enabled(HomeWidgets::LAUNCH)) {
                    y += Page::ROW_H;
                    Page::label("组件位置", y);
                    select("settings#homeAlign",
                           std::make_shared<ModeBinding>(
                                   "settings#homeAlign",
                                   [] { return std::string(HomeWidgets::alignBottom() ? "靠下" : "垂直居中"); },
                                   [](const std::string& value) { HomeWidgets::setAlignBottom(value == "靠下"); },
                                   [] { return std::vector<std::string>{"垂直居中", "靠下"}; }),
                           y, blocked);
                }
                if (HomeWidgets::enabled(HomeWidgets::QUICK)) {
                    y += Page::ROW_H;
                    Page::label("快捷操作", y);
                    select("settings#quickActions", QuickActions::choice(), y, blocked);
                }
                if (HomeWidgets::enabled(HomeWidgets::SERVER)) {
                    y += Page::ROW_H;
                    savedServerRow(y, blocked);
                    y += Page::ROW_H;
                    textRow(
                            "settings#serverAddress", "服务器地址", [] { return HomeWidgets::serverAddress(); },
                            [](const std::string& value) { HomeWidgets::setServerAddress(value); }, y, blocked,
                            "例如 mc.example.com");
                    y += Page::ROW_H;
                    textRow(
                            "settings#serverName", "服务器名称", [] { return HomeWidgets::serverName(); },
                            [](const std::string& value) { HomeWidgets::setServerName(value); }, y, blocked,
                            "使用服务器地址");
                }
            } else if (Homepage::source() == Homepage::SOURCE_FILE) {
                homepageFileRow(y, blocked);
            } else if (Homepage::source() == Homepage::SOURCE_URL) {
                textRow(
                        "settings#homepageUrl", "联网主页网址", [] { return Homepage::url(); },
                        [](const std::string& value) { Homepage::setUrl(value); }, y, blocked);
            }
        }

        void downloadRows(float y, bool blocked) {
            struct Row {
                DownloadSources::Kind kind;
                const char* id;
                const char* label;
            };
            const Row rows[] = {
                    {DownloadSources::Kind::Minecraft, "settings#sourceMinecraft", "Minecraft"},
                    {DownloadSources::Kind::Assets, "settings#sourceAssets", "资源文件"},
                    {DownloadSources::Kind::Modrinth, "settings#sourceModrinth", "Modrinth"},
                    {DownloadSources::Kind::CurseForge, "settings#sourceCurseForge", "CurseForge"},
            };
            for (const Row& row : rows) {
                DownloadSources::Kind kind = row.kind;
                Page::label(row.label, y);
                select(row.id,
                       ModeBinding::mapped<blb::net::source_order>(
                               row.id, [kind] { return DownloadSources::order(kind); },
                               [kind](blb::net::source_order value) { DownloadSources::setOrder(kind, value); },
                               {{blb::net::source_order::mirror_first, "镜像源优先"},
                                {blb::net::source_order::official_first, "官方源优先"},
                                {blb::net::source_order::official_only, "仅官方源"}}),
                       y, blocked);
                y += Page::ROW_H;
            }
        }

        void networkRows(float y, bool blocked) {
            Page::label("使用系统代理", y);
            toggle(
                    "settings#systemProxy", [] { return Network::systemProxy(); },
                    [](bool on) { Network::setSystemProxy(on); }, y, blocked);
        }

        void confirmReset() {
            Dialog::open("settings#resetAll", 480.0f, 186.0f, [](float x, float y, float w, float h) {
                UiRender::textY("重置启动器选项？", x + 20.0f, y + 20.0f, 26.0f, UiTheme::textPrimary(), UiFont::MEDIUM,
                                16.0f);
                UiRender::textY("外观、主页、窗口等设置恢复默认，启动器会重启。", x + 20.0f, y + 56.0f, 20.0f,
                                UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                UiRender::textY("账户、实例和下载的文件不受影响。", x + 20.0f, y + 76.0f, 20.0f,
                                UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                float buttonY = y + h - 20.0f - Page::FIELD_H;
                if (Page::button("settings#resetAll#cancel", "取消", x + w - 212.0f, buttonY, 72.0f, false, false)) {
                    Dialog::close();
                    return;
                }
                if (dangerButton("settings#resetAll#confirm", "重置并重启", x + w - 124.0f, buttonY, 104.0f, 0xf2f1,
                                 false)) {
                    Dialog::close();
                    if (!Config::reset()) {
                        Notices::add(Notices::Kind::Failed, "无法保存重置后的设置，请稍后重试", 5000);
                        return;
                    }
                    if (!Platform::restart()) {
                        Notices::add(Notices::Kind::Failed, "重启失败，请手动重新打开启动器", 5000);
                    }
                }
            });
        }

        void guideRow(float y, bool blocked) {
            const std::string label = "使用启动引导";
            const std::string text = "开始引导";
            Page::label(label, y);
            float w = Page::buttonWidth(text, 0xf0d0);
            float x = Page::controlRight() - w;
            if (Page::button("settings#guide", text, x, y + Page::FIELD_Y, w, false, blocked, 0xf0d0)) {
                Onboarding::start();
            }
        }

        void resetRow(float y, bool blocked) {
            const std::string label = "重置启动器选项";
            const std::string text = "重置并重启";
            Page::label(label, y);
            float w = Page::buttonWidth(text, 0xf2f1);
            float x = Page::controlRight() - w;
            if (dangerButton("settings#resetAll", text, x, y + Page::FIELD_Y, w, 0xf2f1, blocked || Services::busy())) {
                confirmReset();
            }
        }

        void networkSection(bool covered) {
            float contentH = 2 * Page::CAPTION_H + 5 * Page::ROW_H + Page::GAP;
            Page::Scroll body("settings#network", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            Page::caption("下载源", y);
            Page::card(y + Page::CAPTION_H, 4);
            downloadRows(y + Page::CAPTION_H, blocked);
            y += Page::CAPTION_H + 4 * Page::ROW_H + Page::GAP;
            Page::caption("网络", y);
            Page::card(y + Page::CAPTION_H, 1);
            networkRows(y + Page::CAPTION_H, blocked);
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
            if (Page::iconButton("settings#java#refresh", 0xf2f1, "重新查找", refreshX, Page::TOP + 1.0f,
                           covered || loading)) {
                Catalog::refreshJava();
            }
#if defined(_WIN32)
            float searchX = refreshX - 32.0f;
            bool diskRunning = DiskScan::status().state == DiskScan::State::Running;
            if (diskRunning) {
                Page::spinner(searchX + 14.0f, Page::TOP + 15.0f, 6.0f, UiTheme::accent());
            } else if (Page::iconButton("settings#java#disks", 0xf0a0, "全盘搜索", searchX, Page::TOP + 1.0f,
                                        covered || !DiskScan::anySelected())) {
                startScan(DiskScan::Kind::Java, "settings#java#disks#bar");
            }
            float drivesX = searchX - 4.0f - DRIVE_W;
            bool javaScanning = scanning(DiskScan::Kind::Java);
            if (!javaScanning) {
                driveSelect("settings#java#drives", drivesX, Page::TOP + 4.0f, covered || diskRunning);
            }
#else
            float drivesX = refreshX;
            bool javaScanning = false;
#endif
            std::string add = "添加";
            float addW = Page::buttonWidth(add, 0xf067);
            float addX = drivesX - 8.0f - addW;
            if (Page::button("settings#java#add", add, addX, Page::TOP + 4.0f, addW, false, covered, 0xf067)) {
                if (std::optional<std::filesystem::path> picked = Platform::pickJava(std::filesystem::path())) {
                    Catalog::addJava(*picked);
                }
            }
            std::string download = "下载";
            float downloadW = Page::buttonWidth(download, 0xf019);
            float downloadX = addX - 8.0f - downloadW;
            if (Page::button("settings#java#download", download, downloadX, Page::TOP + 4.0f, downloadW, true, covered,
                             0xf019)) {
                javaViews.go(1);
            }
            if (javaScanning) {
                float barX = drivesX + 8.0f;
                scanBar("settings#java#disks#bar", barX, Page::TOP + (NAV_ITEM_H - SCAN_BAR_H) * 0.5f, DRIVE_W - 16.0f);
                const std::string& step = DiskScan::status().step;
                float stepW = UiFont::width(step, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                float stepX = downloadX - 12.0f - stepW;
                float countEnd = mainX() + 2.0f + UiFont::width(count, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 12.0f;
                if (stepX >= countEnd) {
                    UiRender::textY(step, stepX, Page::TOP, NAV_ITEM_H, UiTheme::textMuted(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_SMALL);
                }
            }

            float top = Page::TOP + 40.0f;
            float bottom = MainWindow::height() - 14.0f;
            if (javas.list.empty()) {
#if defined(_WIN32)
                std::string text = loading ? "正在查找 Java…" : "没有找到 Java，点击“添加”选择 java.exe";
#else
                std::string text = loading ? "正在查找 Java…" : "没有找到 Java，点击“添加”选择 java";
#endif
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
                Page::rowHover(id, rowY, hovered, Page::rowCorners(i, rows));
                UiWidgets::icon(Ui::glyph(0xf0f4), mainX() + 24.0f, rowY + Page::ROW_H * 0.5f, UiTheme::textMuted(),
                                12.0f);
                std::string name = "Java " + (java.version.empty() ? std::to_string(java.major) : java.version);
                float x = mainX() + 44.0f;
                UiRender::textY(name, x, rowY, Page::ROW_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                                UiWidgets::TEXT_CONTROL);
                x += UiFont::width(name, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
                x += Page::tag(java.jdk ? "JDK" : "JRE", x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                std::string arch = archName(java.arch);
                if (!arch.empty()) {
                    x += Page::tag(arch, x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                }
                if (Catalog::addedByHand(java.executable)) {
                    x += Page::tag("手动添加", x, rowY, UiTheme::accentSoft(40), UiTheme::accentText()) + 4.0f;
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
                    Page::tooltip(where, whereX, rowY + 8.0f, shownW, Page::ROW_H - 16.0f);
                }
                if (Page::iconButton(id + "#menu", 0xf141, "更多操作", menuX, rowY + (Page::ROW_H - 28.0f) * 0.5f, blocked)) {
                    javaMenu(java, menuX - 142.0f, rowY + Page::ROW_H - 4.0f);
                }
            }
            body.end();
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
            if (Page::iconButton("settings#java#back", 0xf053, "返回", mainX() - 4.0f, Page::TOP + 1.0f, covered)) {
                javaViews.go(0);
            }
            UiRender::textY("下载 Java", mainX() + 30.0f, Page::TOP, NAV_ITEM_H, UiTheme::textPrimary(), UiFont::MEDIUM,
                            UiWidgets::TEXT_BODY);
            float refreshX = Page::right() - 28.0f;
            if (Page::iconButton("settings#java#reload", 0xf2f1, "刷新列表", refreshX, Page::TOP + 1.0f,
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
                Page::rowHover(id, rowY, hovered, Page::rowCorners(i, rows));
                float cy = rowY + Page::ROW_H * 0.5f;
                UiWidgets::icon(Ui::glyph(0xf0f4), mainX() + 24.0f, cy, UiTheme::textMuted(), 12.0f);

                JavaDownloads::State state = JavaDownloads::state(package.folder);
                bool done = state == JavaDownloads::State::Done || JavaDownloads::installed(package);
                float right = Page::controlRight();
                float actionX = right;
                if (state == JavaDownloads::State::Running) {
                    Page::progressRing(right - 10.0f, cy, JavaDownloads::progress(package.folder));
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
                x += Page::tag(package.version, x, rowY, UiTheme::surfaceRaised(), UiTheme::textSecondary()) + 4.0f;
                if (package.preview) {
                    Page::tag("预览", x, rowY, UiTheme::accentSoft(40), UiTheme::accentText());
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

        using About::Link;

        constexpr int PROJECT_COUNT = (int) std::size(About::PROJECT);
        constexpr int THANKS_COUNT = (int) std::size(About::THANKS);
        constexpr int CREDIT_COUNT = (int) std::size(About::CREDITS);

        constexpr float HERO_H = 72.0f;
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
                Platform::openUrl(About::REPOSITORY_URL);
            }
        }

        void hero(float y, bool blocked) {
            Page::card(mainX(), y, mainW(), HERO_H);
            Logo::draw(mainX() + 20.0f, y + (HERO_H - LOGO) * 0.5f, LOGO);
            const std::string name = About::BRAND;
            float textX = mainX() + 20.0f + LOGO + 14.0f;
            float titleY = y + (HERO_H - 24.0f - 2.0f - 16.0f) * 0.5f;
            float x = textX;
            UiRender::textY(name, x, titleY, 24.0f, UiTheme::textPrimary(), UiFont::MEDIUM, Page::TITLE_SIZE);
            x += UiFont::width(name, UiFont::MEDIUM, Page::TITLE_SIZE) + 10.0f;
            x += Page::tag(std::string("v") + About::VERSION, x, titleY + 12.0f - Page::ROW_H * 0.5f, UiTheme::accentSoft(40),
                     UiTheme::accentText());
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
                Page::rowHover(id, rowY, hovered, Page::rowCorners(i, count));
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

        std::string patchId(const char* key) {
            return std::string("settings#patch#") + key;
        }

        void patchToggle(const char* label, const char* key, float y, bool blocked) {
            Page::label(label, y);
            std::string id = patchId(key);
            toggle(id, [key] { return Patches::flag(key); }, [key](bool on) { Patches::setFlag(key, on); }, y, blocked);
        }

        std::shared_ptr<ModeBinding> patchChoice(std::string key,
                                                   std::vector<std::pair<std::string, std::string>> options) {
            std::string fallback = options.empty() ? std::string() : options.front().second;
            return ModeBinding::mapped<std::string>(
                    patchId(key.c_str()), [key] { return Patches::value(key); },
                    [key](const std::string& value) { Patches::setValue(key, value); },
                    std::move(options), std::move(fallback));
        }

        void patchSelect(const char* label, const std::shared_ptr<ModeBinding>& choice, float y, bool blocked) {
            Page::label(label, y);
            select(choice->owner, choice, y, blocked);
        }

        float patchText(const char* label, const char* key, const char* hint, float y, bool blocked, int limit) {
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
            return x;
        }

        void pathRow(const char* label, const char* key, const char* hint, bool png, float y, bool blocked) {
            float x = patchText(label, key, hint, y, blocked, 1024);
            if (!Page::iconButton(patchId(key) + "#pick", 0xf07c, "选择图片", x - 36.0f, y + 6.0f, blocked)) {
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
            Page::label("应用给默认设置的实例", y);
            toggle(
                    "settings#patch#all", [] { return Patches::all(); }, [](bool on) { Patches::setAll(on); }, y,
                    blocked);
        }

        void skinRows(float y, bool blocked) {
            patchToggle("修复旧版皮肤", "skin.legacy", y, blocked);
            y += Page::ROW_H;
            patchToggle("自定义皮肤", "skin.authlib", y, blocked);
            y += Page::ROW_H;
            if (!Patches::flag("skin.authlib")) {
                return;
            }
            static auto skinMode = patchChoice("skin.mode", {{"fallback", "只补全没有皮肤的玩家"},
                                                             {"override", "强制使用自定义皮肤"}});
            patchSelect("皮肤优先级", skinMode, y, blocked);
            y += Page::ROW_H;
            static auto skinSource = patchChoice("skin.source", {{"mojang", "Mojang"},
                                                                 {"littleskin", "LittleSkin"},
                                                                 {"custom", "自定义"}});
            patchSelect("皮肤来源", skinSource, y, blocked);
            y += Page::ROW_H;
            std::string source = Patches::value("skin.source");
            if (source == "custom") {
                pathRow("皮肤图片", "skin.template.skin", "不使用", true, y, blocked);
                y += Page::ROW_H;
                pathRow("披风图片", "skin.template.cape", "不使用", true, y, blocked);
                y += Page::ROW_H;
            } else if (source != "littleskin") {
                patchText("正版玩家名", "skin.player", "留空则按玩家自己的名字查询", y, blocked, 16);
                y += Page::ROW_H;
            }
            patchToggle("自定义材质标记为已签名", "skin.trusted", y, blocked);
        }

        void fixRows(float y, bool blocked) {
            patchToggle("Log4Shell 漏洞修复", "log4j.enabled", y, blocked);
            y += Page::ROW_H;
            patchToggle("中文输入修复", "input.enabled", y, blocked);
            y += Page::ROW_H;
            patchToggle("Java 25 LWJGL 兼容", "lwjgl.unsafe", y, blocked);
        }

        void windowRows(float y, bool blocked) {
            patchText("窗口标题", "window.title", "不修改", y, blocked, 1024);
            y += Page::ROW_H;
            pathRow("窗口图标", "window.icon", "不修改", false, y, blocked);
        }

        void lanRows(float y, bool blocked) {
            patchToggle("离线加入局域网", "lan.offlineJoin", y, blocked);
        }

        void debugRows(float y, bool blocked) {
            patchToggle("输出调试日志", "debug", y, blocked);
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
            float contentH = 6 * Page::CAPTION_H + (1 + skins + 3 + 2 + 1 + 1) * Page::ROW_H + 5 * Page::GAP;
            Page::Scroll body("settings#patch", Page::TOP, MainWindow::height() - 14.0f, contentH);
            bool outside = body.begin();
            bool blocked = outside || covered;
            float y = body.top - body.offset;
            y = patchCard("启用", 1, y, [blocked](float top) { enableRows(top, blocked); });
            y = patchCard("皮肤", skins, y, [blocked](float top) { skinRows(top, blocked); });
            y = patchCard("修复", 3, y, [blocked](float top) { fixRows(top, blocked); });
            y = patchCard("窗口", 2, y, [blocked](float top) { windowRows(top, blocked); });
            y = patchCard("联机", 1, y, [blocked](float top) { lanRows(top, blocked); });
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
            y = linkCard("项目", "settings#project#", About::PROJECT, PROJECT_COUNT, y, blocked);
            y = linkCard("鸣谢", "settings#thanks#", About::THANKS, THANKS_COUNT, y, blocked);
            linkCard("开源组件", "settings#credit#", About::CREDITS, CREDIT_COUNT, y, blocked);
            body.end();
        }
    }

    namespace {
        void drawSection(int section, bool covered) {
            switch (section) {
                case SECTION_OPTIONS: optionsSection(covered); break;
                case SECTION_KEYS: keysTab(covered); break;
                case SECTION_PATCH: patchTab(covered); break;
                case SECTION_FOLDERS: foldersSection(covered); break;
                case SECTION_LOOK: cardPage("settings#look", lookCount(), covered, lookRows); break;
                case SECTION_WINDOW: cardPage("settings#window", windowCount(), covered, windowSettingRows); break;
                case SECTION_HOME: cardPage("settings#home", homepageCount(), covered, homepageRows); break;
                case SECTION_NETWORK: networkSection(covered); break;
                case SECTION_RESET:
                    cardPage("settings#reset", 2, covered, [](float y, bool blocked) {
                        guideRow(y, blocked);
                        resetRow(y + Page::ROW_H, blocked);
                    });
                    break;
                case SECTION_JAVA: javaTab(covered); break;
                case SECTION_TOOLS: ToolboxTab::draw(covered); break;
                case SECTION_ABOUT: aboutTab(covered); break;
                default: cardPage("settings#launch", 6, covered, launchRows); break;
            }
        }

        void collectLabels() {
            collectedLabels.clear();
            float savedAlpha = UiRender::alphaMul;
            float savedMouseX = NewUi::mouseX;
            float savedMouseY = NewUi::mouseY;
            bool savedDown = NewUi::mouseDown;
            NewUi::mouseX = -1.0e6f;
            NewUi::mouseY = -1.0e6f;
            NewUi::mouseDown = false;
            UiRender::alphaMul = 0.0f;
            UiRender::scissor(0.0f, 0.0f, 0.0f, 0.0f);
            {
                Page::ScrollScope scope("settings#collect");
                for (const Tab& tab : TABS) {
                    if (tab.section == SECTION_KEYS || tab.section == SECTION_JAVA || tab.section == SECTION_ABOUT) {
                        continue;
                    }
                    Page::collect(&collectedLabels[tab.section]);
                    drawSection(tab.section, true);
                    Page::collect(nullptr);
                }
            }
            UiRender::unscissor();
            UiRender::alphaMul = savedAlpha;
            NewUi::mouseX = savedMouseX;
            NewUi::mouseY = savedMouseY;
            NewUi::mouseDown = savedDown;
        }
    }

    void draw() {
        bool covered = NewUi::overlayCoversMouse();
        MainWindow::claimSearch();
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false) && !NewUi::popupOpen &&
            UiOverlays::isEmpty() && NewUi::textEditing != SEARCH_OWNER) {
            NewUi::beginTextEdit(searchValue());
        }
        rail(covered);
        std::string query = Ui::trim(typedSearch());
        sections.begin();
        Page::Column column(mainX());
        if (query.empty()) {
            labelsCollected = false;
            drawSection(sections.page, covered);
        } else {
            if (!labelsCollected && !NewUi::popupOpen) {
                collectLabels();
                labelsCollected = true;
            }
            resultsView(query, covered);
        }
        sections.end();
    }
}
