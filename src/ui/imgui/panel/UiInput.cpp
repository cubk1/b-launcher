#include "UiInput.h"
#include "UiCommon.h"

#include "imgui.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace UiInput {

    std::unordered_map<std::string, std::string> texts;
    std::unordered_map<std::string, int> carets;
    std::unordered_map<std::string, int> anchors;
    std::unordered_map<std::string, int> limits;
    std::unordered_map<std::string, int> byteLimits;
    std::string focused;
    std::unordered_set<std::string> secrets;

    namespace {
        struct Edit {
            std::string text;
            int caret;
        };
        struct History {
            std::vector<Edit> undo;
            std::vector<Edit> redo;
            std::optional<long long> typedAt;
        };
        std::unordered_map<std::string, History> histories;

        void restore(const std::string& id, History& history, bool redo) {
            auto& from = redo ? history.redo : history.undo;
            auto& to = redo ? history.undo : history.redo;
            if (from.empty()) return;
            to.push_back(Edit{get(id), caret(id)});
            Edit edit = std::move(from.back());
            from.pop_back();
            texts[id] = std::move(edit.text);
            moveCaret(id, edit.caret, false);
            history.typedAt.reset();
        }
    }

    std::string get(const std::string& id) {
        auto it = texts.find(id);
        return it == texts.end() ? std::string() : it->second;
    }

    std::string masked(const std::string& value) {
        std::string out;
        for (int i = Ui::length(value); i > 0; i--) {
            out += "\xE2\x80\xA2";
        }
        return out;
    }

    std::string shown(const std::string& id) {
        return secrets.count(id) != 0 ? masked(get(id)) : get(id);
    }

    void set(const std::string& id, const std::string& value) {
        histories.erase(id);
        texts[id] = value;
        int length = Ui::length(value);
        carets[id] = length;
        anchors[id] = length;
    }

    int caret(const std::string& id) {
        int length = Ui::length(get(id));
        auto it = carets.find(id);
        int clamped = it == carets.end() ? length : it->second;
        return std::max(0, std::min(length, clamped));
    }

    int anchor(const std::string& id) {
        int length = Ui::length(get(id));
        auto it = anchors.find(id);
        int clamped = it == anchors.end() ? caret(id) : it->second;
        return std::max(0, std::min(length, clamped));
    }

    int selectionStart(const std::string& id) {
        return std::min(caret(id), anchor(id));
    }

    int selectionEnd(const std::string& id) {
        return std::max(caret(id), anchor(id));
    }

    bool hasSelection(const std::string& id) {
        return selectionStart(id) != selectionEnd(id);
    }

    std::string selection(const std::string& id) {
        return Ui::substring(get(id), selectionStart(id), selectionEnd(id));
    }

    void select(const std::string& id, const std::string& value) {
        std::string current = get(id);
        if (value.empty()) {
            anchors[id] = caret(id);
            return;
        }
        int at = Ui::indexOf(current, value);
        if (at < 0) {
            return;
        }
        anchors[id] = at;
        carets[id] = at + Ui::length(value);
    }

    void selectRange(const std::string& id, int from, int to) {
        int length = Ui::length(get(id));
        anchors[id] = std::max(0, std::min(length, from));
        carets[id] = std::max(0, std::min(length, to));
    }

    void moveCaret(const std::string& id, int index, bool keepAnchor) {
        int length = Ui::length(get(id));
        int clamped = std::max(0, std::min(length, index));
        carets[id] = clamped;
        if (!keepAnchor) {
            anchors[id] = clamped;
        }
    }

    int limit(const std::string& id) {
        auto it = limits.find(id);
        return it == limits.end() ? 22 : it->second;
    }

    int byteLimit(const std::string& id) {
        auto it = byteLimits.find(id);
        return it == byteLimits.end() ? 0 : it->second;
    }

    void setByteLimit(const std::string& id, int max) {
        byteLimits[id] = max;
    }

    int utf8Length(const std::string& value) {
        return (int) value.size();
    }

    void focus(const std::string& id, int maxLength) {
        focused = id;
        limits[id] = maxLength;
        int length = Ui::length(get(id));
        carets[id] = length;
        anchors[id] = length;
    }

    void blur() {
        focused.clear();
    }

    bool isFocused(const std::string& id) {
        return !focused.empty() && id == focused;
    }

    bool ctrlDown() {
        ImGuiIO& io = ImGui::GetIO();
        return io.KeyCtrl;
    }

    bool shiftDown() {
        ImGuiIO& io = ImGui::GetIO();
        return io.KeyShift;
    }

    std::string clipboard() {
        const char* value = ImGui::GetClipboardText();
        return value == nullptr ? std::string() : std::string(value);
    }

    void setClipboard(const std::string& value) {
        ImGui::SetClipboardText(value.c_str());
    }

    std::string filterAllowed(const std::string& value) {
        std::u32string chars = Ui::decode(value);
        std::u32string out;
        for (char32_t c : chars) {
            if (c >= 32 && c != 127) {
                out.push_back(c);
            }
        }
        return Ui::encode(out);
    }

    void snapshot(const std::string& id, bool coalesce) {
        History& history = histories[id];
        std::string current = get(id);
        if (!history.undo.empty() && history.undo.back().text == current) {
            return;
        }
        long long now = Ui::millis();
        if (coalesce && history.typedAt && now - *history.typedAt < 600 && !history.undo.empty()) {
            history.typedAt = now;
            return;
        }
        history.typedAt = now;
        history.undo.push_back(Edit{std::move(current), caret(id)});
        while (history.undo.size() > 64) {
            history.undo.erase(history.undo.begin());
        }
        history.redo.clear();
    }

    void undo(const std::string& id) {
        auto found = histories.find(id);
        if (found != histories.end()) restore(id, found->second, false);
    }

    void redo(const std::string& id) {
        auto found = histories.find(id);
        if (found != histories.end()) restore(id, found->second, true);
    }

    bool canUndo(const std::string& id) {
        auto found = histories.find(id);
        return found != histories.end() && !found->second.undo.empty();
    }

    bool canRedo(const std::string& id) {
        auto found = histories.find(id);
        return found != histories.end() && !found->second.redo.empty();
    }

    void replaceSelection(const std::string& id, const std::string& insert) {
        std::string current = get(id);
        int from = selectionStart(id);
        int to = selectionEnd(id);
        int room = limit(id) - (Ui::length(current) - (to - from));
        std::string piece = insert;
        if (Ui::length(piece) > room) {
            piece = room <= 0 ? std::string() : Ui::substring(piece, 0, room);
        }
        int cap = byteLimit(id);
        if (cap > 0 && !piece.empty()) {
            int fixed = utf8Length(Ui::substring(current, 0, from)) + utf8Length(Ui::substring(current, to));
            while (!piece.empty() && fixed + utf8Length(piece) > cap) {
                piece = Ui::substring(piece, 0, Ui::length(piece) - 1);
            }
        }
        texts[id] = Ui::substring(current, 0, from) + piece + Ui::substring(current, to);
        moveCaret(id, from + Ui::length(piece), false);
    }

    void deleteSelection(const std::string& id) {
        replaceSelection(id, "");
    }

    bool wordChar(char32_t c) {
        return Ui::isLetterOrDigit(c) || c == '_';
    }

    int wordLeft(const std::string& id, int from) {
        std::u32string value = Ui::decode(get(id));
        int index = from;
        while (index > 0 && !wordChar(value[index - 1])) {
            index--;
        }
        while (index > 0 && wordChar(value[index - 1])) {
            index--;
        }
        return index;
    }

    int wordRight(const std::string& id, int from) {
        std::u32string value = Ui::decode(get(id));
        int index = from;
        int length = (int) value.size();
        while (index < length && !wordChar(value[index])) {
            index++;
        }
        while (index < length && wordChar(value[index])) {
            index++;
        }
        return index;
    }

    std::pair<int, int> wordAt(const std::string& id, int index) {
        std::u32string value = Ui::decode(get(id));
        int length = (int) value.size();
        if (length == 0) {
            return {0, 0};
        }
        int at = std::max(0, std::min(length - 1, index));
        bool word = wordChar(value[at]);
        bool blank = value[at] == ' ' || value[at] == '\t';
        auto same = [&](char32_t c) {
            return word ? wordChar(c) : blank ? c == ' ' || c == '\t' : !wordChar(c) && c != ' ' && c != '\t';
        };
        int from = at;
        while (from > 0 && same(value[from - 1])) {
            from--;
        }
        int to = at + 1;
        while (to < length && same(value[to])) {
            to++;
        }
        return {from, to};
    }

    void selectWordAt(const std::string& id, int index) {
        if (get(id).empty()) {
            return;
        }
        if (secrets.count(id) != 0) {
            selectAll(id);
            return;
        }
        auto [from, to] = wordAt(id, index);
        selectRange(id, from, to);
    }

    void cut(const std::string& id) {
        if (!hasSelection(id) || secrets.count(id) != 0) {
            return;
        }
        setClipboard(selection(id));
        snapshot(id, false);
        deleteSelection(id);
    }

    void copy(const std::string& id) {
        if (hasSelection(id) && secrets.count(id) == 0) {
            setClipboard(selection(id));
        }
    }

    void paste(const std::string& id) {
        std::string value = filterAllowed(clipboard());
        if (value.empty()) {
            return;
        }
        snapshot(id, false);
        replaceSelection(id, value);
    }

    void selectAll(const std::string& id) {
        selectRange(id, 0, Ui::length(get(id)));
    }

    void deleteForward(const std::string& id) {
        if (hasSelection(id)) {
            snapshot(id, false);
            deleteSelection(id);
            return;
        }
        std::string value = get(id);
        int at = caret(id);
        if (at >= Ui::length(value)) {
            return;
        }
        snapshot(id, false);
        texts[id] = Ui::substring(value, 0, at) + Ui::substring(value, at + 1);
        moveCaret(id, at, false);
    }

    bool keyPressed(const std::string& id, int key) {
        bool shift = shiftDown();
        bool ctrl = ctrlDown();

        if (ctrl && key == ImGuiKey_A) {
            selectAll(id);
            return true;
        }
        if (ctrl && key == ImGuiKey_C) {
            copy(id);
            return true;
        }
        if (ctrl && key == ImGuiKey_X) {
            cut(id);
            return true;
        }
        if (ctrl && key == ImGuiKey_V) {
            paste(id);
            return true;
        }
        if (ctrl && !shift && key == ImGuiKey_Z) {
            undo(id);
            return true;
        }
        if (ctrl && (key == ImGuiKey_Y || (shift && key == ImGuiKey_Z))) {
            redo(id);
            return true;
        }
        if (key == ImGuiKey_Backspace) {
            if (hasSelection(id)) {
                snapshot(id, false);
                deleteSelection(id);
                return true;
            }
            int at = caret(id);
            if (at <= 0) {
                return true;
            }
            int from = ctrl ? wordLeft(id, at) : at - 1;
            snapshot(id, true);
            std::string value = get(id);
            texts[id] = Ui::substring(value, 0, from) + Ui::substring(value, at);
            moveCaret(id, from, false);
            return true;
        }
        if (key == ImGuiKey_Delete) {
            if (ctrl && !hasSelection(id)) {
                selectRange(id, caret(id), wordRight(id, caret(id)));
            }
            deleteForward(id);
            return true;
        }
        if (key == ImGuiKey_LeftArrow) {
            int at = caret(id);
            if (!shift && hasSelection(id)) {
                moveCaret(id, selectionStart(id), false);
                return true;
            }
            moveCaret(id, ctrl ? wordLeft(id, at) : at - 1, shift);
            return true;
        }
        if (key == ImGuiKey_RightArrow) {
            int at = caret(id);
            if (!shift && hasSelection(id)) {
                moveCaret(id, selectionEnd(id), false);
                return true;
            }
            moveCaret(id, ctrl ? wordRight(id, at) : at + 1, shift);
            return true;
        }
        if (key == ImGuiKey_Home) {
            moveCaret(id, 0, shift);
            return true;
        }
        if (key == ImGuiKey_End) {
            moveCaret(id, Ui::length(get(id)), shift);
            return true;
        }
        if (ctrl) {
            return true;
        }
        if (key == ImGuiKey_Escape) {
            focused.clear();
            return true;
        }
        return false;
    }

    bool charTyped(const std::string& id, char32_t typed) {
        std::string filtered = filterAllowed(Ui::encode(typed));
        if (filtered.empty()) {
            return false;
        }
        if (!hasSelection(id) && Ui::length(get(id)) >= limit(id)) {
            return true;
        }
        snapshot(id, true);
        replaceSelection(id, filtered);
        return true;
    }
}
