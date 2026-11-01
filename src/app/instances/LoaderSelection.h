#pragma once

#include <core/instance/loader_editor.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

// Selection rules shared by the install and loader-edit interfaces.
// Each page/window owns its own selection; display and fetch state stay in the UI.
class LoaderSelection {
public:
    using Type = b_launcher_base::install::loader_type;
    using Choices = std::map<Type, std::string>;

    const std::string& minecraft() const { return game; }
    const Choices& chosen() const { return picked; }

    // Restore exactly what was read, including combinations the user may need to fix.
    void reset(std::string minecraft = {}, Choices chosen = {});
    void select(Type type, std::string version);
    void remove(Type type);

    std::vector<b_launcher_base::install::loader_info> infos(std::optional<Type> skip = {}) const;
    std::vector<b_launcher_base::install::loader_request> requests() const;
    std::vector<b_launcher_base::instance::loader_choice> choices() const;
    std::string defaultName() const;

private:
    std::string game;
    Choices picked;
    void sanitize();
};
