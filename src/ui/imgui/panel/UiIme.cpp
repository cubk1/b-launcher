#include "UiIme.h"
#include "UiInput.h"

namespace UiIme {

    namespace {
        std::string composing;
        std::string committed;
        int cursor = 0;
    }

    void set(const std::string& text, int caret) {
        composing = text;
        cursor = caret;
    }

    void clear() {
        composing.clear();
        cursor = 0;
    }

    void commit(const std::string& text) {
        committed += text;
    }

    std::string takeCommitted() {
        std::string out;
        out.swap(committed);
        return out;
    }

    bool active() {
        return !composing.empty();
    }

    const std::string& text() {
        return composing;
    }

    int caret() {
        return cursor;
    }

    bool wanted() {
        return !UiInput::focused.empty() && UiInput::secrets.count(UiInput::focused) == 0;
    }
}
