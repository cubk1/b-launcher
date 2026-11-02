#pragma once

#include "app/system/Notices.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

namespace ForgottenMelody {

    // Owned by the main window and accessed on its UI thread. Keeps the latest
    // 200 completed messages, plus every task that is still running.
    class NoticeHistory {
    public:
        using Clock = std::chrono::steady_clock;

        struct Entry {
            std::string id;
            Notices::Kind kind;
            std::wstring text;
            std::time_t time;
            bool running;
            bool read = false;
        };

        std::string add(Notices::Kind kind, const std::wstring& text, long long durationMs);
        std::string live(Notices::Kind kind, const std::wstring& text);
        // Unknown or already finished IDs do not change another message.
        bool finish(const std::string& id, Notices::Kind kind, const std::wstring& text, long long durationMs);
        // Only completed results can be read. Keep them available for copying
        // until the history window closes (or their error box is confirmed).
        bool markRead(const std::string& id);
        bool markAllRead();
        void clearRead();

        const std::vector<Entry>& entries() const { return messages; }
        const Entry* find(const std::string& id) const;
        size_t runningCount() const;
        size_t unreadCount() const;
        uint64_t revision() const { return changed; }
        // A queued result gets its full display time, then the newest running
        // task resumes. Read results no longer appear in the hint queue.
        const Entry* current(Clock::time_point now = Clock::now());

    private:
        struct Pending {
            std::string id;
            long long durationMs;
        };

        std::vector<Entry> messages;
        std::deque<Pending> pending;
        uint64_t serial = 0, changed = 0;
        std::string shown;
        Clock::time_point shownAt{};

        std::string append(Notices::Kind kind, const std::wstring& text, bool running);
        void enqueue(const std::string& id, long long durationMs);
        void trim();
    };
}
