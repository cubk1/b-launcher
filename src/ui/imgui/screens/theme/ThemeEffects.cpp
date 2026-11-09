#include "ui/imgui/screens/theme/ThemeEffects.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/theme/ColorMath.h"
#include "ui/imgui/theme/ThemeStyle.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace ThemeEffects {

    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr double LIMIT_MS = 10000.0;
        constexpr size_t MAX_PRESSES = 8;

        struct Press {
            float x = 0.0f;
            float y = 0.0f;
            Clock::time_point start;
            double held = -1.0;
        };

        struct Entry {
            std::vector<Press> presses;
            unsigned long long seen = 0;
        };

        std::unordered_map<std::string, Entry> entries;
        unsigned long long frameNumber = 0;
        long long lastSerial = 0;
        bool pressedNow = false;

        bool enabled() {
            return ThemeRuntime::effects() && ThemeStyle::animations() && ThemeRuntime::animationEnabled();
        }

        double millis(Clock::time_point from, Clock::time_point to) {
            return std::chrono::duration<double, std::milli>(to - from).count();
        }

        bool colorOf(const nlohmann::json& value, uint32_t& out) {
            if (value.is_number()) {
                out = (uint32_t) value.get<double>();
                return true;
            }
            return value.is_string() && ColorMath::parse(value.get<std::string>(), out);
        }

        bool numbers(const nlohmann::json& list, size_t count, float* out) {
            if (!list.is_array() || list.size() < count) {
                return false;
            }
            for (size_t i = 0; i < count; i++) {
                if (!list[i].is_number()) {
                    return false;
                }
                out[i] = list[i].get<float>();
            }
            return true;
        }

        void draw(const nlohmann::json& shapes, float x, float y, const ShaderUtil::Shape& clip, float opacity) {
            for (const nlohmann::json& item : shapes) {
                if (!item.is_object()) {
                    continue;
                }
                uint32_t color = 0;
                auto paint = item.find("color");
                if (paint == item.end() || !colorOf(*paint, color)) {
                    continue;
                }
                float box[4] = {};
                float radius = 0.0f;
                float values[5] = {};
                auto circle = item.find("circle");
                auto rect = item.find("rect");
                if (circle != item.end() && numbers(*circle, 3, values)) {
                    box[0] = values[0] - values[2];
                    box[1] = values[1] - values[2];
                    box[2] = values[2] * 2.0f;
                    box[3] = values[2] * 2.0f;
                    radius = values[2];
                } else if (rect != item.end() && numbers(*rect, 4, values)) {
                    std::copy(values, values + 4, box);
                    if (rect->size() > 4 && (*rect)[4].is_number()) {
                        radius = std::max(0.0f, (*rect)[4].get<float>());
                    }
                } else {
                    continue;
                }
                if (box[2] <= 0.0f || box[3] <= 0.0f) {
                    continue;
                }
                float corner = UiRender::transformW(std::min(radius, std::min(box[2], box[3]) * 0.5f));
                ShaderUtil::Shape shape = {{UiRender::transformX(x + box[0]), UiRender::transformY(y + box[1]),
                                            UiRender::transformW(box[2]), UiRender::transformW(box[3])},
                                           {corner, corner, corner, corner}};
                ShaderUtil::Paint fill;
                fill.kind = ShaderUtil::PAINT_SOLID;
                fill.count = 1;
                fill.colors[0] = color;
                fill.opacity = opacity;
                ShaderUtil::paint(shape, &clip, nullptr, fill);
            }
        }
    }

    void frame() {
        frameNumber++;
        pressedNow = NewUi::pressSerial != lastSerial;
        lastSerial = NewUi::pressSerial;
        if (!enabled()) {
            entries.clear();
            return;
        }
        Clock::time_point now = Clock::now();
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->second.seen + 2 < frameNumber) {
                it = entries.erase(it);
                continue;
            }
            for (Press& press : it->second.presses) {
                if (press.held < 0.0 && !NewUi::mouseDown) {
                    press.held = millis(press.start, now);
                }
            }
            ++it;
        }
    }

    void button(const std::string& id, const char* kind, float x, float y, float w, float h, float radius,
                int cornerMask, uint32_t ink, bool hovered) {
        if (!enabled()) {
            return;
        }
        Clock::time_point now = Clock::now();
        auto found = entries.find(id);
        if (pressedNow && hovered) {
            Entry& entry = entries[id];
            if (entry.presses.size() >= MAX_PRESSES) {
                entry.presses.erase(entry.presses.begin());
            }
            entry.presses.push_back({NewUi::mouseX - x, NewUi::mouseY - y, now, NewUi::mouseDown ? -1.0 : 0.0});
            found = entries.find(id);
        }
        if (found == entries.end()) {
            return;
        }
        Entry& entry = found->second;
        entry.seen = frameNumber;
        if (ShaderUtil::paintBroken()) {
            entry.presses.clear();
            return;
        }
        ShaderUtil::Shape clip =
                ShaderUtil::roundShape(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(w),
                                       UiRender::transformW(h), UiRender::transformW(radius), cornerMask);
        float opacity = std::clamp(UiRender::alphaMul, 0.0f, 1.0f);
        for (auto it = entry.presses.begin(); it != entry.presses.end();) {
            double age = millis(it->start, now);
            bool down = it->held < 0.0;
            nlohmann::json event = {{"kind", kind},
                                    {"width", w},
                                    {"height", h},
                                    {"radius", radius},
                                    {"x", it->x},
                                    {"y", it->y},
                                    {"age", age},
                                    {"down", down},
                                    {"held", down ? age : it->held},
                                    {"ink", ink},
                                    {"accent", UiTheme::accent()},
                                    {"light", UiTheme::isLight()}};
            std::optional<nlohmann::json> shapes = age < LIMIT_MS ? ThemeRuntime::press(event) : std::nullopt;
            if (!shapes || !shapes->is_array()) {
                it = entry.presses.erase(it);
                continue;
            }
            draw(*shapes, x, y, clip, opacity);
            ++it;
        }
        if (entry.presses.empty()) {
            entries.erase(found);
            return;
        }
        UiState::keepAnimating();
    }
}
