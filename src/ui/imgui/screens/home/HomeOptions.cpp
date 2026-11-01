#include "ui/imgui/screens/home/HomeOptions.h"
#include "app/system/Config.h"
#include "ui/imgui/panel/UiCommon.h"

#include <algorithm>
#include <utility>

HomeOptions::HomeOptions(std::string section, std::span<const Item> items, std::vector<std::string> defaults,
                         std::string emptyLabel)
    : section(std::move(section)), items(items), defaults(std::move(defaults)), emptyLabel(std::move(emptyLabel)) {}

const HomeOptions::Item* HomeOptions::byId(const std::string& id) const {
    for (const Item& item : items) {
        if (id == item.id) {
            return &item;
        }
    }
    return nullptr;
}

const HomeOptions::Item* HomeOptions::byLabel(const std::string& label) const {
    for (const Item& item : items) {
        if (label == item.label) {
            return &item;
        }
    }
    return nullptr;
}

std::vector<std::string> HomeOptions::knownIds(const nlohmann::json& list) const {
    std::vector<std::string> out;
    if (list.is_array()) {
        for (const auto& item : list) {
            if (item.is_string()) {
                auto id = item.get<std::string>();
                if (byId(id) != nullptr && std::find(out.begin(), out.end(), id) == out.end()) {
                    out.push_back(std::move(id));
                }
            }
        }
    }
    return out;
}

void HomeOptions::load() {
    if (loaded) {
        return;
    }
    loaded = true;
    const auto& doc = Config::data();
    auto saved = doc.find(section);
    bool present = saved != doc.end() && saved->is_object();
    order = present && saved->contains("order") ? knownIds((*saved)["order"]) : std::vector<std::string>();
    for (const Item& item : items) {
        if (std::find(order.begin(), order.end(), item.id) == order.end()) {
            order.push_back(item.id);
        }
    }
    chosen = present && saved->contains("enabled") ? knownIds((*saved)["enabled"]) : defaults;
}

void HomeOptions::save() {
    Config::data()[section] = {{"order", order}, {"enabled", chosen}};
    Config::save();
}

bool HomeOptions::enabled(const std::string& id) {
    load();
    return std::find(chosen.begin(), chosen.end(), id) != chosen.end();
}

std::vector<const HomeOptions::Item*> HomeOptions::shown() {
    load();
    std::vector<const Item*> out;
    for (const auto& id : order) {
        if (enabled(id)) {
            out.push_back(byId(id));
        }
    }
    return out;
}

class HomeOptions::Binding final : public Choice {
public:
    explicit Binding(HomeOptions& group) : Choice("settings#" + group.section), group(group) {}

    std::vector<std::string> options() override {
        group.load();
        std::vector<std::string> out;
        for (const auto& id : group.order) {
            out.push_back(group.byId(id)->label);
        }
        return out;
    }

    bool multi() override { return true; }
    bool reorderable() override { return true; }

    bool selected(const std::string& option) override {
        group.load();
        const Item* item = group.byLabel(option);
        return item != nullptr && group.enabled(item->id);
    }

    void choose(const std::string& option) override {
        group.load();
        const Item* item = group.byLabel(option);
        if (item == nullptr) {
            return;
        }
        auto at = std::find(group.chosen.begin(), group.chosen.end(), item->id);
        if (at != group.chosen.end()) {
            group.chosen.erase(at);
        } else {
            group.chosen.push_back(item->id);
        }
        group.save();
    }

    std::string label() override {
        std::string out;
        for (const Item* item : group.shown()) {
            out += (out.empty() ? "" : "、") + std::string(item->label);
        }
        return out.empty() ? group.emptyLabel : out;
    }

    void reorder(int from, int to) override {
        group.load();
        int size = (int) group.order.size();
        if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
            return;
        }
        std::string moved = group.order[from];
        group.order.erase(group.order.begin() + from);
        group.order.insert(group.order.begin() + to, moved);
        group.save();
    }

    std::string optionIcon(const std::string& option) override {
        const Item* item = group.byLabel(option);
        return item != nullptr ? Ui::glyph(item->glyph) : std::string();
    }

private:
    HomeOptions& group;
};

std::shared_ptr<Choice> HomeOptions::choice() {
    return std::make_shared<Binding>(*this);
}
