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
        finished.post(std::move(next));
    });
}

void AsyncTasks::poll() {
    workers.reap();
    finished.poll();
}

void AsyncTasks::cancel() {
    stopping.cancel();
}

void AsyncTasks::stop() {
    cancel();
    workers.joinAll();
}
