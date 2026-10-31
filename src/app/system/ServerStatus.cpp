#include "app/system/ServerStatus.h"

#include <core/base/cancellation.hpp>
#include <core/network/server_ping.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace ServerStatus {

    namespace blb = b_launcher_base;

    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr double FRESH = 60.0;
        constexpr double RETRY = 20.0;

        struct Request {
            std::string address;
            int serial = 0;
        };

        std::thread worker;
        std::mutex lock;
        std::condition_variable wake;
        bool stopping = false;
        std::optional<Request> queued;
        blb::cancellation_source current;
        std::optional<std::pair<int, Result>> landed;

        State state;
        int serial = 0;
        Clock::time_point fetchedAt;

        std::string describe(const blb::error& error) {
            switch (error.code) {
                case blb::errc::invalid_argument: return "服务器地址无效";
                case blb::errc::not_found: return "找不到这个服务器";
                case blb::errc::timeout: return "连接超时";
                case blb::errc::parse: return "服务器响应无法识别";
                default: return "无法连接到服务器";
            }
        }

        Result ping(const std::string& address, const blb::cancellation_token& token) {
            Result out;
            auto status = blb::net::ping_server(address, token);
            if (!status) {
                out.error = describe(status.err());
                return out;
            }
            out.ok = true;
            out.motd = std::move(status->motd);
            out.version = std::move(status->version);
            out.online = status->online;
            out.max = status->max;
            out.latency = (int) status->latency.count();
            if (!status->favicon.empty()) {
                out.favicon = Pixels::decode(status->favicon.data(), status->favicon.size());
            }
            return out;
        }

        void run() {
            for (;;) {
                Request request;
                blb::cancellation_token token;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [] { return stopping || queued.has_value(); });
                    if (stopping) {
                        return;
                    }
                    request = std::move(*queued);
                    queued.reset();
                    token = current.token();
                }
                Result result = ping(request.address, token);
                if (token.is_cancelled()) {
                    continue;
                }
                std::lock_guard<std::mutex> guard(lock);
                landed = std::make_pair(request.serial, std::move(result));
            }
        }

        void start() {
            serial++;
            state.pinging = true;
            if (!worker.joinable()) {
                worker = std::thread(run);
            }
            {
                std::lock_guard<std::mutex> guard(lock);
                current.cancel();
                current = blb::cancellation_source();
                queued = Request{state.address, serial};
            }
            wake.notify_one();
        }
    }

    const State& of(const std::string& address) {
        if (address != state.address) {
            state = State();
            state.address = address;
            if (!address.empty()) {
                start();
            }
            return state;
        }
        double age = std::chrono::duration<double>(Clock::now() - fetchedAt).count();
        if (!address.empty() && !state.pinging && age >= (state.result.ok ? FRESH : RETRY)) {
            start();
        }
        return state;
    }

    void refresh() {
        if (!state.address.empty()) {
            start();
        }
    }

    void poll() {
        std::optional<std::pair<int, Result>> got;
        {
            std::lock_guard<std::mutex> guard(lock);
            got.swap(landed);
        }
        if (!got || got->first != serial) {
            return;
        }
        state.pinging = false;
        state.done = true;
        state.result = std::move(got->second);
        fetchedAt = Clock::now();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
            current.cancel();
        }
        wake.notify_one();
        if (worker.joinable()) {
            worker.join();
        }
    }
}
