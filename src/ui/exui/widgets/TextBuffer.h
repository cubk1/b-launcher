#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace exui {

    // Text with a selection, as edited by EditBox. Positions index UTF-16 code
    // units; a surrogate pair or a CRLF line break is always crossed as one step.
    class TextBuffer {
    public:
        struct Line {
            size_t start, end; // end excludes the line break
        };

        explicit TextBuffer(bool multiline = false) : m_multiline(multiline) {}

        const std::wstring& Text() const noexcept {
            return m_text;
        }
        // Replaces everything and puts the caret at the start (or the end). Clears undo.
        void SetText(std::wstring text, bool caretToEnd = false);
        bool IsMultiline() const noexcept {
            return m_multiline;
        }
        void SetMultiline(bool multiline);

        size_t Anchor() const noexcept {
            return m_anchor;
        }
        size_t Caret() const noexcept {
            return m_caret;
        }
        std::pair<size_t, size_t> Selection() const noexcept {
            return std::minmax(m_anchor, m_caret);
        }
        bool HasSelection() const noexcept {
            return m_anchor != m_caret;
        }
        std::wstring_view SelectedText() const;
        void Select(size_t anchor, size_t caret);
        void SelectAll() {
            Select(0, m_text.size());
        }
        // Moves the caret; with extend the anchor stays, otherwise the selection collapses.
        void MoveCaret(size_t position, bool extend);

        size_t Previous(size_t position) const;
        size_t Next(size_t position) const;
        size_t PreviousWord(size_t position) const;
        size_t NextWord(size_t position) const;
        // The word (or run of other characters) around position, on its line.
        std::pair<size_t, size_t> WordAt(size_t position) const;

        std::vector<Line> Lines() const;
        size_t LineIndexOf(size_t position) const;
        Line LineOf(size_t position) const {
            return Lines()[LineIndexOf(position)];
        }

        // Replaces the selection; the caret ends after the inserted text. Single-line
        // buffers keep only the first line of value. Returns whether the text changed.
        bool Replace(std::wstring_view value);
        // Deletes the selection, or the step/word next to the caret when there is none.
        bool DeleteBackward(bool word = false);
        bool DeleteForward(bool word = false);

        bool CanUndo() const noexcept {
            return !m_undo.empty();
        }
        bool CanRedo() const noexcept {
            return !m_redo.empty();
        }
        bool Undo();
        bool Redo();

        // Line breaks become CRLF; single-line text is cut at the first break.
        static std::wstring Normalize(std::wstring_view text, bool multiline);

    private:
        struct Snapshot {
            std::wstring text;
            size_t anchor, caret;
        };
        static constexpr size_t npos = size_t(-1);
        void Remember();
        std::wstring m_text;
        size_t m_anchor = 0, m_caret = 0;
        size_t m_typingEnd = npos; // caret position after the last typed character
        bool m_multiline = false;
        std::vector<Snapshot> m_undo, m_redo;
    };

} // namespace exui
