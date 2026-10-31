#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace GameLog {

    enum class Level : uint8_t { Fatal, Error, Warn, Info, Debug, Launcher };
    constexpr int LEVEL_COUNT = 6;

    constexpr size_t MAX_LINES = 100000;
    constexpr size_t MAX_LOGS = 8;

    struct Line {
        std::string text;
        Level level = Level::Info;
    };

    struct Tab {
        int id = 0;
        std::string title;
    };

    int begin(const std::string& title, int replace = 0);
    void feed(int log, std::string_view line, bool error);
    void note(int log, const std::string& text);
    void finish(int log);

    void poll();
    std::vector<Tab> tabs();
    int shown();
    void show(int log);
    void close(int log);

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
