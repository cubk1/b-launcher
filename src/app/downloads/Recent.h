#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// When each entry of a cache was last used, so that once there are too many the ones used longest ago are dropped.
// What is shown is used all the time, so entries used a moment ago always stay.
class Recent {
public:
    using Clock = std::chrono::steady_clock;

    void use(const std::string& key) {
        used[key] = Clock::now();
    }

    // Drops from cache, keyed by strings, the entries used longest ago until keep are left, of those that mayGo lets
    // go; at most once a second, as it sorts them.
    template <class Cache, class MayGo>
    void trim(Cache& cache, size_t keep, MayGo mayGo) {
        Clock::time_point now = Clock::now();
        if (cache.size() <= keep || now - trimmed < std::chrono::seconds(1)) {
            return;
        }
        trimmed = now;
        std::vector<std::pair<Clock::time_point, std::string>> order;
        for (const auto& [key, entry] : cache) {
            auto found = used.find(key);
            Clock::time_point at = found != used.end() ? found->second : Clock::time_point();
            if (now - at >= std::chrono::seconds(10) && mayGo(entry)) {
                order.emplace_back(at, key);
            }
        }
        std::sort(order.begin(), order.end());
        for (const auto& [at, key] : order) {
            if (cache.size() <= keep) {
                break;
            }
            cache.erase(key);
            used.erase(key);
        }
    }

private:
    std::unordered_map<std::string, Clock::time_point> used;
    Clock::time_point trimmed;
};
