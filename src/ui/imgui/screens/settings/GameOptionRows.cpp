#include "ui/imgui/screens/settings/GameOptionRows.h"
#include "app/launch/GameOptions.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/SliderWidget.h"

#include <charconv>
#include <memory>
#include <string_view>
#include <vector>

namespace GameOptionRows {

    namespace {
        constexpr float TRACK_W = 200.0f;
        constexpr float SELECT_W = 180.0f;
        constexpr float BAR_H = 4.0f;

        using Option = GameOptions::OptionSpec;

        void store(const std::filesystem::path& file, GameOptions::Option option, double value) {
            std::string problem = GameOptions::setValue(file, option, value);
            if (!problem.empty()) {
                Notifications::add(Notifications::FAILED, "无法修改游戏设置：" + problem, 4000);
            }
        }

        void slider(const std::string& id, const std::filesystem::path& file, const Option& option, float rowY,
                    bool blocked) {
            Page::label(option.label, rowY);
            std::string name = id + "#" + option.key;
            const Option* shown = &option;
            auto value = std::make_shared<NumberBinding>(
                    name, [file, shown] { return (double) GameOptions::value(file, shown->id); },
                    [file, shown](double picked) {
                        if (picked != GameOptions::value(file, shown->id)) store(file, shown->id, picked);
                    },
                    option.low, option.high, 1.0, option.unit);
            float trackX = Page::controlRight() - TRACK_W;
            SliderWidget widget(name, value);
            widget.compactTrack(trackX, rowY + (Page::ROW_H - BAR_H) * 0.5f, TRACK_W);
            widget.block(blocked);
            widget.paint();
        }

        void scale(const std::string& id, const std::filesystem::path& file, float rowY, bool blocked) {
            const auto& option = GameOptions::OPTIONS.back();
            Page::label(option.label, rowY);
            std::string name = id + "#guiScale";
            auto choice = std::make_shared<ModeBinding>(
                    name,
                    [file] {
                        int scale = GameOptions::value(file, GameOptions::Option::GuiScale);
                        return scale <= 0 ? std::string("自动") : std::to_string(scale) + "x";
                    },
                    [file](const std::string& picked) {
                        if (picked == "自动") { store(file, GameOptions::Option::GuiScale, 0); return; }
                        std::string_view text(picked);
                        if (text.ends_with('x')) text.remove_suffix(1);
                        int value = 0;
                        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                        if (error == std::errc() && end == text.data() + text.size())
                            store(file, GameOptions::Option::GuiScale, value);
                    },
                    [] { return std::vector<std::string>{"自动", "1x", "2x", "3x", "4x", "5x", "6x"}; });
            float x = Page::controlRight() - SELECT_W;
            SelectWidget widget(name, choice);
            widget.bounds(x, rowY + Page::FIELD_Y, SELECT_W, Page::FIELD_H).block(blocked);
            widget.paint();
        }
    }

    void caption(const std::string& title, const std::filesystem::path& file, const std::string& note, float y) {
        Page::caption(title, y);
        bool running = GameOptions::inUse(file);
        std::string text = running ? std::string("游戏正在使用这些设置，退出游戏后才能修改") : note;
        if (text.empty()) {
            return;
        }
        uint32_t color = running ? UiTheme::warning() : UiTheme::textMuted();
        float w = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        UiRender::textY(text, Page::right() - 2.0f - w, y + 2.0f, 16.0f, color, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
    }

    void draw(const std::string& id, const std::filesystem::path& file, float y, bool blocked) {
        bool running = GameOptions::inUse(file);
        float shade = UiState::motion(id + "#shade", running ? 0.45f : 1.0f, 16.0f);
        float savedAlpha = UiRender::alphaMul;
        UiRender::alphaMul = savedAlpha * shade;
        bool off = blocked || running;
        float rowY = y;
        for (const Option& option : GameOptions::OPTIONS) {
            if (option.id == GameOptions::Option::GuiScale) continue;
            slider(id, file, option, rowY, off);
            rowY += Page::ROW_H;
        }
        scale(id, file, rowY, off);
        UiRender::alphaMul = savedAlpha;
    }
}
