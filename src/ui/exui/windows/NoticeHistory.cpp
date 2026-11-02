#include "ui/exui/windows/NoticeHistory.h"

#include <algorithm>

namespace ForgottenMelody {

    std::string NoticeHistory::append(Notices::Kind kind, const std::wstring& text, bool running) {
        std::string id = "exui-notice-" + std::to_string(++serial);
        messages.push_back({id, kind, text, std::time(nullptr), running});
        ++changed;
        return id;
    }

    void NoticeHistory::enqueue(const std::string& id, long long durationMs) {
        // A duration of zero keeps the hint until another notice arrives.
        std::erase_if(pending, [](const Pending& item) { return item.durationMs <= 0; });
        pending.push_back({id, durationMs});
    }

    std::string NoticeHistory::add(Notices::Kind kind, const std::wstring& text, long long durationMs) {
        std::string id = append(kind, text, false);
        enqueue(id, durationMs);
        trim();
        return id;
    }

    std::string NoticeHistory::live(Notices::Kind kind, const std::wstring& text) {
        std::erase_if(pending, [](const Pending& item) { return item.durationMs <= 0; });
        return append(kind, text, true);
    }

    bool NoticeHistory::finish(const std::string& id, Notices::Kind kind, const std::wstring& text,
                               long long durationMs) {
        auto at = std::find_if(messages.begin(), messages.end(), [&](const Entry& entry) { return entry.id == id; });
        if (at == messages.end() || !at->running) return false;
        Entry result{id, kind, text, std::time(nullptr), false};
        messages.erase(at);
        messages.push_back(std::move(result));
        ++changed;
        enqueue(messages.back().id, durationMs);
        trim();
        return true;
    }

    const NoticeHistory::Entry* NoticeHistory::find(const std::string& id) const {
        auto at = std::find_if(messages.begin(), messages.end(), [&](const Entry& entry) { return entry.id == id; });
        return at == messages.end() ? nullptr : &*at;
    }

    size_t NoticeHistory::runningCount() const {
        return (size_t) std::count_if(messages.begin(), messages.end(), [](const Entry& entry) { return entry.running; });
    }

    size_t NoticeHistory::unreadCount() const {
        return (size_t) std::count_if(messages.begin(), messages.end(), [](const Entry& entry) {
            return !entry.running && !entry.read;
        });
    }

    bool NoticeHistory::markRead(const std::string& id) {
        auto at = std::find_if(messages.begin(), messages.end(), [&](const Entry& entry) { return entry.id == id; });
        if (at == messages.end() || at->running || at->read) return false;
        at->read = true;
        std::erase_if(pending, [&](const Pending& item) { return item.id == id; });
        ++changed;
        return true;
    }

    void NoticeHistory::clearRead() {
        if (std::erase_if(messages, [](const Entry& entry) { return !entry.running && entry.read; }) != 0) ++changed;
    }

    bool NoticeHistory::markAllRead() {
        bool any = false;
        for (Entry& entry : messages) {
            if (!entry.running && !entry.read) {
                entry.read = true;
                any = true;
            }
        }
        if (any) {
            pending.clear();
            shown.clear();
            ++changed;
        }
        return any;
    }

    void NoticeHistory::trim() {
        size_t completed = messages.size() - runningCount();
        while (completed > 200) {
            auto at = std::find_if(messages.begin(), messages.end(), [](const Entry& entry) { return !entry.running; });
            std::erase_if(pending, [&](const Pending& item) { return item.id == at->id; });
            messages.erase(at);
            --completed;
        }
    }

    const NoticeHistory::Entry* NoticeHistory::current(Clock::time_point now) {
        while (!pending.empty()) {
            const Pending& next = pending.front();
            const Entry* entry = find(next.id);
            if (entry == nullptr || entry->read) {
                pending.pop_front();
                continue;
            }
            if (shown != next.id) {
                shown = next.id;
                shownAt = now;
            }
            if (next.durationMs > 0 &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - shownAt).count() >= next.durationMs) {
                pending.pop_front();
                shown.clear();
                continue;
            }
            return entry;
        }
        shown.clear();
        auto active = std::find_if(messages.rbegin(), messages.rend(), [](const Entry& entry) { return entry.running; });
        return active == messages.rend() ? nullptr : &*active;
    }
}
