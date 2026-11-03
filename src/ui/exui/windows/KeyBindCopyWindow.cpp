#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/KeyBindCopyWindow.h"
#include "app/instances/Instances.h"
#include "app/launch/GameOptions.h"
#include "app/launch/KeyBinds.h"
#include "app/system/Platform.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"

#include <core/base/fs.hpp>
#include <core/base/strings.hpp>
#include <algorithm>
#include <tuple>

namespace ForgottenMelody {
    namespace {
        namespace blb = b_launcher_base;
        std::string pathKey(const std::filesystem::path& path) {
            return blb::to_lower(blb::path_to_utf8(blb::fs::absolute(path).lexically_normal()));
        }

        class KeyBindCopyWindow : public SectionedForm {
        public:
            enum : uint32_t { PAGE = FIRST_CONTROL, SOURCE, TARGETS, RESULT, COPY };
            explicit KeyBindCopyWindow(std::filesystem::path file) : source(std::move(file)) {}
            bool create(HWND parent) {
                Frame frame; frame.width = 740; frame.height = 520; frame.top = 65; frame.bottom = 465;
                frame.listWidth = 0; frame.done = L"取消";
                if (!createFrame(parent, L"复制按键绑定", L"", {{L"复制按键绑定", PAGE}}, frame)) return false;
                const exui::ControlSpec controls[] = {
                        {SOURCE, exui::ControlKind::Label, {}, L"来源：" + source.parent_path().filename().wstring() + L"/" + source.filename().wstring()},
                        {TARGETS, exui::ControlKind::List, {}},
                        {RESULT, exui::ControlKind::Label, {}},
                        {COPY, exui::ControlKind::Button, {}, L"复制所选"},
                };
                AddControls(controls);
                MoveInto(PAGE, {SOURCE, TARGETS, RESULT});
                FindControl(SOURCE)->SetTooltipText(source.wstring());
                Control<exui::Label>(SOURCE).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                auto& list = Control<exui::ListView>(TARGETS);
                list.SetSingleSelection(false);
                list.InsertColumn(L"实例 / 共用设置", 380);
                list.InsertColumn(L"版本", 150);
                list.InsertColumn(L"状态", 125);
                Control<exui::Label>(RESULT).SetMultiline(true);
                Control<exui::Label>(RESULT).SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                exui::Layout::Column({cell(SOURCE, 26), cell(TARGETS), cell(RESULT, 48)}, 8)
                        .Apply({15, 10, FindControl(PAGE)->GetWidth() - 30, FindControl(PAGE)->GetHeight() - 20});
                setFooterActions({exui::Layout::Row({}), cell(COPY, 100)});
                showSection(0); startRefreshing(); return true;
            }
        protected:
            void refresh() override {
                decltype(previousInput) input;
                for (const auto& entry : Instances::list()) {
                    auto file = GameOptions::fileOf(entry);
                    std::string label = entry.info.id;
                    if (!GameOptions::ownFile(entry)) {
                        const auto* directory = Instances::source(entry.source);
                        label = (directory ? directory->label : label) + " 共用设置";
                    }
                    input.emplace_back(std::move(file), std::move(label), entry.info.minecraft_version);
                }
                // File identity checks are needed when the target set changes, and once more before copying.
                if (reloadTargets || input != previousInput) {
                    reloadTargets = false;
                    previousInput = std::move(input);
                    destinations.clear(); targetRows.clear(); targetKeys.clear();
                    std::vector<std::filesystem::path> seen{source};
                    for (const auto& [file, label, version] : previousInput) {
                        std::string key = pathKey(file);
                        if (std::any_of(seen.begin(), seen.end(), [&](const auto& previous) {
                                return pathKey(previous) == key || blb::fs::same_file(previous, file);
                            })) continue;
                        seen.push_back(file);
                        targetRows.push_back({Platform::widen(label), Platform::widen(version), L""});
                        targetKeys.push_back(std::move(key));
                        destinations.push_back({file, KeyBinds::legacyVersion(version)});
                    }
                }
                Rows rows = targetRows;
                auto keys = targetKeys;
                for (size_t i = 0; i < destinations.size(); ++i)
                    rows[i][2] = GameOptions::inUse(destinations[i].file) ? L"游戏正在使用" : L"";
                bool empty = rows.empty();
                if (empty) { rows.push_back({L"没有其他可复制的设置文件", L"", L""}); keys.emplace_back(); }
                auto& list = Control<exui::ListView>(TARGETS);
                fill(list, shown, shownKeys, std::move(rows), std::move(keys));
                auto selected = list.GetSelectedRows();
                bool ready = !selected.empty() && !empty;
                for (int row : selected)
                    ready = ready && row >= 0 && row < (int) destinations.size() && !GameOptions::inUse(destinations[row].file);
                FindControl(COPY)->SetEnabled(ready);
            }
            void OnSelection(uint32_t id, int index) override {
                if (id == TARGETS) refresh();
                else SectionedForm::OnSelection(id, index);
            }
            void OnAction(uint32_t id) override {
                if (id != COPY) { SectionedForm::OnAction(id); return; }
                reloadTargets = true;
                refresh();
                if (!FindControl(COPY)->IsEnabled()) return;
                std::vector<KeyBinds::Target> chosen;
                for (int row : Control<exui::ListView>(TARGETS).GetSelectedRows()) chosen.push_back(destinations[row]);
                std::string error = KeyBinds::copy(source, chosen);
                if (error.empty()) { CloseWindow(); return; }
                UpdateText(RESULT, Platform::widen(error));
                FindControl(RESULT)->SetTooltipText(Platform::widen(error));
            }
        private:
            std::filesystem::path source;
            Rows shown;
            std::vector<std::string> shownKeys;
            std::vector<KeyBinds::Target> destinations;
            std::vector<std::tuple<std::filesystem::path, std::string, std::string>> previousInput;
            Rows targetRows;
            std::vector<std::string> targetKeys;
            bool reloadTargets = false;
        };
    }
    void showCopyKeyBinds(HWND parent, const std::filesystem::path& source) {
        KeyBindCopyWindow window(source);
        if (window.create(parent)) runModal(window);
    }
}
