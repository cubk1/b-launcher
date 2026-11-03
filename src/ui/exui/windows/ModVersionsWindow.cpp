#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ModVersionsWindow.h"
#include "app/instances/Instances.h"
#include "app/instances/ModManager.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"

#include <core/base/strings.hpp>
#include <algorithm>

namespace ForgottenMelody {
    namespace {
        class ModVersionsWindow : public SectionedForm {
        public:
            enum : uint32_t { PAGE = FIRST_CONTROL, SOURCE_LABEL, SOURCE, SEARCH_LABEL, SEARCH, LIST, STATUS, RELOAD, SELECT };
            ModVersionsWindow(std::string instance, std::filesystem::path file) : key(std::move(instance)), file(std::move(file)) {}
            bool create(HWND parent) {
                const auto* mod = current();
                if (!mod) return false;
                Frame frame; frame.width = 760; frame.height = 590; frame.top = 65; frame.bottom = 535;
                frame.listWidth = 0; frame.done = L"取消";
                if (!createFrame(parent, L"切换模组版本", Platform::widen(ModManager::displayName(*mod)), {{L"切换版本", PAGE}}, frame)) return false;
                using exui::ControlKind;
                const exui::ControlSpec controls[] = {
                        {SOURCE_LABEL, ControlKind::Label, {}, L"来源"}, {SOURCE, ControlKind::Combo, {}},
                        {SEARCH_LABEL, ControlKind::Label, {}, L"搜索"}, {SEARCH, ControlKind::Edit, {}},
                        {LIST, ControlKind::List, {}}, {STATUS, ControlKind::Label, {}},
                        {RELOAD, ControlKind::Button, {}, L"重试"}, {SELECT, ControlKind::Button, {}, L"选择版本"},
                };
                AddControls(controls);
                MoveInto(PAGE, {SOURCE_LABEL, SOURCE, SEARCH_LABEL, SEARCH, LIST, STATUS});
                Addons::Source initial = Addons::Source::Modrinth;
                if (const auto* change = ModManager::changeFor(key, file)) initial = change->target.source;
                else if (const auto* remote = ModManager::remote(*mod)) initial = remote->source;
                choose(SOURCE, {L"Modrinth", L"CurseForge"}, initial == Addons::Source::CurseForge ? 1 : 0);
                auto& list = Control<exui::ListView>(LIST);
                list.InsertColumn(L"版本", 240); list.InsertColumn(L"名称", 225);
                list.InsertColumn(L"发布于", 105); list.InsertColumn(L"状态", 105);
                Control<exui::EditBox>(SEARCH).OnTextChanged = [this](const std::wstring&) { refresh(); };
                Control<exui::Label>(STATUS).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                using exui::Layout;
                Layout::Column({Layout::Row({cell(SOURCE_LABEL, 35), cell(SOURCE, 150), cell(SEARCH_LABEL, 35), cell(SEARCH)}, 8, 28),
                                cell(LIST), cell(STATUS, 26)}, 10)
                        .Apply({15, 12, FindControl(PAGE)->GetWidth() - 30, FindControl(PAGE)->GetHeight() - 24});
                setFooterActions({Layout::Row({}), cell(RELOAD, 80), cell(SELECT, 100)});
                showSection(0); startRefreshing(); return true;
            }
        protected:
            void refresh() override {
                ModManager::prepare(key);
                const auto* mod = current();
                if (!Instances::find(key) || !mod) { CloseWindow(); return; }
                setSubject(Platform::widen(ModManager::displayName(*mod)));
                bool busy = ModManager::busy(key), loading = Mods::of(key).loading;
                const auto source = sourceOf();
                const auto* remote = ModManager::remote(*mod, source);
                std::string project = remote ? remote->projectId : "";
                if (project != shownProject) {
                    shownProject = project;
                    Control<exui::ListView>(LIST).SetSelectedRow(-1);
                }
                Rows rows;
                std::vector<std::string> ids;
                std::wstring status;
                bool retry = false;
                if (!remote) {
                    std::string error = ModManager::identifyError(key, source);
                    bool identifying = ModManager::identifying(key, source);
                    status = identifying ? L"正在从 " + Platform::widen(Addons::sourceName(source)) + L" 识别…"
                            : !error.empty() ? L"识别失败：" + Platform::widen(error)
                                            : L"没有在 " + Platform::widen(Addons::sourceName(source)) + L" 上找到这个模组";
                    retry = !identifying && !error.empty();
                } else {
                    const auto& versions = ModManager::versions(key, project);
                    if (versions.status == Addons::Status::Loading) status = L"正在获取版本…";
                    else if (versions.status == Addons::Status::Failed) { status = L"获取版本失败：" + Platform::widen(versions.error); retry = true; }
                    else {
                        std::string query = trimmed(Text(SEARCH));
                        const auto* change = ModManager::changeFor(key, mod->info.file);
                        for (const auto& version : versions.list) {
                            if (!query.empty() && !b_launcher_base::icontains(version.name, query) &&
                                !b_launcher_base::icontains(version.number, query)) continue;
                            std::wstring state = version.type == "beta" ? L"测试版" : version.type == "alpha" ? L"预览版" : L"";
                            if (version.source == source && version.id == remote->versionId) state += state.empty() ? L"当前" : L" - 当前";
                            if (change && change->target.source == source && change->target.id == version.id) state += state.empty() ? L"待应用" : L" - 待应用";
                            rows.push_back({Platform::widen(version.number), Platform::widen(version.name),
                                            Platform::widen(version.published.substr(0, 10)), state});
                            ids.push_back(version.id);
                        }
                        status = versions.list.empty() ? L"没有适用于 " + Platform::widen(ModManager::gameVersion(key)) + L" 的版本"
                                : rows.empty() ? L"没有匹配的版本" : L"共 " + std::to_wstring(rows.size()) + L" 个版本";
                    }
                }
                if (rows.empty()) { rows.push_back({status, L"", L"", L""}); ids.emplace_back(); }
                fill(Control<exui::ListView>(LIST), shown, shownIds, std::move(rows), std::move(ids));
                UpdateText(STATUS, status); FindControl(STATUS)->SetTooltipText(status);
                FindControl(RELOAD)->SetEnabled(retry && !busy && !loading);
                FindControl(SELECT)->SetEnabled(!busy && !loading && selectedVersion() != nullptr);
                FindControl(SOURCE)->SetEnabled(!busy);
            }
            void OnSelection(uint32_t id, int index) override {
                if (id == SOURCE) { shownProject.clear(); Control<exui::ListView>(LIST).SetSelectedRow(-1); refresh(); }
                else if (id == LIST) refresh();
                else SectionedForm::OnSelection(id, index);
            }
            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) { SectionedForm::OnAction(id); return; }
                if (ModManager::busy(key) || Mods::of(key).loading) return;
                const auto* mod = current();
                if (!mod || !Instances::find(key)) { CloseWindow(); return; }
                if (id == RELOAD) {
                    if (const auto* remote = ModManager::remote(*mod, sourceOf())) ModManager::refreshVersions(key, remote->projectId);
                    else Mods::scan(key);
                } else if (id == SELECT) {
                    if (const auto* version = selectedVersion()) {
                        Addons::Version target = *version;
                        ModManager::stage(key, mod->info.file, target);
                        CloseWindow(); return;
                    }
                }
                refresh();
            }
        private:
            std::string key, shownProject;
            std::filesystem::path file;
            Rows shown;
            std::vector<std::string> shownIds;
            Addons::Source sourceOf() const {
                return Control<exui::ComboBox>(SOURCE).GetSelectedIndex() == 1 ? Addons::Source::CurseForge : Addons::Source::Modrinth;
            }
            const Mods::Mod* current() const {
                for (const auto& mod : Mods::of(key).mods) if (mod.info.file == file) return &mod;
                return nullptr;
            }
            const Addons::Version* selectedVersion() const {
                const auto* mod = current();
                const auto* remote = mod ? ModManager::remote(*mod, sourceOf()) : nullptr;
                if (!remote || remote->projectId != shownProject) return nullptr;
                std::string id = selectedKey(Control<exui::ListView>(LIST), shownIds);
                if (id.empty()) return nullptr;
                const auto& versions = ModManager::versions(key, remote->projectId);
                if (versions.status != Addons::Status::Ready) return nullptr;
                for (const auto& version : versions.list)
                    if (version.id == id && version.source == sourceOf() && version.projectId == remote->projectId) return &version;
                return nullptr;
            }
        };
    }
    void showModVersions(HWND parent, const std::string& instance, const std::filesystem::path& file) {
        ModVersionsWindow window(instance, file);
        if (window.create(parent)) runModal(window);
    }
}
