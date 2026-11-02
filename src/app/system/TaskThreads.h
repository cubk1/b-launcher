#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <thread>
#include <utility>

bool threadExited(std::thread& thread);

// The owner thread starts tasks, reaps completed threads and joins at shutdown.
// Tasks retain their own completion callbacks and cancellation handling.
class TaskThreads {
public:
    TaskThreads() = default;
    TaskThreads(const TaskThreads&) = delete;
    TaskThreads& operator=(const TaskThreads&) = delete;
    ~TaskThreads() { joinAll(); }

    template<class Work>
    void start(Work work) {
        reap();
        auto done = std::make_shared<std::atomic<bool>>(false);
        // Allocate the entry before starting its thread, so a failed allocation
        // cannot destroy a joinable local std::thread.
        auto entry = workers.emplace(workers.end(), Worker{done, {}});
        try {
            entry->thread = std::thread([work = std::move(work), done]() mutable {
                {
                    auto task = std::move(work);
                    task();
                }
                done->store(true, std::memory_order_release);
            });
        } catch (...) {
            workers.erase(entry);
            throw;
        }
    }

    void reap() {
        for (auto it = workers.begin(); it != workers.end();) {
            if (it->done->load(std::memory_order_acquire) && threadExited(it->thread)) {
                it->thread.join();
                it = workers.erase(it);
            } else {
                ++it;
            }
        }
    }

    void joinAll() {
        for (Worker& worker : workers) {
            if (worker.thread.joinable()) {
                worker.thread.join();
            }
        }
        workers.clear();
    }

private:
    struct Worker {
        std::shared_ptr<std::atomic<bool>> done;
        std::thread thread;
    };
    std::list<Worker> workers;
};
