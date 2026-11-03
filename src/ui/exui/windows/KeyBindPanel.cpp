#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/KeyBindPanel.h"
#include "app/instances/Instances.h"
#include "app/launch/GameOptions.h"
#include "app/launch/KeyBinds.h"
#include "app/system/Platform.h"
#include "ui/exui/widgets/Button.h"
#include "ui/exui/widgets/CheckRadio.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/EditBox.h"
#include "ui/exui/widgets/KeyBoard.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/ListView.h"
#include "ui/exui/windows/Dialogs.h"
#include "ui/exui/windows/KeyBindCopyWindow.h"

#include <core/base/strings.hpp>
#include <algorithm>
#include <map>
#include <utility>

namespace ForgottenMelody {
    namespace {
        namespace blb = b_launcher_base;
        std::string keyText(const KeyBinds::Binding& binding) {
            std::string modifier = KeyBinds::modifierLabel(binding.modifier);
            return (modifier.empty() ? "" : modifier + " + ") + KeyBinds::keyLabel(binding.key);
        }
        bool inside(const exui::Widget& widget, POINT point) {
            auto origin = widget.GetWindowOrigin();
            return widget.IsEffectivelyVisible() && widget.Contains(point.x - origin.x, point.y - origin.y);
        }
    }

    KeyBindPanel::KeyBindPanel() {
        m_paints = true;
        device = &Add<exui::ComboBox>(); device->SetId(DEVICE);
        group = &Add<exui::ComboBox>(); group->SetId(GROUP);
        search = &Add<exui::EditBox>(); search->SetId(SEARCH);
        searchLabel = &Add<exui::Label>(0, 0, 0, 0, L"搜索");
        allKeys = &Add<exui::Button>(0, 0, 0, 0, L"全部按键"); allKeys->SetId(ALL_KEYS);
        board = &Add<exui::KeyBoard>(); board->SetId(BOARD); board->SetFont(L"Segoe UI", 10);
        bindings = &Add<exui::ListView>(); bindings->SetId(BINDINGS);
        change = &Add<exui::Button>(0, 0, 0, 0, L"更改按键"); change->SetId(CHANGE);
        unbind = &Add<exui::Button>(0, 0, 0, 0, L"解除绑定"); unbind->SetId(UNBIND);
        cancel = &Add<exui::Button>(0, 0, 0, 0, L"取消录入"); cancel->SetId(CANCEL);
        status = &Add<exui::Label>(); status->SetId(STATUS);
        copy = &Add<exui::Button>(0, 0, 0, 0, L"复制到…"); copy->SetId(COPY);
        onlyConflicts = &Add<exui::CheckBox>(0, 0, 0, 0, L"仅冲突"); onlyConflicts->SetId(CONFLICTS);
        status->SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        search->SetTooltipText(L"搜索操作、分类或按键");
        device->SetItems({L"键盘", L"鼠标"}); device->SetSelectedIndex(0);
        device->OnSelectionChanged = [this](int index) { CancelCapture(); board->SetMouse(index == 1); keyFilter.clear(); Refresh(); };
        group->OnSelectionChanged = [this](int index) {
            CancelCapture();
            groupKey = index >= 0 && index < (int) groupKeys.size() ? groupKeys[index] : "";
            keyFilter.clear(); Refresh();
        };
        search->OnTextChanged = [this](const std::wstring&) { CancelCapture(); Refresh(); };
        allKeys->OnClick = [this] { CancelCapture(); keyFilter.clear(); Refresh(); };
        board->OnPick = [this](const std::string& key) {
            if (!capturing.empty()) Assign(key);
            else { keyFilter = keyFilter == key ? "" : key; Refresh(); }
        };
        bindings->InsertColumn(L"操作", 260);
        bindings->InsertColumn(L"按键", 155);
        bindings->InsertColumn(L"分类", 210);
        bindings->InsertColumn(L"状态", 55);
        bindings->OnSelectionChanged = [this](int) { CancelCapture(); notice.clear(); Refresh(); };
        change->OnClick = [this] { Capture(); };
        unbind->OnClick = [this] { capturing = Selected(); Assign(KeyBinds::UNBOUND); };
        cancel->OnClick = [this] { CancelCapture(); Refresh(); };
        onlyConflicts->OnClick = [this] { CancelCapture(); Refresh(); };
        copy->OnClick = [this] {
            auto keep = weak_from_this().lock();
            CancelCapture();
            if (!file.empty()) showCopyKeyBinds(GetWindow()->GetHwnd(), file);
            if (GetWindow()) Refresh();
        };
        cancel->SetVisible(false);
    }

    void KeyBindPanel::SetFile(std::filesystem::path value, bool old) {
        if (file == value && legacy == old) return;
        file = std::move(value); legacy = old;
        CancelCapture(); groupKey.clear(); keyFilter.clear(); notice.clear();
        rowKeys.clear(); shown.clear(); bindings->ClearRows();
        Refresh();
    }

    std::string KeyBindPanel::Selected() const {
        int row = bindings->GetSelectedRow();
        return row >= 0 && row < (int) rowKeys.size() ? rowKeys[row] : "";
    }

    void KeyBindPanel::CancelCapture() { capturing.clear(); }

    void KeyBindPanel::Capture() {
        if (file.empty() || GameOptions::inUse(file)) return;
        capturing = Selected(); notice.clear();
        if (!capturing.empty()) board->Focus();
        Refresh();
    }

    void KeyBindPanel::Assign(const std::string& key) {
        std::string id = std::exchange(capturing, {});
        if (file.empty() || id.empty()) return;
        const auto& table = KeyBinds::table(file);
        if (std::none_of(table.bindings.begin(), table.bindings.end(), [&](const auto& binding) { return binding.id == id; })) {
            Refresh(); return;
        }
        notice = KeyBinds::assign(file, id, key, legacy);
        if (notice.empty()) notice = "已保存";
        Refresh();
    }

    void KeyBindPanel::Refresh() {
        if (!IsEffectivelyVisible()) { CancelCapture(); return; }
        bool available = !file.empty();
        static const KeyBinds::Table empty;
        const auto& table = available ? KeyBinds::table(file) : empty;
        FindConflicts(table);
        bool locked = available && GameOptions::inUse(file);
        if (locked) CancelCapture();
        std::vector<std::wstring> labels{L"全部分类"};
        groupKeys = {""};
        for (const auto& item : table.groups) { labels.push_back(Platform::widen(item.label)); groupKeys.push_back(item.id); }
        auto chosen = std::find(groupKeys.begin(), groupKeys.end(), groupKey);
        if (chosen == groupKeys.end()) { groupKey.clear(); chosen = groupKeys.begin(); }
        group->SetItems(labels); group->SetSelectedIndex((int) (chosen - groupKeys.begin()));
        std::string query = trimmed(search->GetText()), selected = Selected(), selectedKey;
        std::vector<std::vector<std::wstring>> rows;
        std::vector<std::string> keys;
        std::map<std::string, int> counts;
        for (const auto& binding : table.bindings) {
            if (binding.id == selected) selectedKey = binding.key;
            if (!groupKey.empty() && binding.group != groupKey) continue;
            bool conflict = conflicts.contains(binding.id);
            if (KeyBinds::bound(binding.key)) {
                auto& count = counts[binding.key];
                if (conflict) count = -1;
                else if (count >= 0) ++count;
            }
            if (onlyConflicts->IsChecked() && !conflict) continue;
            if ((!keyFilter.empty() && binding.key != keyFilter) ||
                (!query.empty() && !blb::icontains(binding.name, query) && !blb::icontains(binding.id, query) &&
                 !blb::icontains(binding.category, query) && !blb::icontains(keyText(binding), query))) continue;
            rows.push_back({Platform::widen(binding.name), Platform::widen(keyText(binding)), Platform::widen(binding.category), conflict ? L"冲突" : L""});
            keys.push_back(binding.id);
        }
        bool any = !keys.empty();
        if (!any) { rows.push_back({!available ? L"还没有游戏目录" : table.bindings.empty() ? L"暂无按键记录，请先运行一次游戏" : L"没有匹配的按键", L"", L"", L""}); keys.emplace_back(); }
        bool changed = keys != rowKeys;
        if (LiveForm::fill(*bindings, shown, std::move(rows)) || changed) {
            rowKeys = std::move(keys);
            auto at = std::find(rowKeys.begin(), rowKeys.end(), selected);
            bindings->SetSelectedRow(!selected.empty() && at != rowKeys.end() ? (int) (at - rowKeys.begin()) : -1);
        }
        board->SetKeys(std::move(counts), keyFilter.empty() ? selectedKey : keyFilter);
        bool capture = !capturing.empty();
        change->SetEnabled(available && !locked && !Selected().empty() && !capture);
        unbind->SetEnabled(change->IsEnabled());
        cancel->SetVisible(capture);
        allKeys->SetEnabled(!keyFilter.empty());
        copy->SetEnabled(available && !table.bindings.empty() && !capture);
        std::wstring text = locked ? L"游戏正在使用这些设置"
                : capture ? L"请按键或点键帽，Esc 解除绑定"
                : !notice.empty() ? Platform::widen(notice)
                : table.naming ? L"正在读取模组按键名称…"
                : conflicts.contains(Selected()) ? L"此按键与其他操作冲突"
                : !keyFilter.empty() ? L"按键：" + Platform::widen(KeyBinds::keyLabel(keyFilter)) : L"";
        if (status->GetCaption() != text) status->SetCaption(text);
        status->SetTooltipText(text.empty() ? file.wstring() : text);
        Arrange();
    }

    void KeyBindPanel::FindConflicts(const KeyBinds::Table& table) {
        decltype(conflictInput) input;
        for (const auto& binding : table.bindings) input.emplace_back(binding.id, binding.key, binding.modifier, binding.group);
        if (input == conflictInput) return;
        conflictInput = std::move(input);
        conflicts.clear();
        for (size_t i = 0; i < table.bindings.size(); ++i) {
            for (size_t j = i + 1; j < table.bindings.size(); ++j) {
                if (KeyBinds::conflicts(table.bindings[i], table.bindings[j])) {
                    conflicts.insert(table.bindings[i].id);
                    conflicts.insert(table.bindings[j].id);
                }
            }
        }
    }

    bool KeyBindPanel::HandleInput(UINT msg, WPARAM wp, LPARAM lp) {
        if (!IsEffectivelyVisible()) { CancelCapture(); return false; }
        if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE)) { CancelCapture(); Refresh(); return false; }
        if (capturing.empty()) return false;
        if (file.empty() || GameOptions::inUse(file)) { CancelCapture(); Refresh(); return false; }
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            if (lp & (1LL << 30)) return true;
            std::string key = wp == VK_ESCAPE ? KeyBinds::UNBOUND : exui::KeyBoard::KeyOf((UINT) wp, lp);
            if (!key.empty()) Assign(key);
            return true;
        }
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) {
            POINT point{};
            point.x = (int) (static_cast<short>(LOWORD(lp)) / GetWindow()->GetScale());
            point.y = (int) (static_cast<short>(HIWORD(lp)) / GetWindow()->GetScale());
            if (!inside(*this, point)) { CancelCapture(); Refresh(); return false; }
            if (inside(*cancel, point) || (msg == WM_LBUTTONDOWN && inside(*board, point))) return false;
            Assign(msg == WM_LBUTTONDOWN ? "key.mouse.left" : msg == WM_RBUTTONDOWN ? "key.mouse.right"
                    : msg == WM_MBUTTONDOWN ? "key.mouse.middle" : HIWORD(wp) == XBUTTON1 ? "key.mouse.4" : "key.mouse.5");
            return true;
        }
        return msg == WM_CHAR || msg == WM_SYSCHAR;
    }

    void KeyBindPanel::Arrange() {
        using exui::Layout;
        int width = GetWidth(), height = GetHeight();
        int boardHeight = std::min(190, (int) ((width - 12) / 23.0 * 6.25));
        Layout::Row({{*device, 70}, {*group, 125}, {*searchLabel, 28}, {*search}, {*onlyConflicts, 74}, {*allKeys, 80}}, 6).Apply({0, 0, width, 28});
        board->SetBounds(0, 34, width, boardHeight);
        bindings->SetBounds(0, 40 + boardHeight, width, std::max(50, height - boardHeight - 78));
        int columns = std::max(0, width - 16);
        bindings->SetColumnWidth(0, columns * 40 / 100);
        bindings->SetColumnWidth(1, columns * 26 / 100);
        bindings->SetColumnWidth(2, columns - columns * 40 / 100 - columns * 26 / 100 - 50);
        bindings->SetColumnWidth(3, 50);
        Layout::Row({{*status}, {*change, 90}, {*unbind, 80}, {*copy, 80}}, 6).Apply({0, height - 30, width, 30});
        cancel->SetBounds(width - 90, height - 30, 90, 30);
        change->SetVisible(capturing.empty()); unbind->SetVisible(capturing.empty());
        copy->SetVisible(capturing.empty());
    }
    void KeyBindPanel::OnPaint(Gdiplus::Graphics&) { Arrange(); }

    namespace {
        class KeyBindWindow : public SectionedForm {
        public:
            explicit KeyBindWindow(std::string key) : instance(std::move(key)) {}
            bool create(HWND parent) {
                Frame frame; frame.width = 940; frame.height = 680; frame.bottom = 625; frame.listWidth = 0;
                if (!createFrame(parent, L"按键绑定", L"", {{L"按键绑定", FIRST_CONTROL}}, frame)) return false;
                auto& panel = *FindControl(FIRST_CONTROL);
                editor = &panel.Add<KeyBindPanel>();
                editor->SetBounds(15, 12, panel.GetWidth() - 30, panel.GetHeight() - 24);
                showSection(0); startRefreshing(); return true;
            }
        protected:
            void refresh() override {
                const auto* entry = Instances::find(instance);
                if (!entry) { CloseWindow(); return; }
                setSubject(Platform::widen(entry->info.id));
                editor->SetFile(GameOptions::fileOf(*entry), KeyBinds::legacyVersion(entry->info.minecraft_version));
                editor->Refresh();
            }
            LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp) override {
                if (editor && editor->HandleInput(msg, wp, lp)) return 0;
                return SectionedForm::HandleMessage(msg, wp, lp);
            }
        private:
            std::string instance;
            KeyBindPanel* editor = nullptr;
        };
    }
    void showKeyBinds(HWND parent, const std::string& instance) {
        KeyBindWindow window(instance);
        if (window.create(parent)) runModal(window);
    }
}
