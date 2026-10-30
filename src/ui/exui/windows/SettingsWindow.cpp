#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/SettingsWindow.h"
#include "app/instances/Instances.h"
#include "app/launch/JavaDownloads.h"
#include "app/launch/LaunchDefaults.h"
#include "app/launch/Patches.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"
#include "app/system/Updates.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/InstancesWindow.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifndef B_LAUNCHER_VERSION
#define B_LAUNCHER_VERSION "0.0.0"
#endif

namespace ForgottenMelody {

    namespace {
        namespace blb = b_launcher_base;
        using exui::ControlKind;

        enum : uint32_t {
            // 游戏设置
            GAME = SectionedForm::FIRST_CONTROL,
            INSTANCES,
            ISOLATION_LABEL,
            ISOLATION,
            COUNT_LABEL,
            COUNT,
            REFRESH,
            MANAGE,
            LAUNCH,
            LANGUAGE_LABEL,
            LANGUAGE,
            GC_LABEL,
            GC,
            JVM_LABEL,
            JVM,
            GAME_ARGS_LABEL,
            GAME_ARGS,
            LOGS,
            PRE_LAUNCH_LABEL,
            PRE_LAUNCH,
            // 补丁
            PATCH,
            ENABLE,
            ALL,
            SKINS,
            SKIN_LEGACY,
            SKIN_AUTHLIB,
            SKIN_MODE_LABEL,
            SKIN_MODE,
            SKIN_SOURCE_LABEL,
            SKIN_SOURCE,
            SKIN_IMAGE_LABEL,
            SKIN_IMAGE,
            SKIN_IMAGE_PICK,
            CAPE_IMAGE_LABEL,
            CAPE_IMAGE,
            CAPE_IMAGE_PICK,
            PLAYER_LABEL,
            PLAYER,
            SKIN_TRUSTED,
            FIXES,
            LOG4J,
            IME,
            LWJGL,
            GAME_WINDOW,
            WINDOW_TITLE_LABEL,
            WINDOW_TITLE,
            WINDOW_ICON_LABEL,
            WINDOW_ICON,
            WINDOW_ICON_PICK,
            DEBUGGING,
            DEBUG_LOG,
            // 启动器设置
            LAUNCHER,
            LOOK,
            STYLE_LABEL,
            STYLE,
            STYLE_HINT,
            RESTART,
            // Java 管理
            JAVA,
            INSTALLED,
            JAVA_LIST,
            ADD_JAVA,
            REMOVE_JAVA,
            JAVA_FOLDER,
            JAVA_COUNT,
            RESCAN,
            DOWNLOADS,
            DOWNLOAD_LIST,
            MAJOR_LABEL,
            MAJOR,
            DOWNLOAD_STATUS,
            RELOAD,
            INSTALL,
            // 关于
            ABOUT,
            PRODUCT,
            PRODUCT_VERSION,
            TAGLINE,
            UPDATE_STATUS,
            UPDATE,
            PROJECT,
            THANKS,
            CREDITS,
            // The links of 关于 take two ids each from here on.
            LINKS,
        };

        // The window's coordinates of the sections' panels.
        constexpr int PANEL_LEFT = 210, PANEL_TOP = 75;

        struct Isolation {
            blb::instance::isolation_mode mode;
            const wchar_t* label;
        };
        const Isolation ISOLATIONS[] = {
                {blb::instance::isolation_mode::none, L"不隔离"},
                {blb::instance::isolation_mode::modded, L"仅隔离 Mod 实例"},
                {blb::instance::isolation_mode::all, L"全部隔离"},
        };

        struct Language {
            LaunchDefaults::Language language;
            const wchar_t* label;
        };
        const Language LANGUAGES[] = {
                {LaunchDefaults::Language::None, L"不自动设置"},
                {LaunchDefaults::Language::Chinese, L"自动设置为中文"},
                {LaunchDefaults::Language::English, L"自动设置为英文"},
        };

        struct Collector {
            blb::launch::gc_mode mode;
            const wchar_t* label;
        };
        const Collector COLLECTORS[] = {
                {blb::launch::gc_mode::zgc, L"尽量使用 ZGC"},
                {blb::launch::gc_mode::generational_zgc, L"尽量使用分代 ZGC"},
                {blb::launch::gc_mode::g1, L"标准 G1GC"},
                {blb::launch::gc_mode::tuned_g1, L"调优 G1GC"},
                {blb::launch::gc_mode::none, L"不指定（可自定义）"},
        };

        // The patch settings with the explanations of the default interface, whose hints for empty fields end them.
        struct Choice {
            const char* value;
            const wchar_t* label;
        };
        const Choice SKIN_MODES[] = {
                {"fallback", L"只补全没有皮肤的玩家"},
                {"override", L"强制使用自定义皮肤"},
        };
        const Choice SKIN_SOURCES[] = {
                {"mojang", L"Mojang"},
                {"littleskin", L"LittleSkin"},
                {"custom", L"自定义"},
        };

        struct Flag {
            uint32_t id;
            const char* key;
            const wchar_t* explanation;
        };
        const Flag FLAGS[] = {
                {SKIN_LEGACY, "skin.legacy",
                 L"1.7.5 及以下版本使用的 s3.amazonaws.com、skins.minecraft.net "
                 L"皮肤地址已经失效，改为从皮肤来源加载。"},
                {SKIN_AUTHLIB, "skin.authlib", L"通过 authlib 从皮肤来源加载皮肤和披风，适用于 1.7.6 及以上版本。"},
                {SKIN_TRUSTED, "skin.trusted",
                 L"1.20.3 起游戏只显示其他玩家已签名的材质，开启后自定义材质会被当作已签名。"},
                {LOG4J, "log4j.enabled", L"修复 log4j2 的远程代码执行漏洞（CVE-2021-44228）。"},
                {IME, "input.enabled", L"修复 1.12.2 及以下版本（LWJGL 2）无法输入中文的问题。"},
                {LWJGL, "lwjgl.unsafe",
                 L"在 Java 25 以上运行 LWJGL 3 时，让 MemoryUtil 改用 jdk.internal.misc.Unsafe，避开 sun.misc.Unsafe "
                 L"的弃用路径；LWJGL 3.4 会启用自带的对应后端。"},
                {DEBUG_LOG, "debug", L"在游戏日志里输出每个被修改的类和每次皮肤查询。"},
        };

        struct Value {
            uint32_t label;
            uint32_t id;
            const char* key;
            const wchar_t* explanation;
        };
        const Value VALUES[] = {
                {SKIN_IMAGE_LABEL, SKIN_IMAGE, "skin.template.skin",
                 L"PNG 皮肤图片，也可以填写网址；手臂粗细会自动识别。留空则不使用。"},
                {CAPE_IMAGE_LABEL, CAPE_IMAGE, "skin.template.cape", L"PNG 披风图片，也可以填写网址。留空则不使用。"},
                {PLAYER_LABEL, PLAYER, "skin.player",
                 L"填写后使用这个正版玩家的皮肤和披风，启动游戏时获取；留空则按每个玩家自己的名字查询。"},
                {WINDOW_TITLE_LABEL, WINDOW_TITLE, "window.title",
                 L"{title} 代表游戏原来的标题，例如 {title} - 我的服务器。留空则不修改。"},
                {WINDOW_ICON_LABEL, WINDOW_ICON, "window.icon",
                 L"图片路径，多个不同尺寸的图片用英文逗号分隔；支持 PNG、ICO、BMP、GIF 等格式。留空则不修改。"},
        };

        // Every control of 皮肤 that its rows move or hide.
        const uint32_t SKIN_CONTROLS[] = {
                SKIN_LEGACY, SKIN_AUTHLIB,     SKIN_MODE_LABEL, SKIN_MODE,       SKIN_SOURCE_LABEL,
                SKIN_SOURCE, SKIN_IMAGE_LABEL, SKIN_IMAGE,      SKIN_IMAGE_PICK, CAPE_IMAGE_LABEL,
                CAPE_IMAGE,  CAPE_IMAGE_PICK,  PLAYER_LABEL,    PLAYER,          SKIN_TRUSTED,
        };

        struct Link {
            const wchar_t* name;
            const wchar_t* value;
            const wchar_t* url;
        };
        const Link PROJECT_LINKS[] = {
                {L"作者", L"cubk", L"https://cubk.cn/"},
                {L"GitHub", L"cubk1/b-launcher", L"https://github.com/cubk1/b-launcher"},
        };
        const Link THANKS_LINKS[] = {
                {L"BMCLAPI", L"提供下载源", L"https://www.bangbang93.com/"},
                {L"PCL", L"提供主页系统", L"https://github.com/Meloong-Git/PCL"},
        };
        const Link CREDIT_LINKS[] = {
                {L"Dear ImGui", L"MIT", L"https://github.com/ocornut/imgui"},
                {L"FreeType", L"FreeType License", L"https://freetype.org"},
                {L"libcurl", L"curl License", L"https://curl.se"},
                {L"zlib-ng", L"zlib License", L"https://github.com/zlib-ng/zlib-ng"},
                {L"minizip-ng", L"zlib License", L"https://github.com/zlib-ng/minizip-ng"},
                {L"simdjson", L"Apache-2.0", L"https://github.com/simdjson/simdjson"},
                {L"JSON for Modern C++", L"MIT", L"https://github.com/nlohmann/json"},
                {L"reproc", L"MIT", L"https://github.com/DaanDeMeyer/reproc"},
                {L"stb", L"MIT", L"https://github.com/nothings/stb"},
                {L"nanosvg", L"zlib License", L"https://github.com/memononen/nanosvg"},
                {L"Font Awesome Free", L"SIL OFL 1.1", L"https://fontawesome.com"},
        };

        // Text that says less than the text around it.
        constexpr Gdiplus::ARGB MUTED = 0x96000000;

        const wchar_t* archName(blb::cpu_arch arch) {
            switch (arch) {
                case blb::cpu_arch::x86: return L"x86";
                case blb::cpu_arch::x86_64: return L"x64";
                case blb::cpu_arch::arm32: return L"ARM32";
                case blb::cpu_arch::arm64: return L"ARM64";
                default: return L"";
            }
        }

        // The section shown last and the Java shown for download, for the rest of the run.
        int lastSection = 0;
        int javaMajor = 21;

        // The sections of the default interface in the frame of 下载中心. Every change applies at once, as in the
        // default interface.
        class SettingsWindow : public SectionedForm {
        public:
            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        // 游戏设置
                        {INSTANCES, ControlKind::Group, {225, 85, 645, 105}, L"实例"},
                        {ISOLATION_LABEL, ControlKind::Label, {245, 112, 95, 26}, L"默认版本隔离"},
                        {ISOLATION, ControlKind::Combo, {345, 110, 200, 28}},
                        {COUNT_LABEL, ControlKind::Label, {245, 147, 95, 26}, L"实例列表"},
                        {COUNT, ControlKind::Label, {345, 147, 110, 26}},
                        {REFRESH, ControlKind::Button, {460, 145, 80, 30}, L"重新读取"},
                        {MANAGE, ControlKind::Button, {550, 145, 90, 30}, L"实例管理"},
                        {LAUNCH, ControlKind::Group, {225, 205, 645, 245}, L"启动"},
                        {LANGUAGE_LABEL, ControlKind::Label, {245, 232, 95, 26}, L"游戏语言"},
                        {LANGUAGE, ControlKind::Combo, {345, 230, 200, 28}},
                        {GC_LABEL, ControlKind::Label, {245, 267, 95, 26}, L"内存管理"},
                        {GC, ControlKind::Combo, {345, 265, 200, 28}},
                        {JVM_LABEL, ControlKind::Label, {245, 302, 95, 26}, L"JVM 参数"},
                        {JVM, ControlKind::Edit, {345, 300, 505, 26}},
                        {GAME_ARGS_LABEL, ControlKind::Label, {245, 337, 95, 26}, L"游戏参数"},
                        {GAME_ARGS, ControlKind::Edit, {345, 335, 505, 26}},
                        {LOGS, ControlKind::Check, {245, 370, 200, 26}, L"显示游戏日志"},
                        {PRE_LAUNCH_LABEL, ControlKind::Label, {245, 407, 95, 26}, L"启动前命令"},
                        {PRE_LAUNCH, ControlKind::Edit, {345, 405, 505, 26}},
                        // 补丁, in two columns. 皮肤 is laid out as with every row shown; refresh() closes it up.
                        {ENABLE, ControlKind::Group, {225, 85, 315, 70}, L"启用"},
                        {ALL, ControlKind::Check, {245, 110, 270, 26}, L"为所有实例启用"},
                        {SKINS, ControlKind::Group, {225, 170, 315, 280}, L"皮肤"},
                        {SKIN_LEGACY, ControlKind::Check, {245, 195, 270, 26}, L"修复旧版皮肤"},
                        {SKIN_AUTHLIB, ControlKind::Check, {245, 230, 270, 26}, L"自定义皮肤"},
                        {SKIN_MODE_LABEL, ControlKind::Label, {245, 267, 75, 26}, L"皮肤优先级"},
                        {SKIN_MODE, ControlKind::Combo, {325, 265, 195, 28}},
                        {SKIN_SOURCE_LABEL, ControlKind::Label, {245, 302, 75, 26}, L"皮肤来源"},
                        {SKIN_SOURCE, ControlKind::Combo, {325, 300, 195, 28}},
                        {SKIN_IMAGE_LABEL, ControlKind::Label, {245, 337, 75, 26}, L"皮肤图片"},
                        {SKIN_IMAGE, ControlKind::Edit, {325, 335, 140, 26}},
                        {SKIN_IMAGE_PICK, ControlKind::Button, {470, 335, 50, 26}, L"浏览"},
                        {CAPE_IMAGE_LABEL, ControlKind::Label, {245, 372, 75, 26}, L"披风图片"},
                        {CAPE_IMAGE, ControlKind::Edit, {325, 370, 140, 26}},
                        {CAPE_IMAGE_PICK, ControlKind::Button, {470, 370, 50, 26}, L"浏览"},
                        {PLAYER_LABEL, ControlKind::Label, {245, 337, 75, 26}, L"正版玩家名"},
                        {PLAYER, ControlKind::Edit, {325, 335, 195, 26}},
                        {SKIN_TRUSTED, ControlKind::Check, {245, 405, 270, 26}, L"标记为已签名"},
                        {FIXES, ControlKind::Group, {555, 85, 315, 140}, L"修复"},
                        {LOG4J, ControlKind::Check, {575, 110, 270, 26}, L"Log4Shell 漏洞修复"},
                        {IME, ControlKind::Check, {575, 145, 270, 26}, L"中文输入修复"},
                        {LWJGL, ControlKind::Check, {575, 180, 270, 26}, L"LWJGL 内存访问"},
                        {GAME_WINDOW, ControlKind::Group, {555, 240, 315, 105}, L"窗口"},
                        {WINDOW_TITLE_LABEL, ControlKind::Label, {575, 267, 75, 26}, L"窗口标题"},
                        {WINDOW_TITLE, ControlKind::Edit, {655, 265, 195, 26}},
                        {WINDOW_ICON_LABEL, ControlKind::Label, {575, 302, 75, 26}, L"窗口图标"},
                        {WINDOW_ICON, ControlKind::Edit, {655, 300, 140, 26}},
                        {WINDOW_ICON_PICK, ControlKind::Button, {800, 300, 50, 26}, L"浏览"},
                        {DEBUGGING, ControlKind::Group, {555, 360, 315, 70}, L"调试"},
                        {DEBUG_LOG, ControlKind::Check, {575, 385, 270, 26}, L"输出调试日志"},
                        // 启动器设置
                        {LOOK, ControlKind::Group, {225, 85, 645, 75}, L"外观"},
                        {STYLE_LABEL, ControlKind::Label, {245, 112, 95, 26}, L"界面风格"},
                        {STYLE, ControlKind::Combo, {345, 110, 200, 28}},
                        {STYLE_HINT, ControlKind::Label, {555, 112, 195, 26}},
                        {RESTART, ControlKind::Button, {760, 108, 90, 30}, L"立即重启"},
                        // Java 管理: the Java found on this computer above, the Java to download below.
                        {INSTALLED, ControlKind::Group, {225, 85, 645, 205}, L"本机 Java"},
                        {JAVA_LIST, ControlKind::List, {240, 108, 615, 140}},
                        {ADD_JAVA, ControlKind::Button, {240, 254, 80, 28}, L"添加"},
                        {REMOVE_JAVA, ControlKind::Button, {325, 254, 80, 28}, L"移除"},
                        {JAVA_FOLDER, ControlKind::Button, {410, 254, 80, 28}, L"打开目录"},
                        {JAVA_COUNT, ControlKind::Label, {500, 255, 260, 26}},
                        {RESCAN, ControlKind::Button, {765, 254, 90, 28}, L"重新查找"},
                        {DOWNLOADS, ControlKind::Group, {225, 300, 645, 205}, L"下载 Java"},
                        {DOWNLOAD_LIST, ControlKind::List, {240, 323, 615, 140}},
                        {MAJOR_LABEL, ControlKind::Label, {240, 470, 40, 26}, L"版本"},
                        {MAJOR, ControlKind::Combo, {280, 469, 125, 28}},
                        {DOWNLOAD_STATUS, ControlKind::Label, {415, 470, 245, 26}},
                        {RELOAD, ControlKind::Button, {670, 469, 90, 28}, L"刷新列表"},
                        {INSTALL, ControlKind::Button, {765, 469, 90, 28}, L"安装"},
                        // 关于
                        {.id = PRODUCT,
                         .kind = ControlKind::Label,
                         .bounds = {240, 88, 200, 36},
                         .caption = L"B Launcher™",
                         .font = L"微软雅黑",
                         .fontSize = 22,
                         .fontStyle = Gdiplus::FontStyleBold},
                        {.id = PRODUCT_VERSION,
                         .kind = ControlKind::Label,
                         .bounds = {400, 97, 120, 22},
                         .caption = L"v" B_LAUNCHER_VERSION,
                         .textColor = MUTED},
                        {.id = TAGLINE,
                         .kind = ControlKind::Label,
                         .bounds = {242, 122, 300, 20},
                         .caption = L"an open-source minecraft launcher",
                         .textColor = MUTED},
                        {.id = UPDATE_STATUS,
                         .kind = ControlKind::Label,
                         .bounds = {600, 97, 150, 26},
                         .textColor = 0xD2C00000},
                        {UPDATE, ControlKind::Button, {760, 95, 90, 30}},
                        {PROJECT, ControlKind::Group, {225, 150, 315, 95}, L"项目"},
                        {THANKS, ControlKind::Group, {555, 150, 315, 95}, L"鸣谢"},
                        {CREDITS, ControlKind::Group, {225, 260, 645, 211}, L"开源组件"},
                };
                if (!createFrame(parent, L"设置", L"",
                                 {{L"游戏设置", GAME},
                                  {L"补丁", PATCH},
                                  {L"启动器设置", LAUNCHER},
                                  {L"Java 管理", JAVA},
                                  {L"关于", ABOUT}})) {
                    return false;
                }
                AddControls(CONTROLS);
                MoveInto(GAME,
                         {INSTANCES, ISOLATION_LABEL, ISOLATION, COUNT_LABEL, COUNT, REFRESH, MANAGE, LAUNCH,
                          LANGUAGE_LABEL, LANGUAGE, GC_LABEL, GC, JVM_LABEL, JVM, GAME_ARGS_LABEL, GAME_ARGS, LOGS,
                          PRE_LAUNCH_LABEL, PRE_LAUNCH});
                MoveInto(PATCH,
                         {ENABLE, ALL, SKINS, FIXES, LOG4J, IME, LWJGL, GAME_WINDOW, WINDOW_TITLE_LABEL, WINDOW_TITLE,
                          WINDOW_ICON_LABEL, WINDOW_ICON, WINDOW_ICON_PICK, DEBUGGING, DEBUG_LOG});
                for (uint32_t id : SKIN_CONTROLS) {
                    MoveInto(PATCH, {id});
                }
                MoveInto(LAUNCHER, {LOOK, STYLE_LABEL, STYLE, STYLE_HINT, RESTART});
                MoveInto(JAVA,
                         {INSTALLED, JAVA_LIST, ADD_JAVA, REMOVE_JAVA, JAVA_FOLDER, JAVA_COUNT, RESCAN, DOWNLOADS,
                          DOWNLOAD_LIST, MAJOR_LABEL, MAJOR, DOWNLOAD_STATUS, RELOAD, INSTALL});
                MoveInto(ABOUT, {PRODUCT, PRODUCT_VERSION, TAGLINE, UPDATE_STATUS, UPDATE, PROJECT, THANKS, CREDITS});
                fillGame();
                fillPatch();
                fillLauncher();
                fillJava();
                fillAbout();
                showSection(lastSection);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                refreshGame();
                layoutSkins();
                refreshLauncher();
                // The list of Java to download is fetched the first time it is asked for.
                if (FindControl(JAVA)->IsVisible()) {
                    refreshJava();
                }
                refreshAbout();
            }

            void OnSelection(uint32_t id, int index) override {
                if (id < FIRST_CONTROL) {
                    SectionedForm::OnSelection(id, index);
                    return;
                }
                if (index < 0) {
                    return;
                }
                switch (id) {
                    case ISOLATION: Instances::setIsolation(ISOLATIONS[index].mode); break;
                    case LANGUAGE: LaunchDefaults::setLanguage(LANGUAGES[index].language); break;
                    case GC: LaunchDefaults::setGc(COLLECTORS[index].mode); break;
                    case SKIN_MODE: Patches::setValue("skin.mode", SKIN_MODES[index].value); break;
                    case SKIN_SOURCE: Patches::setValue("skin.source", SKIN_SOURCES[index].value); break;
                    case STYLE: UiStyles::set(UiStyles::all()[index].key); break;
                    case MAJOR:
                        if (index < (int) shownMajors.size()) {
                            javaMajor = shownMajors[index];
                            Control<exui::ListView>(DOWNLOAD_LIST).SetSelectedRow(-1);
                        }
                        break;
                }
                refresh();
            }

            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) {
                    SectionedForm::OnAction(id);
                    return;
                }
                switch (id) {
                    case REFRESH: Instances::refresh(); break;
                    case MANAGE: showInstances(GetHwnd()); break;
                    case LOGS: LaunchDefaults::setShowLogs(Checked(LOGS)); break;
                    case ALL: Patches::setAll(Checked(ALL)); break;
                    case SKIN_IMAGE_PICK: pickImage(SKIN_IMAGE, true); break;
                    case CAPE_IMAGE_PICK: pickImage(CAPE_IMAGE, true); break;
                    case WINDOW_ICON_PICK: pickImage(WINDOW_ICON, false); break;
                    case RESTART:
                        if (!Platform::restart()) {
                            message(GetHwnd(), L"重启失败，请手动重新打开启动器", L"设置");
                        }
                        break;
                    case ADD_JAVA:
                        if (std::optional<std::filesystem::path> picked = Platform::pickJava({})) {
                            Catalog::addJava(*picked);
                        }
                        break;
                    case REMOVE_JAVA: removeJava(); break;
                    case JAVA_FOLDER: openJava(); break;
                    case RESCAN: Catalog::refreshJava(); break;
                    case RELOAD: JavaDownloads::refresh(); break;
                    case INSTALL:
                        if (const JavaDownloads::Package* package = selectedPackage()) {
                            JavaDownloads::install(*package);
                        }
                        break;
                    case UPDATE:
                        if (Updates::state() == Updates::State::Available) {
                            Updates::openDownload();
                        } else {
                            // A failure shows beside the button rather than as a notice behind the window.
                            Updates::check(false, false);
                        }
                        break;
                    default:
                        for (const Flag& flag : FLAGS) {
                            if (flag.id == id) {
                                Patches::setFlag(flag.key, Checked(id));
                            }
                        }
                        break;
                }
                refresh();
            }

            // Like the default interface, 关于 looks for a new version whenever it is opened.
            void sectionShown(int index) override {
                lastSection = index;
                if (FindControl(ABOUT)->IsVisible() && Updates::state() != Updates::State::Checking) {
                    Updates::check(false, false);
                }
                refresh();
            }

        private:
            // How 皮肤 is laid out now, so that its rows only move when it changes.
            int skinShape = -1;
            Rows shownJava, shownDownloads;
            // The executable of each Java shown and the folder of each download; empty for a row saying there is none.
            std::vector<std::string> javaKeys, downloadKeys;
            // The Java versions offered to filter the downloads by, 0 for all of them.
            std::vector<int> shownMajors;

            // A text setting, applied as it is typed, with the explanation of the default interface on its label.
            void text(uint32_t label, uint32_t id, const std::string& value,
                      std::function<void(const std::string&)> apply, const wchar_t* explanation) {
                auto& edit = Control<exui::EditBox>(id);
                edit.SetText(Platform::widen(value));
                edit.OnTextChanged = [apply](const std::wstring& typed) { apply(trimmed(typed)); };
                FindControl(label)->SetTooltipText(explanation);
                edit.SetTooltipText(explanation);
            }

            void setValue(uint32_t id, const std::wstring& value) {
                if (Text(id) != value) {
                    SetText(id, value);
                }
            }

            void fillGame() {
                std::vector<std::wstring> options;
                int selected = 0;
                for (int i = 0; i < (int) std::size(ISOLATIONS); i++) {
                    options.push_back(ISOLATIONS[i].label);
                    selected = ISOLATIONS[i].mode == Instances::isolation() ? i : selected;
                }
                choose(ISOLATION, options, selected);

                options.clear();
                for (int i = 0; i < (int) std::size(LANGUAGES); i++) {
                    options.push_back(LANGUAGES[i].label);
                    selected = LANGUAGES[i].language == LaunchDefaults::language() ? i : selected;
                }
                choose(LANGUAGE, options, selected);
                FindControl(LANGUAGE_LABEL)
                        ->SetTooltipText(L"启动时，如果游戏目录里还没有设置过语言（比如新实例第一次启动），就把游戏语言"
                                         L"设为中文或英文；之后在游戏里改的语言不会被覆盖。");

                options.clear();
                for (int i = 0; i < (int) std::size(COLLECTORS); i++) {
                    options.push_back(COLLECTORS[i].label);
                    selected = COLLECTORS[i].mode == LaunchDefaults::gc() ? i : selected;
                }
                choose(GC, options, selected);
                FindControl(GC_LABEL)->SetTooltipText(
                        L"游戏用的垃圾回收器。ZGC 停顿更短，需要 Java 15 以上，分代 ZGC 需要 Java 21 以上，"
                        L"Java 版本不够时改用标准 G1GC；调优 G1GC 是启动器默认的一组 G1 参数；不指定则不加回收器参数，"
                        L"可以在 JVM 参数里自己写。实例在高级设置里关闭默认 JVM 参数时，这里不生效。");

                text(JVM_LABEL, JVM, LaunchDefaults::jvmArgs(), LaunchDefaults::setJvmArgs,
                     L"所有实例都会加上，排在实例自己的 JVM 参数前面。空格分隔，含空格的参数用双引号括起来。");
                text(GAME_ARGS_LABEL, GAME_ARGS, LaunchDefaults::gameArgs(), LaunchDefaults::setGameArgs,
                     L"所有实例都会加上，排在实例自己的游戏参数前面。空格分隔，含空格的参数用双引号括起来。");
                text(PRE_LAUNCH_LABEL, PRE_LAUNCH, LaunchDefaults::preLaunch(), LaunchDefaults::setPreLaunch,
                     L"每个实例启动前通过 cmd 运行，在实例自己的启动前命令之前，启动器等它结束再启动游戏；工作目录是"
                     L"游戏目录，可用 %INST_NAME%、%INST_DIR%、%INST_MC_DIR%、%INST_JAVA% 等环境变量。");

                SetChecked(LOGS, LaunchDefaults::showLogs());
                FindControl(LOGS)->SetTooltipText(
                        L"游戏启动时打开日志窗口；实例可以在版本设置里另行设置。不显示时，游戏异常退出也会自动打开。");
            }

            void refreshGame() {
                setValue(COUNT,
                         Instances::loading() ? std::wstring(L"正在读取…")
                                              : std::to_wstring(Instances::list().size()) + L" 个实例");
                FindControl(REFRESH)->SetEnabled(!Instances::loading());
            }

            void choosePatch(uint32_t label, uint32_t id, const char* key, std::span<const Choice> choices,
                             const wchar_t* explanation) {
                std::string current = Patches::value(key);
                std::vector<std::wstring> options;
                int selected = 0;
                for (int i = 0; i < (int) choices.size(); i++) {
                    options.push_back(choices[i].label);
                    selected = current == choices[i].value ? i : selected;
                }
                choose(id, options, selected);
                FindControl(label)->SetTooltipText(explanation);
                FindControl(id)->SetTooltipText(explanation);
            }

            void fillPatch() {
                SetChecked(ALL, Patches::all());
                FindControl(ALL)->SetTooltipText(
                        L"关闭时，只有在版本设置里把“启动器补丁”设为启用的实例会加载补丁；开启后，实例也可以单独设为"
                        L"不启用。补丁和 authlib-injector、自己在 JVM 参数里添加的 -javaagent 可以同时使用。");
                for (const Flag& flag : FLAGS) {
                    SetChecked(flag.id, Patches::flag(flag.key));
                    FindControl(flag.id)->SetTooltipText(flag.explanation);
                }
                choosePatch(
                        SKIN_MODE_LABEL, SKIN_MODE, "skin.mode", SKIN_MODES,
                        L"只补全没有皮肤的玩家：正版玩家自带的皮肤不变，只给没有皮肤的玩家（如离线玩家）使用皮肤来源。"
                        L"强制使用自定义皮肤：先查皮肤来源，皮肤来源没有的材质再用玩家原有的。");
                choosePatch(SKIN_SOURCE_LABEL, SKIN_SOURCE, "skin.source", SKIN_SOURCES,
                            L"Mojang：从正版服务器获取皮肤；LittleSkin：按玩家名从 LittleSkin "
                            L"皮肤站获取；自定义：使用选择的"
                            L"皮肤和披风图片。");
                for (const Value& value : VALUES) {
                    const char* key = value.key;
                    text(
                            value.label, value.id, Patches::value(key),
                            [key](const std::string& typed) { Patches::setValue(key, typed); }, value.explanation);
                }
            }

            // The rows of 皮肤 as in the default interface: those of the source only while custom skins are on.
            void layoutSkins() {
                bool custom = Patches::flag("skin.authlib");
                std::string source = Patches::value("skin.source");
                int shape = !custom ? 0 : source == "custom" ? 1 : source == "littleskin" ? 2 : 3;
                if (shape == skinShape) {
                    return;
                }
                skinShape = shape;
                std::vector<std::vector<uint32_t>> rows = {{SKIN_LEGACY}, {SKIN_AUTHLIB}};
                if (custom) {
                    rows.push_back({SKIN_MODE_LABEL, SKIN_MODE});
                    rows.push_back({SKIN_SOURCE_LABEL, SKIN_SOURCE});
                    if (shape == 1) {
                        rows.push_back({SKIN_IMAGE_LABEL, SKIN_IMAGE, SKIN_IMAGE_PICK});
                        rows.push_back({CAPE_IMAGE_LABEL, CAPE_IMAGE, CAPE_IMAGE_PICK});
                    } else if (shape == 3) {
                        rows.push_back({PLAYER_LABEL, PLAYER});
                    }
                    rows.push_back({SKIN_TRUSTED});
                }
                for (uint32_t id : SKIN_CONTROLS) {
                    FindControl(id)->SetVisible(false);
                }
                const RECT group = FindControl(SKINS)->GetBounds();
                for (int i = 0; i < (int) rows.size(); i++) {
                    for (uint32_t id : rows[i]) {
                        exui::Widget* widget = FindControl(id);
                        RECT bounds = widget->GetBounds();
                        // Labels sit 2 pixels lower than the controls beside them.
                        int top = group.top + 25 + 35 * i + (dynamic_cast<exui::Label*>(widget) != nullptr ? 2 : 0);
                        widget->SetBounds(bounds.left, top, bounds.right - bounds.left, bounds.bottom - bounds.top);
                        widget->SetVisible(true);
                    }
                }
                FindControl(SKINS)->SetBounds(group.left, group.top, group.right - group.left,
                                              35 * (int) rows.size() + 35);
            }

            // Like the default interface: PNG files for the skin and the cape, and the dialog opens where the
            // current file is.
            void pickImage(uint32_t id, bool png) {
                std::string current = trimmed(Text(id));
                bool local = !current.empty() && current.find(',') == std::string::npos &&
                        current.find("://") == std::string::npos;
                std::filesystem::path start = local ? std::filesystem::path(Platform::widen(current)).parent_path()
                                                    : Platform::exeDirectory();
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
                    // Saved by the edit box, as if typed.
                    SetText(id, picked->wstring());
                }
            }

            void fillLauncher() {
                std::vector<std::wstring> options;
                int selected = 0;
                const auto& styles = UiStyles::all();
                for (int i = 0; i < (int) styles.size(); i++) {
                    options.push_back(Platform::widen(styles[i].label));
                    selected = &styles[i] == &UiStyles::current() ? i : selected;
                }
                choose(STYLE, options, selected);
                FindControl(STYLE_LABEL)->SetTooltipText(L"整个启动器的界面，重启后生效。");
            }

            void refreshLauncher() {
                // A new interface style waits for a restart, which a game, an install or a download would block.
                bool pending = &UiStyles::current() != &UiStyles::running();
                bool busy = Services::busy();
                setValue(STYLE_HINT, !pending ? L"" : busy ? L"任务进行中，暂不能重启" : L"重启后生效");
                FindControl(RESTART)->SetVisible(pending);
                FindControl(RESTART)->SetEnabled(!busy);
            }

            void fillJava() {
                auto& javas = Control<exui::ListView>(JAVA_LIST);
                javas.InsertColumn(L"版本", 120);
                javas.InsertColumn(L"类型", 50);
                javas.InsertColumn(L"架构", 55);
                javas.InsertColumn(L"位置", 305);
                javas.InsertColumn(L"来源", 70);
                auto& downloads = Control<exui::ListView>(DOWNLOAD_LIST);
                downloads.InsertColumn(L"名称", 250);
                downloads.InsertColumn(L"版本", 150);
                downloads.InsertColumn(L"大小", 80);
                downloads.InsertColumn(L"状态", 120);
                Control<exui::Label>(JAVA_COUNT)
                        .SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
            }

            void refreshJava() {
                const Catalog::JavaList& javas = Catalog::java();
                bool finding = javas.status == Catalog::Status::Loading;
                Rows rows;
                std::vector<std::string> keys;
                for (const blb::java::java_installation& java : javas.list) {
                    std::string name = "Java " + (java.version.empty() ? std::to_string(java.major) : java.version);
                    rows.push_back({Platform::widen(name), java.jdk ? L"JDK" : L"JRE", archName(java.arch),
                                    Platform::widen(Platform::display(java.home)),
                                    Catalog::addedByHand(java.executable) ? L"手动添加" : L"自动查找"});
                    keys.push_back(Platform::narrow(java.executable.wstring()));
                }
                if (rows.empty()) {
                    rows.push_back({finding ? L"正在查找…" : L"没有找到 Java", L"", L"", L"", L""});
                    keys.emplace_back();
                }
                fill(Control<exui::ListView>(JAVA_LIST), shownJava, javaKeys, std::move(rows), std::move(keys));
                setValue(JAVA_COUNT,
                         finding && javas.list.empty() ? std::wstring(L"正在查找…")
                                                       : L"共 " + std::to_wstring(javas.list.size()) + L" 个");
                bool chosen = !selectedKey(Control<exui::ListView>(JAVA_LIST), javaKeys).empty();
                FindControl(REMOVE_JAVA)->SetEnabled(chosen);
                FindControl(JAVA_FOLDER)->SetEnabled(chosen);
                FindControl(RESCAN)->SetEnabled(!finding);

                const JavaDownloads::List& list = JavaDownloads::list();
                bool loading = list.status == JavaDownloads::Status::Loading;
                std::vector<int> majors = {0};
                for (const JavaDownloads::Package& package : list.packages) {
                    if (std::find(majors.begin(), majors.end(), package.major) == majors.end()) {
                        majors.push_back(package.major);
                    }
                }
                // The version chosen stays offered while the list loads, or when it has none of it.
                if (std::find(majors.begin(), majors.end(), javaMajor) == majors.end()) {
                    majors.push_back(javaMajor);
                }
                if (majors != shownMajors) {
                    std::vector<std::wstring> options;
                    for (int major : majors) {
                        options.push_back(major == 0 ? std::wstring(L"全部版本") : L"Java " + std::to_wstring(major));
                    }
                    Control<exui::ComboBox>(MAJOR).ClearItems();
                    choose(MAJOR, options, (int) (std::find(majors.begin(), majors.end(), javaMajor) - majors.begin()));
                    shownMajors = std::move(majors);
                }
                FindControl(MAJOR)->SetEnabled(!loading);

                rows.clear();
                keys.clear();
                for (const JavaDownloads::Package& package : list.packages) {
                    if (javaMajor != 0 && package.major != javaMajor) {
                        continue;
                    }
                    std::wstring size = package.archiveSize > 0
                            ? std::to_wstring((package.archiveSize + 500000) / 1000000) + L" MB"
                            : std::wstring();
                    rows.push_back({Platform::widen(JavaDownloads::name(package)),
                                    Platform::widen(package.version) + (package.preview ? L" 预览" : L""), size,
                                    stateOf(package)});
                    keys.push_back(package.folder);
                }
                int count = (int) rows.size();
                if (rows.empty()) {
                    const wchar_t* none = loading                          ? L"正在获取…"
                            : list.status == JavaDownloads::Status::Failed ? L"获取失败"
                            : list.packages.empty()                        ? L"列表是空的"
                                                                           : L"没有这个版本的 Java";
                    rows.push_back({none, L"", L"", L""});
                    keys.emplace_back();
                }
                fill(Control<exui::ListView>(DOWNLOAD_LIST), shownDownloads, downloadKeys, std::move(rows),
                     std::move(keys));
                std::wstring status = loading ? std::wstring(L"正在获取 Java 列表…")
                        : list.status == JavaDownloads::Status::Failed
                        ? L"获取 Java 列表失败：" + Platform::widen(list.error)
                        : L"共 " + std::to_wstring(count) + L" 个";
                if (Text(DOWNLOAD_STATUS) != status) {
                    SetText(DOWNLOAD_STATUS, status);
                    FindControl(DOWNLOAD_STATUS)->SetTooltipText(status);
                }
                FindControl(RELOAD)->SetEnabled(!loading);
                const JavaDownloads::Package* package = selectedPackage();
                FindControl(INSTALL)->SetEnabled(
                        package != nullptr && JavaDownloads::state(package->folder) != JavaDownloads::State::Running &&
                        stateOf(*package) != L"已安装");
            }

            static std::wstring stateOf(const JavaDownloads::Package& package) {
                switch (JavaDownloads::state(package.folder)) {
                    case JavaDownloads::State::Running:
                        return L"安装中 " +
                                std::to_wstring((int) std::lround(JavaDownloads::progress(package.folder) * 100)) +
                                L"%";
                    case JavaDownloads::State::Done: return L"已安装";
                    case JavaDownloads::State::Failed: return L"安装失败";
                    default: return JavaDownloads::installed(package) ? L"已安装" : L"";
                }
            }

            const JavaDownloads::Package* selectedPackage() const {
                std::string folder = selectedKey(Control<exui::ListView>(DOWNLOAD_LIST), downloadKeys);
                for (const JavaDownloads::Package& package : JavaDownloads::list().packages) {
                    if (!folder.empty() && package.folder == folder) {
                        return &package;
                    }
                }
                return nullptr;
            }

            // The list of Java is found again under the question, so the Java is taken before it.
            void removeJava() {
                const auto& list = Control<exui::ListView>(JAVA_LIST);
                std::string key = selectedKey(list, javaKeys);
                if (key.empty()) {
                    return;
                }
                std::wstring name = shownJava[list.GetSelectedRow()][0];
                if (message(GetHwnd(), L"要从列表中移除 " + name + L" 吗？", L"Java 管理",
                            exui::MsgBoxButtons::OkCancel, {L"取消", L"移除"}) == 0) {
                    Catalog::removeJava(Platform::widen(key));
                }
            }

            void openJava() {
                std::filesystem::path executable =
                        Platform::widen(selectedKey(Control<exui::ListView>(JAVA_LIST), javaKeys));
                for (const blb::java::java_installation& java : Catalog::java().list) {
                    if (java.executable == executable) {
                        Platform::open(java.home);
                        return;
                    }
                }
            }

            // A link of 关于: a name and a value, of which the one that names the page opens it.
            void addLink(uint32_t& id, const Link& link, int x, int y, int valueX, bool valueOpens) {
                uint32_t name = id++, value = id++;
                AddControl({.id = name,
                            .kind = valueOpens ? ControlKind::Label : ControlKind::Hyperlink,
                            .bounds = {x, y, valueX - x - 5, 22},
                            .caption = link.name});
                AddControl({.id = value,
                            .kind = valueOpens ? ControlKind::Hyperlink : ControlKind::Label,
                            .bounds = {valueX, y, x + 295 - valueX, 22},
                            .caption = link.value,
                            .textColor = valueOpens ? 0xD2000000 : MUTED});
                auto& hyperlink = Control<exui::Hyperlink>(valueOpens ? value : name);
                hyperlink.SetTargetUrl(link.url);
                hyperlink.SetTooltipText(link.url);
                // Only the text opens the page.
                RECT bounds = hyperlink.GetBounds();
                int width = (int) std::ceil(exui::widget_detail::UnboundedTextWidth(hyperlink)) + 2;
                hyperlink.SetBounds(bounds.left, bounds.top, width, bounds.bottom - bounds.top);
                MoveInto(ABOUT, {name, value});
            }

            void fillAbout() {
                auto& product = *FindControl(PRODUCT);
                int productRight = 240 + (int) std::ceil(exui::widget_detail::UnboundedTextWidth(product));
                FindControl(PRODUCT_VERSION)->SetBounds(productRight + 10 - PANEL_LEFT, 104 - PANEL_TOP, 120, 22);
                Control<exui::Label>(UPDATE_STATUS)
                        .SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
                uint32_t id = LINKS;
                for (int i = 0; i < (int) std::size(PROJECT_LINKS); i++) {
                    addLink(id, PROJECT_LINKS[i], 245, 175 + 30 * i, 320, true);
                }
                for (int i = 0; i < (int) std::size(THANKS_LINKS); i++) {
                    addLink(id, THANKS_LINKS[i], 575, 175 + 30 * i, 670, false);
                }
                // Two columns of six.
                for (int i = 0; i < (int) std::size(CREDIT_LINKS); i++) {
                    int x = i < 6 ? 245 : 575;
                    addLink(id, CREDIT_LINKS[i], x, 287 + 30 * (i % 6), x + 160, false);
                }
            }

            void refreshAbout() {
                Updates::State state = Updates::state();
                setValue(UPDATE,
                         state == Updates::State::Checking            ? std::wstring(L"正在检查…")
                                 : state == Updates::State::Latest    ? std::wstring(L"已是最新版本")
                                 : state == Updates::State::Available ? L"下载 v" + Platform::widen(Updates::latest())
                                                                      : std::wstring(L"检查更新"));
                FindControl(UPDATE)->SetEnabled(state != Updates::State::Checking && state != Updates::State::Latest);
                setValue(UPDATE_STATUS, state == Updates::State::Failed ? L"检查失败" : L"");
            }
        };
    }

    void showSettings(HWND parent) {
        SettingsWindow window;
        if (window.create(parent)) {
            runModal(window);
        }
    }
}
