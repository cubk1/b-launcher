#pragma once

#include <string>
#include <vector>

namespace Features {

    enum Feature {
        NEW_INSTANCE,
        DOWNLOADS,
        TRANSFERS,
        ACCOUNTS,
        MULTIPLAYER,
        VERSIONS,
        INSTANCE_EDIT,
        INSTANCE_DELETE,
        MOD_UPDATES,
        TOOLBOX,
        JAVA,
        UPDATE_CHECK,
        THEMES,
        SERVER_BUILD,
        SECTION_LAUNCH,
        SECTION_OPTIONS,
        SECTION_KEYS,
        SECTION_PATCH,
        SECTION_FOLDERS,
        SECTION_LOOK,
        SECTION_WINDOW,
        SECTION_HOME,
        SECTION_NETWORK,
        SECTION_RESET,
        FEATURE_COUNT
    };

    struct Info {
        Feature id;
        const char* key;
        const char* label;
        int group;
    };

    constexpr int GROUP_PAGES = 0;
    constexpr int GROUP_ACTIONS = 1;
    constexpr int GROUP_SETTINGS = 2;

    const std::vector<Info>& all();
    bool enabled(Feature feature);
    bool hidden(Feature feature);
    bool locked(Feature feature);
    void setHidden(Feature feature, bool hide);
    bool revealed();
    void toggleReveal();
    bool anyHidden();
}
