#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace AskAi {

    struct Service {
        const char* key;
        const char* name;
        const char* icon;
        const char* home;
        size_t limit;
    };

    std::span<const Service> services();
    std::string nameOf(const std::string& key);
    std::string chosen();
    void setChosen(const std::string& key);
    size_t budget(const std::string& key);
    bool ask(const std::string& key, const std::string& prompt);
}
