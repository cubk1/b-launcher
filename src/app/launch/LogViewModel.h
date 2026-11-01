#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace LogViewModel {

    // Each view feeds the lines it displays, in increasing log-number order.
    // Folding, selection, scrolling and the log's lifetime belong to the caller.
    class Search {
    public:
        const std::string& query() const { return text; }
        size_t count() const { return matches.size(); }
        int index() const { return position; }
        std::optional<uint64_t> current() const;

        bool setQuery(std::string_view query);
        bool contains(std::string_view line) const;
        void scan(uint64_t number, std::string_view line);
        void resetMatches();
        void discardBefore(uint64_t first);
        void erase(uint64_t number);
        std::optional<uint64_t> step(int direction, uint64_t top);

        static std::string lowered(std::string_view text);

    private:
        std::string text;
        std::deque<uint64_t> matches;
        int position = -1;
    };

    // The log fonts' existing column convention: UTF-8 sequences of three or
    // four bytes occupy two columns, shorter sequences one.
    int columnsBefore(std::string_view text, size_t bytes);
    std::string_view columnsOf(std::string_view text, int skip, int columns, int& skipped);
}
