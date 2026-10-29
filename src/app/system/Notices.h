#pragma once

#include <functional>
#include <string>

namespace Notices {

    enum class Kind { Info, Done, Failed };

    struct Display {
        std::function<void(Kind kind, const std::string& text, long long durationMs)> add;
        std::function<std::string(Kind kind, const std::string& text)> live;
        std::function<void(const std::string& id, Kind kind, const std::string& text, long long durationMs)> finish;
    };

    void show(Display display);
    void add(Kind kind, const std::string& text, long long durationMs);
    std::string live(Kind kind, const std::string& text);
    void finish(const std::string& id, Kind kind, const std::string& text, long long durationMs);
}
