#include "app/system/UiStyles.h"
#include "app/system/Config.h"

namespace UiStyles {

    namespace {
        // "uiStyle" picks the whole interface; "theme", "accent" and "lastDarkTheme" are the colors of
        // the default one. Settings of another interface go under a key of their own.
#if defined(_WIN32)
        const std::vector<Style> STYLES = {
                {"default", "默认", true},
                {"forgotten-melody", "忘却的旋律", true},
        };
#else
        const std::vector<Style> STYLES = {
                {"default", "默认", true},
        };
#endif

        const Style* active = nullptr;

        const Style* find(const std::string& key) {
            for (const Style& style : STYLES) {
                if (key == style.key) {
                    return &style;
                }
            }
            return nullptr;
        }
    }

    const std::vector<Style>& all() {
        return STYLES;
    }

    const Style& current() {
#if defined(_WIN32)
        const nlohmann::json& doc = Config::data();
        auto value = doc.find("uiStyle");
        const Style* saved = value != doc.end() && value->is_string() ? find(value->get<std::string>()) : nullptr;
        return saved != nullptr && saved->available ? *saved : STYLES[0];
#else
        return STYLES[0];
#endif
    }

    void start(const std::string& requested) {
#if defined(_WIN32)
        const Style* named = find(requested);
        active = named != nullptr ? named : &current();
#else
        (void) requested;
        active = &STYLES[0];
#endif
    }

    const Style& running() {
        return active != nullptr ? *active : current();
    }

    void set(const std::string& key) {
        const Style* next = find(key);
        if (next == nullptr || !next->available || next == &current()) {
            return;
        }
        Config::data()["uiStyle"] = next->key;
        Config::save();
    }
}
