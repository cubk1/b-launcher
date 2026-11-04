#pragma once

#include "ui/exui/Widget.h"
#include "app/launch/GameOptions.h"

namespace exui { class Slider; class ComboBox; class Label; }

namespace ForgottenMelody {
    // Shared rows for a directory's options.txt and an instance's private options file.
    class GameOptionsPanel : public exui::Panel {
    public:
        static constexpr int BASE_HEIGHT = 178;
        GameOptionsPanel();
        void SetFile(std::filesystem::path file);
        void Refresh();
        int PreferredHeight() const;
        void OnPaint(Gdiplus::Graphics&) override;

    private:
        std::array<exui::Label*, 5> labels{};
        std::array<exui::Slider*, 4> sliders{};
        std::array<exui::Label*, 4> values{};
        exui::ComboBox* scale;
        exui::Label* status;
        std::vector<int> scales;
        std::filesystem::path file;
        std::wstring error;
        void Save(GameOptions::Option option, int value);
        void Arrange();
    };
}
