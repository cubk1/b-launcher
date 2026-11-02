#include "ui/imgui/screens/settings/GameOptionRows.h"
#include "app/launch/GameOptions.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"
#include "ui/imgui/value/Bind.h"
#include "ui/imgui/widget/SelectWidget.h"
#include "ui/imgui/widget/SliderWidget.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace GameOptionRows {

    namespace {
        constexpr float TRACK_W = 200.0f;
        constexpr float SELECT_W = 180.0f;
        constexpr float BAR_H = 4.0f;

        struct Option {
            const char* key;
            const char* label;
            const char* doc;
            int low;
            int high;
            int fallback;
            const char* unit;
            int (*read)(double);
            std::string (*write)(int);
        };

        std::string decimal(double value) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.6f", value);
            std::string out = text;
            while (out.size() > 1 && out.back() == '0') {
                out.pop_back();
            }
            if (out.back() == '.') {
                out += '0';
            }
            return out == "-0.0" ? std::string("0.0") : out;
        }

        int rounded(double value) {
            return (int) std::lround(value);
        }

        const Option OPTIONS[] = {
                {"gamma", "亮度", "游戏里的亮度，0% 是昏暗，100% 是明亮。", 0, 100, 50, "%",
                 [](double v) { return rounded(v * 100.0); }, [](int v) { return decimal(v / 100.0); }},
                {"fov", "视野", "视野范围（FOV），默认 70，越大看到的范围越广。", 30, 110, 70, "",
                 [](double v) { return std::abs(v) <= 1.0001 ? rounded(70.0 + v * 40.0) : rounded(v); },
                 [](int v) { return decimal((v - 70) / 40.0); }},
                {"mouseSensitivity", "鼠标灵敏度", "鼠标转动视角的快慢，默认 100%。", 0, 200, 100, "%",
                 [](double v) { return rounded(v * 200.0); }, [](int v) { return decimal(v / 200.0); }},
                {"renderDistance", "渲染距离", "能看到多远的区块，默认 12，越大越占用性能。", 2, 32, 12, " 区块",
                 [](double v) { return rounded(v); }, [](int v) { return std::to_string(v); }},
        };

        std::optional<double> number(const std::optional<std::string>& text) {
            if (!text || text->empty()) {
                return std::nullopt;
            }
            double value = 0.0;
            auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
            if (error != std::errc()) {
                return std::nullopt;
            }
            return value;
        }

        int current(const std::filesystem::path& file, const Option& option) {
            std::optional<double> value = number(GameOptions::get(file, option.key));
            return value ? std::clamp(option.read(*value), option.low, option.high) : option.fallback;
        }

        void store(const std::filesystem::path& file, const std::string& key, const std::string& value) {
            std::string problem = GameOptions::set(file, key, value);
            if (!problem.empty()) {
                Notifications::add(Notifications::FAILED, "无法修改游戏设置：" + problem, 4000);
            }
        }

        void explain(const std::string& id, const char* text, float rowY, float labelRight, bool blocked) {
            float left = Page::columnLeft();
            if (!blocked && NewUi::hit(left, rowY, labelRight - left, Page::ROW_H)) {
                NewUi::markDoc(id, text, left + 6.0f, rowY + Page::ROW_H + 10.0f);
            }
        }

        void slider(const std::string& id, const std::filesystem::path& file, const Option& option, float rowY,
                    bool blocked) {
            Page::label(option.label, rowY);
            std::string name = id + "#" + option.key;
            const Option* shown = &option;
            auto value = std::make_shared<NumberBinding>(
                    name, [file, shown] { return (double) current(file, *shown); },
                    [file, shown](double picked) {
                        int next = rounded(picked);
                        if (next != current(file, *shown)) {
                            store(file, shown->key, shown->write(next));
                        }
                    },
                    option.low, option.high, 1.0, option.unit);
            float trackX = Page::controlRight() - TRACK_W;
            SliderWidget widget(name, value);
            widget.compactTrack(trackX, rowY + (Page::ROW_H - BAR_H) * 0.5f, TRACK_W);
            widget.block(blocked);
            widget.paint();
            explain(name, option.doc, rowY, trackX - 16.0f, blocked);
        }

        void scale(const std::string& id, const std::filesystem::path& file, float rowY, bool blocked) {
            Page::label("界面尺寸", rowY);
            std::string name = id + "#guiScale";
            auto choice = std::make_shared<ModeBinding>(
                    name,
                    [file] {
                        std::optional<double> value = number(GameOptions::get(file, "guiScale"));
                        int scale = value ? rounded(*value) : 0;
                        return scale <= 0 ? std::string("自动") : std::to_string(scale) + "x";
                    },
                    [file](const std::string& picked) {
                        store(file, "guiScale", picked == "自动" ? "0" : picked.substr(0, picked.find('x')));
                    },
                    [] { return std::vector<std::string>{"自动", "1x", "2x", "3x", "4x", "5x", "6x"}; });
            float x = Page::controlRight() - SELECT_W;
            SelectWidget widget(name, choice);
            widget.bounds(x, rowY + Page::FIELD_Y, SELECT_W, Page::FIELD_H).block(blocked);
            widget.paint();
            explain(name, "游戏界面的缩放倍数，自动时随窗口大小变化；超过窗口能容纳的倍数时游戏会自动调小。", rowY,
                    x - 8.0f, blocked);
        }
    }

    void caption(const std::string& title, const std::filesystem::path& file, const std::string& note, float y) {
        Page::caption(title, y);
        bool running = GameOptions::inUse(file);
        std::string text = running ? std::string("游戏正在使用这些设置，退出游戏后才能修改") : note;
        if (text.empty()) {
            return;
        }
        uint32_t color = running ? (UiTheme::isLight() ? 0xFFC27803u : 0xFFE8A33Du) : UiTheme::textMuted();
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
        for (const Option& option : OPTIONS) {
            slider(id, file, option, rowY, off);
            rowY += Page::ROW_H;
        }
        scale(id, file, rowY, off);
        UiRender::alphaMul = savedAlpha;
    }
}
