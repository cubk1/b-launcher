#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/LoadersWindow.h"
#include "app/instances/Installer.h"
#include "app/instances/Instances.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/InstallControls.h"

#include <core/instance/loader_editor.hpp>

#include <map>
#include <memory>
#include <string>

namespace ForgottenMelody {

    namespace {
        using blb::install::loader_type;
        using exui::ControlKind;

        enum : uint32_t {
            SETUP_VIEW = 1,
            PROGRESS_VIEW,
            LOADERS,
            GAME_LABEL,
            GAME,
            STATUS,
            REFRESH,
            APPLY,
            CLOSE,
            ROWS,
            PROGRESS = ROWS + LoaderRows::ID_COUNT,
        };

        constexpr int WIDTH = 660, MARGIN = 18, TOP = 35;

        std::map<loader_type, std::string> loadersOf(const Instances::Entry& entry) {
            std::map<loader_type, std::string> out;
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                out[loader.type] = loader.version;
            }
            return out;
        }

        // The default interface's 修改加载器 as a dialog: the game version and a row for each loader it takes, then
        // 应用, after which the change takes the place of the rows until it ends.
        class LoadersDialog : public LiveForm {
        public:
            explicit LoadersDialog(std::string instance) : key(std::move(instance)) {}

            bool create(HWND parent, const Instances::Entry& entry) {
                minecraft = entry.info.minecraft_version;
                original = loadersOf(entry);
                rows.reset(minecraft, original);
                // As tall as the loaders this game version takes, which are always the same ones.
                int count = (int) blb::instance::loader_choices(minecraft, rows.infos()).size();
                int groupHeight = 65 + count * LoaderRows::ROW_H;
                int bottom = TOP + groupHeight + 12;
                int width = WIDTH - 2 * MARGIN;
                std::vector<exui::ControlSpec> controls = {
                        {LOADERS, ControlKind::Group, {MARGIN, TOP, width, groupHeight}, L"加载器"},
                        {GAME_LABEL, ControlKind::Label, {MARGIN + 20, TOP + 25, 115, 26}, L"Minecraft"},
                        {GAME, ControlKind::Label, {MARGIN + 20 + LoaderRows::LABEL_W, TOP + 25, 300, 26}},
                        {STATUS, ControlKind::Label, {MARGIN + 2, bottom + 4, 334, 26}},
                        {REFRESH, ControlKind::Button, {WIDTH - MARGIN - 282, bottom, 90, 35}, L"刷新列表"},
                        {APPLY, ControlKind::Button, {WIDTH - MARGIN - 186, bottom, 90, 35}, L"应用"},
                        {CLOSE, ControlKind::Button, {WIDTH - MARGIN - 90, bottom, 90, 35}, L"关闭"},
                };
                if (!createDialog(*this, parent, L"修改加载器", WIDTH, bottom + 35 + 25)) {
                    return false;
                }
                for (uint32_t id : {SETUP_VIEW, PROGRESS_VIEW}) {
                    AddControl(id, std::make_shared<exui::Panel>(MARGIN, TOP, width, groupHeight));
                }
                AddControls(controls);
                AddControls(LoaderRows::controls(ROWS, MARGIN + 20, TOP + 59, width - 40));
                AddControls(InstallProgress::controls(PROGRESS, MARGIN, TOP, width, groupHeight));
                for (uint32_t id : {LOADERS, GAME_LABEL, GAME}) {
                    MoveInto(SETUP_VIEW, {id});
                }
                for (uint32_t id = ROWS; id < PROGRESS; id++) {
                    MoveInto(SETUP_VIEW, {id});
                }
                for (uint32_t id = PROGRESS; id < PROGRESS + InstallProgress::ID_COUNT; id++) {
                    MoveInto(PROGRESS_VIEW, {id});
                }
                rows.create();
                progress.create();
                SetText(GAME, Platform::widen(minecraft));
                FindControl(REFRESH)->SetTooltipText(L"重新获取加载器的版本列表");
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const Instances::Entry* entry = Instances::find(key);
                if (entry == nullptr && !started) {
                    CloseWindow();
                    return;
                }
                if (started) {
                    progress.refresh();
                }
                bool changing = started && progress.active();
                FindControl(SETUP_VIEW)->SetVisible(!changing);
                FindControl(PROGRESS_VIEW)->SetVisible(changing);
                FindControl(REFRESH)->SetVisible(!changing);
                if (changing) {
                    setText(STATUS, L"");
                    setText(APPLY, progress.hasEnded() ? L"返回" : L"取消");
                    FindControl(APPLY)->SetEnabled(true);
                    return;
                }
                if (entry == nullptr) {
                    return;
                }

                // The rows follow the instance until they are changed, so that they show what a change made of it.
                std::map<loader_type, std::string> current = loadersOf(*entry);
                if (current != original && rows.chosen() == original) {
                    rows.reset(minecraft, current);
                }
                original = current;
                bool busy = Installer::running();
                rows.refresh(!busy);
                std::wstring problem = busy                              ? L"正在安装，等它完成后再修改加载器"
                        : rows.chosen() == original && entry->info.valid ? L"没有改动"
                                                                         : L"";
                setText(STATUS, problem);
                setText(APPLY, L"应用");
                FindControl(APPLY)->SetEnabled(problem.empty());
                FindControl(REFRESH)->SetEnabled(!busy);
            }

            void OnSelection(uint32_t id, int index) override {
                if (index >= 0 && rows.select(id, index)) {
                    refresh();
                }
            }

            void OnAction(uint32_t id) override {
                switch (id) {
                    case APPLY:
                        if (!started) {
                            Installer::startModify(key, rows.requests());
                            started = Installer::running();
                        } else if (!progress.hasEnded()) {
                            progress.cancel();
                        } else {
                            progress.dismiss();
                            started = false;
                        }
                        break;
                    case REFRESH: rows.reload(); break;
                    case CLOSE: CloseWindow(); return;
                }
                refresh();
            }

        private:
            std::string key, minecraft;
            // The loaders of the instance as they were last seen.
            std::map<loader_type, std::string> original;
            LoaderRows rows{*this, ROWS};
            InstallProgress progress{*this, PROGRESS, L"修改"};
            // Whether this dialog started the change the installer makes, rather than another install.
            bool started = false;

            void setText(uint32_t id, const std::wstring& text) {
                if (Text(id) != text) {
                    SetText(id, text);
                }
            }
        };
    }

    void showLoaders(HWND parent, const std::string& instanceKey) {
        const Instances::Entry* entry = Instances::find(instanceKey);
        if (entry == nullptr) {
            return;
        }
        if (entry->info.minecraft_version.empty()) {
            message(parent, L"认不出这个实例的游戏版本，不能修改加载器", L"修改加载器");
            return;
        }
        LoadersDialog dialog(instanceKey);
        if (dialog.create(parent, *entry)) {
            runModal(dialog);
        }
    }
}
