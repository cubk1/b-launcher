#pragma once

#include <core/base/progress.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Transfers {

    enum class Kind { Instance, Modpack, Java, Project, Mods, Launch, File, Theme };
    enum class State { Running, Done, Failed, Cancelled };

    struct Spec {
        Kind kind = Kind::File;
        std::string title;
        std::string minecraft;
        std::string cover;
        std::string icon;
        std::string instance;
        std::filesystem::path folder;
        std::function<void()> cancel;
    };

    struct Rate {
        uint64_t bytes = 0;
        double speed = 0.0;
        std::vector<float> history;
    };

    struct Task {
        uint64_t id = 0;
        Kind kind = Kind::File;
        std::string title;
        std::string minecraft;
        std::string cover;
        std::string icon;
        std::string instance;
        std::filesystem::path folder;
        bool cancellable = false;
        State state = State::Running;
        std::string stage;
        std::string error;
        uint64_t done = 0;
        uint64_t total = 0;
        uint64_t items = 0;
        uint64_t itemsTotal = 0;
        float fraction = -1.0f;
        Rate download;
        Rate read;
        Rate write;
        double started = 0.0;
        double ended = 0.0;

        float progress() const;
        double elapsed() const;
        double remaining() const;
    };

    uint64_t begin(Spec spec);
    void report(uint64_t id, const b_launcher_base::progress_event& event);
    void stage(uint64_t id, const std::string& text);
    void fraction(uint64_t id, float value);
    void io(uint64_t id, uint64_t read, uint64_t written);
    void finish(uint64_t id, State state, const std::string& error = {});
    void cancel(uint64_t id);
    void remove(uint64_t id);
    void clear();

    const std::vector<Task>& list();
    int running();
    double now();
    void poll();
}
