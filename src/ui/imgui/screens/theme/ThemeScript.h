#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class ThemeScript {
public:
    ThemeScript();
    ~ThemeScript();
    ThemeScript(const ThemeScript&) = delete;
    ThemeScript& operator=(const ThemeScript&) = delete;

    bool ready() const;
    bool load(const std::string& source, const std::string& name, std::string& error);
    std::optional<nlohmann::json> call(const char* function, const nlohmann::json& env, bool& missing,
                                       std::string& error);
    std::vector<std::string> takeLogs();
    bool has(const char* function);

    struct State;

private:
    std::unique_ptr<State> state;
    void* context = nullptr;
};
