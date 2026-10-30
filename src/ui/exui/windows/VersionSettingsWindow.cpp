#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/VersionSettingsWindow.h"
#include "app/instances/InstanceSettings.h"
#include "app/instances/Instances.h"
#include "app/launch/LaunchDefaults.h"
#include "app/launch/Patches.h"
#include "app/system/Catalog.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/ScrollPanel.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/ModsWindow.h"
#include "ui/exui/windows/ShortcutWindow.h"

#include <core/auth/yggdrasil.hpp>

#include <functional>
#include <iterator>
#include <optional>

namespace ForgottenMelody {

    namespace {
        namespace blb = b_launcher_base;
        using exui::ControlKind;
        using Settings = blb::instance::instance_settings;

        enum : uint32_t {
            PAGE = SectionedForm::FIRST_CONTROL,
            SCROLL,
            // 管理
            MANAGE_GROUP,
            MODS,
            LOADERS,
            EXPORT,
            SHORTCUT,
            // 游戏
            GAME_GROUP,
            DIR_LABEL,
            DIR,
            JAVA_LABEL,
            JAVA,
            VISIBILITY_LABEL,
            VISIBILITY,
            PRIORITY_LABEL,
            PRIORITY,
            LOGS_LABEL,
            LOGS,
            PATCH_LABEL,
            PATCH,
            // 内存
            MEMORY_GROUP,
            AUTO_MEMORY,
            MAX_LABEL,
            MAX,
            MAX_UNIT,
            MAX_HINT,
            // 窗口
            WINDOW_GROUP,
            SIZE_LABEL,
            WIDTH,
            TIMES,
            HEIGHT,
            SIZE_HINT,
            FULLSCREEN,
            // 服务器
            SERVER_GROUP,
            LOGIN_LABEL,
            LOGIN,
            AUTH_LABEL,
            AUTH,
            AUTH_PRESET,
            JOIN_LABEL,
            JOIN,
            // 其他
            OTHER_GROUP,
            NOTES_LABEL,
            NOTES,
            // 高级设置
            COMMANDS_GROUP,
            GAME_ARGS_LABEL,
            GAME_ARGS,
            PRE_LAUNCH_LABEL,
            PRE_LAUNCH,
            WRAPPER_LABEL,
            WRAPPER,
            POST_EXIT_LABEL,
            POST_EXIT,
            JVM_GROUP,
            JVM_LABEL,
            JVM,
            MIN_LABEL,
            MIN,
            MIN_UNIT,
            ENVIRONMENT_LABEL,
            ENVIRONMENT,
            COMPATIBILITY_GROUP,
            NO_DEFAULT_JVM_ARGS,
            NO_INTEGRITY_CHECK,
        };

        struct Option {
            const wchar_t* label;
            const char* key;
        };

        const Option VISIBILITIES[] = {
                {L"保持不变", ""},
                {L"最小化", "minimize"},
                {L"隐藏，游戏退出后恢复", "hide"},
                {L"关闭启动器", "close"},
        };

        const Option PRIORITIES[] = {
                {L"低", "low"},  {L"低于正常", "below-normal"}, {L"正常", ""}, {L"高于正常", "above-normal"},
                {L"高", "high"},
        };

        const Option LOGINS[] = {
                {L"任意账户", ""},
                {L"仅微软账户", "microsoft"},
                {L"仅离线账户", "offline"},
                {L"第三方登录：Authlib Injector", "yggdrasil"},
                {L"第三方登录：统一通行证", "nide8"},
        };

        const wchar_t* const COMMAND_VARIABLES =
                L"可用的环境变量：%INST_NAME%（实例名）、%INST_DIR%（版本文件夹）、%INST_MC_DIR%（游戏目录）、"
                L"%INST_JAVA%（Java 路径），装有对应加载器时 %INST_FORGE%、%INST_NEOFORGE%、%INST_FABRIC%、"
                L"%INST_QUILT%、%INST_OPTIFINE% 等为 1。";

        int optionOf(const Option* options, size_t count, const std::string& key) {
            for (size_t i = 0; i < count; i++) {
                if (key == options[i].key) {
                    return (int) i;
                }
            }
            return 0;
        }

        // A setting left unset follows the default, the first choice; then on or off.
        int tristate(const std::optional<bool>& value) {
            return !value ? 0 : *value ? 1 : 2;
        }

        std::optional<bool> fromTristate(int index) {
            return index == 0 ? std::nullopt : std::optional<bool>(index == 1);
        }

        // The frame of the settings without the list of sections: one page, as the default interface has it.
        const SectionedForm::Frame FRAME = {.width = 620,
                                            .height = 480,
                                            .top = 60,
                                            .bottom = 425,
                                            .listWidth = 0,
                                            .doneWidth = 80,
                                            .doneHeight = 32};
        // The page's height, and the rows from the one after the server on, which move up a row while there is none.
        constexpr int PAGE_HEIGHT = 1208, ROW = 33, BELOW_SERVER = 633;

        // The groups of the default interface down the page, the advanced ones after the others; the bounds count
        // from the top left of the scrolled page. The groups keep 12 pixels from both sides of the backing, the
        // scroll bar running in the right margin, and their fields 20 from both sides of the groups.
        const exui::ControlSpec CONTROLS[] = {
                // 管理
                {MANAGE_GROUP, ControlKind::Group, {12, 10, 566, 72}, L"管理"},
                {MODS, ControlKind::Button, {32, 36, 100, 30}, L"模组管理"},
                {LOADERS, ControlKind::Button, {140, 36, 100, 30}, L"修改加载器"},
                {EXPORT, ControlKind::Button, {248, 36, 100, 30}, L"导出整合包"},
                {SHORTCUT, ControlKind::Button, {356, 36, 100, 30}, L"生成快捷方式"},
                // 游戏
                {GAME_GROUP, ControlKind::Group, {12, 92, 566, 228}, L"游戏"},
                {DIR_LABEL, ControlKind::Label, {32, 117, 95, 26}, L"游戏目录"},
                {DIR, ControlKind::Combo, {132, 117, 250, 26}},
                {JAVA_LABEL, ControlKind::Label, {32, 150, 95, 26}, L"Java"},
                {JAVA, ControlKind::Combo, {132, 150, 350, 26}},
                {VISIBILITY_LABEL, ControlKind::Label, {32, 183, 95, 26}, L"启动器可见性"},
                {VISIBILITY, ControlKind::Combo, {132, 183, 250, 26}},
                {PRIORITY_LABEL, ControlKind::Label, {32, 216, 95, 26}, L"进程优先级"},
                {PRIORITY, ControlKind::Combo, {132, 216, 250, 26}},
                {LOGS_LABEL, ControlKind::Label, {32, 249, 95, 26}, L"显示游戏日志"},
                {LOGS, ControlKind::Combo, {132, 249, 250, 26}},
                {PATCH_LABEL, ControlKind::Label, {32, 282, 95, 26}, L"启动器补丁"},
                {PATCH, ControlKind::Combo, {132, 282, 250, 26}},
                // 内存
                {MEMORY_GROUP, ControlKind::Group, {12, 330, 566, 96}, L"内存"},
                {AUTO_MEMORY, ControlKind::Check, {32, 355, 200, 26}, L"自动分配"},
                {MAX_LABEL, ControlKind::Label, {32, 388, 95, 26}, L"最大内存"},
                {MAX, ControlKind::Edit, {132, 388, 80, 26}},
                {MAX_UNIT, ControlKind::Label, {217, 388, 30, 26}, L"MB"},
                {MAX_HINT, ControlKind::Label, {256, 388, 290, 26}},
                // 窗口
                {WINDOW_GROUP, ControlKind::Group, {12, 436, 566, 96}, L"窗口"},
                {SIZE_LABEL, ControlKind::Label, {32, 461, 95, 26}, L"窗口大小"},
                {WIDTH, ControlKind::Edit, {132, 461, 70, 26}},
                {TIMES, ControlKind::Label, {207, 461, 15, 26}, L"×"},
                {HEIGHT, ControlKind::Edit, {227, 461, 70, 26}},
                {SIZE_HINT, ControlKind::Label, {307, 461, 240, 26}, L"留空则使用游戏默认的大小"},
                {FULLSCREEN, ControlKind::Check, {32, 494, 200, 26}, L"全屏启动"},
                // 服务器
                {SERVER_GROUP, ControlKind::Group, {12, 542, 566, 129}, L"服务器"},
                {LOGIN_LABEL, ControlKind::Label, {32, 567, 95, 26}, L"登录方式"},
                {LOGIN, ControlKind::Combo, {132, 567, 250, 26}},
                {AUTH_LABEL, ControlKind::Label, {32, 600, 95, 26}},
                {AUTH, ControlKind::Edit, {132, 600, 260, 26}},
                {AUTH_PRESET, ControlKind::Button, {397, 598, 134, 30}},
                {JOIN_LABEL, ControlKind::Label, {32, 633, 95, 26}, L"自动加入服务器"},
                {JOIN, ControlKind::Edit, {132, 633, 300, 26}},
                // 其他
                {OTHER_GROUP, ControlKind::Group, {12, 681, 566, 105}, L"其他"},
                {NOTES_LABEL, ControlKind::Label, {32, 706, 95, 26}, L"备注"},
                {NOTES, ControlKind::Edit, {132, 706, 426, 70}},
                // 高级设置
                {COMMANDS_GROUP, ControlKind::Group, {12, 796, 566, 160}, L"自定义命令"},
                {GAME_ARGS_LABEL, ControlKind::Label, {32, 821, 95, 26}, L"游戏参数"},
                {GAME_ARGS, ControlKind::Edit, {132, 821, 426, 26}},
                {PRE_LAUNCH_LABEL, ControlKind::Label, {32, 854, 95, 26}, L"启动前命令"},
                {PRE_LAUNCH, ControlKind::Edit, {132, 854, 426, 26}},
                {WRAPPER_LABEL, ControlKind::Label, {32, 887, 95, 26}, L"包装命令"},
                {WRAPPER, ControlKind::Edit, {132, 887, 426, 26}},
                {POST_EXIT_LABEL, ControlKind::Label, {32, 920, 95, 26}, L"退出后命令"},
                {POST_EXIT, ControlKind::Edit, {132, 920, 426, 26}},
                {JVM_GROUP, ControlKind::Group, {12, 966, 566, 127}, L"Java 虚拟机"},
                {JVM_LABEL, ControlKind::Label, {32, 991, 95, 26}, L"JVM 参数"},
                {JVM, ControlKind::Edit, {132, 991, 426, 26}},
                {MIN_LABEL, ControlKind::Label, {32, 1024, 95, 26}, L"最小内存"},
                {MIN, ControlKind::Edit, {132, 1024, 80, 26}},
                {MIN_UNIT, ControlKind::Label, {217, 1024, 30, 26}, L"MB"},
                {ENVIRONMENT_LABEL, ControlKind::Label, {32, 1057, 95, 26}, L"环境变量"},
                {ENVIRONMENT, ControlKind::Edit, {132, 1057, 426, 26}},
                {COMPATIBILITY_GROUP, ControlKind::Group, {12, 1103, 566, 95}, L"兼容性"},
                {NO_DEFAULT_JVM_ARGS, ControlKind::Check, {32, 1128, 250, 26}, L"不添加默认 JVM 参数"},
                {NO_INTEGRITY_CHECK, ControlKind::Check, {32, 1161, 250, 26}, L"不检查游戏完整性"},
        };

        // The version settings of the default interface on one page that scrolls, in the frame of the settings.
        // Every change applies at once.
        class VersionSettingsWindow : public SectionedForm {
        public:
            explicit VersionSettingsWindow(std::string instance) : key(std::move(instance)) {}

            bool create(HWND parent) {
                const Instances::Entry* entry = Instances::find(key);
                if (entry == nullptr ||
                    !createFrame(parent, L"版本设置", Platform::widen(entry->info.id), {{L"版本设置", PAGE}}, FRAME)) {
                    return false;
                }
                const RECT backing = FindControl(PAGE)->GetBounds();
                auto scroll = std::make_shared<exui::ScrollPanel>(
                        backing.left, backing.top, backing.right - backing.left, backing.bottom - backing.top);
                page = scroll.get();
                AddControl(SCROLL, scroll);
                MoveInto(PAGE, {SCROLL});
                AddControls(CONTROLS);
                for (const exui::ControlSpec& spec : CONTROLS) {
                    MoveInto(page->GetContent(), {spec.id});
                }
                fill(*entry);
                showSection(0);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const Instances::Entry* entry = Instances::find(key);
                if (entry == nullptr) {
                    CloseWindow();
                    return;
                }
                const Settings& settings = entry->info.settings;
                bool automatic = settings.max_memory_mb == 0;
                FindControl(MAX)->SetEnabled(!automatic);
                std::wstring hint = automatic ? L"自动分配时为 " + std::to_wstring(automaticMemory()) + L" MB" : L"";
                if (Text(MAX_HINT) != hint) {
                    SetText(MAX_HINT, hint);
                }
                bool server = settings.login == "yggdrasil" || settings.login == "nide8";
                bool nide8 = settings.login == "nide8";
                for (uint32_t id : {AUTH_LABEL, AUTH, AUTH_PRESET}) {
                    FindControl(id)->SetVisible(server);
                }
                if (!laidOut || server != withServer) {
                    layout(server);
                }
                revealFocus();
                std::wstring authLabel = nide8 ? L"服务器 ID" : L"认证服务器";
                if (Text(AUTH_LABEL) != authLabel) {
                    SetText(AUTH_LABEL, authLabel);
                }
                std::wstring preset = nide8 ? L"服主后台" : L"设置为 LittleSkin";
                if (Text(AUTH_PRESET) != preset) {
                    SetText(AUTH_PRESET, preset);
                }
                // The Java list may still be loading when the window opens.
                if (javaLabels() != shownJavas) {
                    fillJava(settings);
                }
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
                    case DIR: edit([index](Settings& s) { s.isolated = fromTristate(index); }); break;
                    case JAVA: chooseJava(index); break;
                    case VISIBILITY:
                        edit([index](Settings& s) { s.launcher_visibility = VISIBILITIES[index].key; });
                        break;
                    case PRIORITY: edit([index](Settings& s) { s.process_priority = PRIORITIES[index].key; }); break;
                    case LOGS: edit([index](Settings& s) { s.show_logs = fromTristate(index); }); break;
                    case PATCH: edit([index](Settings& s) { s.patch = fromTristate(index); }); break;
                    case LOGIN: edit([index](Settings& s) { s.login = LOGINS[index].key; }); break;
                }
                refresh();
            }

            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) {
                    SectionedForm::OnAction(id);
                    return;
                }
                switch (id) {
                    case MODS: showMods(GetHwnd(), key); break;
                    case SHORTCUT: showShortcut(GetHwnd(), key); break;
                    case LOADERS:
                    case EXPORT: message(GetHwnd(), Text(id) + L"还在开发中", L"版本设置"); break;
                    case AUTO_MEMORY: {
                        uint32_t memory = Checked(AUTO_MEMORY) ? 0 : automaticMemory();
                        edit([memory](Settings& s) { s.max_memory_mb = memory; });
                        SetText(MAX, memory != 0 ? std::to_wstring(memory) : L"");
                        break;
                    }
                    case FULLSCREEN: {
                        bool on = Checked(FULLSCREEN);
                        edit([on](Settings& s) { s.fullscreen = on; });
                        break;
                    }
                    case AUTH_PRESET:
                        if (Text(AUTH_LABEL) == L"服务器 ID") {
                            Platform::openUrl(std::string(blb::auth::nide8_admin_url));
                        } else {
                            SetText(AUTH, Platform::widen(std::string(blb::auth::littleskin_api)));
                        }
                        break;
                    case NO_DEFAULT_JVM_ARGS: {
                        bool off = Checked(NO_DEFAULT_JVM_ARGS);
                        edit([off](Settings& s) { s.default_jvm_args = !off; });
                        break;
                    }
                    case NO_INTEGRITY_CHECK: {
                        bool off = Checked(NO_INTEGRITY_CHECK);
                        edit([off](Settings& s) { s.check_integrity = !off; });
                        break;
                    }
                }
                refresh();
            }

        private:
            std::string key;
            exui::ScrollPanel* page = nullptr;
            bool laidOut = false, withServer = false;
            // The control that had the focus at the last refresh.
            exui::Widget* focused = nullptr;

            // Puts every control where the table has it, those below the server row a row higher while there is
            // none, as the default interface does.
            void layout(bool server) {
                laidOut = true;
                withServer = server;
                const int shift = server ? 0 : ROW;
                for (const exui::ControlSpec& spec : CONTROLS) {
                    const Gdiplus::Rect& at = spec.bounds;
                    int height = spec.id == SERVER_GROUP ? at.Height - shift : at.Height;
                    FindControl(spec.id)->SetBounds(at.X, at.Y >= BELOW_SERVER ? at.Y - shift : at.Y, at.Width, height);
                }
                page->SetContentHeight(PAGE_HEIGHT - shift);
            }

            // Tab may move the focus to a control scrolled out of sight; the page then scrolls to it.
            void revealFocus() {
                exui::Widget* now = GetFocusedWidget();
                if (now == focused) {
                    return;
                }
                focused = now;
                if (now == nullptr || now->GetParent() != &page->GetContent()) {
                    return;
                }
                int top = now->GetY(), bottom = now->GetY() + now->GetHeight();
                int view = page->GetHeight(), scrolled = page->GetScrollPosition();
                if (top < scrolled) {
                    page->ScrollTo(top - 30);
                } else if (bottom > scrolled + view) {
                    page->ScrollTo(bottom - view + 12);
                }
            }
            // The paths behind the Java choices: none for 自动选择, then the ones found and the one chosen by hand.
            std::vector<std::filesystem::path> javaPaths;
            std::vector<std::wstring> shownJavas;

            void edit(const std::function<void(Settings&)>& change) {
                Instances::editSettings(key, change);
            }

            static uint32_t automaticMemory() {
                return InstanceSettings::automaticMemory(Platform::memory().totalMb);
            }

            // A text setting, applied as it is typed, with the explanation of the default interface on its label.
            void text(uint32_t label, uint32_t id, const std::string& value,
                      std::function<void(Settings&, const std::wstring&)> apply, const std::wstring& explanation = {}) {
                auto& box = Control<exui::EditBox>(id);
                box.SetText(Platform::widen(value));
                box.OnTextChanged = [this, apply](const std::wstring& typed) {
                    edit([&](Settings& s) { apply(s, typed); });
                };
                if (!explanation.empty()) {
                    FindControl(label)->SetTooltipText(explanation);
                    box.SetTooltipText(explanation);
                }
            }

            std::vector<std::wstring> javaLabels() const {
                std::vector<std::wstring> labels;
                for (const InstanceSettings::JavaOption& option : InstanceSettings::javaOptions()) {
                    labels.push_back(Platform::widen(option.label));
                }
                return labels;
            }

            void fillJava(const Settings& settings) {
                shownJavas = javaLabels();
                std::vector<std::wstring> options{L"自动选择"};
                javaPaths = {std::filesystem::path()};
                int selected = 0;
                const auto& found = Catalog::java().list;
                std::vector<InstanceSettings::JavaOption> javas = InstanceSettings::javaOptions();
                for (size_t i = 0; i < javas.size(); i++) {
                    options.push_back(Platform::widen(javas[i].label));
                    javaPaths.push_back(javas[i].path);
                    if (settings.java && i < found.size() && InstanceSettings::isJava(*settings.java, found[i])) {
                        selected = (int) i + 1;
                    }
                }
                if (settings.java && selected == 0) {
                    options.push_back(Platform::widen(Platform::display(*settings.java)));
                    javaPaths.push_back(*settings.java);
                    selected = (int) javaPaths.size() - 1;
                }
                options.push_back(L"浏览…");
                Control<exui::ComboBox>(JAVA).ClearItems();
                choose(JAVA, options, selected);
            }

            void chooseJava(int index) {
                const Instances::Entry* entry = Instances::find(key);
                if (entry == nullptr) {
                    return;
                }
                if (index < (int) javaPaths.size()) {
                    std::optional<std::filesystem::path> chosen;
                    if (index > 0) {
                        chosen = javaPaths[index];
                    }
                    edit([&](Settings& s) { s.java = chosen; });
                    return;
                }
                // The file dialog runs a loop of its own, under which the list may be rebuilt or lose the instance.
                std::filesystem::path initial = entry->info.settings.java.value_or(std::filesystem::path());
                std::optional<std::filesystem::path> picked = Platform::pickJava(initial);
                if (picked) {
                    edit([&](Settings& s) { s.java = *picked; });
                    Catalog::addJava(*picked);
                }
                if (const Instances::Entry* now = Instances::find(key)) {
                    fillJava(now->info.settings);
                }
            }

            void fill(const Instances::Entry& entry) {
                const Settings& settings = entry.info.settings;
                const wchar_t* byDefault =
                        InstanceSettings::isolatedByDefault(entry) ? L"版本独立" : L"共享 .minecraft";
                choose(DIR, {std::wstring(L"跟随默认（") + byDefault + L"）", L"版本独立", L"共享 .minecraft"},
                       tristate(settings.isolated));
                fillJava(settings);

                std::vector<std::wstring> options;
                for (const Option& option : VISIBILITIES) {
                    options.push_back(option.label);
                }
                choose(VISIBILITY, options,
                       optionOf(VISIBILITIES, std::size(VISIBILITIES), settings.launcher_visibility));
                options.clear();
                for (const Option& option : PRIORITIES) {
                    options.push_back(option.label);
                }
                choose(PRIORITY, options, optionOf(PRIORITIES, std::size(PRIORITIES), settings.process_priority));
                choose(LOGS,
                       {std::wstring(L"跟随全局（") + (LaunchDefaults::showLogs() ? L"显示" : L"不显示") + L"）",
                        L"显示", L"不显示"},
                       tristate(settings.show_logs));
                choose(PATCH,
                       {std::wstring(L"跟随全局（") + (Patches::all() ? L"启用" : L"不启用") + L"）", L"启用",
                        L"不启用"},
                       tristate(settings.patch));

                SetChecked(AUTO_MEMORY, settings.max_memory_mb == 0);
                text(MAX_LABEL, MAX, settings.max_memory_mb != 0 ? std::to_string(settings.max_memory_mb) : "",
                     [](Settings& s, const std::wstring& typed) {
                         s.max_memory_mb = InstanceSettings::parseMegabytes(Platform::narrow(typed));
                     });

                text(SIZE_LABEL, WIDTH, settings.width ? std::to_string(*settings.width) : "",
                     [](Settings& s, const std::wstring& typed) {
                         s.width = InstanceSettings::parseSize(trimmed(typed));
                     });
                text(SIZE_LABEL, HEIGHT, settings.height ? std::to_string(*settings.height) : "",
                     [](Settings& s, const std::wstring& typed) {
                         s.height = InstanceSettings::parseSize(trimmed(typed));
                     });
                SetChecked(FULLSCREEN, settings.fullscreen);

                options.clear();
                for (const Option& option : LOGINS) {
                    options.push_back(option.label);
                }
                choose(LOGIN, options, optionOf(LOGINS, std::size(LOGINS), settings.login));
                text(AUTH_LABEL, AUTH, settings.auth_server,
                     [](Settings& s, const std::wstring& typed) { s.auth_server = trimmed(typed); });
                text(JOIN_LABEL, JOIN, settings.server,
                     [](Settings& s, const std::wstring& typed) { s.server = trimmed(typed); });

                Control<exui::EditBox>(NOTES).SetMultiline(true);
                text(NOTES_LABEL, NOTES, settings.notes,
                     [](Settings& s, const std::wstring& typed) { s.notes = Platform::narrow(typed); });

                text(
                        GAME_ARGS_LABEL, GAME_ARGS, InstanceSettings::joinArgs(settings.game_args),
                        [](Settings& s, const std::wstring& typed) {
                            s.game_args = InstanceSettings::splitArgs(Platform::narrow(typed));
                        },
                        L"加在游戏参数的最后，空格分隔；含空格的参数用双引号括起来。");
                text(
                        PRE_LAUNCH_LABEL, PRE_LAUNCH, settings.pre_launch,
                        [](Settings& s, const std::wstring& typed) { s.pre_launch = trimmed(typed); },
                        std::wstring(
                                L"游戏准备好后、启动前通过 cmd 运行，启动器等它结束再启动游戏；工作目录是游戏目录。") +
                                COMMAND_VARIABLES);
                text(
                        WRAPPER_LABEL, WRAPPER, settings.wrapper,
                        [](Settings& s, const std::wstring& typed) { s.wrapper = trimmed(typed); },
                        L"放在 java 前面，由它来运行游戏，例如性能分析工具。游戏同样能读到 INST_* 环境变量。");
                text(
                        POST_EXIT_LABEL, POST_EXIT, settings.post_exit,
                        [](Settings& s, const std::wstring& typed) { s.post_exit = trimmed(typed); },
                        std::wstring(L"游戏退出后通过 cmd 运行，不等它结束；工作目录是游戏目录。") + COMMAND_VARIABLES);
                text(
                        JVM_LABEL, JVM, InstanceSettings::joinArgs(settings.jvm_args),
                        [](Settings& s, const std::wstring& typed) {
                            s.jvm_args = InstanceSettings::splitArgs(Platform::narrow(typed));
                        },
                        L"加在启动器默认的 JVM 参数之后，空格分隔；含空格的参数用双引号括起来。");
                text(
                        MIN_LABEL, MIN, settings.min_memory_mb != 0 ? std::to_string(settings.min_memory_mb) : "",
                        [](Settings& s, const std::wstring& typed) {
                            s.min_memory_mb = InstanceSettings::parseMegabytes(Platform::narrow(typed));
                        },
                        L"Java 一开始就占用的内存（-Xms），不超过最大内存；留空则由 Java 决定。");
                text(
                        ENVIRONMENT_LABEL, ENVIRONMENT, InstanceSettings::joinEnvironment(settings.environment),
                        [](Settings& s, const std::wstring& typed) {
                            s.environment = InstanceSettings::splitEnvironment(Platform::narrow(typed));
                        },
                        L"游戏进程额外的环境变量，写成 名称=值，空格分隔；值里有空格时把整项用双引号括起来。");
                SetChecked(NO_DEFAULT_JVM_ARGS, !settings.default_jvm_args);
                FindControl(NO_DEFAULT_JVM_ARGS)
                        ->SetTooltipText(L"不加启动器默认的 G1 "
                                         L"垃圾回收、日志安全和输出编码等参数，只用版本自带的和上面填写的。");
                SetChecked(NO_INTEGRITY_CHECK, !settings.check_integrity);
                FindControl(NO_INTEGRITY_CHECK)
                        ->SetTooltipText(L"启动前不校验也不补全游戏文件，启动更快；文件缺失或损坏时游戏会启动失败。");

                bool modded = false;
                for (const auto& loader : entry.info.loaders) {
                    modded = modded || blb::install::is_primary_loader(loader.type) ||
                            loader.type == blb::install::loader_type::liteloader;
                }
                FindControl(MODS)->SetEnabled(modded);
            }
        };
    }

    void showVersionSettings(HWND parent, const std::string& key) {
        VersionSettingsWindow window(key);
        if (window.create(parent)) {
            runModal(window);
        }
    }
}
