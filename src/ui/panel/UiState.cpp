#include "UiState.h"
#include "UiRender.h"

#include <cmath>

namespace UiState {

    std::unordered_map<std::string, float> motionMap;

    float motion(const std::string& id, float target, float speed) {
        auto it = motionMap.find(id);
        float current = it == motionMap.end() ? target : it->second;
        float value = UiRender::lerp(current, target, 1.0f - (float) std::exp(-speed * UiRender::deltaTime()));
        if (std::fabs(value - target) < 0.0005f) {
            value = target;
        }
        motionMap[id] = value;
        return value;
    }

    float motion(const std::string& id, float target) {
        return motion(id, target, 16.0f);
    }

    float motionFrom(const std::string& id, float target, float speed, float initial) {
        if (motionMap.find(id) == motionMap.end()) {
            motionMap[id] = initial;
            return initial;
        }
        return motion(id, target, speed);
    }

    float getOrDefault(const std::string& id, float fallback) {
        auto it = motionMap.find(id);
        return it == motionMap.end() ? fallback : it->second;
    }

    bool contains(const std::string& id) {
        return motionMap.find(id) != motionMap.end();
    }

    void remove(const std::string& id) {
        motionMap.erase(id);
    }

    void put(const std::string& id, float value) {
        motionMap[id] = value;
    }
}
