#include "ui/exui/widgets/TextBuffer.h"
#include <algorithm>
#include <cwctype>

namespace exui {
    namespace {

        bool IsHighSurrogate(wchar_t ch) {
            return ch >= 0xD800 && ch <= 0xDBFF;
        }
        bool IsLowSurrogate(wchar_t ch) {
            return ch >= 0xDC00 && ch <= 0xDFFF;
        }
        bool IsBreak(wchar_t ch) {
            return ch == L'\r' || ch == L'\n';
        }

        // 0 = space, 1 = word character, 2 = anything else; line breaks are their own class.
        int CharClass(wchar_t ch) {
            if (IsBreak(ch)) return 3;
            if (std::iswspace(ch)) return 0;
            if (std::iswalnum(ch) || ch == L'_' || ch >= 0x2E80) return 1; // CJK and other scripts count as words
            return 2;
        }

    } // namespace

    std::wstring TextBuffer::Normalize(std::wstring_view text, bool multiline) {
        std::wstring result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const wchar_t ch = text[i];
            if (!IsBreak(ch)) {
                result.push_back(ch);
                continue;
            }
            if (!multiline) break;
            result += L"\r\n";
            if (ch == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        }
        return result;
    }

    void TextBuffer::SetText(std::wstring text, bool caretToEnd) {
        m_text = Normalize(text, m_multiline);
        m_anchor = m_caret = caretToEnd ? m_text.size() : 0;
        m_undo.clear();
        m_redo.clear();
        m_typingEnd = npos;
    }

    void TextBuffer::SetMultiline(bool multiline) {
        if (m_multiline == multiline) return;
        m_multiline = multiline;
        SetText(m_text);
    }

    std::wstring_view TextBuffer::SelectedText() const {
        const auto [first, last] = Selection();
        return std::wstring_view(m_text).substr(first, last - first);
    }

    void TextBuffer::Select(size_t anchor, size_t caret) {
        m_anchor = std::min(anchor, m_text.size());
        m_caret = std::min(caret, m_text.size());
        m_typingEnd = npos;
    }

    void TextBuffer::MoveCaret(size_t position, bool extend) {
        Select(extend ? m_anchor : position, position);
    }

    size_t TextBuffer::Previous(size_t position) const {
        position = std::min(position, m_text.size());
        if (position == 0) return 0;
        --position;
        if (position > 0 &&
            ((IsLowSurrogate(m_text[position]) && IsHighSurrogate(m_text[position - 1])) ||
             (m_text[position] == L'\n' && m_text[position - 1] == L'\r')))
            --position;
        return position;
    }

    size_t TextBuffer::Next(size_t position) const {
        if (position >= m_text.size()) return m_text.size();
        const wchar_t ch = m_text[position++];
        if (position < m_text.size() &&
            ((IsHighSurrogate(ch) && IsLowSurrogate(m_text[position])) || (ch == L'\r' && m_text[position] == L'\n')))
            ++position;
        return position;
    }

    // Ctrl+Right: to the start of the next word (past the current run and the spaces after it).
    size_t TextBuffer::NextWord(size_t position) const {
        position = std::min(position, m_text.size());
        if (position == m_text.size()) return position;
        const int start = CharClass(m_text[position]);
        if (start == 3) return Next(position);
        if (start != 0)
            while (position < m_text.size() && CharClass(m_text[position]) == start)
                position = Next(position);
        while (position < m_text.size() && CharClass(m_text[position]) == 0)
            position = Next(position);
        return position;
    }

    // Ctrl+Left: to the start of the current or previous word.
    size_t TextBuffer::PreviousWord(size_t position) const {
        position = std::min(position, m_text.size());
        while (position > 0 && CharClass(m_text[position - 1]) == 0)
            position = Previous(position);
        if (position == 0) return 0;
        const int cls = CharClass(m_text[position - 1]);
        if (cls == 3) return Previous(position);
        while (position > 0 && CharClass(m_text[position - 1]) == cls)
            position = Previous(position);
        return position;
    }

    std::pair<size_t, size_t> TextBuffer::WordAt(size_t position) const {
        const auto line = LineOf(position);
        position = std::clamp(position, line.start, line.end);
        if (line.start == line.end) return {position, position};
        if (position == line.end) position = Previous(position);
        const int cls = CharClass(m_text[position]);
        size_t first = position, last = Next(position);
        while (first > line.start && CharClass(m_text[first - 1]) == cls)
            first = Previous(first);
        while (last < line.end && CharClass(m_text[last]) == cls)
            last = Next(last);
        return {first, last};
    }

    std::vector<TextBuffer::Line> TextBuffer::Lines() const {
        std::vector<Line> lines;
        size_t start = 0;
        for (size_t i = 0; i < m_text.size(); ++i) {
            if (m_text[i] != L'\n') continue;
            lines.push_back({start, i > start && m_text[i - 1] == L'\r' ? i - 1 : i});
            start = i + 1;
        }
        lines.push_back({start, m_text.size()});
        return lines;
    }

    size_t TextBuffer::LineIndexOf(size_t position) const {
        const auto lines = Lines();
        size_t row = 0;
        while (row + 1 < lines.size() && position >= lines[row + 1].start)
            ++row;
        return row;
    }

    void TextBuffer::Remember() {
        m_undo.push_back({m_text, m_anchor, m_caret});
        m_redo.clear();
    }

    bool TextBuffer::Replace(std::wstring_view value) {
        const auto insert = Normalize(value, m_multiline);
        const auto [first, last] = Selection();
        if (insert.empty() && first == last) return false;
        // Consecutive typing forms one undo step.
        const bool typing = insert.size() <= 2 && first == last && first == m_typingEnd;
        if (!typing) Remember();
        m_text.replace(first, last - first, insert);
        m_anchor = m_caret = first + insert.size();
        m_typingEnd = insert.size() <= 2 && !insert.empty() ? m_caret : npos;
        return true;
    }

    bool TextBuffer::DeleteBackward(bool word) {
        if (!HasSelection()) {
            if (m_caret == 0) return false;
            Select(word ? PreviousWord(m_caret) : Previous(m_caret), m_caret);
        }
        return Replace({});
    }

    bool TextBuffer::DeleteForward(bool word) {
        if (!HasSelection()) {
            if (m_caret == m_text.size()) return false;
            Select(m_caret, word ? NextWord(m_caret) : Next(m_caret));
        }
        return Replace({});
    }

    bool TextBuffer::Undo() {
        if (m_undo.empty()) return false;
        m_redo.push_back({m_text, m_anchor, m_caret});
        auto snapshot = std::move(m_undo.back());
        m_undo.pop_back();
        m_text = std::move(snapshot.text);
        m_anchor = snapshot.anchor;
        m_caret = snapshot.caret;
        m_typingEnd = npos;
        return true;
    }

    bool TextBuffer::Redo() {
        if (m_redo.empty()) return false;
        m_undo.push_back({m_text, m_anchor, m_caret});
        auto snapshot = std::move(m_redo.back());
        m_redo.pop_back();
        m_text = std::move(snapshot.text);
        m_anchor = snapshot.anchor;
        m_caret = snapshot.caret;
        m_typingEnd = npos;
        return true;
    }

} // namespace exui
