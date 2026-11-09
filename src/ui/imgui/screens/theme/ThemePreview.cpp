#include "ui/imgui/screens/theme/ThemePreview.h"

#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeShaders.h"

#include <map>

namespace ThemePreview {

    namespace {
        struct Entry {
            Data data;
            uint32_t accent = 0;
            bool dark = false;
        };

        std::map<std::string, Entry> cache;
        Data live;
    }

    const Data& of(const std::string& id) {
        if (id == ThemeRuntime::current()) {
            live.ok = true;
            live.palette = UiTheme::palette();
            live.style = ThemeStyle::current();
            live.layout = ShellLayout::theme();
            live.paints = ThemePaintSpec::current();
            live.facts = ThemeRuntime::facts();
            live.problems = ThemeRuntime::problems();
            return live;
        }
        const ThemeInputs& inputs = ThemeRuntime::currentInputs();
        bool dark = inputs.system.darkKnown && inputs.system.dark;
        auto found = cache.find(id);
        if (found != cache.end() && found->second.accent == inputs.accent && found->second.dark == dark) {
            return found->second.data;
        }
        Entry entry;
        entry.accent = inputs.accent;
        entry.dark = dark;
        ThemeResolver resolver;
        if (resolver.load(id, dark)) {
            ThemeInputs copy = inputs;
            copy.time = 0.0;
            entry.data.palette = resolver.resolve(copy);
            entry.data.style = resolver.style();
            entry.data.layout = resolver.layout();
            entry.data.paints = resolver.paints();
            entry.data.facts = resolver.facts();
            entry.data.problems = resolver.problems();
            entry.data.ok = true;
        } else {
            entry.data.problems = resolver.loadIssues();
        }
        Entry& stored = cache[id];
        stored = std::move(entry);
        return stored.data;
    }

    void forget() {
        cache.clear();
    }

    Scope::Scope(const std::string& id) {
        const Data& data = of(id);
        shown = data.ok ? data.layout : ShellLayout::theme();
        ThemeShaders::suppress(true);
        if (!data.ok || id == ThemeRuntime::current()) {
            return;
        }
        swapped = true;
        savedPalette = UiTheme::palette();
        savedStyle = ThemeStyle::current();
        UiTheme::apply(data.palette);
        ThemeStyle::apply(data.style);
        ThemePaintSpec::override(&data.paints);
    }

    Scope::~Scope() {
        ThemeShaders::suppress(false);
        if (!swapped) {
            return;
        }
        UiTheme::apply(savedPalette);
        ThemeStyle::apply(savedStyle);
        ThemePaintSpec::override(nullptr);
    }

    const ShellLayout::Spec& Scope::layout() const {
        return shown;
    }
}
