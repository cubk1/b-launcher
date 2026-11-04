#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/SettingsWindow.h"
#include "app/instances/Instances.h"
#include "app/downloads/DownloadSources.h"
#include "app/launch/JavaDownloads.h"
#include "app/launch/GameOptions.h"
#include "app/launch/LaunchDefaults.h"
#include "app/launch/Patches.h"
#include "app/system/About.h"
#include "app/system/Catalog.h"
#include "app/system/Config.h"
#include "app/system/CustomDownload.h"
#include "app/system/Notices.h"
#include "app/system/Network.h"
#include "app/system/PclThemes.h"
#include "app/system/Platform.h"
#include "app/system/Services.h"
#include "app/system/UiStyles.h"
#include "app/system/Updates.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/ProgressBar.h"
#include "ui/exui/widgets/ScrollPanel.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/InstancesWindow.h"
#include "ui/exui/windows/KeyBindPanel.h"
#include "ui/exui/windows/GameOptionsPanel.h"

#include <core/base/format.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

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
            GAME_OPTIONS_GROUP, GAME_SOURCE_LABEL, GAME_SOURCE, GAME_OPTIONS_PANEL,
            KEYS, KEYS_SOURCE_LABEL, KEYS_SOURCE,
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
            LAN_GROUP,
            LAN_OFFLINE_JOIN,
            DEBUGGING,
            DEBUG_LOG,
            // 启动器设置
            LAUNCHER,
            LOOK,
            STYLE_LABEL,
            STYLE,
            STYLE_HINT,
            RESTART,
            NETWORK_GROUP,
            SYSTEM_PROXY,
            SOURCE_MINECRAFT_LABEL, SOURCE_MINECRAFT,
            SOURCE_ASSETS_LABEL, SOURCE_ASSETS,
            SOURCE_MODRINTH_LABEL, SOURCE_MODRINTH,
            SOURCE_CURSEFORGE_LABEL, SOURCE_CURSEFORGE,
            RESET_GROUP,
            RESET_LABEL,
            RESET_SETTINGS,
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
            // 工具箱
            TOOLS,
            FILE_DOWNLOAD,
            URL_LABEL, URL, FOLDER_LABEL, FOLDER, PICK_FOLDER, NAME_LABEL, NAME, DOWNLOAD_NOTE,
            START_DOWNLOAD, OPEN_FOLDER, DOWNLOAD_STATE, PROGRESS, DOWNLOAD_DETAILS, DOWNLOAD_TARGET,
            PCL_GROUP, PCL_NOTE, UNLOCK_PCL, PCL_RESULT,
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

        constexpr uint32_t PAGE_IDS[] = {GAME, KEYS, PATCH, LAUNCHER, JAVA, TOOLS, ABOUT};

        struct DownloadSourceRow {
            uint32_t label, control;
            DownloadSources::Kind kind;
            const wchar_t* explanation;
        };
        const DownloadSourceRow DOWNLOAD_SOURCES[] = {
                {SOURCE_MINECRAFT_LABEL, SOURCE_MINECRAFT, DownloadSources::Kind::Minecraft,
                 L"版本列表、游戏本体、支持库、模组加载器和 Java 运行时，镜像源是 BMCLAPI。"},
                {SOURCE_ASSETS_LABEL, SOURCE_ASSETS, DownloadSources::Kind::Assets,
                 L"游戏的声音、语言等资源文件，镜像源是 BMCLAPI。"},
                {SOURCE_MODRINTH_LABEL, SOURCE_MODRINTH, DownloadSources::Kind::Modrinth,
                 L"Modrinth 的搜索、版本信息和文件下载，镜像源是 MCIM。"},
                {SOURCE_CURSEFORGE_LABEL, SOURCE_CURSEFORGE, DownloadSources::Kind::CurseForge,
                 L"CurseForge 的搜索、版本信息和文件下载，镜像源是 MCIM。"},
        };
        const blb::net::source_order SOURCE_ORDERS[] = {
                blb::net::source_order::mirror_first, blb::net::source_order::official_first, blb::net::source_order::official_only,
        };
        const std::vector<std::wstring> SOURCE_ORDER_LABELS = {L"镜像源优先", L"官方源优先", L"仅官方源"};

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
                {LAN_OFFLINE_JOIN, "lan.offlineJoin",
                 L"在加入方的游戏中启用，允许离线账号在认证失败后继续尝试连接局域网世界。服务器仍按自身规则验证玩家。"},
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

        struct DownloadInput {
            std::string url, folder, name;
            bool initialized = false;
        } downloadInput;

        std::string amount(double bytes) {
            if (bytes >= 1073741824.0) return blb::sformat("{:.2f} GB", bytes / 1073741824.0);
            if (bytes >= 1048576.0) return blb::sformat("{:.1f} MB", bytes / 1048576.0);
            if (bytes >= 1024.0) return blb::sformat("{:.0f} KB", bytes / 1024.0);
            return blb::sformat("{:.0f} B", bytes);
        }

        // The sections of the default interface in the frame of 下载中心. Every change applies at once, as in the
        // default interface.
        class SettingsWindow : public SectionedForm {
        public:
            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        // 游戏设置
                        {INSTANCES, ControlKind::Group, {}, L"实例"},
                        {ISOLATION_LABEL, ControlKind::Label, {}, L"默认版本隔离"},
                        {ISOLATION, ControlKind::Combo, {}},
                        {COUNT_LABEL, ControlKind::Label, {}, L"实例列表"},
                        {COUNT, ControlKind::Label, {}},
                        {REFRESH, ControlKind::Button, {}, L"重新读取"},
                        {MANAGE, ControlKind::Button, {}, L"实例管理"},
                        {LAUNCH, ControlKind::Group, {}, L"启动"},
                        {LANGUAGE_LABEL, ControlKind::Label, {}, L"游戏语言"},
                        {LANGUAGE, ControlKind::Combo, {}},
                        {GC_LABEL, ControlKind::Label, {}, L"内存管理"},
                        {GC, ControlKind::Combo, {}},
                        {JVM_LABEL, ControlKind::Label, {}, L"JVM 参数"},
                        {JVM, ControlKind::Edit, {}},
                        {GAME_ARGS_LABEL, ControlKind::Label, {}, L"游戏参数"},
                        {GAME_ARGS, ControlKind::Edit, {}},
                        {LOGS, ControlKind::Check, {}, L"显示游戏日志"},
                        {PRE_LAUNCH_LABEL, ControlKind::Label, {}, L"启动前命令"},
                        {PRE_LAUNCH, ControlKind::Edit, {}},
                        {GAME_OPTIONS_GROUP, ControlKind::Group, {}, L"游戏选项"},
                        {GAME_SOURCE_LABEL, ControlKind::Label, {}, L"游戏目录"},
                        {GAME_SOURCE, ControlKind::Combo, {}},
                        {KEYS_SOURCE_LABEL, ControlKind::Label, {}, L"游戏目录"},
                        {KEYS_SOURCE, ControlKind::Combo, {}},
                        // 补丁
                        {ENABLE, ControlKind::Group, {}, L"启用"},
                        {ALL, ControlKind::Check, {}, L"为所有实例启用"},
                        {SKINS, ControlKind::Group, {}, L"皮肤"},
                        {SKIN_LEGACY, ControlKind::Check, {}, L"修复旧版皮肤"},
                        {SKIN_AUTHLIB, ControlKind::Check, {}, L"自定义皮肤"},
                        {SKIN_MODE_LABEL, ControlKind::Label, {}, L"皮肤优先级"},
                        {SKIN_MODE, ControlKind::Combo, {}},
                        {SKIN_SOURCE_LABEL, ControlKind::Label, {}, L"皮肤来源"},
                        {SKIN_SOURCE, ControlKind::Combo, {}},
                        {SKIN_IMAGE_LABEL, ControlKind::Label, {}, L"皮肤图片"},
                        {SKIN_IMAGE, ControlKind::Edit, {}},
                        {SKIN_IMAGE_PICK, ControlKind::Button, {}, L"浏览"},
                        {CAPE_IMAGE_LABEL, ControlKind::Label, {}, L"披风图片"},
                        {CAPE_IMAGE, ControlKind::Edit, {}},
                        {CAPE_IMAGE_PICK, ControlKind::Button, {}, L"浏览"},
                        {PLAYER_LABEL, ControlKind::Label, {}, L"正版玩家名"},
                        {PLAYER, ControlKind::Edit, {}},
                        {SKIN_TRUSTED, ControlKind::Check, {}, L"标记为已签名"},
                        {FIXES, ControlKind::Group, {}, L"修复"},
                        {LOG4J, ControlKind::Check, {}, L"Log4Shell 漏洞修复"},
                        {IME, ControlKind::Check, {}, L"中文输入修复"},
                        {LWJGL, ControlKind::Check, {}, L"LWJGL 内存访问"},
                        {GAME_WINDOW, ControlKind::Group, {}, L"窗口"},
                        {WINDOW_TITLE_LABEL, ControlKind::Label, {}, L"窗口标题"},
                        {WINDOW_TITLE, ControlKind::Edit, {}},
                        {WINDOW_ICON_LABEL, ControlKind::Label, {}, L"窗口图标"},
                        {WINDOW_ICON, ControlKind::Edit, {}},
                        {WINDOW_ICON_PICK, ControlKind::Button, {}, L"浏览"},
                        {LAN_GROUP, ControlKind::Group, {}, L"联机"},
                        {LAN_OFFLINE_JOIN, ControlKind::Check, {}, L"离线加入局域网"},
                        {DEBUGGING, ControlKind::Group, {}, L"调试"},
                        {DEBUG_LOG, ControlKind::Check, {}, L"输出调试日志"},
                        // 启动器设置
                        {LOOK, ControlKind::Group, {}, L"外观"},
                        {STYLE_LABEL, ControlKind::Label, {}, L"界面风格"},
                        {STYLE, ControlKind::Combo, {}},
                        {STYLE_HINT, ControlKind::Label, {}},
                        {RESTART, ControlKind::Button, {}, L"立即重启"},
                        {NETWORK_GROUP, ControlKind::Group, {}, L"网络"},
                        {SYSTEM_PROXY, ControlKind::Check, {}, L"使用系统代理"},
                        {SOURCE_MINECRAFT_LABEL, ControlKind::Label, {}, L"Minecraft"},
                        {SOURCE_MINECRAFT, ControlKind::Combo, {}},
                        {SOURCE_ASSETS_LABEL, ControlKind::Label, {}, L"资源文件"},
                        {SOURCE_ASSETS, ControlKind::Combo, {}},
                        {SOURCE_MODRINTH_LABEL, ControlKind::Label, {}, L"Modrinth"},
                        {SOURCE_MODRINTH, ControlKind::Combo, {}},
                        {SOURCE_CURSEFORGE_LABEL, ControlKind::Label, {}, L"CurseForge"},
                        {SOURCE_CURSEFORGE, ControlKind::Combo, {}},
                        {RESET_GROUP, ControlKind::Group, {}, L"重置"},
                        {RESET_LABEL, ControlKind::Label, {}, L"恢复默认启动器设置"},
                        {RESET_SETTINGS, ControlKind::Button, {}, L"重置并重启"},
                        // Java 管理: the Java found on this computer above, the Java to download below.
                        {INSTALLED, ControlKind::Group, {}, L"本机 Java"},
                        {JAVA_LIST, ControlKind::List, {}},
                        {ADD_JAVA, ControlKind::Button, {}, L"添加"},
                        {REMOVE_JAVA, ControlKind::Button, {}, L"移除"},
                        {JAVA_FOLDER, ControlKind::Button, {}, L"打开目录"},
                        {JAVA_COUNT, ControlKind::Label, {}},
                        {RESCAN, ControlKind::Button, {}, L"重新查找"},
                        {DOWNLOADS, ControlKind::Group, {}, L"下载 Java"},
                        {DOWNLOAD_LIST, ControlKind::List, {}},
                        {MAJOR_LABEL, ControlKind::Label, {}, L"版本"},
                        {MAJOR, ControlKind::Combo, {}},
                        {DOWNLOAD_STATUS, ControlKind::Label, {}},
                        {RELOAD, ControlKind::Button, {}, L"刷新列表"},
                        {INSTALL, ControlKind::Button, {}, L"安装"},
                        // 工具箱
                        {FILE_DOWNLOAD, ControlKind::Group, {}, L"自定义下载"},
                        {URL_LABEL, ControlKind::Label, {}, L"下载地址"},
                        {URL, ControlKind::Edit, {}},
                        {FOLDER_LABEL, ControlKind::Label, {}, L"保存到"},
                        {FOLDER, ControlKind::Edit, {}},
                        {PICK_FOLDER, ControlKind::Button, {}, L"选择文件夹"},
                        {NAME_LABEL, ControlKind::Label, {}, L"文件名"},
                        {NAME, ControlKind::Edit, {}},
                        {DOWNLOAD_NOTE, ControlKind::Label, {}, L"文件名留空时根据地址生成。关闭设置后，下载仍会继续。"},
                        {START_DOWNLOAD, ControlKind::Button, {}, L"开始下载"},
                        {OPEN_FOLDER, ControlKind::Button, {}, L"打开文件夹"},
                        {DOWNLOAD_STATE, ControlKind::Label, {}},
                        {PROGRESS, ControlKind::Progress, {}},
                        {DOWNLOAD_DETAILS, ControlKind::Label, {}},
                        {DOWNLOAD_TARGET, ControlKind::Label, {}},
                        {PCL_GROUP, ControlKind::Group, {}, L"PCL 隐藏主题"},
                        {PCL_NOTE, ControlKind::Label, {}, L"请先运行过 PCL，解锁后重启 PCL 即可生效。"},
                        {UNLOCK_PCL, ControlKind::Button, {}, L"解锁隐藏主题"},
                        {PCL_RESULT, ControlKind::Label, {}},
                        // 关于
                        {.id = PRODUCT,
                         .kind = ControlKind::Label,
                         .bounds = {},
                         .caption = Platform::widen(About::BRAND),
                         .font = L"微软雅黑",
                         .fontSize = 22,
                         .fontStyle = Gdiplus::FontStyleBold},
                        {.id = PRODUCT_VERSION,
                         .kind = ControlKind::Label,
                         .bounds = {},
                         .caption = L"v" + Platform::widen(About::VERSION),
                         .textColor = MUTED},
                        {.id = TAGLINE,
                         .kind = ControlKind::Label,
                         .bounds = {},
                         .caption = L"an open-source minecraft launcher",
                         .textColor = MUTED},
                        {.id = UPDATE_STATUS,
                         .kind = ControlKind::Label,
                         .bounds = {},
                         .textColor = 0xD2C00000},
                        {UPDATE, ControlKind::Button, {}},
                        {PROJECT, ControlKind::Group, {}, L"项目"},
                        {THANKS, ControlKind::Group, {}, L"鸣谢"},
                        {CREDITS, ControlKind::Group, {}, L"开源组件"},
                };
                if (!createFrame(parent, L"设置", L"",
                                 {{L"游戏设置", GAME},
                                  {L"按键绑定", KEYS},
                                  {L"补丁", PATCH},
                                  {L"启动器设置", LAUNCHER},
                                  {L"Java 管理", JAVA},
                                  {L"工具箱", TOOLS},
                                  {L"关于", ABOUT}})) {
                    return false;
                }
                AddControls(CONTROLS);
                gameOptions = &static_cast<GameOptionsPanel&>(AddControl(GAME_OPTIONS_PANEL, std::make_shared<GameOptionsPanel>()));
                for (uint32_t id : PAGE_IDS) {
                    if (id == KEYS) continue;
                    auto& panel = *FindControl(id);
                    pages[id] = &panel.Add<exui::ScrollPanel>(0, 0, panel.GetWidth(), panel.GetHeight());
                }
                auto& keysPage = *FindControl(KEYS);
                MoveInto(KEYS, {KEYS_SOURCE_LABEL, KEYS_SOURCE});
                row(KEYS_SOURCE_LABEL, {cell(KEYS_SOURCE)}, 28, 70).Apply({15, 10, keysPage.GetWidth() - 30, 28});
                keyEditor = &keysPage.Add<KeyBindPanel>();
                keyEditor->SetBounds(15, 46, keysPage.GetWidth() - 30, keysPage.GetHeight() - 58);
                MoveInto(pages.at(GAME)->GetContent(),
                         {INSTANCES, ISOLATION_LABEL, ISOLATION, COUNT_LABEL, COUNT, REFRESH, MANAGE, LAUNCH,
                          LANGUAGE_LABEL, LANGUAGE, GC_LABEL, GC, JVM_LABEL, JVM, GAME_ARGS_LABEL, GAME_ARGS, LOGS,
                          PRE_LAUNCH_LABEL, PRE_LAUNCH});
                MoveInto(pages.at(GAME)->GetContent(), {GAME_OPTIONS_GROUP, GAME_SOURCE_LABEL, GAME_SOURCE, GAME_OPTIONS_PANEL});
                MoveInto(pages.at(PATCH)->GetContent(),
                         {ENABLE, ALL, SKINS, FIXES, LOG4J, IME, LWJGL, GAME_WINDOW, WINDOW_TITLE_LABEL, WINDOW_TITLE,
                          WINDOW_ICON_LABEL, WINDOW_ICON, WINDOW_ICON_PICK, LAN_GROUP, LAN_OFFLINE_JOIN, DEBUGGING, DEBUG_LOG});
                for (uint32_t id : SKIN_CONTROLS) {
                    MoveInto(pages.at(PATCH)->GetContent(), {id});
                }
                MoveInto(pages.at(LAUNCHER)->GetContent(), {LOOK, STYLE_LABEL, STYLE, STYLE_HINT, RESTART,
                                                         NETWORK_GROUP, SYSTEM_PROXY, RESET_GROUP, RESET_LABEL, RESET_SETTINGS});
                for (const auto& source : DOWNLOAD_SOURCES) MoveInto(pages.at(LAUNCHER)->GetContent(), {source.label, source.control});
                MoveInto(pages.at(JAVA)->GetContent(),
                         {INSTALLED, JAVA_LIST, ADD_JAVA, REMOVE_JAVA, JAVA_FOLDER, JAVA_COUNT, RESCAN, DOWNLOADS,
                          DOWNLOAD_LIST, MAJOR_LABEL, MAJOR, DOWNLOAD_STATUS, RELOAD, INSTALL});
                MoveInto(pages.at(TOOLS)->GetContent(),
                         {FILE_DOWNLOAD, URL_LABEL, URL, FOLDER_LABEL, FOLDER, PICK_FOLDER, NAME_LABEL, NAME,
                          DOWNLOAD_NOTE, START_DOWNLOAD, OPEN_FOLDER, DOWNLOAD_STATE, PROGRESS, DOWNLOAD_DETAILS,
                          DOWNLOAD_TARGET, PCL_GROUP, PCL_NOTE, UNLOCK_PCL, PCL_RESULT});
                MoveInto(pages.at(ABOUT)->GetContent(), {PRODUCT, PRODUCT_VERSION, TAGLINE, UPDATE_STATUS, UPDATE, PROJECT, THANKS, CREDITS});
                layoutGame();
                layoutLauncher();
                layoutJava();
                fillGame();
                fillPatch();
                fillLauncher();
                fillJava();
                fillTools();
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
                refreshTools();
                if (FindControl(KEYS)->IsVisible() || FindControl(GAME)->IsVisible()) refreshSources();
                if (PAGE_IDS[lastSection] != KEYS) revealFocus(*pages.at(PAGE_IDS[lastSection]));
            }

            void OnSelection(uint32_t id, int index) override {
                if (id < FIRST_CONTROL) {
                    SectionedForm::OnSelection(id, index);
                    return;
                }
                if (index < 0) {
                    return;
                }
                for (const auto& source : DOWNLOAD_SOURCES) {
                    if (id == source.control) {
                        if (index < (int) std::size(SOURCE_ORDERS)) DownloadSources::setOrder(source.kind, SOURCE_ORDERS[index]);
                        refresh();
                        return;
                    }
                }
                switch (id) {
                    case GAME_SOURCE:
                    case KEYS_SOURCE:
                        if (index < (int) sharedSources.size()) sharedSource = sharedSources[index];
                        break;
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
                    case START_DOWNLOAD:
                    case PICK_FOLDER:
                    case OPEN_FOLDER:
                    case UNLOCK_PCL: toolAction(id); break;
                    case LOGS: LaunchDefaults::setShowLogs(Checked(LOGS)); break;
                    case ALL: Patches::setAll(Checked(ALL)); break;
                    case SYSTEM_PROXY: Network::setSystemProxy(Checked(SYSTEM_PROXY)); break;
                    case RESET_SETTINGS: resetSettings(); return;
                    case SKIN_IMAGE_PICK: pickImage(SKIN_IMAGE, true); break;
                    case CAPE_IMAGE_PICK: pickImage(CAPE_IMAGE, true); break;
                    case WINDOW_ICON_PICK: pickImage(WINDOW_ICON, false); break;
                    case RESTART:
                        if (Services::busy()) break;
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
            void resetSettings() {
                if (Services::busy()) return;
                if (message(GetHwnd(), L"启动器设置将恢复默认，随后重启。\n账户、已添加的游戏目录和下载的文件会保留。",
                            L"重置启动器设置", exui::MsgBoxButtons::OkCancel, {L"取消", L"重置并重启"}) != 0) return;
                if (!GetHwnd()) return;
                if (Services::busy()) {
                    message(GetHwnd(), L"当前有任务进行中，未重置设置。请等待任务结束后重试。", L"重置启动器设置");
                    return;
                }
                if (!Config::reset()) {
                    message(GetHwnd(), L"无法保存重置后的设置，请稍后重试。", L"重置启动器设置");
                    return;
                }
                if (!Platform::restart()) {
                    message(GetHwnd(), L"重置已保存，但重启失败。请关闭启动器后重新打开。", L"重置启动器设置");
                }
                CloseWindow();
            }

            KeyBindPanel* keyEditor = nullptr;
            GameOptionsPanel* gameOptions = nullptr;
            int gameOptionsHeight = -1;
            std::vector<std::string> sharedSources;
            std::string sharedSource;

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (keyEditor && keyEditor->HandleInput(msg, wp, lp)) return 0;
                return SectionedForm::HandleMessage(msg, wp, lp);
            }

            void refreshSources() {
                std::vector<std::wstring> labels;
                sharedSources.clear();
                for (const auto& source : Instances::sources()) {
                    sharedSources.push_back(source.key); labels.push_back(Platform::widen(source.label));
                }
                if (std::find(sharedSources.begin(), sharedSources.end(), sharedSource) == sharedSources.end()) {
                    const auto* entry = Instances::find(Instances::selected());
                    const auto* source = Instances::installSource(entry ? entry->source : "");
                    sharedSource = source ? source->key : "";
                }
                auto at = std::find(sharedSources.begin(), sharedSources.end(), sharedSource);
                UpdateOptions(KEYS_SOURCE, labels, at == sharedSources.end() ? -1 : (int) (at - sharedSources.begin()));
                UpdateOptions(GAME_SOURCE, labels, at == sharedSources.end() ? -1 : (int) (at - sharedSources.begin()));
                auto file = GameOptions::sharedFile(sharedSource);
                keyEditor->SetFile(file);
                keyEditor->Refresh();
                gameOptions->SetVisible(!file.empty());
                gameOptions->SetFile(std::move(file));
                gameOptions->Refresh();
                int height = gameOptions->IsVisible() ? gameOptions->PreferredHeight() : 0;
                if (height != gameOptionsHeight) { gameOptionsHeight = height; layoutGame(); }
            }

            // How 皮肤 is laid out now, so that its rows only move when it changes.
            int skinShape = -1;
            int toolsShape = -1;
            bool cancelling = false;
            std::optional<CustomDownload::State> paintedDownloadState;
            std::map<uint32_t, exui::ScrollPanel*> pages;
            Rows shownJava, shownDownloads;
            // The executable of each Java shown and the folder of each download; empty for a row saying there is none.
            std::vector<std::string> javaKeys, downloadKeys;
            // The Java versions offered to filter the downloads by, 0 for all of them.
            std::vector<int> shownMajors;

            exui::Layout field(uint32_t id, int width, int height = 26) {
                return cell(id, width, height, exui::Layout::Align::Start);
            }

            exui::Layout setting(uint32_t label, std::vector<exui::Layout> fields, int labelWidth = 95) {
                return row(label, std::move(fields), 30, labelWidth, exui::Layout::Align::Center);
            }

            exui::Layout toggle(uint32_t id, int width = 270) {
                return exui::Layout::Row({field(id, width)}, 0, 30);
            }

            exui::Layout formGroup(uint32_t id, std::vector<exui::Layout> rows) {
                return group(id, std::move(rows), {20, 25, 20, 15}, 5);
            }

            void setPageLayout(uint32_t id, exui::Layout layout) {
                // The scroll panel reserves 12 pixels for its bar within the frame's right gutter.
                pages.at(id)->SetContentLayout(std::move(layout), {15, 10, 3, 10});
            }

            void layoutGame() {
                using exui::Layout;
                std::vector<Layout> groups{
                        formGroup(INSTANCES, {
                                setting(ISOLATION_LABEL, {field(ISOLATION, 200, 28)}),
                                setting(COUNT_LABEL, {cell(COUNT, 110, 26),
                                        Layout::Row({cell(REFRESH, 80), cell(MANAGE, 90)}, 10, 180)}),
                        }),
                        formGroup(LAUNCH, {
                                setting(LANGUAGE_LABEL, {field(LANGUAGE, 200, 28)}),
                                setting(GC_LABEL, {field(GC, 200, 28)}),
                                setting(JVM_LABEL, {field(JVM, 505)}),
                                setting(GAME_ARGS_LABEL, {field(GAME_ARGS, 505)}),
                                toggle(LOGS, 200),
                                setting(PRE_LAUNCH_LABEL, {field(PRE_LAUNCH, 505)}),
                        }),
                };
                bool options = gameOptionsHeight > 0;
                for (uint32_t id : {GAME_OPTIONS_GROUP, GAME_SOURCE_LABEL, GAME_SOURCE}) FindControl(id)->SetVisible(options);
                if (options) groups.push_back(formGroup(GAME_OPTIONS_GROUP, {
                        setting(GAME_SOURCE_LABEL, {field(GAME_SOURCE, 300, 28)}), cell(GAME_OPTIONS_PANEL, gameOptionsHeight),
                }));
                setPageLayout(GAME, Layout::Column(std::move(groups), 15));
            }

            void layoutLauncher() {
                using exui::Layout;
                std::vector<Layout> network{toggle(SYSTEM_PROXY)};
                for (const auto& source : DOWNLOAD_SOURCES) network.push_back(setting(source.label, {field(source.control, 200, 28)}));
                setPageLayout(LAUNCHER, Layout::Column({group(LOOK, {
                        row(STYLE_LABEL, {
                                Layout::Column({Layout::Inset(Layout::Row({field(STYLE, 200, 28)}, 0, 28),
                                                                    {0, 2, 0, 4})}, 0, 205),
                                Layout::Row({cell(STYLE_HINT, 195, 26)}, 0, 200),
                                field(RESTART, 90, 30),
                        }, 34, 95, Layout::Align::Center),
                }, {20, 23, 20, 18}),
                        formGroup(NETWORK_GROUP, std::move(network)),
                        formGroup(RESET_GROUP, {Layout::Row({cell(RESET_LABEL), cell(RESET_SETTINGS, 110)}, 10, 30)}),
                }, 15));
            }

            void fillTools() {
                for (uint32_t id : {DOWNLOAD_NOTE, DOWNLOAD_STATE, DOWNLOAD_DETAILS, DOWNLOAD_TARGET, PCL_NOTE, PCL_RESULT})
                    Control<exui::Label>(id).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::Label>(PCL_RESULT).SetMultiline(true);
                Control<exui::Label>(DOWNLOAD_DETAILS).SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
                FindControl(URL)->SetTooltipText(L"HTTP 或 HTTPS 文件地址");
                FindControl(NAME)->SetTooltipText(L"留空时根据下载地址生成文件名");
                Control<exui::ProgressBar>(PROGRESS).SetRange(0, 1000);
                if (!downloadInput.initialized) {
                    downloadInput.initialized = true;
                    downloadInput.folder = blb::path_to_utf8(Platform::exeDirectory());
                    if (CustomDownload::busy()) {
                        const auto& task = CustomDownload::status();
                        downloadInput.url = task.url;
                        downloadInput.folder = blb::path_to_utf8(task.target.parent_path());
                        downloadInput.name = blb::path_to_utf8(task.target.filename());
                    }
                }
                bindDownload(URL, downloadInput.url);
                bindDownload(FOLDER, downloadInput.folder);
                bindDownload(NAME, downloadInput.name);
            }

            void layoutTools(bool progress, bool details, bool target, bool pclResult) {
                int shape = (int) progress | (details << 1) | (target << 2) | (pclResult << 3);
                if (shape == toolsShape) return;
                toolsShape = shape;
                using exui::Layout;
                FindControl(DOWNLOAD_STATE)->SetVisible(progress);
                FindControl(PROGRESS)->SetVisible(progress);
                FindControl(DOWNLOAD_DETAILS)->SetVisible(progress && details);
                FindControl(DOWNLOAD_TARGET)->SetVisible(target);
                FindControl(PCL_RESULT)->SetVisible(pclResult);
                Layout indicator = Layout::Column({}, 0);
                if (progress) {
                    std::vector<Layout> status{cell(DOWNLOAD_STATE, details ? 85 : 0)};
                    if (details) status.push_back(cell(DOWNLOAD_DETAILS));
                    indicator = Layout::Column({Layout::Row(std::move(status), 8, 18), cell(PROGRESS, 8)}, 4);
                }
                std::vector<Layout> download{
                        setting(URL_LABEL, {cell(URL)}),
                        setting(FOLDER_LABEL, {cell(FOLDER), cell(PICK_FOLDER, 88)}),
                        setting(NAME_LABEL, {cell(NAME)}),
                        cell(DOWNLOAD_NOTE, 24),
                        Layout::Row({std::move(indicator), cell(START_DOWNLOAD, 100), cell(OPEN_FOLDER, 100)}, 10, 30),
                };
                if (target) download.push_back(cell(DOWNLOAD_TARGET, 22));
                std::vector<Layout> pcl{Layout::Row({cell(PCL_NOTE), cell(UNLOCK_PCL, 120)}, 10, 30)};
                if (pclResult) pcl.push_back(cell(PCL_RESULT, 32));
                setPageLayout(TOOLS, Layout::Column({formGroup(FILE_DOWNLOAD, std::move(download)),
                                                   formGroup(PCL_GROUP, std::move(pcl))}, 15));
            }

            void refreshTools() {
                const auto& task = CustomDownload::status();
                bool running = CustomDownload::busy();
                if (!running) cancelling = false;
                for (uint32_t id : {URL, FOLDER, NAME, PICK_FOLDER}) FindControl(id)->SetEnabled(!running);
                UpdateText(START_DOWNLOAD, !running ? L"开始下载" : cancelling ? L"正在取消…" : L"取消下载");
                FindControl(START_DOWNLOAD)->SetEnabled(running ? !cancelling : !trimmed(Text(URL)).empty());
                FindControl(OPEN_FOLDER)->SetEnabled(!trimmed(Text(FOLDER)).empty());
                std::wstring state, details;
                switch (task.state) {
                    case CustomDownload::State::Idle: state = task.url.empty() ? L"填写地址后开始下载" : L"下载已取消，可以重新开始"; break;
                    case CustomDownload::State::Probing: state = cancelling ? L"正在取消下载…" : L"正在连接…"; break;
                    case CustomDownload::State::Running: state = cancelling ? L"正在取消下载…" : L"下载中"; break;
                    case CustomDownload::State::Done: state = L"下载完成"; details = Platform::widen(amount((double) std::max(task.done, task.total))); break;
                    case CustomDownload::State::Failed: state = L"下载失败：" + Platform::widen(task.error); break;
                }
                if (task.state == CustomDownload::State::Running) {
                    std::string text = amount((double) task.done) + (task.total > 0 ? " / " + amount((double) task.total) : "（总大小未知）");
                    if (std::isfinite(task.speed) && task.speed > 0) {
                        text += " · " + amount(task.speed) + "/s";
                        if (task.total > task.done && task.speed > 1) {
                            double seconds = (double) (task.total - task.done) / task.speed;
                            if (seconds < 60) text += blb::sformat(" · 剩余约 {:.0f} 秒", std::ceil(seconds));
                            else if (seconds < 3600) text += blb::sformat(" · 剩余约 {:.0f} 分钟", std::ceil(seconds / 60));
                            else text += blb::sformat(" · 剩余约 {:.0f} 小时", std::ceil(seconds / 3600));
                        }
                    }
                    details = Platform::widen(text);
                }
                UpdateText(DOWNLOAD_STATE, state);
                FindControl(DOWNLOAD_STATE)->SetTooltipText(state);
                UpdateText(DOWNLOAD_DETAILS, details);
                FindControl(DOWNLOAD_DETAILS)->SetTooltipText(details);
                std::wstring target = task.target.empty() ? L"" : L"保存到：" + task.target.wstring();
                UpdateText(DOWNLOAD_TARGET, target);
                FindControl(DOWNLOAD_TARGET)->SetTooltipText(target);
                auto& bar = Control<exui::ProgressBar>(PROGRESS);
                bar.SetIndeterminate(running && (task.state == CustomDownload::State::Probing || task.total == 0));
                int position = task.state == CustomDownload::State::Done ? 1000 : task.total > 0 &&
                        task.state != CustomDownload::State::Idle ? (int) (1000 * std::clamp((double) task.done / task.total, 0.0, 1.0)) : 0;
                if (bar.GetPosition() != position) bar.SetPosition(position);
                if (paintedDownloadState != task.state) {
                    bool failed = task.state == CustomDownload::State::Failed;
                    FindControl(DOWNLOAD_STATE)->SetTextColor(failed ? 0xD2C00000 : 0xD2000000);
                    bar.SetFillColors(failed ? 0xE6C04030 : 0xE60088FF, failed ? 0xE6D07040 : 0xE600E080);
                    paintedDownloadState = task.state;
                }
                const auto& pcl = PclThemes::status();
                std::wstring result = pcl.state == PclThemes::State::Done ? L"已解锁隐藏主题，重启 PCL 后生效。"
                        : pcl.state == PclThemes::State::Failed ? L"解锁失败：" + Platform::widen(pcl.error)
                                                               : L"";
                UpdateText(PCL_RESULT, result);
                FindControl(PCL_RESULT)->SetTooltipText(result);
                bool progress = task.state != CustomDownload::State::Idle;
                layoutTools(progress, !details.empty(), progress && !target.empty(), !result.empty());
            }

            void toolAction(uint32_t id) {
                if (id == START_DOWNLOAD) {
                    if (CustomDownload::busy()) { CustomDownload::cancel(); cancelling = true; }
                    else beginDownload();
                } else if (id == PICK_FOLDER && !CustomDownload::busy()) {
                    std::string folder = trimmed(Text(FOLDER));
                    auto picked = Platform::pickFolder(folder.empty() ? Platform::exeDirectory() : blb::path_from_utf8(folder));
                    if (picked && !CustomDownload::busy()) SetText(FOLDER, picked->wstring());
                } else if (id == OPEN_FOLDER) {
                    std::error_code error;
                    auto folder = blb::path_from_utf8(trimmed(Text(FOLDER)));
                    if (!std::filesystem::is_directory(folder, error) || !Platform::open(folder))
                        message(GetHwnd(), L"无法打开保存文件夹，请确认路径存在。", L"自定义下载");
                } else if (id == UNLOCK_PCL) {
                    PclThemes::unlock();
                    const auto& result = PclThemes::status();
                    if (result.state == PclThemes::State::Done)
                        Notices::add(Notices::Kind::Done, "PCL 主题已解锁，重启 PCL 后生效", 4000);
                    else if (result.state == PclThemes::State::Failed)
                        Notices::add(Notices::Kind::Failed, "解锁失败：" + result.error, 5000);
                }
            }

            void bindDownload(uint32_t id, std::string& value) {
                SetText(id, Platform::widen(value));
                Control<exui::EditBox>(id).OnTextChanged = [this, &value](const std::wstring& text) {
                    value = Platform::narrow(text);
                    refreshTools();
                };
            }

            void beginDownload() {
                std::string url = trimmed(Text(URL)), folder = trimmed(Text(FOLDER)), name = trimmed(Text(NAME));
                if (!blb::iequals(std::string_view(url).substr(0, 7), "http://") &&
                    !blb::iequals(std::string_view(url).substr(0, 8), "https://")) {
                    message(GetHwnd(), L"下载地址需要以 http:// 或 https:// 开头。", L"自定义下载");
                    return;
                }
                if (folder.empty()) { message(GetHwnd(), L"请选择保存文件夹。", L"自定义下载"); return; }
                if (name.empty()) name = CustomDownload::nameOf(url);
                if (!blb::fs::safe_filename(name)) { message(GetHwnd(), L"文件名无效，含有不能使用的字符。", L"自定义下载"); return; }
                std::error_code error;
                auto target = std::filesystem::absolute(blb::path_from_utf8(folder) / blb::path_from_utf8(name), error);
                bool exists = !error && std::filesystem::exists(target, error);
                if (error) { message(GetHwnd(), L"无法检查目标文件：" + Platform::widen(error.message()), L"自定义下载"); return; }
                if (exists) {
                    bool directory = std::filesystem::is_directory(target, error);
                    if (directory || error) { message(GetHwnd(), L"目标路径无法写入文件，请更换文件名或目录。", L"自定义下载"); return; }
                    if (message(GetHwnd(), L"文件已存在：\n" + target.wstring() + L"\n是否覆盖？原文件将被替换。",
                                L"覆盖文件", exui::MsgBoxButtons::OkCancel, {L"取消", L"覆盖"}) != 0) return;
                }
                if (!CustomDownload::busy()) CustomDownload::start(url, target);
            }

            void layoutJava() {
                using exui::Layout;
                setPageLayout(JAVA, Layout::Column({
                        group(INSTALLED, {
                                cell(JAVA_LIST, 150),
                                Layout::Row({
                                        Layout::Row({cell(ADD_JAVA, 80), cell(REMOVE_JAVA, 80), cell(JAVA_FOLDER, 80)}, 5, 255),
                                        cell(JAVA_COUNT, 260, 26), cell(RESCAN, 90),
                                }, 5, 28),
                        }, {15, 23, 15, 8}, 6),
                        group(DOWNLOADS, {
                                cell(DOWNLOAD_LIST, 150),
                                Layout::Row({
                                        Layout::Row({cell(MAJOR_LABEL, 40, 26), cell(MAJOR, 125)}, 0, 165),
                                        cell(DOWNLOAD_STATUS, 245, 26),
                                        Layout::Row({cell(RELOAD, 90), cell(INSTALL, 90)}, 5, 185),
                                }, 10, 28),
                        }, {15, 23, 15, 8}, 6),
                }, 10));
            }

            // A text setting, applied as it is typed, with the explanation of the default interface on its label.
            void text(uint32_t label, uint32_t id, const std::string& value,
                      std::function<void(const std::string&)> apply, const wchar_t* explanation) {
                auto& edit = Control<exui::EditBox>(id);
                edit.SetText(Platform::widen(value));
                edit.OnTextChanged = [apply](const std::wstring& typed) { apply(trimmed(typed)); };
                FindControl(label)->SetTooltipText(explanation);
                edit.SetTooltipText(explanation);
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
                UpdateText(COUNT,
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

            // These rows also decide which source-specific controls are visible.
            void layoutSkins() {
                using exui::Layout;
                bool custom = Patches::flag("skin.authlib");
                std::string source = Patches::value("skin.source");
                int shape = !custom ? 0 : source == "custom" ? 1 : source == "littleskin" ? 2 : 3;
                if (shape == skinShape) return;
                skinShape = shape;
                for (uint32_t id : SKIN_CONTROLS) FindControl(id)->SetVisible(false);
                std::vector<Layout> rows{toggle(SKIN_LEGACY), toggle(SKIN_AUTHLIB)};
                if (custom) {
                    rows.push_back(setting(SKIN_MODE_LABEL, {field(SKIN_MODE, 195, 28)}, 75));
                    rows.push_back(setting(SKIN_SOURCE_LABEL, {field(SKIN_SOURCE, 195, 28)}, 75));
                    if (shape == 1) {
                        rows.push_back(setting(SKIN_IMAGE_LABEL, {field(SKIN_IMAGE, 140), field(SKIN_IMAGE_PICK, 50)}, 75));
                        rows.push_back(setting(CAPE_IMAGE_LABEL, {field(CAPE_IMAGE, 140), field(CAPE_IMAGE_PICK, 50)}, 75));
                    } else if (shape == 3) {
                        rows.push_back(setting(PLAYER_LABEL, {field(PLAYER, 195)}, 75));
                    }
                    rows.push_back(toggle(SKIN_TRUSTED));
                }
                for (const Layout& row : rows) row.SetVisible(true);
                setPageLayout(PATCH, Layout::Row({
                        Layout::Column({formGroup(ENABLE, {toggle(ALL)}), formGroup(SKINS, std::move(rows))}, 15),
                        Layout::Column({
                                formGroup(FIXES, {toggle(LOG4J), toggle(IME), toggle(LWJGL)}),
                                formGroup(GAME_WINDOW, {
                                        setting(WINDOW_TITLE_LABEL, {field(WINDOW_TITLE, 195)}, 75),
                                        setting(WINDOW_ICON_LABEL, {field(WINDOW_ICON, 140), field(WINDOW_ICON_PICK, 50)}, 75),
                                }),
                                formGroup(LAN_GROUP, {toggle(LAN_OFFLINE_JOIN)}),
                                formGroup(DEBUGGING, {toggle(DEBUG_LOG)}),
                        }, 15),
                }, 15));
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
                SetChecked(SYSTEM_PROXY, Network::systemProxy());
                FindControl(SYSTEM_PROXY)->SetTooltipText(L"下载和其他网络请求会使用 Windows 系统代理。代理软件未运行时请关闭。");
                FindControl(RESET_SETTINGS)->SetTooltipText(L"恢复默认启动器设置并重启；保留账户、已添加的游戏目录和下载的文件。");
                FindControl(RESET_SETTINGS)->SetTextColor(0xD2C00000);
                for (const auto& source : DOWNLOAD_SOURCES) {
                    FindControl(source.label)->SetTooltipText(source.explanation);
                    FindControl(source.control)->SetTooltipText(source.explanation);
                }
            }

            void refreshLauncher() {
                // A new interface style waits for a restart, which a game, an install or a download would block.
                bool pending = &UiStyles::current() != &UiStyles::running();
                bool busy = Services::busy();
                UpdateText(STYLE_HINT, !pending ? L"" : busy ? L"任务进行中，暂不能重启" : L"重启后生效");
                FindControl(RESTART)->SetVisible(pending);
                FindControl(RESTART)->SetEnabled(!busy);
                FindControl(RESET_SETTINGS)->SetEnabled(!busy);
                SetChecked(SYSTEM_PROXY, Network::systemProxy());
                for (const auto& source : DOWNLOAD_SOURCES) {
                    auto order = DownloadSources::order(source.kind);
                    auto at = std::find(std::begin(SOURCE_ORDERS), std::end(SOURCE_ORDERS), order);
                    UpdateOptions(source.control, SOURCE_ORDER_LABELS, at == std::end(SOURCE_ORDERS) ? 0 : (int) (at - std::begin(SOURCE_ORDERS)));
                }
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
                UpdateText(JAVA_COUNT,
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

            exui::Layout addLink(uint32_t& id, const About::Link& link, int column, bool valueOpens) {
                using exui::Layout;
                uint32_t name = id++, value = id++;
                AddControl({.id = name,
                            .kind = valueOpens ? ControlKind::Label : ControlKind::Hyperlink,
                            .bounds = {},
                            .caption = Platform::widen(link.name)});
                AddControl({.id = value,
                            .kind = valueOpens ? ControlKind::Hyperlink : ControlKind::Label,
                            .bounds = {},
                            .caption = Platform::widen(link.value),
                            .textColor = valueOpens ? 0xD2000000 : MUTED});
                auto& hyperlink = Control<exui::Hyperlink>(valueOpens ? value : name);
                hyperlink.SetTargetUrl(Platform::widen(link.url));
                hyperlink.SetTooltipText(Platform::widen(link.url));
                int linkWidth = (int) std::ceil(exui::widget_detail::UnboundedTextWidth(hyperlink)) + 2;
                MoveInto(pages.at(ABOUT)->GetContent(), {name, value});
                Layout label = valueOpens ? cell(name, column - 5)
                                           : Layout::Row({cell(name, linkWidth)}, 0, column - 5);
                return Layout::Row({std::move(label), cell(value, valueOpens ? linkWidth : 295 - column)}, 5, 22);
            }

            void fillAbout() {
                using exui::Layout;
                Control<exui::Label>(UPDATE_STATUS)
                        .SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
                uint32_t id = LINKS;
                std::vector<Layout> project, thanks, left, right;
                for (const About::Link& link : About::PROJECT) project.push_back(addLink(id, link, 75, true));
                for (const About::Link& link : About::THANKS) thanks.push_back(addLink(id, link, 95, false));
                size_t split = (std::size(About::CREDITS) + 1) / 2;
                for (size_t i = 0; i < std::size(About::CREDITS); ++i) {
                    (i < split ? left : right).push_back(addLink(id, About::CREDITS[i], 160, false));
                }
                int productWidth = (int) std::ceil(exui::widget_detail::UnboundedTextWidth(*FindControl(PRODUCT)));
                auto heading = Layout::Inset(Layout::Row({
                        Layout::Column({
                                Layout::Row({field(PRODUCT, productWidth, 36),
                                             cell(PRODUCT_VERSION, 120, 22, Layout::Align::End)}, 10, 38),
                                Layout::Inset(Layout::Row({field(TAGLINE, 300, 20)}, 0, 20), {2, 0, 0, 0}),
                        }, 0, 365),
                        Layout::Inset(Layout::Row({cell(UPDATE_STATUS, 150, 26), cell(UPDATE, 90)}, 10, 30),
                                      {0, 7, 0, 21}),
                }, 0), {15, 3, 15, 4});
                setPageLayout(ABOUT, Layout::Column({
                        std::move(heading),
                        Layout::Column({
                                Layout::Row({
                                        Layout::Column({group(PROJECT, std::move(project), {20, 25, 0, 18}, 8)}),
                                        Layout::Column({group(THANKS, std::move(thanks), {20, 25, 0, 18}, 8)}),
                                }, 15, Layout::Auto),
                                Layout::Frame(*FindControl(CREDITS), Layout::Row({
                                        Layout::Column(std::move(left), 8), Layout::Column(std::move(right), 8),
                                }, 35), {20, 27, 0, 12}),
                        }, 15, Layout::Auto),
                }, 0));
            }

            void refreshAbout() {
                Updates::State state = Updates::state();
                UpdateText(UPDATE,
                         state == Updates::State::Checking            ? std::wstring(L"正在检查…")
                                 : state == Updates::State::Latest    ? std::wstring(L"已是最新版本")
                                 : state == Updates::State::Available ? L"下载 v" + Platform::widen(Updates::latest())
                                                                      : std::wstring(L"检查更新"));
                FindControl(UPDATE)->SetEnabled(state != Updates::State::Checking && state != Updates::State::Latest);
                UpdateText(UPDATE_STATUS, state == Updates::State::Failed ? L"检查失败" : L"");
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
