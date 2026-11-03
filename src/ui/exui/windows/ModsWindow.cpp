#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ModsWindow.h"
#include "app/instances/Mods.h"
#include "app/instances/Instances.h"
#include "app/instances/ModManager.h"
#include "app/launch/Game.h"
#include "app/downloads/ModNames.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/widgets/CheckRadio.h"
#include "ui/exui/widgets/ProgressBar.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/ModVersionsWindow.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <optional>
#include <core/base/strings.hpp>
#include <vector>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        // Keep the key across both disabled suffixes supported by the mod scanner.
        std::string keyOf(const std::filesystem::path& file) {
            std::filesystem::path enabled = file;
            if (b_launcher_base::modpack::is_disabled_file(file)) enabled.replace_extension();
            return b_launcher_base::path_to_utf8(enabled);
        }

        // What the default interface takes as a mod when files are dropped on it.
        bool looksLikeMod(const std::filesystem::path& file) {
            return Mods::acceptsDrop(b_launcher_base::path_to_utf8(file.filename()));
        }

        // The default interface's tags, in words.
        std::wstring stateOf(const Mods::Mod& mod) {
            std::wstring state;
            auto add = [&state](const std::wstring& part) { state += (state.empty() ? L"" : L"，") + part; };
            if (!mod.info.problem.empty()) {
                add(L"无法读取：" + Platform::widen(mod.info.problem));
            }
            if (!mod.missing.empty()) {
                add(L"缺少前置：" + Platform::widen(b_launcher_base::join(mod.missing, "、")));
            }
            if (!mod.info.enabled) {
                add(L"已禁用");
            }
            return state;
        }

        class ModsWindow : public SectionedForm {
        public:
            enum : uint32_t {
                PAGE = FIRST_CONTROL, LIST, STATUS, SEARCH_LABEL, SEARCH, ADD, FOLDER, RESCAN, HOMEPAGE, TOGGLE, REMOVE,
                ORIGINAL, WIKI, VERSIONS, CHANGES_GROUP, CHANGES, CHANGE_STATUS, PROGRESS, UNDO, ADD_DEPENDENCY,
                DISCARD, APPLY, CANCEL,
            };

            explicit ModsWindow(std::string instance) : key(std::move(instance)) {}

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {LIST, ControlKind::List, {}}, {STATUS, ControlKind::Label, {}},
                        {SEARCH_LABEL, ControlKind::Label, {}, L"搜索"}, {SEARCH, ControlKind::Edit, {}},
                        {ADD, ControlKind::Button, {}, L"添加"}, {FOLDER, ControlKind::Button, {}, L"打开文件夹"},
                        {RESCAN, ControlKind::Button, {}, L"刷新"}, {HOMEPAGE, ControlKind::Button, {}, L"主页"},
                        {TOGGLE, ControlKind::Button, {}, L"禁用"}, {REMOVE, ControlKind::Button, {}, L"删除"},
                        {ORIGINAL, ControlKind::Check, {}, L"原名"}, {WIKI, ControlKind::Button, {}, L"百科"},
                        {VERSIONS, ControlKind::Button, {}, L"切换版本"},
                        {CHANGES_GROUP, ControlKind::Group, {}, L"待应用更改"}, {CHANGES, ControlKind::List, {}},
                        {CHANGE_STATUS, ControlKind::Label, {}}, {PROGRESS, ControlKind::Progress, {}},
                        {UNDO, ControlKind::Button, {}, L"撤销所选"}, {ADD_DEPENDENCY, ControlKind::Button, {}, L"添加前置"},
                        {DISCARD, ControlKind::Button, {}, L"全部放弃"}, {APPLY, ControlKind::Button, {}, L"应用更改"},
                        {CANCEL, ControlKind::Button, {}, L"取消"},
                };
                const auto* entry = Instances::find(key);
                Frame frame; frame.width = 960; frame.height = 680; frame.top = 65; frame.bottom = 625; frame.listWidth = 0;
                if (!entry || !createFrame(parent, L"模组管理", Platform::widen(entry->info.id), {{L"模组管理", PAGE}}, frame)) return false;
                AddControls(CONTROLS);
                for (const auto& control : CONTROLS) MoveInto(PAGE, {control.id});
                auto& list = Control<exui::ListView>(LIST);
                list.SetSingleSelection(false);
                list.InsertColumn(L"名称", 210); list.InsertColumn(L"版本", 115); list.InsertColumn(L"来源", 165);
                list.InsertColumn(L"状态", 220); list.InsertColumn(L"文件", 175);
                auto& changes = Control<exui::ListView>(CHANGES);
                changes.InsertColumn(L"项目 / 问题", 555); changes.InsertColumn(L"版本", 165); changes.InsertColumn(L"来源", 120);
                Control<exui::Label>(STATUS).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::Label>(CHANGE_STATUS).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::ProgressBar>(PROGRESS).SetRange(0, 1000);
                Control<exui::EditBox>(SEARCH).OnTextChanged = [this](const std::wstring&) { refresh(); };
                FindControl(SEARCH)->SetTooltipText(L"按中文名、原名、ID 或文件名搜索");
                FindControl(FOLDER)->SetTooltipText(L"打开实例的 mods 文件夹");
                FindControl(HOMEPAGE)->SetTooltipText(L"打开模组的主页");
                FindControl(REMOVE)->SetTooltipText(L"移到回收站");
                DragAcceptFiles(GetHwnd(), TRUE);
                // Like the default interface, the folder is read again on the way in.
                if (!ModManager::busy(key)) Mods::scan(key);
                showSection(0);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const auto* entry = Instances::find(key);
                if (!entry) { CloseWindow(); return; }
                setSubject(Platform::widen(entry->info.id));
                ModManager::prepare(key);
                const Mods::List& mods = Mods::of(key);
                bool namesReady = ModManager::namesReady(mods);
                bool busy = ModManager::busy(key);
                bool loadingNames = !namesReady && !hasListedMods;
                std::string query = trimmed(Text(SEARCH));
                Rows rows;
                std::vector<std::string> keys;
                int enabled = 0, lacking = 0;
                bool identifying = ModManager::identifying(key, Addons::Source::Modrinth) ||
                        ModManager::identifying(key, Addons::Source::CurseForge);
                for (const auto& mod : mods.mods) {
                    enabled += mod.info.enabled;
                    lacking += !mod.missing.empty();
                    if (loadingNames || !ModManager::matches(mod, query)) continue;
                    std::string name = ModManager::displayName(mod);
                    if (name.empty()) name = mod.info.file_name;
                    std::wstring version = Platform::widen(mod.info.version);
                    if (const auto* change = ModManager::changeFor(key, mod.info.file)) version += L" → " + Platform::widen(change->target.number);
                    std::wstring sources;
                    for (auto source : {Addons::Source::Modrinth, Addons::Source::CurseForge}) {
                        if (ModManager::remote(mod, source)) {
                            if (!sources.empty()) sources += L" / ";
                            sources += Platform::widen(Addons::sourceName(source));
                        }
                    }
                    if (sources.empty()) sources = identifying ? L"正在识别…" : L"未识别";
                    rows.push_back({Platform::widen(name), version, sources, stateOf(mod), Platform::widen(mod.info.file_name)});
                    keys.push_back(keyOf(mod.info.file));
                    hasListedMods = true;
                }
                if (rows.empty()) {
                    std::wstring none = loadingNames ? L"正在获取模组中文名…" : !mods.mods.empty() ? L"没有匹配的模组"
                            : mods.loading ? L"正在读取…" : L"还没有模组";
                    rows.push_back({none, L"", L"", L"", L""}); keys.emplace_back();
                }
                fill(Control<exui::ListView>(LIST), shown, shownKeys, std::move(rows), std::move(keys));
                std::wstring status = L"共 " + std::to_wstring(mods.mods.size()) + L" 个，已启用 " + std::to_wstring(enabled) + L" 个";
                if (mods.loading) status = L"正在读取模组…";
                else if (lacking) status += L"，" + std::to_wstring(lacking) + L" 个缺少前置";
                UpdateText(STATUS, status);
                FindControl(STATUS)->SetTooltipText(status);
                SetChecked(ORIGINAL, ModNames::original());
                auto chosen = selected();
                bool anyOn = std::any_of(chosen.begin(), chosen.end(), [](const auto* mod) { return mod->info.enabled; });
                UpdateText(TOGGLE, anyOn || chosen.empty() ? L"禁用" : L"启用");
                FindControl(TOGGLE)->SetEnabled(canEditFiles() && !chosen.empty());
                FindControl(REMOVE)->SetEnabled(canEditFiles() && !chosen.empty());
                FindControl(ADD)->SetEnabled(canEditFiles());
                FindControl(RESCAN)->SetEnabled(!busy && !mods.loading);
                FindControl(HOMEPAGE)->SetEnabled(chosen.size() == 1 && chosen[0]->info.homepage.starts_with("http"));
                FindControl(WIKI)->SetEnabled(chosen.size() == 1 && ModManager::wikiId(*chosen[0]) > 0);
                FindControl(VERSIONS)->SetEnabled(!busy && !mods.loading && chosen.size() == 1);
                bool accept = canEditFiles();
                if (accept != acceptsDrops) { acceptsDrops = accept; DragAcceptFiles(GetHwnd(), accept); }
                refreshChanges();
            }

            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (msg == WM_DROPFILES) {
                    dropped((HDROP) wp);
                    return 0;
                }
                return LiveForm::HandleMessage(msg, wp, lp);
            }

            // The mods are scanned again and renamed under a dialog, so files rather than mods are kept across one.
            void OnAction(uint32_t id) override {
                if (id < FIRST_CONTROL) { SectionedForm::OnAction(id); return; }
                switch (id) {
                    case ADD: {
                        if (!canEditFiles()) break;
                        std::vector<std::filesystem::path> files =
                                Platform::pickFiles(Mods::folder(key, "mods"), L"添加模组", L"模组 (*.jar)", L"*.jar");
                        if (canEditFiles()) Mods::add(key, files, "mods", "模组");
                        break;
                    }
                    case FOLDER:
                        if (std::filesystem::path folder = Mods::folder(key, "mods"); !folder.empty()) {
                            std::error_code error;
                            std::filesystem::create_directories(folder, error);
                            Platform::open(folder);
                        }
                        break;
                    case RESCAN: if (!ModManager::busy(key) && !Mods::of(key).loading) Mods::scan(key); break;
                    case HOMEPAGE:
                        if (std::vector<const Mods::Mod*> chosen = selected(); chosen.size() == 1) {
                            Platform::openUrl(chosen[0]->info.homepage);
                        }
                        break;
                    case ORIGINAL: ModNames::setOriginal(Checked(ORIGINAL)); break;
                    case WIKI:
                        if (auto chosen = selected(); chosen.size() == 1) {
                            auto id = ModManager::wikiId(*chosen[0]);
                            if (id > 0) Platform::openUrl("https://www.mcmod.cn/class/" + std::to_string(id) + ".html");
                        }
                        break;
                    case VERSIONS:
                        if (auto chosen = selected(); !ModManager::busy(key) && !Mods::of(key).loading && chosen.size() == 1) {
                            auto file = chosen[0]->info.file;
                            showModVersions(GetHwnd(), key, file);
                        }
                        break;
                    case UNDO:
                    case ADD_DEPENDENCY:
                    case DISCARD:
                    case APPLY:
                    case CANCEL: changeAction(id); break;
                    case TOGGLE: if (canEditFiles()) toggle(); break;
                    case REMOVE: if (canEditFiles()) remove(); break;
                }
                refresh();
            }

            void OnSelection(uint32_t, int) override {
                refresh();
            }

        private:
            struct PendingRow {
                std::string id, text;
                std::optional<ModManager::Change> change;
                std::string missing;
            };
            std::string key;
            Rows shownChanges;
            std::vector<std::string> changeKeys;
            std::vector<PendingRow> pending;
            int layoutShape = -1;
            bool acceptsDrops = true, cancelling = false, hasListedMods = false;

            bool canEditFiles() const {
                return Instances::find(key) && !ModManager::busy(key) && !Mods::of(key).loading && Game::state(key) == Game::State::Idle;
            }

            const PendingRow* chosenChange() const {
                std::string id = selectedKey(Control<exui::ListView>(CHANGES), changeKeys);
                if (id.empty()) return nullptr;
                for (const auto& row : pending) if (row.id == id) return &row;
                return nullptr;
            }

            void layout(bool changes, bool busy) {
                int shape = (int) changes | ((int) busy << 1);
                if (shape == layoutShape) return;
                layoutShape = shape;
                for (uint32_t id : {CHANGES_GROUP, CHANGES, CHANGE_STATUS}) FindControl(id)->SetVisible(changes);
                for (uint32_t id : {ADD_DEPENDENCY, UNDO, DISCARD, APPLY}) FindControl(id)->SetVisible(changes && !busy);
                FindControl(CANCEL)->SetVisible(busy);
                FindControl(PROGRESS)->SetVisible(busy);
                using exui::Layout;
                std::vector<Layout> rows{
                        Layout::Row({cell(SEARCH_LABEL, 32), cell(SEARCH), cell(ORIGINAL, 65), cell(ADD, 65), cell(FOLDER, 95), cell(RESCAN, 65)}, 8, 30),
                        cell(LIST),
                        Layout::Row({cell(STATUS), cell(HOMEPAGE, 60), cell(WIKI, 60), cell(VERSIONS, 90), cell(TOGGLE, 65), cell(REMOVE, 65)}, 8, 30),
                };
                if (changes) {
                    Layout actions = busy ? Layout::Row({Layout::Column({cell(CHANGE_STATUS, 18), cell(PROGRESS, 8)}, 4), cell(CANCEL, 85)}, 10, 32)
                            : Layout::Row({cell(CHANGE_STATUS), cell(ADD_DEPENDENCY, 85), cell(UNDO, 85), cell(DISCARD, 85), cell(APPLY, 95)}, 8, 32);
                    rows.push_back(group(CHANGES_GROUP, {cell(CHANGES, 130), std::move(actions)}, {15, 24, 15, 12}, 8));
                }
                Layout::Column(std::move(rows), 10).Apply({15, 10, FindControl(PAGE)->GetWidth() - 30, FindControl(PAGE)->GetHeight() - 20});
            }

            void refreshChanges() {
                bool busy = ModManager::busy(key);
                if (!busy) cancelling = false;
                std::vector<ModManager::Issue> issues = !busy && !ModManager::changes(key).empty() ? ModManager::check(key) : std::vector<ModManager::Issue>();
                const auto& changes = ModManager::changes(key);
                const auto& progress = ModManager::progress(key);
                bool shown = !changes.empty() || busy || progress.phase == ModManager::Phase::Failed;
                Rows rows;
                std::vector<std::string> ids;
                pending.clear();
                for (const auto& change : changes) {
                    std::string id = change.file.empty() ? "project:" + change.projectId : "file:" + keyOf(change.file);
                    std::string version = change.file.empty() ? "新增 " + change.target.number : change.from + " → " + change.target.number;
                    rows.push_back({Platform::widen(change.name), Platform::widen(version), Platform::widen(Addons::sourceName(change.target.source))});
                    ids.push_back(id);
                    pending.push_back({std::move(id), change.name + " " + version, change, {}});
                }
                int errors = 0, warnings = 0;
                for (const auto& issue : issues) {
                    bool error = issue.severity == ModManager::Severity::Error;
                    error ? ++errors : ++warnings;
                    std::string text = (error ? "错误 - " : "警告 - ") + issue.text;
                    rows.push_back({Platform::widen(text), {}, {}});
                    ids.push_back("issue:" + text);
                    pending.push_back({ids.back(), text, {}, issue.missingProject});
                }
                if (progress.phase == ModManager::Phase::Failed) {
                    std::vector<std::string> problems{progress.error};
                    problems.insert(problems.end(), progress.problems.begin(), progress.problems.end());
                    for (const auto& problem : problems) {
                        if (problem.empty()) continue;
                        rows.push_back({L"失败 - " + Platform::widen(problem), {}, {}});
                        ids.push_back("failure:" + std::to_string(ids.size()));
                        pending.push_back({ids.back(), problem, {}, {}});
                    }
                }
                if (rows.empty()) { rows.push_back({L"没有待应用更改", {}, {}}); ids.emplace_back(); }
                fill(Control<exui::ListView>(CHANGES), shownChanges, changeKeys, std::move(rows), std::move(ids));
                UpdateText(CHANGES_GROUP, L"待应用更改（" + std::to_wstring(changes.size()) + L"）");
                const auto* chosen = chosenChange();
                FindControl(CHANGES)->SetTooltipText(chosen ? Platform::widen(chosen->text) : L"");
                bool adding = chosen && !chosen->missing.empty() && ModManager::adding(key, chosen->missing);
                FindControl(UNDO)->SetEnabled(!busy && chosen && chosen->change.has_value());
                FindControl(ADD_DEPENDENCY)->SetEnabled(!busy && chosen && !chosen->missing.empty() && !adding);
                UpdateText(ADD_DEPENDENCY, adding ? L"正在添加…" : L"添加前置");
                FindControl(DISCARD)->SetEnabled(!busy);
                bool checking = shown && !busy && ModManager::checking(key);
                bool game = Game::state(key) != Game::State::Idle;
                FindControl(APPLY)->SetEnabled(!busy && !checking && !game && !changes.empty() && errors == 0);
                bool cancellable = progress.phase == ModManager::Phase::Downloading || progress.phase == ModManager::Phase::Verifying;
                FindControl(CANCEL)->SetEnabled(busy && cancellable && !cancelling);
                UpdateText(CANCEL, cancelling ? L"正在取消…" : L"取消");
                std::wstring status = checking ? L"正在检查依赖…" : game ? L"请先关闭游戏再应用" : errors ? std::to_wstring(errors) + L" 个错误"
                        : warnings ? std::to_wstring(warnings) + L" 个警告" : L"";
                if (busy) status = progress.phase == ModManager::Phase::Verifying ? L"正在校验新文件…"
                        : progress.phase == ModManager::Phase::Replacing ? L"正在替换文件…" : L"正在下载…";
                UpdateText(CHANGE_STATUS, status);
                FindControl(CHANGE_STATUS)->SetTooltipText(status);
                auto& bar = Control<exui::ProgressBar>(PROGRESS);
                bool known = std::isfinite(progress.fraction) && progress.fraction >= 0;
                bar.SetIndeterminate(busy && !known);
                int position = known ? (int) (std::clamp(progress.fraction, 0.0f, 1.0f) * 1000) : 0;
                if (bar.GetPosition() != position) bar.SetPosition(position);
                layout(shown, busy);
            }

            void changeAction(uint32_t id) {
                refreshChanges();
                if (!FindControl(id)->IsEnabled()) return;
                const auto* chosen = chosenChange();
                if (id == UNDO && chosen && chosen->change) {
                    auto change = *chosen->change;
                    ModManager::unstage(key, change);
                } else if (id == ADD_DEPENDENCY && chosen && !chosen->missing.empty()) {
                    auto project = chosen->missing;
                    ModManager::addProject(key, project);
                } else if (id == DISCARD) ModManager::discard(key);
                else if (id == APPLY) ModManager::apply(key);
                else if (id == CANCEL) { ModManager::cancel(key); cancelling = true; }
            }

            Rows shown;
            // The mod of each row; empty for the row that says there is none.
            std::vector<std::string> shownKeys;

            std::vector<const Mods::Mod*> selected() const {
                std::vector<std::string> keys = selectedKeys(Control<exui::ListView>(LIST), shownKeys);
                std::vector<const Mods::Mod*> chosen;
                for (const Mods::Mod& mod : Mods::of(key).mods) {
                    if (std::find(keys.begin(), keys.end(), keyOf(mod.info.file)) != keys.end()) {
                        chosen.push_back(&mod);
                    }
                }
                return chosen;
            }

            // Turns all chosen mods off while any of them is on, and on otherwise.
            void toggle() {
                std::vector<std::filesystem::path> files;
                bool anyOn = false;
                for (const Mods::Mod* mod : selected()) {
                    files.push_back(mod->info.file);
                    anyOn = anyOn || mod->info.enabled;
                }
                for (const std::filesystem::path& file : files) {
                    Mods::setEnabled(key, file, !anyOn);
                }
            }

            void remove() {
                std::vector<std::filesystem::path> files;
                std::wstring name;
                for (const Mods::Mod* mod : selected()) {
                    files.push_back(mod->info.file);
                    name = Platform::widen(mod->info.name.empty() ? mod->info.file_name : mod->info.name);
                }
                if (files.empty()) {
                    return;
                }
                std::wstring question = files.size() == 1
                        ? L"要删除模组 " + name + L" 吗？"
                        : L"要删除选中的 " + std::to_wstring(files.size()) + L" 个模组吗？";
                if (message(GetHwnd(), question + L"\n文件会移到回收站。", L"模组管理", exui::MsgBoxButtons::OkCancel,
                            {L"取消", L"删除"}) != 0) {
                    return;
                }
                if (!canEditFiles()) return;
                for (const std::filesystem::path& file : files) {
                    Mods::remove(key, file);
                }
            }

            void dropped(HDROP drop) {
                std::vector<std::filesystem::path> files;
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; i++) {
                    UINT length = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring name(length, L'\0');
                    DragQueryFileW(drop, i, name.data(), length + 1);
                    if (looksLikeMod(name)) {
                        files.emplace_back(name);
                    }
                }
                DragFinish(drop);
                if (!canEditFiles()) return;
                if (files.empty()) {
                    message(GetHwnd(), L"拖进来的文件里没有模组（.jar）", L"模组管理");
                    return;
                }
                Mods::add(key, files, "mods", "模组");
            }
        };
    }

    void showMods(HWND parent, const std::string& instanceKey) {
        ModsWindow window(instanceKey);
        if (window.create(parent)) {
            runModal(window);
        }
    }
}
