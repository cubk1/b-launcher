#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ExportWindow.h"
#include "app/instances/Exports.h"
#include "app/instances/Instances.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/windows/Dialogs.h"

#include <string>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;
        using Options = Exports::Options;

        enum : uint32_t {
            PACK = 1,
            NAME_LABEL,
            NAME,
            VERSION_LABEL,
            VERSION,
            AUTHOR_LABEL,
            AUTHOR,
            SUMMARY_LABEL,
            SUMMARY,
            CONTENT,
            MODS,
            DISABLED_MODS,
            MOD_SETTINGS,
            GAME_SETTINGS,
            RESOURCE_PACKS,
            SHADER_PACKS,
            SAVES,
            SERVERS,
            FILES,
            BUNDLE,
            STATUS,
            EXPORT,
            CLOSE
        };

        struct Field {
            uint32_t label, id;
            std::string Options::* member;
        };
        const Field FIELDS[] = {
                {NAME_LABEL, NAME, &Options::name},
                {VERSION_LABEL, VERSION, &Options::version},
                {AUTHOR_LABEL, AUTHOR, &Options::author},
                {SUMMARY_LABEL, SUMMARY, &Options::summary},
        };

        // What goes into the pack, with the explanations of the default interface.
        struct Part {
            uint32_t id;
            bool Options::* member;
            const wchar_t* explanation;
        };
        const Part PARTS[] = {
                {MODS, &Options::mods, nullptr},
                {DISABLED_MODS, &Options::disabled_mods, nullptr},
                {MOD_SETTINGS, &Options::mod_settings, L"config 等文件夹里模组的设置。"},
                {GAME_SETTINGS, &Options::game_settings, L"options.txt 里的按键、视频等设置。"},
                {RESOURCE_PACKS, &Options::resource_packs, nullptr},
                {SHADER_PACKS, &Options::shader_packs, nullptr},
                {SAVES, &Options::saves, nullptr},
                {SERVERS, &Options::servers, nullptr},
                {BUNDLE, &Options::bundle_files,
                 L"关闭时，能在 Modrinth 上找到的模组和资源只记下载地址，整合包更小，安装时再下载；"
                 L"打开则把文件都放进整合包。"},
        };

        // The default interface's export page as a dialog: the pack, what goes into it and how its files are
        // stored, then 导出, which turns into 取消 while this instance's pack is written. Every change is kept.
        class ExportDialog : public LiveForm {
        public:
            explicit ExportDialog(std::string instance) : key(std::move(instance)) {}

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {PACK, ControlKind::Group, {18, 35, 484, 96}, L"整合包"},
                        {NAME_LABEL, ControlKind::Label, {38, 60, 40, 26}, L"名称"},
                        {NAME, ControlKind::Edit, {80, 60, 170, 26}},
                        {VERSION_LABEL, ControlKind::Label, {270, 60, 40, 26}, L"版本"},
                        {VERSION, ControlKind::Edit, {312, 60, 170, 26}},
                        {AUTHOR_LABEL, ControlKind::Label, {38, 93, 40, 26}, L"作者"},
                        {AUTHOR, ControlKind::Edit, {80, 93, 170, 26}},
                        {SUMMARY_LABEL, ControlKind::Label, {270, 93, 40, 26}, L"简介"},
                        {SUMMARY, ControlKind::Edit, {312, 93, 170, 26}},
                        {CONTENT, ControlKind::Group, {18, 141, 484, 96}, L"包含的内容"},
                        {MODS, ControlKind::Check, {38, 166, 105, 26}, L"模组"},
                        {DISABLED_MODS, ControlKind::Check, {149, 166, 105, 26}, L"已禁用的模组"},
                        {MOD_SETTINGS, ControlKind::Check, {260, 166, 105, 26}, L"模组配置文件"},
                        {GAME_SETTINGS, ControlKind::Check, {371, 166, 110, 26}, L"游戏设置"},
                        {RESOURCE_PACKS, ControlKind::Check, {38, 199, 105, 26}, L"资源包"},
                        {SHADER_PACKS, ControlKind::Check, {149, 199, 105, 26}, L"光影包"},
                        {SAVES, ControlKind::Check, {260, 199, 105, 26}, L"存档"},
                        {SERVERS, ControlKind::Check, {371, 199, 110, 26}, L"服务器列表"},
                        {FILES, ControlKind::Group, {18, 247, 484, 63}, L"文件"},
                        {BUNDLE, ControlKind::Check, {38, 272, 300, 26}, L"全部打包进整合包"},
                        {STATUS, ControlKind::Label, {20, 326, 290, 26}},
                        {EXPORT, ControlKind::Button, {316, 322, 90, 35}, L"导出"},
                        {CLOSE, ControlKind::Button, {412, 322, 90, 35}, L"关闭"},
                };
                if (!createDialog(*this, parent, L"导出整合包", 520, 382)) {
                    return false;
                }
                AddControls(CONTROLS);
                const Options& options = Exports::options(key);
                for (const Field& field : FIELDS) {
                    auto& box = Control<exui::EditBox>(field.id);
                    box.SetText(Platform::widen(options.*field.member));
                    std::string Options::* member = field.member;
                    box.OnTextChanged = [this, member](const std::wstring& typed) {
                        Exports::options(key).*member = trimmed(typed);
                        Exports::save(key);
                    };
                }
                FindControl(VERSION)->SetTooltipText(L"例如 1.0.0");
                for (const Part& part : PARTS) {
                    SetChecked(part.id, options.*part.member);
                    if (part.explanation != nullptr) {
                        FindControl(part.id)->SetTooltipText(part.explanation);
                    }
                }
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                if (Instances::find(key) == nullptr) {
                    CloseWindow();
                    return;
                }
                bool running = Exports::running();
                bool mine = running && Exports::runningKey() == key;
                std::wstring status = mine ? Platform::widen(Exports::status())
                        : running          ? std::wstring(L"另一个实例正在导出")
                                           : std::wstring();
                if (Text(STATUS) != status) {
                    SetText(STATUS, status);
                }
                std::wstring action = mine ? L"取消" : L"导出";
                if (Text(EXPORT) != action) {
                    SetText(EXPORT, action);
                }
                FindControl(EXPORT)->SetEnabled(!running || mine);
                // The options stay as they are while the pack is written from them.
                for (const Field& field : FIELDS) {
                    FindControl(field.label)->SetEnabled(!mine);
                    FindControl(field.id)->SetEnabled(!mine);
                }
                for (const Part& part : PARTS) {
                    FindControl(part.id)->SetEnabled(!mine);
                }
            }

            void OnAction(uint32_t id) override {
                switch (id) {
                    case EXPORT:
                        if (Exports::running()) {
                            Exports::cancel();
                        } else {
                            Exports::start(key);
                        }
                        break;
                    case CLOSE: CloseWindow(); return;
                    default:
                        for (const Part& part : PARTS) {
                            if (part.id == id) {
                                Exports::options(key).*part.member = Checked(id);
                                Exports::save(key);
                            }
                        }
                        break;
                }
                refresh();
            }

        private:
            std::string key;
        };
    }

    void showExport(HWND parent, const std::string& instanceKey) {
        ExportDialog dialog(instanceKey);
        if (dialog.create(parent)) {
            runModal(dialog);
        }
    }
}
