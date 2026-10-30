#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/ModsWindow.h"
#include "app/instances/Mods.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"

#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace ForgottenMelody {

    namespace {
        using exui::ControlKind;

        bool contains(const std::string& text, const std::string& query) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; };
            return std::search(text.begin(), text.end(), query.begin(), query.end(),
                               [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

        // Like the default interface: by name, id or file name.
        bool matches(const Mods::Mod& mod, const std::string& query) {
            return query.empty() || contains(mod.info.name, query) || contains(mod.info.id, query) ||
                    contains(mod.info.file_name, query);
        }

        bool endsWith(const std::wstring& text, const std::wstring& suffix) {
            return text.size() >= suffix.size() &&
                    text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::wstring lowered(std::wstring text) {
            for (wchar_t& c : text) {
                c = (wchar_t) std::towlower(c);
            }
            return text;
        }

        // A mod keeps its key while it is turned off and on, which renames its file to and from .disabled.
        std::string keyOf(const std::filesystem::path& file) {
            std::wstring text = file.wstring();
            if (endsWith(lowered(text), L".disabled")) {
                text.resize(text.size() - 9);
            }
            return Platform::narrow(text);
        }

        // What the default interface takes as a mod when files are dropped on it.
        bool looksLikeMod(const std::filesystem::path& file) {
            std::wstring name = lowered(file.filename().wstring());
            return endsWith(name, L".jar") || endsWith(name, L".jar.disabled") || endsWith(name, L".litemod");
        }

        // The default interface's tags, in words.
        std::wstring stateOf(const Mods::Mod& mod) {
            std::wstring state;
            auto add = [&state](const std::wstring& part) { state += (state.empty() ? L"" : L"，") + part; };
            if (!mod.info.problem.empty()) {
                add(L"无法读取：" + Platform::widen(mod.info.problem));
            }
            if (!mod.missing.empty()) {
                std::string missing;
                for (const std::string& item : mod.missing) {
                    missing += (missing.empty() ? "" : "、") + item;
                }
                add(L"缺少前置：" + Platform::widen(missing));
            }
            if (!mod.info.enabled) {
                add(L"已禁用");
            }
            return state;
        }

        // The default interface's mod page in the look of the instance manager: the mods above, the search and what
        // to do with the chosen ones below. Several can be chosen at once, and .jar files dropped on it are added.
        class ModsWindow : public LiveForm {
        public:
            enum : uint32_t { LIST = 1, STATUS, SEARCH_LABEL, SEARCH, ADD, FOLDER, RESCAN, HOMEPAGE, TOGGLE, REMOVE };

            explicit ModsWindow(std::string instance) : key(std::move(instance)) {}

            bool create(HWND parent) {
                static const exui::ControlSpec CONTROLS[] = {
                        {LIST, ControlKind::List, {18, 35, 684, 330}},
                        {STATUS, ControlKind::Label, {20, 370, 680, 26}},
                        {SEARCH_LABEL, ControlKind::Label, {18, 406, 32, 26}, L"搜索"},
                        {SEARCH, ControlKind::Edit, {52, 406, 120, 26}},
                        {ADD, ControlKind::Button, {180, 402, 70, 35}, L"添加"},
                        {FOLDER, ControlKind::Button, {256, 402, 90, 35}, L"打开文件夹"},
                        {RESCAN, ControlKind::Button, {352, 402, 60, 35}, L"刷新"},
                        {HOMEPAGE, ControlKind::Button, {472, 402, 70, 35}, L"主页"},
                        {TOGGLE, ControlKind::Button, {548, 402, 76, 35}, L"禁用"},
                        {REMOVE, ControlKind::Button, {630, 402, 72, 35}, L"删除"},
                };
                if (!createDialog(*this, parent, L"模组管理", 720, 450)) {
                    return false;
                }
                AddControls(CONTROLS);
                auto& list = Control<exui::ListView>(LIST);
                list.SetSingleSelection(false);
                list.InsertColumn(L"名称", 200);
                list.InsertColumn(L"版本", 110);
                list.InsertColumn(L"状态", 150);
                list.InsertColumn(L"文件", 208);
                Control<exui::Label>(STATUS).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                Control<exui::EditBox>(SEARCH).OnTextChanged = [this](const std::wstring&) { refresh(); };
                FindControl(SEARCH)->SetTooltipText(L"按名称、ID 或文件名搜索");
                FindControl(FOLDER)->SetTooltipText(L"打开实例的 mods 文件夹");
                FindControl(HOMEPAGE)->SetTooltipText(L"打开模组的主页");
                FindControl(REMOVE)->SetTooltipText(L"移到回收站");
                DragAcceptFiles(GetHwnd(), TRUE);
                // Like the default interface, the folder is read again on the way in.
                Mods::rescan(key);
                startRefreshing();
                return true;
            }

        protected:
            void refresh() override {
                const Mods::List& mods = Mods::of(key);
                std::string query = trimmed(Text(SEARCH));
                Rows rows;
                std::vector<std::string> keys;
                int enabled = 0, lacking = 0;
                for (const Mods::Mod& mod : mods.mods) {
                    enabled += mod.info.enabled ? 1 : 0;
                    lacking += mod.missing.empty() ? 0 : 1;
                    if (!matches(mod, query)) {
                        continue;
                    }
                    const std::string& name = mod.info.name.empty() ? mod.info.file_name : mod.info.name;
                    rows.push_back({Platform::widen(name), Platform::widen(mod.info.version), stateOf(mod),
                                    Platform::widen(mod.info.file_name)});
                    keys.push_back(keyOf(mod.info.file));
                }
                if (rows.empty()) {
                    std::wstring none = !mods.mods.empty() ? L"没有找到“" + Platform::widen(query) + L"”"
                            : mods.loading                 ? L"正在读取…"
                                                           : L"还没有模组";
                    rows.push_back({none, L"", L"", L""});
                    keys.emplace_back();
                }
                fill(Control<exui::ListView>(LIST), shown, shownKeys, std::move(rows), std::move(keys));

                std::wstring status;
                if (mods.mods.empty()) {
                    status = mods.loading ? L"正在读取模组…" : L"点击“添加”或把 .jar 文件拖进窗口来添加模组";
                } else {
                    status = L"共 " + std::to_wstring(mods.mods.size()) + L" 个，已启用 " + std::to_wstring(enabled) +
                            L" 个";
                    if (lacking > 0) {
                        status += L"，" + std::to_wstring(lacking) + L" 个缺少前置";
                    }
                }
                setText(STATUS, status);

                std::vector<const Mods::Mod*> chosen = selected();
                bool anyOn = std::any_of(chosen.begin(), chosen.end(),
                                         [](const Mods::Mod* mod) { return mod->info.enabled; });
                setText(TOGGLE, anyOn || chosen.empty() ? L"禁用" : L"启用");
                FindControl(TOGGLE)->SetEnabled(!chosen.empty());
                FindControl(REMOVE)->SetEnabled(!chosen.empty());
                FindControl(HOMEPAGE)->SetEnabled(chosen.size() == 1 && chosen[0]->info.homepage.rfind("http", 0) == 0);
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
                switch (id) {
                    case ADD: {
                        std::vector<std::filesystem::path> files =
                                Platform::pickFiles(Mods::folder(key, "mods"), L"添加模组", L"模组 (*.jar)", L"*.jar");
                        Mods::add(key, files, "mods", "模组");
                        break;
                    }
                    case FOLDER:
                        if (std::filesystem::path folder = Mods::folder(key, "mods"); !folder.empty()) {
                            std::error_code error;
                            std::filesystem::create_directories(folder, error);
                            Platform::open(folder);
                        }
                        break;
                    case RESCAN: Mods::rescan(key); break;
                    case HOMEPAGE:
                        if (std::vector<const Mods::Mod*> chosen = selected(); chosen.size() == 1) {
                            Platform::openUrl(chosen[0]->info.homepage);
                        }
                        break;
                    case TOGGLE: toggle(); break;
                    case REMOVE: remove(); break;
                }
                refresh();
            }

            void OnSelection(uint32_t, int) override {
                refresh();
            }

        private:
            std::string key;
            Rows shown;
            // The mod of each row; empty for the row that says there is none.
            std::vector<std::string> shownKeys;

            void setText(uint32_t id, const std::wstring& text) {
                if (Text(id) != text) {
                    SetText(id, text);
                }
            }

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
