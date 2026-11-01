#include "app/launch/LogViewModel.h"

#include <algorithm>
#include <cctype>

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
}
