#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ShortcutWindow.h"
#include "app/instances/Shortcuts.h"
#include "app/system/Platform.h"
#include "ui/exui/Form.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/windows/Dialogs.h"

#include <filesystem>
#include <optional>
#include <string>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        // The default interface's shortcut page in the layout of the login dialog: the icon, its picture when it is
        // a custom one, and whether the game starts without the launcher. The choices are kept for the instance.
        class ShortcutDialog : public exui::Form {
        public:
            enum : uint32_t {
                ICON_LABEL = 1,
                LAUNCHER_ICON,
                INSTANCE_ICON,
                CUSTOM_ICON,
                FILE_LABEL,
                FILE,
                PICK,
                HEADLESS,
                CREATE,
                CANCEL
            };

            explicit ShortcutDialog(std::string instance) : key(std::move(instance)) {}

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {ICON_LABEL, ControlKind::Label, {25, 35, 65, 25}, L"图标"},
                        {LAUNCHER_ICON, ControlKind::Radio, {95, 35, 90, 25}, L"启动器图标"},
                        {INSTANCE_ICON, ControlKind::Radio, {190, 35, 85, 25}, L"实例图标"},
                        {CUSTOM_ICON, ControlKind::Radio, {280, 35, 95, 25}, L"自定义图片"},
                        {FILE_LABEL, ControlKind::Label, {25, 70, 65, 25}, L"图标文件"},
                        {FILE, ControlKind::Edit, {95, 70, 220, 25}},
                        {PICK, ControlKind::Button, {320, 70, 55, 25}, L"浏览"},
                        {HEADLESS, ControlKind::Check, {25, 105, 200, 25}, L"静默启动"},
                        {CREATE, ControlKind::Button, {230, 137, 70, 35}, L"生成"},
                        {CANCEL, ControlKind::Button, {305, 137, 70, 35}, L"取消"},
                };
                if (!createDialog(*this, parent, L"生成快捷方式", 400, 197)) {
                    return false;
                }
                AddControls(CONTROLS);
                GroupRadios({LAUNCHER_ICON, INSTANCE_ICON, CUSTOM_ICON});
                const Shortcuts::Draft& draft = Shortcuts::draft(key);
                SetChecked(draft.icon == Shortcuts::Icon::Launcher         ? LAUNCHER_ICON
                                   : draft.icon == Shortcuts::Icon::Custom ? CUSTOM_ICON
                                                                           : INSTANCE_ICON,
                           true);
                SetText(FILE, draft.custom.wstring());
                Control<exui::EditBox>(FILE).OnTextChanged = [this](const std::wstring& typed) {
                    Shortcuts::draft(key).custom = Platform::widen(trimmed(typed));
                };
                SetChecked(HEADLESS, draft.headless);
                FindControl(LAUNCHER_ICON)->SetTooltipText(L"启动器程序的图标");
                FindControl(INSTANCE_ICON)->SetTooltipText(L"实例所用加载器的图标，原版是草方块");
                FindControl(FILE)->SetTooltipText(L"PNG、JPG、ICO 等图片");
                FindControl(HEADLESS)->SetTooltipText(
                        L"不显示启动器窗口，直接启动游戏；游戏启动后启动器自动退出，启动失败时才显示窗口。"
                        L"启动器已经打开时在后台启动，不切换到启动器窗口。");
                update();
                return true;
            }

        protected:
            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_KEYDOWN && wp == VK_RETURN) {
                    make();
                    return 0;
                }
                if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
                    CloseWindow();
                    return 0;
                }
                return Form::HandleMessage(msg, wp, lp);
            }

            void OnAction(uint32_t id) override {
                Shortcuts::Draft& draft = Shortcuts::draft(key);
                switch (id) {
                    case LAUNCHER_ICON: draft.icon = Shortcuts::Icon::Launcher; break;
                    case INSTANCE_ICON: draft.icon = Shortcuts::Icon::Instance; break;
                    case CUSTOM_ICON: draft.icon = Shortcuts::Icon::Custom; break;
                    case HEADLESS: draft.headless = Checked(HEADLESS); break;
                    case PICK: pick(); break;
                    case CREATE: make(); return;
                    case CANCEL: CloseWindow(); return;
                }
                update();
            }

        private:
            std::string key;

            // The picture is only asked for a custom icon.
            void update() {
                bool custom = Checked(CUSTOM_ICON);
                FindControl(FILE_LABEL)->SetEnabled(custom);
                FindControl(FILE)->SetEnabled(custom);
                FindControl(PICK)->SetEnabled(custom);
            }

            void pick() {
                const std::filesystem::path& custom = Shortcuts::draft(key).custom;
                std::filesystem::path start = custom.empty() ? Platform::exeDirectory() : custom;
                if (std::optional<std::filesystem::path> picked = Platform::pickIcon(start)) {
                    // Kept by the edit box, as if typed.
                    SetText(FILE, picked->wstring());
                }
            }

            // Closes once the shortcut is made; a failure is told by a message box and leaves the dialog open.
            void make() {
                if (Shortcuts::create(key)) {
                    CloseWindow();
                }
            }
        };
    }

    void showShortcut(HWND parent, const std::string& instanceKey) {
        ShortcutDialog dialog(instanceKey);
        if (dialog.create(parent)) {
            runModal(dialog);
        }
    }
}
