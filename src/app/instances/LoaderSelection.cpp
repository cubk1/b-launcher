#include "app/instances/LoaderSelection.h"

#include <utility>

namespace blb = b_launcher_base;

void LoaderSelection::reset(std::string minecraft, Choices chosen) {
    game = std::move(minecraft);
    picked = std::move(chosen);
}

void LoaderSelection::select(Type type, std::string version) {
    picked[type] = std::move(version);
    sanitize();
}

void LoaderSelection::remove(Type type) {
    picked.erase(type);
    sanitize();
}

std::vector<blb::install::loader_info> LoaderSelection::infos(std::optional<Type> skip) const {
    std::vector<blb::install::loader_info> out;
    out.reserve(picked.size());
    for (const auto& [type, version] : picked) {
        if (!skip || type != *skip) {
            out.push_back({type, version});
        }
    }
    return out;
}

std::vector<blb::install::loader_request> LoaderSelection::requests() const {
    std::vector<blb::install::loader_request> out;
    out.reserve(picked.size());
    for (const auto& [type, version] : picked) {
        out.push_back({type, version});
    }
    return out;
}

std::vector<blb::instance::loader_choice> LoaderSelection::choices() const {
    return blb::instance::loader_choices(game, infos());
}

std::string LoaderSelection::defaultName() const {
    blb::install::install_request request;
    request.minecraft = game;
    request.loaders = requests();
    return blb::install::default_version_id(request, infos());
}

void LoaderSelection::sanitize() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [type, version] : picked) {
            for (const auto& choice : blb::instance::loader_choices(game, infos(type))) {
                if (choice.type == type && choice.state != blb::instance::choice_state::available) {
                    picked.erase(type);
                    changed = true;
                    break;
                }
            }
            if (changed) {
                break;
            }
        }
    }
}
