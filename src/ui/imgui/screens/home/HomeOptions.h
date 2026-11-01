#pragma once

#include "ui/imgui/value/Values.h"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <span>

// An ordered, configurable group on the homepage. The item descriptors must
// outlive this object; choice bindings share its state.
class HomeOptions {
public:
    struct Item {
        const char* id;
        const char* label;
        int glyph;
    };

    HomeOptions(std::string section, std::span<const Item> items, std::vector<std::string> defaults,
                std::string emptyLabel);

    std::shared_ptr<Choice> choice();
    bool enabled(const std::string& id);
    std::vector<const Item*> shown();

private:
    class Binding;
    std::string section;
    std::span<const Item> items;
    std::vector<std::string> defaults;
    std::string emptyLabel;
    bool loaded = false;
    std::vector<std::string> order;
    std::vector<std::string> chosen;

    const Item* byId(const std::string& id) const;
    const Item* byLabel(const std::string& label) const;
    std::vector<std::string> knownIds(const nlohmann::json& list) const;
    void load();
    void save();
};
