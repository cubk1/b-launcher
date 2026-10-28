#pragma once

#include <string>
#include <unordered_map>

namespace UiState {

    extern std::unordered_map<std::string, float> motionMap;

    float motion(const std::string& id, float target, float speed);
    float motion(const std::string& id, float target);
    float motionFrom(const std::string& id, float target, float speed, float initial);

    float getOrDefault(const std::string& id, float fallback);
    bool contains(const std::string& id);
    void remove(const std::string& id);
    void put(const std::string& id, float value);
}
