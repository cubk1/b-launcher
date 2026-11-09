#include "ThemePaintSpec.h"

namespace ThemePaintSpec {

    namespace {
        Spec active;
        const Spec* overriding = nullptr;
        unsigned version = 0;

        const char* const NAMES[REGION_COUNT] = {"window", "titlebar", "sidebar",       "content",  "card",
                                                 "dialog", "menu",     "primaryButton", "navActive"};
    }

    void apply(const Spec& spec) {
        if (spec == active) {
            return;
        }
        active = spec;
        version++;
    }

    void override(const Spec* spec) {
        overriding = spec;
    }

    const Spec& current() {
        return overriding != nullptr ? *overriding : active;
    }

    unsigned generation() {
        return version;
    }

    bool custom(Region region) {
        return !current().regions[region].layers.empty();
    }

    bool regionOf(const std::string& name, Region& out) {
        for (int i = 0; i < REGION_COUNT; i++) {
            if (name == NAMES[i]) {
                out = (Region) i;
                return true;
            }
        }
        return false;
    }

    bool splitSource(const std::string& source, std::string& owner, std::string& path) {
        size_t colon = source.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= source.size()) {
            return false;
        }
        owner = source.substr(0, colon);
        path = source.substr(colon + 1);
        return true;
    }
}
