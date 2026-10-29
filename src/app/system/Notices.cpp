#include "app/system/Notices.h"

namespace Notices {

    namespace {
        Display display;
    }

    void show(Display next) {
        display = std::move(next);
    }

    void add(Kind kind, const std::string& text, long long durationMs) {
        if (display.add) {
            display.add(kind, text, durationMs);
        }
    }

    std::string live(Kind kind, const std::string& text) {
        return display.live ? display.live(kind, text) : std::string();
    }

    void finish(const std::string& id, Kind kind, const std::string& text, long long durationMs) {
        if (display.finish) {
            display.finish(id, kind, text, durationMs);
        }
    }
}
