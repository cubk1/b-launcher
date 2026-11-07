#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/exui/windows/GameOptionsPanel.h"
#include "app/system/Platform.h"
#include "ui/exui/Layout.h"
#include "ui/exui/widgets/ComboBox.h"
#include "ui/exui/widgets/Label.h"
#include "ui/exui/widgets/Slider.h"

#include <algorithm>

namespace ForgottenMelody {
    GameOptionsPanel::GameOptionsPanel() {
        m_paints = true;
        for (size_t i = 0; i < GameOptions::OPTIONS.size(); ++i) {
            const auto& spec = GameOptions::OPTIONS[i];
            labels[i] = &Add<exui::Label>(0, 0, 0, 0, Platform::widen(spec.label));
            if (spec.id == GameOptions::Option::GuiScale) continue;
            auto* slider = &Add<exui::Slider>(); sliders[i] = slider;
            slider->SetId((uint32_t) i + 1);
            slider->SetRange(spec.low, spec.high);
            values[i] = &Add<exui::Label>();
            values[i]->SetAlignment(Gdiplus::StringAlignmentFar, Gdiplus::StringAlignmentCenter);
            slider->OnChanged = [this, i](int value) {
                values[i]->SetCaption(std::to_wstring(value) + Platform::widen(GameOptions::OPTIONS[i].unit));
            };
            slider->OnCommit = [this, id = spec.id](int value) { Save(id, value); };
        }
        scale = &Add<exui::ComboBox>(); scale->SetId(5);
        scale->OnSelectionChanged = [this](int index) {
            if (index >= 0 && index < (int) scales.size()) Save(GameOptions::Option::GuiScale, scales[index]);
        };
        status = &Add<exui::Label>();
        status->SetTextColor(0xD2C00000);
        status->SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        status->SetVisible(false);
    }

    void GameOptionsPanel::SetFile(std::filesystem::path value) {
        if (file == value) return;
        for (auto* slider : sliders) slider->CancelInteraction();
        file = std::move(value);
        error.clear();
        Refresh();
    }

    void GameOptionsPanel::Save(GameOptions::Option option, int value) {
        if (!IsEffectivelyVisible() || file.empty()) return;
        error = Platform::widen(GameOptions::setValue(file, option, value));
        Refresh();
    }

    void GameOptionsPanel::Refresh() {
        bool visible = IsEffectivelyVisible();
        bool running = !file.empty() && GameOptions::inUse(file);
        bool enabled = visible && !file.empty() && !running;
        for (size_t i = 0; i < sliders.size(); ++i) {
            auto& slider = *sliders[i];
            if (!enabled) slider.CancelInteraction();
            if (slider.IsEnabled() != enabled) slider.SetEnabled(enabled);
            if (!slider.IsDragging()) slider.SetPosition(GameOptions::value(file, GameOptions::OPTIONS[i].id));
            std::wstring text = std::to_wstring(slider.GetPosition()) + Platform::widen(GameOptions::OPTIONS[i].unit);
            if (values[i]->GetCaption() != text) values[i]->SetCaption(text);
        }
        int current = GameOptions::value(file, GameOptions::Option::GuiScale);
        scales = {0, 1, 2, 3, 4, 5, 6};
        if (current > 6) scales.push_back(current);
        std::vector<std::wstring> choices;
        for (int value : scales) choices.push_back(value == 0 ? L"自动" : std::to_wstring(value) + L"x");
        scale->SetItems(choices);
        scale->SetSelectedIndex((int) (std::find(scales.begin(), scales.end(), current) - scales.begin()));
        if (scale->IsEnabled() != enabled) scale->SetEnabled(enabled);
        std::wstring text = running ? L"游戏正在使用这些设置，退出游戏后才能修改" : error;
        if (status->GetCaption() != text) status->SetCaption(text);
        status->SetTooltipText(text);
        status->SetVisible(!text.empty());
        Arrange();
    }

    int GameOptionsPanel::PreferredHeight() const { return BASE_HEIGHT + (status->IsVisible() ? 30 : 0); }

    void GameOptionsPanel::Arrange() {
        using exui::Layout;
        std::vector<Layout> rows;
        for (size_t i = 0; i < sliders.size(); ++i)
            rows.push_back(Layout::Row({{*labels[i], 95}, {*sliders[i], 0, 26}, {*values[i], 75}}, 8, 30));
        rows.push_back(Layout::Row({{*labels.back(), 95}, {*scale, 200}, Layout::Row({})}, 8, 30));
        Layout::Column(std::move(rows), 7).Apply({0, 0, GetWidth(), BASE_HEIGHT});
        status->SetBounds(0, BASE_HEIGHT + 6, GetWidth(), 24);
    }
    void GameOptionsPanel::OnPaint(Gdiplus::Graphics&) { Arrange(); }
}
