#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <vector>

namespace Markdown {

    enum class Format { Markdown, Html };
    enum class Align { Start, Center, End };

    constexpr uint16_t STRONG = 1;
    constexpr uint16_t EM = 2;
    constexpr uint16_t STRIKE = 4;
    constexpr uint16_t CODE = 8;
    constexpr uint16_t SMALL = 16;
    constexpr uint16_t SPOILER = 32;
    constexpr uint16_t KBD = 64;
    constexpr uint16_t UNDERLINE = 128;

    struct Span {
        enum class Kind { Text, Image, Break };
        Kind kind = Kind::Text;
        std::string text;
        uint16_t style = 0;
        int link = -1;
        std::string src;
        float width = 0.0f;
        float height = 0.0f;
        float percent = 0.0f;
    };

    struct Block {
        enum class Kind { Paragraph, Heading, List, Item, Quote, Code, Rule, Table, Details, Container, Video };
        Kind kind = Kind::Paragraph;
        Align align = Align::Start;
        int level = 0;
        std::vector<Span> spans;
        std::list<Block> children;
        bool ordered = false;
        int start = 1;
        int checked = -1;
        bool open = false;
        std::string text;
        std::string url;
        std::vector<Align> columns;
        std::vector<std::vector<std::vector<Span>>> rows;
        int headerRows = 0;
    };

    struct Doc {
        std::list<Block> blocks;
        std::vector<std::string> links;
        std::vector<std::string> unknownTags;
    };

    Doc parse(std::string_view text, Format format);
    std::string plain(const std::vector<Span>& spans);
}
