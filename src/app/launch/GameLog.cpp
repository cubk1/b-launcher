#include "app/launch/GameLog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <vector>

namespace GameLog {

    namespace {

        struct Event {
            bool open = false;
            std::string text;
        };

        struct Log {
            int id = 0;
            std::string name;
            Event events[2];
            bool xml[2] = {false, false};
            Level lastLevel[2] = {Level::Info, Level::Info};
            std::vector<Line> pending;
            uint64_t pendingDropped = 0;
            std::deque<Line> kept;
            std::vector<Line> waiting;
            uint64_t firstNumber = 0;
            uint64_t droppedCount = 0;
            size_t counts[LEVEL_COUNT] = {};
            bool paused = false;
            bool live = true;
        };

        std::mutex lock;
        std::vector<std::shared_ptr<Log>> logs;
        std::shared_ptr<Log> current;
        int nextId = 1;
        uint64_t generationNumber = 0;
        const std::deque<Line> nothing;
        const std::string unnamed;

        std::shared_ptr<Log> find(int id) {
            for (const std::shared_ptr<Log>& log : logs) {
                if (log->id == id) {
                    return log;
                }
            }
            return nullptr;
        }

        bool validUtf8(std::string_view text) {
            size_t i = 0;
            size_t n = text.size();
            while (i < n) {
                unsigned char c = (unsigned char) text[i];
                int more;
                if (c < 0x80) {
                    more = 0;
                } else if ((c >> 5) == 0x6) {
                    more = 1;
                } else if ((c >> 4) == 0xE) {
                    more = 2;
                } else if ((c >> 3) == 0x1E) {
                    more = 3;
                } else {
                    return false;
                }
                if (i + more >= n && more > 0) {
                    return false;
                }
                for (int k = 1; k <= more; k++) {
                    if (((unsigned char) text[i + k] >> 6) != 0x2) {
                        return false;
                    }
                }
                i += more + 1;
            }
            return true;
        }

        std::string toUtf8(std::string_view text) {
            if (validUtf8(text)) {
                return std::string(text);
            }
            int wide = MultiByteToWideChar(CP_ACP, 0, text.data(), (int) text.size(), nullptr, 0);
            if (wide <= 0) {
                return std::string(text);
            }
            std::wstring buffer(wide, L'\0');
            MultiByteToWideChar(CP_ACP, 0, text.data(), (int) text.size(), buffer.data(), wide);
            int size = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, nullptr, 0, nullptr, nullptr);
            std::string out(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, out.data(), size, nullptr, nullptr);
            return out;
        }

        bool startsWith(std::string_view text, std::string_view prefix) {
            return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
        }

        bool levelIn(std::string_view text, Level& level) {
            std::string_view head = text.substr(0, std::min<size_t>(text.size(), 160));
            struct Name {
                const char* text;
                Level level;
            };
            static const Name names[] = {
                    {"/FATAL]", Level::Fatal},  {"/ERROR]", Level::Error}, {"/WARN]", Level::Warn},
                    {"/INFO]", Level::Info},    {"/DEBUG]", Level::Debug}, {"/TRACE]", Level::Debug},
                    {"[FATAL]", Level::Fatal},  {"[ERROR]", Level::Error}, {"[WARN]", Level::Warn},
                    {"[WARNING]", Level::Warn}, {"[INFO]", Level::Info},   {"[DEBUG]", Level::Debug},
            };
            for (const Name& name : names) {
                if (head.find(name.text) != std::string_view::npos) {
                    level = name.level;
                    return true;
                }
            }
            return false;
        }

        bool continuation(std::string_view text) {
            if (text.empty()) {
                return false;
            }
            if (text[0] == '\t' || startsWith(text, "    at ") || startsWith(text, "Caused by: ") ||
                startsWith(text, "\tat ")) {
                return true;
            }
            size_t i = text.find_first_not_of(' ');
            return i != std::string_view::npos && i > 0 &&
                    (startsWith(text.substr(i), "at ") || startsWith(text.substr(i), "... "));
        }

        void push(Log& log, std::string text, Level level) {
            log.pending.push_back(Line{std::move(text), level});
            if (log.pending.size() > MAX_LINES + MAX_LINES / 4) {
                size_t extra = log.pending.size() - MAX_LINES;
                log.pending.erase(log.pending.begin(), log.pending.begin() + (ptrdiff_t) extra);
                log.pendingDropped += extra;
            }
        }

        void plain(Log& log, std::string_view raw, int stream) {
            std::string text = toUtf8(raw);
            Level level;
            if (!levelIn(text, level)) {
                level = continuation(text) ? log.lastLevel[stream] : (stream == 1 ? Level::Warn : Level::Info);
            }
            log.lastLevel[stream] = level;
            std::lock_guard<std::mutex> guard(lock);
            push(log, std::move(text), level);
        }

        std::string attribute(const std::string& event, const char* name) {
            std::string key = std::string(" ") + name + "=\"";
            size_t at = event.find(key);
            if (at == std::string::npos) {
                return {};
            }
            at += key.size();
            size_t end = event.find('"', at);
            return end == std::string::npos ? std::string() : event.substr(at, end - at);
        }

        std::string cdata(const std::string& event, const char* tag) {
            size_t at = event.find(std::string("<") + tag);
            if (at == std::string::npos) {
                return {};
            }
            size_t open = event.find("<![CDATA[", at);
            size_t close = open == std::string::npos ? std::string::npos : event.find("]]>", open);
            if (open == std::string::npos || close == std::string::npos) {
                return {};
            }
            return event.substr(open + 9, close - open - 9);
        }

        Level levelNamed(const std::string& name) {
            if (name == "FATAL") return Level::Fatal;
            if (name == "ERROR") return Level::Error;
            if (name == "WARN") return Level::Warn;
            if (name == "DEBUG" || name == "TRACE") return Level::Debug;
            return Level::Info;
        }

        void event(Log& log, const std::string& raw, int stream) {
            std::string text = toUtf8(raw);
            std::string levelName = attribute(text, "level");
            Level level = levelNamed(levelName);
            std::string time;
            std::string stamp = attribute(text, "timestamp");
            if (!stamp.empty()) {
                long long millis = std::atoll(stamp.c_str());
                time_t seconds = (time_t) (millis / 1000);
                tm local = {};
                localtime_s(&local, &seconds);
                char clock[16];
                std::snprintf(clock, sizeof(clock), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
                time = clock;
            }
            std::string prefix = "[" + time + "] [" + attribute(text, "thread") + "/" +
                    (levelName.empty() ? std::string("INFO") : levelName) + "]: ";
            std::string message = cdata(text, "log4j:Message");
            std::string throwable = cdata(text, "log4j:Throwable");
            log.lastLevel[stream] = level;
            std::lock_guard<std::mutex> guard(lock);
            bool firstLine = true;
            auto emit = [&](const std::string& body, bool prefixed) {
                size_t from = 0;
                while (from <= body.size()) {
                    size_t newline = body.find('\n', from);
                    std::string line =
                            body.substr(from, newline == std::string::npos ? std::string::npos : newline - from);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!(newline == std::string::npos && line.empty() && !firstLine)) {
                        push(log, prefixed && firstLine ? prefix + line : line, level);
                    }
                    firstLine = false;
                    if (newline == std::string::npos) {
                        break;
                    }
                    from = newline + 1;
                }
            };
            emit(message, true);
            if (!throwable.empty()) {
                emit(throwable, false);
            }
        }

        void trim(Log& log) {
            while (log.kept.size() + log.waiting.size() > MAX_LINES && !log.kept.empty()) {
                log.counts[(int) log.kept.front().level]--;
                log.kept.pop_front();
                log.firstNumber++;
                log.droppedCount++;
            }
            if (log.waiting.size() > MAX_LINES) {
                size_t extra = log.waiting.size() - MAX_LINES;
                log.waiting.erase(log.waiting.begin(), log.waiting.begin() + (ptrdiff_t) extra);
                log.droppedCount += extra;
            }
        }

        void keep(Log& log, std::vector<Line>& lines) {
            for (Line& line : lines) {
                log.counts[(int) line.level]++;
                log.kept.push_back(std::move(line));
            }
            lines.clear();
        }
    }

    int begin(const std::string& title, int replace) {
        auto log = std::make_shared<Log>();
        log->name = title;
        std::lock_guard<std::mutex> guard(lock);
        log->id = nextId++;
        auto at = std::find_if(logs.begin(), logs.end(),
                               [replace](const std::shared_ptr<Log>& old) { return old->id == replace; });
        if (replace != 0 && at != logs.end()) {
            *at = log;
        } else {
            logs.push_back(log);
            while (logs.size() > MAX_LOGS) {
                auto ended = std::find_if(logs.begin(), logs.end(),
                                          [](const std::shared_ptr<Log>& old) { return !old->live; });
                if (ended == logs.end()) {
                    break;
                }
                logs.erase(ended);
            }
        }
        current = log;
        generationNumber++;
        return log->id;
    }

    void feed(int id, std::string_view line, bool error) {
        std::shared_ptr<Log> log;
        {
            std::lock_guard<std::mutex> guard(lock);
            log = find(id);
        }
        if (log == nullptr) {
            return;
        }
        int stream = error ? 1 : 0;
        Event& event = log->events[stream];
        size_t indent = line.find_first_not_of(" \t");
        std::string_view trimmed = indent == std::string_view::npos ? std::string_view() : line.substr(indent);
        if (!event.open && startsWith(trimmed, "<log4j:Event")) {
            event.open = true;
            event.text.clear();
            log->xml[stream] = true;
        }
        if (!event.open && log->xml[stream] && trimmed.empty()) {
            return;
        }
        if (event.open) {
            event.text.append(line.data(), line.size());
            event.text += '\n';
            if (line.find("</log4j:Event>") != std::string_view::npos) {
                event.open = false;
                GameLog::event(*log, event.text, stream);
            } else if (event.text.size() > (4u << 20)) {
                event.open = false;
                plain(*log, event.text, stream);
            }
            return;
        }
        plain(*log, line, stream);
    }

    void note(int id, const std::string& text) {
        std::lock_guard<std::mutex> guard(lock);
        if (std::shared_ptr<Log> log = find(id)) {
            push(*log, "[b-launcher] " + text, Level::Launcher);
        }
    }

    void finish(int id) {
        std::lock_guard<std::mutex> guard(lock);
        if (std::shared_ptr<Log> log = find(id)) {
            log->live = false;
        }
    }

    void poll() {
        std::vector<std::pair<std::shared_ptr<Log>, std::vector<Line>>> arrived;
        std::vector<uint64_t> lost;
        {
            std::lock_guard<std::mutex> guard(lock);
            for (const std::shared_ptr<Log>& log : logs) {
                if (log->pending.empty() && log->pendingDropped == 0) {
                    continue;
                }
                arrived.emplace_back(log, std::vector<Line>());
                arrived.back().second.swap(log->pending);
                lost.push_back(log->pendingDropped);
                log->pendingDropped = 0;
            }
        }
        for (size_t i = 0; i < arrived.size(); i++) {
            Log& log = *arrived[i].first;
            std::vector<Line>& lines = arrived[i].second;
            log.droppedCount += lost[i];
            if (log.paused) {
                for (Line& line : lines) {
                    log.waiting.push_back(std::move(line));
                }
            } else {
                keep(log, lines);
            }
            trim(log);
        }
    }

    std::vector<Tab> tabs() {
        std::lock_guard<std::mutex> guard(lock);
        std::vector<Tab> out;
        for (const std::shared_ptr<Log>& log : logs) {
            out.push_back(Tab{log->id, log->name});
        }
        return out;
    }

    int shown() {
        return current != nullptr ? current->id : 0;
    }

    void show(int id) {
        std::lock_guard<std::mutex> guard(lock);
        std::shared_ptr<Log> log = find(id);
        if (log == nullptr || log == current) {
            return;
        }
        current = log;
        generationNumber++;
    }

    void close(int id) {
        std::lock_guard<std::mutex> guard(lock);
        auto at =
                std::find_if(logs.begin(), logs.end(), [id](const std::shared_ptr<Log>& log) { return log->id == id; });
        if (at == logs.end()) {
            return;
        }
        bool wasShown = *at == current;
        size_t index = (size_t) (at - logs.begin());
        logs.erase(at);
        if (wasShown) {
            current = logs.empty() ? nullptr : logs[std::min(index, logs.size() - 1)];
            generationNumber++;
        }
    }

    const std::deque<Line>& lines() {
        return current != nullptr ? current->kept : nothing;
    }

    uint64_t first() {
        return current != nullptr ? current->firstNumber : 0;
    }

    uint64_t end() {
        return current != nullptr ? current->firstNumber + current->kept.size() : 0;
    }

    uint64_t generation() {
        return generationNumber;
    }

    size_t count(Level level) {
        return current != nullptr ? current->counts[(int) level] : 0;
    }

    const std::string& title() {
        return current != nullptr ? current->name : unnamed;
    }

    bool any() {
        return current != nullptr;
    }

    bool paused() {
        return current != nullptr && current->paused;
    }

    void setPaused(bool paused) {
        if (current == nullptr || current->paused == paused) {
            return;
        }
        current->paused = paused;
        if (!paused) {
            keep(*current, current->waiting);
            trim(*current);
        }
    }

    size_t held() {
        return current != nullptr ? current->waiting.size() : 0;
    }

    uint64_t dropped() {
        return current != nullptr ? current->droppedCount : 0;
    }

    void clear() {
        if (current == nullptr) {
            return;
        }
        current->kept.clear();
        current->waiting.clear();
        current->firstNumber = 0;
        current->droppedCount = 0;
        for (size_t& count : current->counts) {
            count = 0;
        }
        generationNumber++;
    }
}
