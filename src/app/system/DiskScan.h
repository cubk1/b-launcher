#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DiskScan {

    enum class State { Idle, Running, Done, Failed };
    enum class Kind { Java, Folders };

    struct Status {
        State state = State::Idle;
        Kind kind = Kind::Java;
        std::string error;
        std::string step;
        float progress = 0.0f;
        bool waiting = false;
        int volumes = 0;
        int javas = 0;
        std::vector<std::filesystem::path> folders;
    };

    struct Drive {
        wchar_t letter = 0;
        std::string label;
        uint64_t size = 0;
        bool removable = false;
    };

    struct Helper {
        std::wstring output;
        std::wstring drives;
        std::wstring cancelEvent;
    };

    const std::vector<Drive>& drives();
    bool drivesReady();
    bool driveSelected(wchar_t letter);
    void selectDrive(wchar_t letter, bool on);
    bool anySelected();

    void start(Kind kind);
    const Status& status();
    std::optional<Helper> helperArgument();
    int runHelper(const Helper& helper);
    void poll();
    void shutdown();
}
