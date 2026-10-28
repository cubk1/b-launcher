#include "GameLog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <vector>

namespace GameLog {

    namespace {

        std::mutex lock;
        std::vector<Line> pending;
        bool restart = false;
        std::string pendingTitle;
        uint64_t pendingDropped = 0;

        struct Event {
            bool open = false;
            std::string text;
        };
        Event events[2];
        bool xml[2] = {false, false};

        Level lastLevel[2] = {Level::Info, Level::Info};

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

        void push(std::string text, Level level) {
            pending.push_back(Line{std::move(text), level});
            if (pending.size() > MAX_LINES + MAX_LINES / 4) {
                size_t extra = pending.size() - MAX_LINES;
                pending.erase(pending.begin(), pending.begin() + (ptrdiff_t) extra);
                pendingDropped += extra;
            }
        }

        void plain(std::string_view raw, int stream) {
            std::string text = toUtf8(raw);
            Level level;
            if (!levelIn(text, level)) {
                level = continuation(text) ? lastLevel[stream] : (stream == 1 ? Level::Warn : Level::Info);
            }
            lastLevel[stream] = level;
            std::lock_guard<std::mutex> guard(lock);
            push(std::move(text), level);
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

        void event(const std::string& raw, int stream) {
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
            lastLevel[stream] = level;
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
                        push(prefixed && firstLine ? prefix + line : line, level);
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

        std::deque<Line> kept;
        std::vector<Line> waiting;
        uint64_t firstNumber = 0;
        uint64_t generationNumber = 0;
        uint64_t droppedCount = 0;
        size_t counts[LEVEL_COUNT] = {};
        std::string name;
        bool pausedNow = false;
        bool started = false;

        void trim() {
            while (kept.size() + waiting.size() > MAX_LINES && !kept.empty()) {
                counts[(int) kept.front().level]--;
                kept.pop_front();
                firstNumber++;
                droppedCount++;
            }
            if (waiting.size() > MAX_LINES) {
                size_t extra = waiting.size() - MAX_LINES;
                waiting.erase(waiting.begin(), waiting.begin() + (ptrdiff_t) extra);
                droppedCount += extra;
            }
        }

        void keep(std::vector<Line>& lines) {
            for (Line& line : lines) {
                counts[(int) line.level]++;
                kept.push_back(std::move(line));
            }
            lines.clear();
        }

        void reset() {
            kept.clear();
            waiting.clear();
            firstNumber = 0;
            droppedCount = 0;
            for (size_t& count : counts) {
                count = 0;
            }
            generationNumber++;
        }
    }

    void begin(const std::string& title) {
        std::lock_guard<std::mutex> guard(lock);
        pending.clear();
        pendingDropped = 0;
        restart = true;
        pendingTitle = title;
        events[0] = Event();
        events[1] = Event();
        xml[0] = false;
        xml[1] = false;
        lastLevel[0] = Level::Info;
        lastLevel[1] = Level::Info;
    }

    void feed(std::string_view line, bool error) {
        int stream = error ? 1 : 0;
        Event& event = events[stream];
        size_t indent = line.find_first_not_of(" \t");
        std::string_view trimmed = indent == std::string_view::npos ? std::string_view() : line.substr(indent);
        if (!event.open && startsWith(trimmed, "<log4j:Event")) {
            event.open = true;
            event.text.clear();
            xml[stream] = true;
        }
        if (!event.open && xml[stream] && trimmed.empty()) {
            return;
        }
        if (event.open) {
            event.text.append(line.data(), line.size());
            event.text += '\n';
            if (line.find("</log4j:Event>") != std::string_view::npos) {
                event.open = false;
                GameLog::event(event.text, stream);
            } else if (event.text.size() > (4u << 20)) {
                event.open = false;
                plain(event.text, stream);
            }
            return;
        }
        plain(line, stream);
    }

    void note(const std::string& text) {
        std::lock_guard<std::mutex> guard(lock);
        push("[b-launcher] " + text, Level::Launcher);
    }

    void poll() {
        std::vector<Line> arrived;
        uint64_t lost = 0;
        bool startOver = false;
        std::string title;
        {
            std::lock_guard<std::mutex> guard(lock);
            arrived.swap(pending);
            lost = pendingDropped;
            pendingDropped = 0;
            startOver = restart;
            restart = false;
            title = pendingTitle;
        }
        if (startOver) {
            reset();
            name = title;
            started = true;
        }
        droppedCount += lost;
        if (arrived.empty()) {
            return;
        }
        if (pausedNow) {
            for (Line& line : arrived) {
                waiting.push_back(std::move(line));
            }
        } else {
            keep(arrived);
        }
        trim();
    }

    const std::deque<Line>& lines() {
        return kept;
    }

    uint64_t first() {
        return firstNumber;
    }

    uint64_t end() {
        return firstNumber + kept.size();
    }

    uint64_t generation() {
        return generationNumber;
    }

    size_t count(Level level) {
        return counts[(int) level];
    }

    const std::string& title() {
        return name;
    }

    bool any() {
        return started;
    }

    bool paused() {
        return pausedNow;
    }

    void setPaused(bool paused) {
        if (pausedNow == paused) {
            return;
        }
        pausedNow = paused;
        if (!paused) {
            keep(waiting);
            trim();
        }
    }

    size_t held() {
        return waiting.size();
    }

    uint64_t dropped() {
        return droppedCount;
    }

    void clear() {
        reset();
    }
}
