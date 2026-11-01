#include "app/system/AsyncTasks.h"

#include <exception>
#include <utility>

AsyncTasks::~AsyncTasks() {
    stop();
}

void AsyncTasks::run(Work work, Failure fail) {
    workers.start([this, work = std::move(work), fail = std::move(fail), token = stopping.token()] {
        Continuation next;
        try {
            next = work(token);
        } catch (const std::exception& error) {
            next = fail ? fail(error.what()) : nullptr;
        }
        if (next) {
            std::lock_guard<std::mutex> guard(lock);
            finished.push_back(std::move(next));
        }
    });
}

void AsyncTasks::poll() {
    workers.reap();
    std::deque<Continuation> ready;
    {
        std::lock_guard<std::mutex> guard(lock);
        ready.swap(finished);
    }
    for (Continuation& next : ready) {
        next();
    }
}

void AsyncTasks::stop() {
    stopping.cancel();
    workers.joinAll();
}
