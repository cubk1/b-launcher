#pragma once

#include <cstdint>
#include <string>

namespace Notifications {

    struct Style {
        int icon;
        uint32_t color;
    };

    extern const Style INFO;
    extern const Style DONE;
    extern const Style FAILED;

    void add(const Style& style, const std::string& text, long long durationMs);
    void keyed(const std::string& key, const Style& style, const std::string& text, long long durationMs);
    std::string live(const Style& style, const std::string& text);
    void update(const std::string& id, const std::string& text);
    void finish(const std::string& id, const std::string& text, long long durationMs);
    void finish(const std::string& id, const Style& style, const std::string& text, long long durationMs);

    // top stacks the items under the window buttons instead of above the bottom edge.
    void render(float layoutW, float layoutH, float bottomInset = 0.0f, bool top = false, float topInset = 0.0f);
    float clearance();
    float insetAbove(float layoutH, float top);
    bool active();
}
