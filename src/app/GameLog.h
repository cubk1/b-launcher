#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace GameLog {

    enum class Level : uint8_t { Fatal, Error, Warn, Info, Debug, Launcher };
    constexpr int LEVEL_COUNT = 6;

    constexpr size_t MAX_LINES = 100000;

    struct Line {
        std::string text;
        Level level = Level::Info;
    };

    void begin(const std::string& title);
    void feed(std::string_view line, bool error);
    void note(const std::string& text);

    void poll();
    const std::deque<Line>& lines();
    uint64_t first();
    uint64_t end();
    uint64_t generation();
    size_t count(Level level);
    const std::string& title();
    bool any();

    bool paused();
    void setPaused(bool paused);
    size_t held();
    uint64_t dropped();
    void clear();
}
