#pragma once

#include "app/system/TaskThreads.h"

#include <core/base/cancellation.hpp>

#include <deque>
#include <functional>
#include <mutex>
#include <string>

// run, poll and stop belong to the owner thread. Work and its error handler
// run in the background and return continuations that poll runs on the owner.
class AsyncTasks {
public:
    using Continuation = std::function<void()>;
    using Work = std::function<Continuation(b_launcher_base::cancellation_token)>;
    using Failure = std::function<Continuation(const std::string&)>;

    AsyncTasks() = default;
    AsyncTasks(const AsyncTasks&) = delete;
    AsyncTasks& operator=(const AsyncTasks&) = delete;
    ~AsyncTasks();

    void run(Work work, Failure fail);
    // Drain one batch, outside the queue lock. New completions wait for a later poll.
    void poll();
    // Signal cancellation before cancelling other owned operations, without joining yet.
    void cancel();
    // Cancel and join work. Queued continuations are only executed by poll.
    void stop();

private:
    std::mutex lock;
    std::deque<Continuation> finished;
    b_launcher_base::cancellation_source stopping;
    TaskThreads workers;
};
