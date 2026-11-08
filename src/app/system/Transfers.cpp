#include "app/system/Transfers.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace Transfers {

    namespace blb = b_launcher_base;

    namespace {
        constexpr double SAMPLE = 0.5;
        constexpr size_t HISTORY = 120;
        constexpr size_t KEEP = 30;

        struct Part {
            uint64_t done = 0;
            uint64_t total = 0;
            uint64_t items = 0;
            uint64_t itemsTotal = 0;
        };

        struct Live {
            Task task;
            std::map<std::string, Part> parts;
            std::map<std::string, uint64_t> checks;
            uint64_t extraRead = 0;
            uint64_t extraWritten = 0;
            uint64_t sampled[3] = {};
            double sampledAt = 0.0;
        };

        std::mutex lock;
        std::map<uint64_t, Live> live;
        std::map<uint64_t, std::function<void()>> cancels;
        uint64_t nextId = 1;
        std::vector<Task> shown;
        int active = 0;

        void sum(Live& entry) {
            Task& task = entry.task;
            task.done = 0;
            task.total = 0;
            task.items = 0;
            task.itemsTotal = 0;
            for (const auto& [name, part] : entry.parts) {
                task.done += part.total > 0 ? std::min(part.done, part.total) : part.done;
                task.total += part.total;
                task.items += part.items;
                task.itemsTotal += part.itemsTotal;
            }
            uint64_t checked = 0;
            for (const auto& [name, bytes] : entry.checks) {
                checked += bytes;
            }
            task.download.bytes = task.done;
            task.read.bytes = checked + entry.extraRead;
            task.write.bytes = task.done + entry.extraWritten;
        }

        void measure(Rate& rate, uint64_t& sampled, double span) {
            uint64_t moved = rate.bytes > sampled ? rate.bytes - sampled : 0;
            double now = (double) moved / span;
            rate.speed = rate.history.empty() ? now : rate.speed * 0.6 + now * 0.4;
            rate.history.push_back((float) rate.speed);
            if (rate.history.size() > HISTORY) {
                rate.history.erase(rate.history.begin(), rate.history.begin() + (rate.history.size() - HISTORY));
            }
            sampled = rate.bytes;
        }

        void sample(Live& entry, double at) {
            double span = at - entry.sampledAt;
            if (span < SAMPLE) {
                return;
            }
            measure(entry.task.download, entry.sampled[0], span);
            measure(entry.task.read, entry.sampled[1], span);
            measure(entry.task.write, entry.sampled[2], span);
            entry.sampledAt = at;
        }

        void trim() {
            size_t finished = 0;
            for (const auto& [id, entry] : live) {
                finished += entry.task.state != State::Running ? 1 : 0;
            }
            for (auto it = live.begin(); it != live.end() && finished > KEEP;) {
                if (it->second.task.state != State::Running) {
                    it = live.erase(it);
                    finished--;
                } else {
                    ++it;
                }
            }
        }
    }

    float Task::progress() const {
        if (state == State::Done) {
            return 1.0f;
        }
        if (fraction >= 0.0f) {
            return std::min(1.0f, fraction);
        }
        if (total > 0) {
            return (float) std::min(1.0, (double) done / (double) total);
        }
        return -1.0f;
    }

    double Task::elapsed() const {
        return std::max(0.0, (state == State::Running ? now() : ended) - started);
    }

    double Task::remaining() const {
        if (state != State::Running || fraction >= 0.0f || total <= done || download.speed < 1.0) {
            return -1.0;
        }
        return (double) (total - done) / download.speed;
    }

    double now() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    uint64_t begin(Spec spec) {
        std::lock_guard<std::mutex> guard(lock);
        uint64_t id = nextId++;
        Live& entry = live[id];
        Task& task = entry.task;
        task.id = id;
        task.kind = spec.kind;
        task.title = std::move(spec.title);
        task.minecraft = std::move(spec.minecraft);
        task.cover = std::move(spec.cover);
        task.icon = std::move(spec.icon);
        task.instance = std::move(spec.instance);
        task.folder = std::move(spec.folder);
        task.cancellable = (bool) spec.cancel;
        task.started = now();
        entry.sampledAt = task.started;
        if (spec.cancel) {
            cancels[id] = std::move(spec.cancel);
        }
        trim();
        return id;
    }

    void report(uint64_t id, const blb::progress_event& event) {
        std::lock_guard<std::mutex> guard(lock);
        auto found = live.find(id);
        if (found == live.end() || found->second.task.state != State::Running) {
            return;
        }
        std::string name(event.task);
        if (name.rfind("verify:", 0) == 0) {
            found->second.checks[name] = event.done_bytes;
            sum(found->second);
            return;
        }
        Part& part = found->second.parts[name];
        part.total = event.total_bytes;
        part.done = event.finished ? std::max(event.done_bytes, event.total_bytes) : event.done_bytes;
        part.itemsTotal = event.total_items;
        part.items = event.finished ? event.total_items : event.done_items;
        sum(found->second);
    }

    void stage(uint64_t id, const std::string& text) {
        std::lock_guard<std::mutex> guard(lock);
        auto found = live.find(id);
        if (found != live.end() && found->second.task.state == State::Running) {
            found->second.task.stage = text;
        }
    }

    void fraction(uint64_t id, float value) {
        std::lock_guard<std::mutex> guard(lock);
        auto found = live.find(id);
        if (found != live.end() && found->second.task.state == State::Running) {
            found->second.task.fraction = value;
        }
    }

    void io(uint64_t id, uint64_t read, uint64_t written) {
        std::lock_guard<std::mutex> guard(lock);
        auto found = live.find(id);
        if (found != live.end() && found->second.task.state == State::Running) {
            found->second.extraRead += read;
            found->second.extraWritten += written;
            sum(found->second);
        }
    }

    void finish(uint64_t id, State state, const std::string& error) {
        std::function<void()> dropped;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto found = live.find(id);
            if (found == live.end() || found->second.task.state != State::Running) {
                return;
            }
            Task& task = found->second.task;
            task.state = state == State::Running ? State::Done : state;
            task.error = error;
            task.ended = now();
            task.download.speed = 0.0;
            task.read.speed = 0.0;
            task.write.speed = 0.0;
            task.stage.clear();
            task.cancellable = false;
            auto cancel = cancels.find(id);
            if (cancel != cancels.end()) {
                dropped = std::move(cancel->second);
                cancels.erase(cancel);
            }
            trim();
        }
    }

    void cancel(uint64_t id) {
        std::function<void()> action;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto found = cancels.find(id);
            if (found == cancels.end()) {
                return;
            }
            action = found->second;
        }
        action();
    }

    void remove(uint64_t id) {
        std::lock_guard<std::mutex> guard(lock);
        auto found = live.find(id);
        if (found != live.end() && found->second.task.state != State::Running) {
            live.erase(found);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> guard(lock);
        std::erase_if(live, [](const auto& entry) { return entry.second.task.state != State::Running; });
    }

    const std::vector<Task>& list() {
        return shown;
    }

    int running() {
        return active;
    }

    void poll() {
        double at = now();
        std::lock_guard<std::mutex> guard(lock);
        shown.clear();
        active = 0;
        for (auto& [id, entry] : live) {
            if (entry.task.state == State::Running) {
                sample(entry, at);
                active++;
            }
            shown.push_back(entry.task);
        }
    }
}
