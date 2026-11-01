#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <utility>

// Producers may post from any thread; the owner polls and runs callbacks.
// Producers must be stopped before the queue is destroyed.
class CallbackQueue {
public:
    using Callback = std::function<void()>;

    void post(Callback next) {
        if (!next) {
            return;
        }
        std::lock_guard<std::mutex> guard(lock);
        pending.push_back(std::move(next));
    }

    // Take one batch so callbacks run outside the lock and may post or poll again.
    void poll() {
        std::deque<Callback> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(pending);
        }
        for (Callback& next : ready) {
            next();
        }
    }

private:
    std::mutex lock;
    std::deque<Callback> pending;
};
