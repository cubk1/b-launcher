#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UiInput {

    extern std::unordered_map<std::string, std::string> texts;
    extern std::unordered_map<std::string, int> carets;
    extern std::unordered_map<std::string, int> anchors;
    extern std::unordered_map<std::string, int> limits;
    extern std::unordered_map<std::string, int> byteLimits;
    extern std::unordered_map<std::string, std::vector<std::string>> undoText;
    extern std::unordered_map<std::string, std::vector<int>> undoCaret;
    extern std::unordered_map<std::string, std::vector<std::string>> redoText;
    extern std::unordered_map<std::string, std::vector<int>> redoCaret;
    extern std::unordered_map<std::string, long long> typedAt;
    extern std::string focused;
    extern std::unordered_set<std::string> secrets;

    std::string get(const std::string& id);
    std::string shown(const std::string& id);
    std::string masked(const std::string& value);
    void set(const std::string& id, const std::string& value);
    int caret(const std::string& id);
    int anchor(const std::string& id);
    int selectionStart(const std::string& id);
    int selectionEnd(const std::string& id);
    bool hasSelection(const std::string& id);
    std::string selection(const std::string& id);
    void select(const std::string& id, const std::string& value);
    void selectRange(const std::string& id, int from, int to);
    void moveCaret(const std::string& id, int index, bool keepAnchor);
    int limit(const std::string& id);
    int byteLimit(const std::string& id);
    void setByteLimit(const std::string& id, int max);
    int utf8Length(const std::string& value);
    void focus(const std::string& id, int maxLength);
    void blur();
    bool isFocused(const std::string& id);
    bool ctrlDown();
    bool shiftDown();
    std::string clipboard();
    void setClipboard(const std::string& value);
    std::string filterAllowed(const std::string& value);
    void snapshot(const std::string& id, bool coalesce);
    void undo(const std::string& id);
    void redo(const std::string& id);
    bool canUndo(const std::string& id);
    bool canRedo(const std::string& id);
    void replaceSelection(const std::string& id, const std::string& insert);
    void deleteSelection(const std::string& id);
    bool wordChar(char32_t c);
    int wordLeft(const std::string& id, int from);
    int wordRight(const std::string& id, int from);
    void selectWordAt(const std::string& id, int index);
    void cut(const std::string& id);
    void copy(const std::string& id);
    void paste(const std::string& id);
    void selectAll(const std::string& id);
    void deleteForward(const std::string& id);
    bool keyPressed(const std::string& id, int key);
    bool charTyped(const std::string& id, char32_t typed);
}
