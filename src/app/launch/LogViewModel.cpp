#include "app/launch/LogViewModel.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace LogViewModel {

    std::string Search::lowered(std::string_view value) {
        std::string out(value);
        for (char& c : out) {
            c = (char) std::tolower((unsigned char) c);
        }
        return out;
    }

    std::optional<uint64_t> Search::current() const {
        return position >= 0 && position < (int) matches.size()
                ? std::optional<uint64_t>(matches[(size_t) position]) : std::nullopt;
    }

    bool Search::setQuery(std::string_view value) {
        std::string lower = lowered(value);
        if (lower == text) {
            return false;
        }
        text = std::move(lower);
        resetMatches();
        return true;
    }

    bool Search::contains(std::string_view line) const {
        return !text.empty() && std::search(line.begin(), line.end(), text.begin(), text.end(), [](char a, char b) {
            return (char) std::tolower((unsigned char) a) == b;
        }) != line.end();
    }

    void Search::scan(uint64_t number, std::string_view line) {
        if (contains(line)) {
            matches.push_back(number);
        }
    }

    void Search::resetMatches() {
        matches.clear();
        position = -1;
    }

    void Search::discardBefore(uint64_t first) {
        while (!matches.empty() && matches.front() < first) {
            matches.pop_front();
            position = std::max(-1, position - 1);
        }
    }

    void Search::erase(uint64_t number) {
        auto at = std::lower_bound(matches.begin(), matches.end(), number);
        if (at == matches.end() || *at != number) {
            return;
        }
        int index = (int) (at - matches.begin());
        matches.erase(at);
        if (index < position) {
            position--;
        }
        position = std::min(position, (int) matches.size() - 1);
    }

    std::optional<uint64_t> Search::step(int direction, uint64_t top) {
        if (matches.empty()) {
            return std::nullopt;
        }
        if (position < 0) {
            position = (int) (std::lower_bound(matches.begin(), matches.end(), top) - matches.begin());
            if (direction < 0) {
                position--;
            }
        } else {
            position += direction;
        }
        int count = (int) matches.size();
        position = ((position % count) + count) % count;
        return current();
    }

    namespace {
        size_t lengthOf(unsigned char lead) {
            return lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
        }

        constexpr std::string_view DELIMITERS = "/\\()\"'-.,:;<>~!@#$%^&*|+=[]{}?";

        int classOf(std::string_view text, size_t at, size_t length) {
            unsigned char lead = (unsigned char) text[at];
            if (lead == ' ' || lead == '\t') {
                return 0;
            }
            if ((length == 1 && DELIMITERS.find((char) lead) != std::string_view::npos) ||
                text.compare(at, length, "\xE2\x94\x82") == 0) {
                return 1;
            }
            return 2;
        }
    }

    int columnsBefore(std::string_view text, size_t bytes) {
        int column = 0;
        for (size_t i = 0; i < bytes && i < text.size();) {
            size_t length = lengthOf((unsigned char) text[i]);
            column += length >= 3 ? 2 : 1;
            i += length;
        }
        return column;
    }

    std::string_view columnsOf(std::string_view text, int skip, int columns, int& skipped) {
        size_t i = 0;
        int column = 0;
        size_t start = std::string_view::npos;
        skipped = 0;
        while (i < text.size()) {
            size_t length = lengthOf((unsigned char) text[i]);
            if (start == std::string_view::npos && column >= skip) {
                start = i;
                skipped = column;
            }
            if (start != std::string_view::npos && column - skipped >= columns) {
                return text.substr(start, i - start);
            }
            column += length >= 3 ? 2 : 1;
            i += length;
        }
        return start == std::string_view::npos ? std::string_view() : text.substr(start);
    }

    size_t byteAt(std::string_view text, int column) {
        int at = 0;
        size_t i = 0;
        while (i < text.size() && at < column) {
            size_t length = lengthOf((unsigned char) text[i]);
            at += length >= 3 ? 2 : 1;
            i += length;
        }
        return std::min(i, text.size());
    }

    int nearestColumn(std::string_view text, float column) {
        int at = 0;
        for (size_t i = 0; i < text.size();) {
            size_t length = lengthOf((unsigned char) text[i]);
            int width = length >= 3 ? 2 : 1;
            if (column < at + width * 0.5f) {
                return at;
            }
            at += width;
            i += length;
        }
        return at;
    }

    int stepColumn(std::string_view text, int column, int direction) {
        int at = 0;
        int last = 0;
        for (size_t i = 0; i < text.size();) {
            size_t length = lengthOf((unsigned char) text[i]);
            at += length >= 3 ? 2 : 1;
            i += length;
            if (direction > 0 && at > column) {
                return at;
            }
            if (direction < 0 && at >= column) {
                return last;
            }
            last = at;
        }
        return at;
    }

    std::vector<int> wrapStarts(std::string_view text, int columns) {
        std::vector<int> starts = {0};
        int at = 0;
        for (size_t i = 0; i < text.size();) {
            size_t length = lengthOf((unsigned char) text[i]);
            int width = length >= 3 ? 2 : 1;
            if (at + width - starts.back() > columns && at > starts.back()) {
                starts.push_back(at);
            }
            at += width;
            i += length;
        }
        return starts;
    }

    int wrappedRows(std::string_view text, int columns) {
        if ((int) text.size() <= columns) {
            return 1;
        }
        int rows = 1;
        int start = 0;
        int at = 0;
        for (size_t i = 0; i < text.size();) {
            size_t length = lengthOf((unsigned char) text[i]);
            int width = length >= 3 ? 2 : 1;
            if (at + width - start > columns && at > start) {
                rows++;
                start = at;
            }
            at += width;
            i += length;
        }
        return rows;
    }

    std::pair<int, int> wordAround(std::string_view text, int column) {
        struct Glyph {
            int start;
            int end;
            int kind;
        };
        std::vector<Glyph> glyphs;
        int at = 0;
        int hit = -1;
        for (size_t i = 0; i < text.size();) {
            size_t length = std::min(lengthOf((unsigned char) text[i]), text.size() - i);
            int width = length >= 3 ? 2 : 1;
            if (column >= at && column < at + width) {
                hit = (int) glyphs.size();
            }
            glyphs.push_back(Glyph{at, at + width, classOf(text, i, length)});
            at += width;
            i += length;
        }
        if (hit < 0) {
            return {at, at};
        }
        int first = hit;
        int last = hit;
        while (first > 0 && glyphs[(size_t) first - 1].kind == glyphs[(size_t) hit].kind) {
            first--;
        }
        while (last + 1 < (int) glyphs.size() && glyphs[(size_t) last + 1].kind == glyphs[(size_t) hit].kind) {
            last++;
        }
        return {glyphs[(size_t) first].start, glyphs[(size_t) last].end};
    }
}
