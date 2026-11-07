#include "ui/imgui/screens/shell/Markdown.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace Markdown {

    namespace {
        struct Tag {
            std::string name;
            std::vector<std::pair<std::string, std::string>> attrs;
            bool closing = false;
            bool selfClosing = false;

            std::string attr(const char* key) const {
                for (const auto& [name, value] : attrs) {
                    if (name == key) {
                        return value;
                    }
                }
                return std::string();
            }
        };

        char32_t nextCode(std::string_view text, size_t& i) {
            unsigned char c = (unsigned char) text[i++];
            if (c < 0x80) {
                return c;
            }
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            char32_t code = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : c & 0x1F;
            for (int k = 0; k < extra && i < text.size(); k++) {
                unsigned char d = (unsigned char) text[i];
                if ((d & 0xC0) != 0x80) {
                    break;
                }
                code = (code << 6) | (d & 0x3F);
                i++;
            }
            return extra == 0 ? 0xFFFD : code;
        }

        void appendUtf8(std::string& out, char32_t c) {
            if (c < 0x80) {
                out += (char) c;
            } else if (c < 0x800) {
                out += (char) (0xC0 | (c >> 6));
                out += (char) (0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                out += (char) (0xE0 | (c >> 12));
                out += (char) (0x80 | ((c >> 6) & 0x3F));
                out += (char) (0x80 | (c & 0x3F));
            } else {
                out += (char) (0xF0 | (c >> 18));
                out += (char) (0x80 | ((c >> 12) & 0x3F));
                out += (char) (0x80 | ((c >> 6) & 0x3F));
                out += (char) (0x80 | (c & 0x3F));
            }
        }

        std::string lower(std::string_view text) {
            std::string out(text);
            for (char& c : out) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return out;
        }

        bool space(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        bool punct(char c) {
            return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
        }

        std::string_view trim(std::string_view text) {
            while (!text.empty() && space(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && space(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        std::string_view trimLeft(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
                text.remove_prefix(1);
            }
            return text;
        }

        bool blank(std::string_view line) {
            return trim(line).empty();
        }

        size_t indentOf(std::string_view line) {
            size_t n = 0;
            for (char c : line) {
                if (c == ' ') {
                    n++;
                } else if (c == '\t') {
                    n += 4 - n % 4;
                } else {
                    break;
                }
            }
            return n;
        }

        std::string stripIndent(std::string_view line, size_t columns) {
            size_t n = 0;
            size_t i = 0;
            while (i < line.size() && n < columns && (line[i] == ' ' || line[i] == '\t')) {
                n += line[i] == '\t' ? 4 - n % 4 : 1;
                i++;
            }
            std::string out = n > columns ? std::string(n - columns, ' ') : std::string();
            out.append(line.substr(i));
            return out;
        }

        const std::unordered_map<std::string, char32_t>& entities() {
            static const std::unordered_map<std::string, char32_t> table = {
                    {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},      {"apos", '\''},
                    {"nbsp", ' '},     {"ensp", ' '},     {"emsp", ' '},     {"thinsp", ' '},    {"copy", 0xA9},
                    {"reg", 0xAE},     {"trade", 0x2122}, {"mdash", 0x2014}, {"ndash", 0x2013},  {"hellip", 0x2026},
                    {"laquo", 0xAB},   {"raquo", 0xBB},   {"bull", 0x2022},  {"middot", 0xB7},   {"rarr", 0x2192},
                    {"larr", 0x2190},  {"uarr", 0x2191},  {"darr", 0x2193},  {"times", 0xD7},    {"deg", 0xB0},
                    {"plusmn", 0xB1},  {"para", 0xB6},    {"sect", 0xA7},    {"euro", 0x20AC},   {"pound", 0xA3},
                    {"yen", 0xA5},     {"cent", 0xA2},    {"lsquo", 0x2018}, {"rsquo", 0x2019},  {"ldquo", 0x201C},
                    {"rdquo", 0x201D}, {"frac12", 0xBD},  {"frac14", 0xBC},  {"hearts", 0x2665}, {"check", 0x2713},
                    {"star", 0x2606},  {"zwj", 0},        {"zwnj", 0},       {"shy", 0},         {"lrm", 0},
                    {"rlm", 0},        {"colon", ':'},    {"lpar", '('},     {"rpar", ')'},      {"lbrack", '['},
                    {"rbrack", ']'},   {"verbar", '|'},   {"ast", '*'},      {"num", '#'},       {"excl", '!'},
                    {"quest", '?'},    {"period", '.'},   {"comma", ','},    {"lowbar", '_'},    {"grave", '`'},
            };
            return table;
        }

        bool entityAt(std::string_view text, size_t i, std::string& out, size_t& end) {
            size_t semi = text.find(';', i + 1);
            if (semi == std::string_view::npos || semi - i > 12 || semi == i + 1) {
                return false;
            }
            std::string_view name = text.substr(i + 1, semi - i - 1);
            char32_t code = 0;
            if (name[0] == '#') {
                bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
                std::string digits(name.substr(hex ? 2 : 1));
                if (digits.empty()) {
                    return false;
                }
                char* stop = nullptr;
                unsigned long value = std::strtoul(digits.c_str(), &stop, hex ? 16 : 10);
                if (*stop != '\0' || value > 0x10FFFF) {
                    return false;
                }
                code = value == 0 ? 0xFFFD : (char32_t) value;
            } else {
                auto found = entities().find(std::string(name));
                if (found == entities().end()) {
                    return false;
                }
                code = found->second;
            }
            if (code != 0) {
                appendUtf8(out, code);
            }
            end = semi + 1;
            return true;
        }

        std::string decode(std::string_view text) {
            std::string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size();) {
                size_t end = 0;
                if (text[i] == '&' && entityAt(text, i, out, end)) {
                    i = end;
                    continue;
                }
                out += text[i++];
            }
            return out;
        }

        std::string clean(std::string_view text) {
            std::string out;
            out.reserve(text.size());
            size_t i = 0;
            bool dropped = false;
            while (i < text.size()) {
                size_t from = i;
                char32_t code = nextCode(text, i);
                if (code > 0xFFFF || code == 0xFE0F || code == 0xFE0E || code == 0x200D || code == 0x20E3 ||
                    (code >= 0x2600 && code <= 0x27BF && code != 0x2713 && code != 0x2714 && code != 0x2715 &&
                     code != 0x2717 && code != 0x2605 && code != 0x2606 && code != 0x2665)) {
                    dropped = true;
                    continue;
                }
                if (dropped && code == ' ' && (out.empty() || out.back() == ' ')) {
                    dropped = false;
                    continue;
                }
                dropped = false;
                out.append(text, from, i - from);
            }
            return out;
        }

        std::string collapse(std::string_view text) {
            std::string out;
            out.reserve(text.size());
            bool gap = false;
            for (char c : text) {
                if (space(c)) {
                    gap = true;
                    continue;
                }
                if (gap) {
                    out += ' ';
                    gap = false;
                }
                out += c;
            }
            if (gap) {
                out += ' ';
            }
            return out;
        }

        bool readTag(std::string_view text, size_t at, Tag& tag, size_t& end) {
            size_t i = at + 1;
            if (i < text.size() && text[i] == '/') {
                tag.closing = true;
                i++;
            }
            size_t nameStart = i;
            if (i >= text.size() || !((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z'))) {
                return false;
            }
            while (i < text.size() && (std::isalnum((unsigned char) text[i]) || text[i] == '-' || text[i] == ':')) {
                i++;
            }
            tag.name = lower(text.substr(nameStart, i - nameStart));
            while (i < text.size()) {
                while (i < text.size() && space(text[i])) {
                    i++;
                }
                if (i >= text.size()) {
                    return false;
                }
                if (text[i] == '>') {
                    end = i + 1;
                    return true;
                }
                if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '>') {
                    tag.selfClosing = true;
                    end = i + 2;
                    return true;
                }
                if (text[i] == '<') {
                    return false;
                }
                size_t keyStart = i;
                while (i < text.size() && !space(text[i]) && text[i] != '=' && text[i] != '>' &&
                       !(text[i] == '/' && i + 1 < text.size() && text[i + 1] == '>')) {
                    i++;
                }
                std::string key = lower(text.substr(keyStart, i - keyStart));
                while (i < text.size() && space(text[i])) {
                    i++;
                }
                std::string value;
                if (i < text.size() && text[i] == '=') {
                    i++;
                    while (i < text.size() && space(text[i])) {
                        i++;
                    }
                    if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
                        char quote = text[i++];
                        size_t close = text.find(quote, i);
                        if (close == std::string_view::npos) {
                            return false;
                        }
                        value = decode(text.substr(i, close - i));
                        i = close + 1;
                    } else {
                        size_t valueStart = i;
                        while (i < text.size() && !space(text[i]) && text[i] != '>') {
                            i++;
                        }
                        value = decode(text.substr(valueStart, i - valueStart));
                    }
                }
                if (key.empty()) {
                    i++;
                    continue;
                }
                tag.attrs.emplace_back(std::move(key), std::move(value));
            }
            return false;
        }

        float pixels(const std::string& value, float& percent) {
            std::string_view v = trim(value);
            if (v.empty()) {
                return 0.0f;
            }
            char* stop = nullptr;
            std::string copy(v);
            float number = std::strtof(copy.c_str(), &stop);
            if (stop == copy.c_str() || number <= 0.0f) {
                return 0.0f;
            }
            if (*stop == '%') {
                percent = std::min(number, 100.0f);
                return 0.0f;
            }
            return number;
        }

        std::string styleValue(const std::string& style, const char* property) {
            std::string css = lower(style);
            size_t at = css.find(property);
            while (at != std::string::npos) {
                size_t colon = css.find(':', at);
                bool boundary = at == 0 || css[at - 1] == ';' || space(css[at - 1]);
                if (boundary && colon != std::string::npos &&
                    trim(std::string_view(css).substr(at + std::strlen(property), colon - at - std::strlen(property)))
                            .empty()) {
                    size_t stop = css.find(';', colon);
                    return std::string(trim(std::string_view(css).substr(
                            colon + 1, stop == std::string::npos ? std::string::npos : stop - colon - 1)));
                }
                at = css.find(property, at + 1);
            }
            return std::string();
        }

        Align alignOf(const Tag& tag, Align fallback) {
            std::string value = lower(tag.attr("align"));
            if (value.empty()) {
                value = styleValue(tag.attr("style"), "text-align");
            }
            if (value == "center" || value == "middle") {
                return Align::Center;
            }
            if (value == "right" || value == "end") {
                return Align::End;
            }
            if (value == "left" || value == "start" || value == "justify") {
                return Align::Start;
            }
            return fallback;
        }

        std::string videoUrl(std::string src) {
            if (src.rfind("//", 0) == 0) {
                src = "https:" + src;
            }
            for (const char* prefix : {"https://www.youtube.com/embed/", "https://youtube.com/embed/",
                                       "https://www.youtube-nocookie.com/embed/", "http://www.youtube.com/embed/"}) {
                if (src.rfind(prefix, 0) == 0) {
                    std::string id = src.substr(std::strlen(prefix));
                    id = id.substr(0, id.find_first_of("?&#/"));
                    return "https://www.youtube.com/watch?v=" + id;
                }
            }
            return src;
        }

        bool isVoid(const std::string& name) {
            static const std::set<std::string> names = {"area",  "base", "br",   "col",   "embed",  "hr",    "img",
                                                        "input", "link", "meta", "param", "source", "track", "wbr"};
            return names.count(name) != 0;
        }

        bool isContainer(const std::string& name) {
            static const std::set<std::string> names = {
                    "div",    "center",     "p",       "section", "article", "header", "footer", "main",
                    "figure", "figcaption", "aside",   "nav",     "address", "dl",     "dd",     "dt",
                    "form",   "fieldset",   "caption", "body",    "html",    "hgroup"};
            return names.count(name) != 0;
        }

        bool isKnownInline(const std::string& name) {
            static const std::set<std::string> names = {
                    "a",        "b",       "strong",  "i",     "em",     "cite",   "var",      "s",          "del",
                    "strike",   "u",       "ins",     "code",  "tt",     "samp",   "kbd",      "sup",        "sub",
                    "small",    "span",    "font",    "abbr",  "mark",   "label",  "picture",  "big",        "q",
                    "time",     "img",     "br",      "wbr",   "source", "video",  "iframe",   "embed",      "audio",
                    "object",   "track",   "head",    "title", "meta",   "link",   "thead",    "tbody",      "tfoot",
                    "tr",       "td",      "th",      "table", "ul",     "ol",     "li",       "blockquote", "pre",
                    "hr",       "details", "summary", "h1",    "h2",     "h3",     "h4",       "h5",         "h6",
                    "colgroup", "col",     "svg",     "path",  "g",      "button", "input",    "nobr",       "bdi",
                    "bdo",      "dfn",     "data",    "ruby",  "rt",     "rp",     "noscript", "style",      "script"};
            return names.count(name) != 0;
        }

        uint16_t styleOf(const Tag& tag) {
            const std::string& n = tag.name;
            if (n == "b" || n == "strong") {
                return STRONG;
            }
            if (n == "i" || n == "em" || n == "cite" || n == "var" || n == "dfn") {
                return EM;
            }
            if (n == "s" || n == "del" || n == "strike") {
                return STRIKE;
            }
            if (n == "u" || n == "ins") {
                return UNDERLINE;
            }
            if (n == "code" || n == "tt" || n == "samp") {
                return CODE;
            }
            if (n == "kbd") {
                return KBD;
            }
            if (n == "sup" || n == "sub" || n == "small") {
                return SMALL;
            }
            if (n == "span" || n == "div" || n == "p") {
                std::string cls = lower(tag.attr("class"));
                if (cls.find("spoiler") != std::string::npos) {
                    return SPOILER;
                }
                std::string weight = styleValue(tag.attr("style"), "font-weight");
                if (weight == "bold" || weight == "bolder" || weight == "700" || weight == "800" || weight == "900") {
                    return STRONG;
                }
                if (styleValue(tag.attr("style"), "font-style") == "italic") {
                    return EM;
                }
            }
            return 0;
        }

        class Builder {
        public:
            explicit Builder(Doc& doc) : doc(doc) {}

            int link(const std::string& url) {
                std::string_view value = trim(url);
                if (value.empty()) {
                    return -1;
                }
                doc.links.emplace_back(value);
                return (int) doc.links.size() - 1;
            }

            void block(Block&& b) {
                para = nullptr;
                container().push_back(std::move(b));
            }

            Block& push(Block&& b) {
                para = nullptr;
                std::list<Block>* into = nullptr;
                if (b.kind == Block::Kind::Item) {
                    for (size_t i = frames.size(); i-- > 0;) {
                        if (frames[i].block == nullptr || frames[i].cell || frames[i].summary) {
                            continue;
                        }
                        if (frames[i].block->kind == Block::Kind::List) {
                            into = &frames[i].block->children;
                        }
                        break;
                    }
                }
                if (into == nullptr) {
                    into = &container();
                }
                into->push_back(std::move(b));
                Frame frame;
                frame.block = &into->back();
                frames.push_back(frame);
                return into->back();
            }

            size_t depth() const {
                return frames.size();
            }

            void popTo(size_t size) {
                while (frames.size() > size) {
                    if (frames.back().block != nullptr && !frames.back().cell && !frames.back().summary) {
                        para = nullptr;
                    }
                    frames.pop_back();
                }
            }

            void html(std::string_view raw) {
                size_t i = 0;
                while (i < raw.size()) {
                    if (raw[i] == '<') {
                        if (raw.compare(i, 4, "<!--") == 0) {
                            size_t close = raw.find("-->", i + 4);
                            i = close == std::string_view::npos ? raw.size() : close + 3;
                            continue;
                        }
                        if (raw.compare(i, 2, "<!") == 0 || raw.compare(i, 2, "<?") == 0) {
                            size_t close = raw.find('>', i);
                            i = close == std::string_view::npos ? raw.size() : close + 1;
                            continue;
                        }
                        Tag tag;
                        size_t end = 0;
                        if (readTag(raw, i, tag, end)) {
                            i = end;
                            if (tag.closing) {
                                close(tag.name);
                                continue;
                            }
                            if (tag.name == "script" || tag.name == "style" || tag.name == "svg" ||
                                tag.name == "noscript" || tag.name == "head" || tag.name == "title") {
                                if (!tag.selfClosing) {
                                    size_t close = findClose(raw, i, tag.name);
                                    i = close;
                                }
                                continue;
                            }
                            if (tag.name == "pre") {
                                size_t close = findClose(raw, i, "pre");
                                size_t contentEnd = raw.rfind("</", close);
                                std::string_view inner = raw.substr(
                                        i,
                                        (contentEnd == std::string_view::npos || contentEnd < i ? close : contentEnd) -
                                                i);
                                i = close;
                                preBlock(inner);
                                continue;
                            }
                            open(tag);
                            continue;
                        }
                    }
                    size_t next = raw.find('<', i + 1);
                    if (next == std::string_view::npos) {
                        next = raw.size();
                    }
                    text(raw.substr(i, next - i));
                    i = next;
                }
            }

            void finish() {
                tidy(doc.blocks);
            }

        private:
            struct Frame {
                std::string tag;
                Block* block = nullptr;
                uint16_t style = 0;
                int link = -1;
                bool cell = false;
                bool summary = false;
                bool header = false;
            };

            Doc& doc;
            std::vector<Frame> frames;
            Block* para = nullptr;
            std::list<Block>* paraIn = nullptr;
            std::set<std::string> unknown;

            static bool holds(const Block* b) {
                return b->kind == Block::Kind::Container || b->kind == Block::Kind::Quote ||
                        b->kind == Block::Kind::Item || b->kind == Block::Kind::Details;
            }

            std::list<Block>& container() {
                for (size_t i = frames.size(); i-- > 0;) {
                    Block* b = frames[i].block;
                    if (b == nullptr || frames[i].cell || frames[i].summary) {
                        continue;
                    }
                    if (holds(b)) {
                        return b->children;
                    }
                    if (b->kind == Block::Kind::List) {
                        Block item;
                        item.kind = Block::Kind::Item;
                        b->children.push_back(std::move(item));
                        Frame frame;
                        frame.tag = "li";
                        frame.block = &b->children.back();
                        frames.insert(frames.begin() + (std::ptrdiff_t) i + 1, frame);
                        return b->children.back().children;
                    }
                }
                return doc.blocks;
            }

            std::vector<Span>* inlineTarget(bool create) {
                for (size_t i = frames.size(); i-- > 0;) {
                    Frame& frame = frames[i];
                    if (frame.cell && frame.block != nullptr) {
                        if (frame.block->rows.empty() || frame.block->rows.back().empty()) {
                            return nullptr;
                        }
                        return &frame.block->rows.back().back();
                    }
                    if (frame.summary && frame.block != nullptr) {
                        return &frame.block->spans;
                    }
                    if (frame.block == nullptr) {
                        continue;
                    }
                    if (frame.block->kind == Block::Kind::Heading) {
                        return &frame.block->spans;
                    }
                    if (frame.block->kind == Block::Kind::Table) {
                        return nullptr;
                    }
                    if (frame.block->kind == Block::Kind::List && !create) {
                        return nullptr;
                    }
                    if (holds(frame.block) || frame.block->kind == Block::Kind::List) {
                        break;
                    }
                }
                std::list<Block>& into = container();
                if (para != nullptr && paraIn == &into && !into.empty() && &into.back() == para) {
                    return &para->spans;
                }
                if (!create) {
                    return nullptr;
                }
                Block paragraph;
                paragraph.kind = Block::Kind::Paragraph;
                into.push_back(std::move(paragraph));
                para = &into.back();
                paraIn = &into;
                return &para->spans;
            }

            uint16_t currentStyle() const {
                uint16_t style = 0;
                for (const Frame& frame : frames) {
                    style |= frame.style;
                }
                return style;
            }

            int currentLink() const {
                for (size_t i = frames.size(); i-- > 0;) {
                    if (frames[i].link >= 0) {
                        return frames[i].link;
                    }
                }
                return -1;
            }

            Block* openBlock(Block::Kind kind) {
                for (size_t i = frames.size(); i-- > 0;) {
                    if (frames[i].block != nullptr && frames[i].block->kind == kind && !frames[i].cell &&
                        !frames[i].summary) {
                        return frames[i].block;
                    }
                }
                return nullptr;
            }

            bool inCell() const {
                for (size_t i = frames.size(); i-- > 0;) {
                    if (frames[i].cell) {
                        return true;
                    }
                    if (frames[i].block != nullptr && frames[i].block->kind == Block::Kind::Table) {
                        return false;
                    }
                }
                return false;
            }

            static size_t findClose(std::string_view raw, size_t from, const std::string& name) {
                std::string low = lower(raw.substr(from));
                size_t at = low.find("</" + name);
                if (at == std::string::npos) {
                    return raw.size();
                }
                size_t end = raw.find('>', from + at);
                return end == std::string_view::npos ? raw.size() : end + 1;
            }

            void preBlock(std::string_view inner) {
                std::string out;
                for (size_t i = 0; i < inner.size();) {
                    if (inner[i] == '<') {
                        Tag tag;
                        size_t end = 0;
                        if (readTag(inner, i, tag, end)) {
                            if (tag.name == "br") {
                                out += '\n';
                            }
                            i = end;
                            continue;
                        }
                    }
                    out += inner[i++];
                }
                std::string text = decode(out);
                while (!text.empty() && text.front() == '\n') {
                    text.erase(text.begin());
                }
                while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
                    text.pop_back();
                }
                if (inCell()) {
                    std::vector<Span>* target = inlineTarget(true);
                    if (target != nullptr) {
                        Span span;
                        span.text = clean(collapse(text));
                        span.style = CODE;
                        target->push_back(std::move(span));
                    }
                    return;
                }
                Block code;
                code.kind = Block::Kind::Code;
                code.text = text;
                block(std::move(code));
            }

            void text(std::string_view raw) {
                std::string value = collapse(decode(raw));
                if (value.empty()) {
                    return;
                }
                bool whitespace = value == " ";
                std::vector<Span>* target = inlineTarget(!whitespace);
                if (target == nullptr) {
                    return;
                }
                if (target->empty() || target->back().kind == Span::Kind::Break) {
                    while (!value.empty() && value.front() == ' ') {
                        value.erase(value.begin());
                    }
                    if (value.empty()) {
                        return;
                    }
                }
                Span span;
                span.text = clean(value);
                span.style = currentStyle();
                span.link = currentLink();
                target->push_back(std::move(span));
            }

            void breakLine() {
                std::vector<Span>* target = inlineTarget(true);
                if (target == nullptr) {
                    return;
                }
                Span span;
                span.kind = Span::Kind::Break;
                target->push_back(std::move(span));
            }

            void image(const Tag& tag) {
                std::string src = tag.attr("src");
                if (src.empty()) {
                    src = tag.attr("data-src");
                }
                if (src.empty()) {
                    return;
                }
                if (src.rfind("//", 0) == 0) {
                    src = "https:" + src;
                }
                Span span;
                span.kind = Span::Kind::Image;
                span.src = src;
                span.text = clean(tag.attr("alt"));
                span.width = pixels(tag.attr("width"), span.percent);
                float ignored = 0.0f;
                span.height = pixels(tag.attr("height"), ignored);
                std::string cssWidth = styleValue(tag.attr("style"), "width");
                if (span.width <= 0.0f && span.percent <= 0.0f && !cssWidth.empty()) {
                    span.width = pixels(cssWidth, span.percent);
                }
                span.style = currentStyle();
                span.link = currentLink();
                std::vector<Span>* target = inlineTarget(true);
                if (target != nullptr) {
                    target->push_back(std::move(span));
                }
            }

            void video(const std::string& src) {
                if (src.empty()) {
                    return;
                }
                Block b;
                b.kind = Block::Kind::Video;
                b.url = videoUrl(src);
                block(std::move(b));
            }

            void closeUntilBelow(const char* name, Block::Kind stop) {
                for (size_t i = frames.size(); i-- > 0;) {
                    if (frames[i].block != nullptr && frames[i].block->kind == stop && frames[i].tag != name) {
                        return;
                    }
                    if (frames[i].tag == name) {
                        popTo(i);
                        return;
                    }
                }
            }

            void open(const Tag& tag) {
                const std::string& name = tag.name;
                if (!isKnownInline(name) && !isContainer(name) && unknown.insert(name).second) {
                    doc.unknownTags.push_back(name);
                }
                if (name == "br" || name == "wbr") {
                    if (name == "br") {
                        breakLine();
                    }
                    return;
                }
                if (name == "img") {
                    image(tag);
                    return;
                }
                if (name == "hr") {
                    if (!inCell()) {
                        Block rule;
                        rule.kind = Block::Kind::Rule;
                        block(std::move(rule));
                    }
                    return;
                }
                if (name == "iframe" || name == "embed") {
                    video(tag.attr("src"));
                    if (name == "iframe" && !tag.selfClosing) {
                        pushTransparent(name);
                    }
                    return;
                }
                if (name == "video") {
                    std::string src = tag.attr("src");
                    if (!src.empty()) {
                        video(src);
                    }
                    if (!tag.selfClosing) {
                        Frame frame;
                        frame.tag = name;
                        frame.header = !src.empty();
                        frames.push_back(frame);
                    }
                    return;
                }
                if (name == "source") {
                    if (!frames.empty() && frames.back().tag == "video" && !frames.back().header) {
                        frames.back().header = true;
                        video(tag.attr("src"));
                    }
                    return;
                }
                if (isVoid(name)) {
                    return;
                }
                if (tag.selfClosing) {
                    return;
                }
                bool blockLevel = isContainer(name) || name == "ul" || name == "ol" || name == "table" ||
                        name == "blockquote" || name == "details" ||
                        (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6');
                if (blockLevel && !frames.empty() && frames.back().tag == "p") {
                    popTo(frames.size() - 1);
                }
                if (inCell() &&
                    (isContainer(name) || name == "ul" || name == "ol" || name == "li" || name == "blockquote" ||
                     (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6'))) {
                    std::vector<Span>* target = inlineTarget(false);
                    if (target != nullptr && !target->empty() && target->back().kind != Span::Kind::Break) {
                        Span span;
                        span.kind = Span::Kind::Break;
                        target->push_back(std::move(span));
                    }
                    pushTransparent(name, styleOf(tag));
                    return;
                }
                if (isContainer(name)) {
                    Block b;
                    b.kind = Block::Kind::Container;
                    b.align = alignOf(tag, name == "center" ? Align::Center : Align::Start);
                    b.open = name == "p";
                    push(std::move(b));
                    frames.back().tag = name;
                    frames.back().style = styleOf(tag);
                    return;
                }
                if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                    Block b;
                    b.kind = Block::Kind::Heading;
                    b.level = name[1] - '0';
                    b.align = alignOf(tag, Align::Start);
                    push(std::move(b));
                    frames.back().tag = name;
                    return;
                }
                if (name == "ul" || name == "ol") {
                    Block b;
                    b.kind = Block::Kind::List;
                    b.ordered = name == "ol";
                    std::string start = tag.attr("start");
                    b.start = start.empty() ? 1 : std::max(0, std::atoi(start.c_str()));
                    push(std::move(b));
                    frames.back().tag = name;
                    return;
                }
                if (name == "li") {
                    closeUntilBelow("li", Block::Kind::List);
                    Block* list = openBlock(Block::Kind::List);
                    if (list == nullptr) {
                        Block b;
                        b.kind = Block::Kind::List;
                        push(std::move(b));
                        frames.back().tag = "ul";
                        list = frames.back().block;
                    }
                    while (!frames.empty() && frames.back().block != list) {
                        frames.pop_back();
                    }
                    Block item;
                    item.kind = Block::Kind::Item;
                    list->children.push_back(std::move(item));
                    Frame frame;
                    frame.tag = "li";
                    frame.block = &list->children.back();
                    frames.push_back(frame);
                    para = nullptr;
                    return;
                }
                if (name == "blockquote") {
                    Block b;
                    b.kind = Block::Kind::Quote;
                    push(std::move(b));
                    frames.back().tag = name;
                    return;
                }
                if (name == "details") {
                    Block b;
                    b.kind = Block::Kind::Details;
                    b.open = false;
                    for (const auto& attr : tag.attrs) {
                        if (attr.first == "open") {
                            b.open = true;
                        }
                    }
                    push(std::move(b));
                    frames.back().tag = name;
                    return;
                }
                if (name == "summary") {
                    Block* details = openBlock(Block::Kind::Details);
                    Frame frame;
                    frame.tag = name;
                    frame.block = details;
                    frame.summary = details != nullptr;
                    frames.push_back(frame);
                    return;
                }
                if (name == "table") {
                    Block b;
                    b.kind = Block::Kind::Table;
                    b.align = alignOf(tag, Align::Start);
                    push(std::move(b));
                    frames.back().tag = name;
                    return;
                }
                if (name == "thead" || name == "tbody" || name == "tfoot") {
                    Frame frame;
                    frame.tag = name;
                    frame.header = name == "thead";
                    frames.push_back(frame);
                    return;
                }
                if (name == "tr") {
                    closeUntilBelow("tr", Block::Kind::Table);
                    Block* table = openBlock(Block::Kind::Table);
                    if (table != nullptr) {
                        table->rows.emplace_back();
                    }
                    pushTransparent(name);
                    return;
                }
                if (name == "td" || name == "th") {
                    closeUntilBelow("td", Block::Kind::Table);
                    closeUntilBelow("th", Block::Kind::Table);
                    Block* table = openBlock(Block::Kind::Table);
                    if (table == nullptr) {
                        pushTransparent(name);
                        return;
                    }
                    if (table->rows.empty()) {
                        table->rows.emplace_back();
                    }
                    table->rows.back().emplace_back();
                    bool header = name == "th";
                    for (const Frame& frame : frames) {
                        header = header || frame.header;
                    }
                    if (header && (int) table->rows.size() == table->headerRows + 1) {
                        table->headerRows = (int) table->rows.size();
                    }
                    size_t column = table->rows.back().size() - 1;
                    if (table->columns.size() <= column) {
                        table->columns.resize(column + 1, Align::Start);
                    }
                    Align align = alignOf(tag, Align::Start);
                    if (align != Align::Start) {
                        table->columns[column] = align;
                    }
                    Frame frame;
                    frame.tag = name;
                    frame.block = table;
                    frame.cell = true;
                    frame.style = header ? STRONG : 0;
                    frames.push_back(frame);
                    return;
                }
                Frame frame;
                frame.tag = name;
                frame.style = styleOf(tag);
                if (name == "a") {
                    frame.link = link(tag.attr("href"));
                }
                frames.push_back(frame);
            }

            void pushTransparent(const std::string& name, uint16_t style = 0) {
                Frame frame;
                frame.tag = name;
                frame.style = style;
                frames.push_back(frame);
            }

            void close(const std::string& name) {
                for (size_t i = frames.size(); i-- > 0;) {
                    if (frames[i].tag == name) {
                        popTo(i);
                        return;
                    }
                }
            }

            static bool visible(const std::vector<Span>& spans) {
                for (const Span& span : spans) {
                    if (span.kind == Span::Kind::Image) {
                        return true;
                    }
                    if (span.kind == Span::Kind::Text && !trim(span.text).empty()) {
                        return true;
                    }
                }
                return false;
            }

            static void tidySpans(std::vector<Span>& spans) {
                while (!spans.empty() && spans.back().kind == Span::Kind::Break) {
                    spans.pop_back();
                }
                while (!spans.empty() && spans.front().kind == Span::Kind::Break) {
                    spans.erase(spans.begin());
                }
                if (!spans.empty() && spans.back().kind == Span::Kind::Text) {
                    std::string& text = spans.back().text;
                    while (!text.empty() && text.back() == ' ') {
                        text.pop_back();
                    }
                }
                std::vector<Span> merged;
                for (Span& span : spans) {
                    if (span.kind == Span::Kind::Text && span.text.empty()) {
                        continue;
                    }
                    if (!merged.empty() && span.kind == Span::Kind::Text && merged.back().kind == Span::Kind::Text &&
                        merged.back().style == span.style && merged.back().link == span.link) {
                        merged.back().text += span.text;
                        continue;
                    }
                    merged.push_back(std::move(span));
                }
                spans = std::move(merged);
            }

            static void tidy(std::list<Block>& blocks) {
                for (auto it = blocks.begin(); it != blocks.end();) {
                    Block& b = *it;
                    tidy(b.children);
                    tidySpans(b.spans);
                    for (auto& row : b.rows) {
                        for (auto& cell : row) {
                            tidySpans(cell);
                        }
                    }
                    bool empty = false;
                    switch (b.kind) {
                        case Block::Kind::Paragraph: empty = !visible(b.spans); break;
                        case Block::Kind::Heading: empty = !visible(b.spans); break;
                        case Block::Kind::Container:
                        case Block::Kind::Quote:
                        case Block::Kind::List: empty = b.children.empty(); break;
                        case Block::Kind::Table: empty = b.rows.empty(); break;
                        default: break;
                    }
                    if (empty) {
                        it = blocks.erase(it);
                        continue;
                    }
                    ++it;
                }
            }
        };

        class Parser {
        public:
            explicit Parser(Builder& out) : out(out) {}

            void collectRefs(const std::vector<std::string>& lines) {
                bool fenced = false;
                for (const std::string& line : lines) {
                    std::string_view body = trimLeft(line);
                    if (body.rfind("```", 0) == 0 || body.rfind("~~~", 0) == 0) {
                        fenced = !fenced;
                        continue;
                    }
                    if (fenced || indentOf(line) >= 4 || body.empty() || body[0] != '[') {
                        continue;
                    }
                    size_t close = body.find("]:");
                    if (close == std::string_view::npos || close < 2) {
                        continue;
                    }
                    std::string label = lower(trim(body.substr(1, close - 1)));
                    std::string_view rest = trim(body.substr(close + 2));
                    if (rest.empty()) {
                        continue;
                    }
                    size_t end = rest.find_first_of(" \t");
                    std::string url(rest.substr(0, end));
                    if (url.size() >= 2 && url.front() == '<' && url.back() == '>') {
                        url = url.substr(1, url.size() - 2);
                    }
                    refs.emplace(label, url);
                }
            }

            void blocks(const std::vector<std::string>& lines) {
                std::string paragraph;
                auto flush = [&] {
                    if (paragraph.empty()) {
                        return;
                    }
                    Block b;
                    b.kind = Block::Kind::Paragraph;
                    b.spans = inlines(paragraph);
                    paragraph.clear();
                    if (!b.spans.empty()) {
                        out.block(std::move(b));
                    }
                };
                size_t i = 0;
                while (i < lines.size()) {
                    const std::string& line = lines[i];
                    if (blank(line)) {
                        flush();
                        i++;
                        continue;
                    }
                    size_t indent = indentOf(line);
                    std::string_view body = trimLeft(line);
                    if (indent >= 4 && paragraph.empty()) {
                        std::vector<std::string> code;
                        while (i < lines.size() && (blank(lines[i]) || indentOf(lines[i]) >= 4)) {
                            code.push_back(stripIndent(lines[i], 4));
                            i++;
                        }
                        while (!code.empty() && blank(code.back())) {
                            code.pop_back();
                        }
                        Block b;
                        b.kind = Block::Kind::Code;
                        for (size_t k = 0; k < code.size(); k++) {
                            b.text += (k ? "\n" : "") + code[k];
                        }
                        out.block(std::move(b));
                        continue;
                    }
                    if (indent < 4) {
                        if (fence(body)) {
                            flush();
                            char mark = body[0];
                            size_t length = 0;
                            while (length < body.size() && body[length] == mark) {
                                length++;
                            }
                            Block b;
                            b.kind = Block::Kind::Code;
                            b.url = std::string(trim(body.substr(length)));
                            i++;
                            bool first = true;
                            while (i < lines.size()) {
                                std::string_view inner = trimLeft(lines[i]);
                                size_t run = 0;
                                while (run < inner.size() && inner[run] == mark) {
                                    run++;
                                }
                                if (indentOf(lines[i]) < 4 && run >= length && blank(inner.substr(run))) {
                                    i++;
                                    break;
                                }
                                b.text += (first ? "" : "\n") + stripIndent(lines[i], indent);
                                first = false;
                                i++;
                            }
                            out.block(std::move(b));
                            continue;
                        }
                        if (int level = headingLevel(body)) {
                            flush();
                            std::string_view text = trim(body.substr((size_t) level));
                            while (!text.empty() && text.back() == '#') {
                                text.remove_suffix(1);
                            }
                            Block b;
                            b.kind = Block::Kind::Heading;
                            b.level = level;
                            b.spans = inlines(std::string(trim(text)));
                            out.block(std::move(b));
                            i++;
                            continue;
                        }
                        if (!paragraph.empty() && setext(body) != 0) {
                            Block b;
                            b.kind = Block::Kind::Heading;
                            b.level = setext(body);
                            b.spans = inlines(paragraph);
                            paragraph.clear();
                            out.block(std::move(b));
                            i++;
                            continue;
                        }
                        if (rule(body)) {
                            flush();
                            Block b;
                            b.kind = Block::Kind::Rule;
                            out.block(std::move(b));
                            i++;
                            continue;
                        }
                        if (body[0] == '>') {
                            flush();
                            std::vector<std::string> inner;
                            while (i < lines.size()) {
                                std::string_view q = trimLeft(lines[i]);
                                if (indentOf(lines[i]) < 4 && !q.empty() && q[0] == '>') {
                                    q.remove_prefix(1);
                                    if (!q.empty() && q[0] == ' ') {
                                        q.remove_prefix(1);
                                    }
                                    inner.emplace_back(q);
                                    i++;
                                    continue;
                                }
                                if (!blank(lines[i]) && !inner.empty() && !blank(inner.back()) && !starts(lines[i])) {
                                    inner.push_back(lines[i]);
                                    i++;
                                    continue;
                                }
                                break;
                            }
                            Block quote;
                            quote.kind = Block::Kind::Quote;
                            size_t depth = out.depth();
                            out.push(std::move(quote));
                            nested(inner);
                            out.popTo(depth);
                            continue;
                        }
                        Marker marker;
                        if (listMarker(line, marker) && (paragraph.empty() || !marker.emptyItem)) {
                            flush();
                            list(lines, i);
                            continue;
                        }
                        if (i + 1 < lines.size() && body.find('|') != std::string_view::npos &&
                            delimiterRow(lines[i + 1])) {
                            flush();
                            table(lines, i);
                            continue;
                        }
                        if (paragraph.empty() && htmlStart(body)) {
                            std::string raw;
                            std::string closer = htmlCloser(body);
                            while (i < lines.size()) {
                                if (closer.empty() && blank(lines[i])) {
                                    break;
                                }
                                raw += lines[i] + "\n";
                                i++;
                                if (!closer.empty() && lower(raw).find(closer) != std::string::npos) {
                                    break;
                                }
                            }
                            out.html(raw);
                            continue;
                        }
                        if (paragraph.empty() && refDefinition(body)) {
                            i++;
                            continue;
                        }
                    }
                    if (!paragraph.empty()) {
                        paragraph += '\n';
                    }
                    paragraph += std::string(trimLeft(line));
                    i++;
                }
                flush();
            }

        private:
            struct Marker {
                bool ordered = false;
                char bullet = 0;
                int number = 0;
                size_t content = 0;
                size_t offset = 0;
                bool emptyItem = false;
            };

            Builder& out;
            std::map<std::string, std::string> refs;
            int nesting = 0;

            void nested(const std::vector<std::string>& lines) {
                if (nesting > 24) {
                    return;
                }
                nesting++;
                blocks(lines);
                nesting--;
            }

            static bool fence(std::string_view body) {
                return body.rfind("```", 0) == 0 || body.rfind("~~~", 0) == 0;
            }

            static int headingLevel(std::string_view body) {
                int level = 0;
                while (level < (int) body.size() && body[(size_t) level] == '#') {
                    level++;
                }
                if (level < 1 || level > 6) {
                    return 0;
                }
                if ((size_t) level < body.size() && body[(size_t) level] != ' ' && body[(size_t) level] != '\t') {
                    return 0;
                }
                return level;
            }

            static int setext(std::string_view body) {
                std::string_view t = trim(body);
                if (t.empty()) {
                    return 0;
                }
                if (std::all_of(t.begin(), t.end(), [](char c) { return c == '='; })) {
                    return 1;
                }
                if (std::all_of(t.begin(), t.end(), [](char c) { return c == '-'; }) && t.size() >= 2) {
                    return 2;
                }
                return 0;
            }

            static bool rule(std::string_view body) {
                char mark = 0;
                int count = 0;
                for (char c : body) {
                    if (c == ' ' || c == '\t') {
                        continue;
                    }
                    if (c != '*' && c != '-' && c != '_') {
                        return false;
                    }
                    if (mark != 0 && c != mark) {
                        return false;
                    }
                    mark = c;
                    count++;
                }
                return count >= 3;
            }

            static bool listMarker(std::string_view line, Marker& marker) {
                size_t indent = indentOf(line);
                if (indent >= 4) {
                    return false;
                }
                std::string_view body = trimLeft(line);
                if (body.empty()) {
                    return false;
                }
                size_t after = 0;
                if (body[0] == '-' || body[0] == '*' || body[0] == '+') {
                    marker.bullet = body[0];
                    after = 1;
                } else {
                    size_t d = 0;
                    while (d < body.size() && d < 9 && body[d] >= '0' && body[d] <= '9') {
                        d++;
                    }
                    if (d == 0 || d >= body.size() || (body[d] != '.' && body[d] != ')')) {
                        return false;
                    }
                    marker.ordered = true;
                    marker.bullet = body[d];
                    marker.number = std::atoi(std::string(body.substr(0, d)).c_str());
                    after = d + 1;
                }
                if (after < body.size() && body[after] != ' ' && body[after] != '\t') {
                    return false;
                }
                std::string_view rest = body.substr(after);
                marker.emptyItem = blank(rest);
                size_t pad = 0;
                while (pad < rest.size() && rest[pad] == ' ' && pad < 5) {
                    pad++;
                }
                if (pad == 0 || pad >= 5 || marker.emptyItem) {
                    pad = 1;
                }
                marker.content = indent + after + pad;
                marker.offset = marker.emptyItem ? line.size() : line.size() - body.size() + after + pad;
                return true;
            }

            static bool delimiterRow(std::string_view line) {
                std::string_view t = trim(line);
                if (t.empty() || t.find('-') == std::string_view::npos) {
                    return false;
                }
                bool pipe = t.find('|') != std::string_view::npos;
                for (char c : t) {
                    if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t') {
                        return false;
                    }
                }
                return pipe || t.find(':') != std::string_view::npos;
            }

            static std::vector<std::string> cells(std::string_view line) {
                std::string_view t = trim(line);
                if (!t.empty() && t.front() == '|') {
                    t.remove_prefix(1);
                }
                if (!t.empty() && t.back() == '|' && (t.size() < 2 || t[t.size() - 2] != '\\')) {
                    t.remove_suffix(1);
                }
                std::vector<std::string> out;
                std::string current;
                bool code = false;
                for (size_t i = 0; i < t.size(); i++) {
                    char c = t[i];
                    if (c == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
                        current += '|';
                        i++;
                        continue;
                    }
                    if (c == '`') {
                        code = !code;
                    }
                    if (c == '|' && !code) {
                        out.emplace_back(trim(current));
                        current.clear();
                        continue;
                    }
                    current += c;
                }
                out.emplace_back(trim(current));
                return out;
            }

            bool starts(std::string_view line) const {
                if (indentOf(line) >= 4) {
                    return false;
                }
                std::string_view body = trimLeft(line);
                Marker marker;
                return fence(body) || headingLevel(body) != 0 || rule(body) || (!body.empty() && body[0] == '>') ||
                        listMarker(line, marker) || htmlStart(body);
            }

            static bool htmlStart(std::string_view body) {
                if (body.size() < 2 || body[0] != '<') {
                    return false;
                }
                if (body.rfind("<!--", 0) == 0) {
                    return true;
                }
                Tag tag;
                size_t end = 0;
                if (!readTag(body, 0, tag, end)) {
                    size_t i = 1 + (body[1] == '/' ? 1 : 0);
                    size_t s = i;
                    while (i < body.size() && std::isalnum((unsigned char) body[i])) {
                        i++;
                    }
                    std::string name = lower(body.substr(s, i - s));
                    return isContainer(name) || name == "table" || name == "details" || name == "summary" ||
                            name == "ul" || name == "ol" || name == "pre" || name == "blockquote";
                }
                const std::string& n = tag.name;
                if (isContainer(n) || n == "table" || n == "thead" || n == "tbody" || n == "tr" || n == "td" ||
                    n == "th" || n == "details" || n == "summary" || n == "ul" || n == "ol" || n == "li" ||
                    n == "pre" || n == "blockquote" || n == "hr" || n == "iframe" || n == "video" || n == "picture" ||
                    n == "script" || n == "style" || (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6')) {
                    return true;
                }
                if (n == "img" || n == "br" || n == "a" || n == "span" || n == "b" || n == "strong" || n == "font" ||
                    n == "sup" || n == "sub" || n == "i" || n == "em" || n == "small" || n == "kbd" || n == "code") {
                    return trim(body.substr(end)).empty();
                }
                return false;
            }

            static std::string htmlCloser(std::string_view body) {
                if (body.rfind("<!--", 0) == 0) {
                    return "-->";
                }
                for (const char* name : {"pre", "script", "style", "textarea"}) {
                    std::string open = std::string("<") + name;
                    if (lower(body.substr(0, open.size())) == open &&
                        (body.size() == open.size() || space(body[open.size()]) || body[open.size()] == '>')) {
                        return std::string("</") + name;
                    }
                }
                return std::string();
            }

            bool refDefinition(std::string_view body) const {
                if (body.empty() || body[0] != '[') {
                    return false;
                }
                size_t close = body.find("]:");
                if (close == std::string_view::npos || close < 2) {
                    return false;
                }
                return refs.count(lower(trim(body.substr(1, close - 1)))) != 0;
            }

            void list(const std::vector<std::string>& lines, size_t& i) {
                Marker first;
                listMarker(lines[i], first);
                Block listBlock;
                listBlock.kind = Block::Kind::List;
                listBlock.ordered = first.ordered;
                listBlock.start = first.ordered ? first.number : 1;
                size_t depth = out.depth();
                out.push(std::move(listBlock));
                while (i < lines.size()) {
                    Marker marker;
                    if (blank(lines[i])) {
                        size_t j = i;
                        while (j < lines.size() && blank(lines[j])) {
                            j++;
                        }
                        Marker next;
                        if (j < lines.size() && listMarker(lines[j], next) && next.ordered == first.ordered &&
                            next.bullet == first.bullet) {
                            i = j;
                            continue;
                        }
                        break;
                    }
                    if (!listMarker(lines[i], marker) || marker.ordered != first.ordered ||
                        marker.bullet != first.bullet) {
                        break;
                    }
                    std::vector<std::string> item;
                    std::string head = lines[i].substr(std::min(marker.offset, lines[i].size()));
                    int checked = -1;
                    std::string_view h = head;
                    if (h.size() >= 3 && h[0] == '[' && h[2] == ']' && (h[1] == ' ' || h[1] == 'x' || h[1] == 'X') &&
                        (h.size() == 3 || h[3] == ' ')) {
                        checked = h[1] == ' ' ? 0 : 1;
                        head = std::string(trimLeft(h.substr(3)));
                    }
                    item.push_back(head);
                    i++;
                    while (i < lines.size()) {
                        const std::string& l = lines[i];
                        if (blank(l)) {
                            size_t j = i;
                            while (j < lines.size() && blank(lines[j])) {
                                j++;
                            }
                            if (j < lines.size() && indentOf(lines[j]) >= marker.content) {
                                for (; i < j; i++) {
                                    item.emplace_back();
                                }
                                continue;
                            }
                            break;
                        }
                        if (indentOf(l) >= marker.content) {
                            item.push_back(stripIndent(l, marker.content));
                            i++;
                            continue;
                        }
                        Marker other;
                        if (!listMarker(l, other) && !starts(l) && !item.empty() && !blank(item.back())) {
                            item.emplace_back(trimLeft(l));
                            i++;
                            continue;
                        }
                        break;
                    }
                    Block entry;
                    entry.kind = Block::Kind::Item;
                    entry.checked = checked;
                    size_t itemDepth = out.depth();
                    out.push(std::move(entry));
                    nested(item);
                    out.popTo(itemDepth);
                }
                out.popTo(depth);
            }

            void table(const std::vector<std::string>& lines, size_t& i) {
                Block b;
                b.kind = Block::Kind::Table;
                std::vector<std::string> head = cells(lines[i]);
                for (const std::string& spec : cells(lines[i + 1])) {
                    bool left = !spec.empty() && spec.front() == ':';
                    bool right = !spec.empty() && spec.back() == ':';
                    b.columns.push_back(left && right ? Align::Center : right ? Align::End : Align::Start);
                }
                b.columns.resize(head.size(), Align::Start);
                std::vector<std::vector<Span>> headerRow;
                for (const std::string& cell : head) {
                    std::vector<Span> spans = inlines(cell);
                    for (Span& span : spans) {
                        span.style |= STRONG;
                    }
                    headerRow.push_back(std::move(spans));
                }
                b.rows.push_back(std::move(headerRow));
                b.headerRows = 1;
                i += 2;
                while (i < lines.size() && !blank(lines[i]) && !starts(lines[i])) {
                    std::vector<std::string> row = cells(lines[i]);
                    row.resize(head.size());
                    std::vector<std::vector<Span>> spans;
                    for (const std::string& cell : row) {
                        spans.push_back(inlines(cell));
                    }
                    b.rows.push_back(std::move(spans));
                    i++;
                }
                out.block(std::move(b));
            }

            struct Node {
                Span span;
                bool delim = false;
                char mark = 0;
                int count = 0;
                int original = 0;
                bool canOpen = false;
                bool canClose = false;
            };

            struct InlineState {
                uint16_t html = 0;
                std::vector<std::pair<std::string, uint16_t>> htmlStack;
                int link = -1;
                int htmlLink = -1;
            };

            static bool isWord(char32_t c) {
                return c > 0x7F || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }

            static char32_t codeBefore(std::string_view text, size_t i) {
                if (i == 0) {
                    return ' ';
                }
                size_t start = i - 1;
                while (start > 0 && ((unsigned char) text[start] & 0xC0) == 0x80) {
                    start--;
                }
                return nextCode(text, start);
            }

            static char32_t codeAt(std::string_view text, size_t i) {
                if (i >= text.size()) {
                    return ' ';
                }
                return nextCode(text, i);
            }

            static bool spaceCode(char32_t c) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || c == 0x3000;
            }

            static bool punctCode(char32_t c) {
                return (c < 0x80 && punct((char) c)) || (c >= 0x2000 && c <= 0x206F) || (c >= 0x3000 && c <= 0x303F) ||
                        (c >= 0xFF00 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20);
            }

            size_t findBracket(std::string_view text, size_t open) const {
                int depth = 0;
                for (size_t i = open; i < text.size(); i++) {
                    char c = text[i];
                    if (c == '\\') {
                        i++;
                        continue;
                    }
                    if (c == '`') {
                        size_t run = 0;
                        while (i + run < text.size() && text[i + run] == '`') {
                            run++;
                        }
                        size_t close = text.find(std::string(run, '`'), i + run);
                        if (close != std::string_view::npos) {
                            i = close + run - 1;
                            continue;
                        }
                        i += run - 1;
                        continue;
                    }
                    if (c == '[') {
                        depth++;
                    } else if (c == ']') {
                        depth--;
                        if (depth == 0) {
                            return i;
                        }
                    }
                }
                return std::string_view::npos;
            }

            bool destination(std::string_view text, size_t at, std::string& url, size_t& end) const {
                size_t i = at;
                if (i >= text.size() || text[i] != '(') {
                    return false;
                }
                i++;
                while (i < text.size() && space(text[i])) {
                    i++;
                }
                if (i < text.size() && text[i] == '<') {
                    size_t close = text.find('>', i);
                    if (close == std::string_view::npos) {
                        return false;
                    }
                    url = std::string(text.substr(i + 1, close - i - 1));
                    i = close + 1;
                } else {
                    int depth = 0;
                    size_t s = i;
                    while (i < text.size()) {
                        char c = text[i];
                        if (c == '\\' && i + 1 < text.size()) {
                            i += 2;
                            continue;
                        }
                        if (space(c)) {
                            break;
                        }
                        if (c == '(') {
                            depth++;
                        } else if (c == ')') {
                            if (depth == 0) {
                                break;
                            }
                            depth--;
                        }
                        i++;
                    }
                    url = std::string(text.substr(s, i - s));
                }
                while (i < text.size() && space(text[i])) {
                    i++;
                }
                if (i < text.size() && (text[i] == '"' || text[i] == '\'' || text[i] == '(')) {
                    char close = text[i] == '(' ? ')' : text[i];
                    size_t c = text.find(close, i + 1);
                    if (c == std::string_view::npos) {
                        return false;
                    }
                    i = c + 1;
                    while (i < text.size() && space(text[i])) {
                        i++;
                    }
                }
                if (i >= text.size() || text[i] != ')') {
                    return false;
                }
                end = i + 1;
                url = decode(url);
                return true;
            }

            bool reference(std::string_view text, size_t at, std::string_view label, std::string& url,
                           size_t& end) const {
                if (at < text.size() && text[at] == '[') {
                    size_t close = text.find(']', at + 1);
                    if (close != std::string_view::npos) {
                        std::string key = lower(trim(text.substr(at + 1, close - at - 1)));
                        if (key.empty()) {
                            key = lower(trim(label));
                        }
                        auto found = refs.find(key);
                        if (found != refs.end()) {
                            url = found->second;
                            end = close + 1;
                            return true;
                        }
                        return false;
                    }
                }
                auto found = refs.find(lower(trim(label)));
                if (found != refs.end()) {
                    url = found->second;
                    end = at;
                    return true;
                }
                return false;
            }

            static size_t bareUrl(std::string_view text, size_t i) {
                bool scheme = text.compare(i, 8, "https://") == 0 || text.compare(i, 7, "http://") == 0;
                bool www = text.compare(i, 4, "www.") == 0;
                if (!scheme && !www) {
                    return 0;
                }
                if (i > 0) {
                    char before = text[i - 1];
                    if (!space(before) && before != '(' && before != '*' && before != '_' && before != '~' &&
                        (unsigned char) before < 0x80) {
                        return 0;
                    }
                }
                size_t end = i;
                while (end < text.size() && !space(text[end]) && text[end] != '<' && (unsigned char) text[end] < 0x80) {
                    end++;
                }
                while (end > i) {
                    char c = text[end - 1];
                    if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?' || c == '"' || c == '\'' ||
                        c == '*' || c == '_' || c == '~') {
                        end--;
                        continue;
                    }
                    if (c == ')') {
                        std::string_view part = text.substr(i, end - i);
                        if (std::count(part.begin(), part.end(), '(') < std::count(part.begin(), part.end(), ')')) {
                            end--;
                            continue;
                        }
                    }
                    break;
                }
                size_t minimum = scheme ? (text[i + 4] == 's' ? 9u : 8u) : 5u;
                return end - i >= minimum ? end - i : 0;
            }

            void pushText(std::vector<Node>& nodes, std::string& buffer, const InlineState& state) {
                if (buffer.empty()) {
                    return;
                }
                Node node;
                node.span.text = buffer;
                node.span.style = state.html;
                node.span.link = state.htmlLink >= 0 ? state.htmlLink : state.link;
                nodes.push_back(std::move(node));
                buffer.clear();
            }

            void inlineHtml(const Tag& tag, std::vector<Node>& nodes, InlineState& state) {
                const std::string& name = tag.name;
                if (tag.closing) {
                    for (size_t k = state.htmlStack.size(); k-- > 0;) {
                        if (state.htmlStack[k].first == name) {
                            state.htmlStack.erase(state.htmlStack.begin() + (std::ptrdiff_t) k);
                            break;
                        }
                    }
                    state.html = 0;
                    for (const auto& entry : state.htmlStack) {
                        state.html |= entry.second;
                    }
                    if (name == "a") {
                        state.htmlLink = -1;
                    }
                    return;
                }
                if (name == "br") {
                    Node node;
                    node.span.kind = Span::Kind::Break;
                    nodes.push_back(std::move(node));
                    return;
                }
                if (name == "img") {
                    std::string src = tag.attr("src");
                    if (src.empty()) {
                        return;
                    }
                    if (src.rfind("//", 0) == 0) {
                        src = "https:" + src;
                    }
                    Node node;
                    node.span.kind = Span::Kind::Image;
                    node.span.src = src;
                    node.span.text = clean(tag.attr("alt"));
                    node.span.width = pixels(tag.attr("width"), node.span.percent);
                    float ignored = 0.0f;
                    node.span.height = pixels(tag.attr("height"), ignored);
                    node.span.style = state.html;
                    node.span.link = state.htmlLink >= 0 ? state.htmlLink : state.link;
                    nodes.push_back(std::move(node));
                    return;
                }
                if (name == "a") {
                    state.htmlLink = out.link(tag.attr("href"));
                    return;
                }
                if (isVoid(name) || tag.selfClosing) {
                    return;
                }
                uint16_t style = styleOf(tag);
                state.htmlStack.emplace_back(name, style);
                state.html |= style;
            }

            std::vector<Node> scan(std::string_view text, InlineState& state) {
                std::vector<Node> nodes;
                std::string buffer;
                size_t i = 0;
                while (i < text.size()) {
                    char c = text[i];
                    if (c == '\\' && i + 1 < text.size()) {
                        if (text[i + 1] == '\n') {
                            pushText(nodes, buffer, state);
                            Node node;
                            node.span.kind = Span::Kind::Break;
                            nodes.push_back(std::move(node));
                            i += 2;
                            continue;
                        }
                        if (punct(text[i + 1])) {
                            buffer += text[i + 1];
                            i += 2;
                            continue;
                        }
                    }
                    if (c == '`') {
                        size_t run = 0;
                        while (i + run < text.size() && text[i + run] == '`') {
                            run++;
                        }
                        size_t close = i + run;
                        bool found = false;
                        while ((close = text.find('`', close)) != std::string_view::npos) {
                            size_t other = 0;
                            while (close + other < text.size() && text[close + other] == '`') {
                                other++;
                            }
                            if (other == run) {
                                found = true;
                                break;
                            }
                            close += other;
                        }
                        if (!found) {
                            buffer.append(run, '`');
                            i += run;
                            continue;
                        }
                        pushText(nodes, buffer, state);
                        std::string code(text.substr(i + run, close - i - run));
                        std::replace(code.begin(), code.end(), '\n', ' ');
                        if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' &&
                            code.find_first_not_of(' ') != std::string::npos) {
                            code = code.substr(1, code.size() - 2);
                        }
                        Node node;
                        node.span.text = clean(code);
                        node.span.style = (uint16_t) (state.html | CODE);
                        node.span.link = state.htmlLink >= 0 ? state.htmlLink : state.link;
                        nodes.push_back(std::move(node));
                        i = close + run;
                        continue;
                    }
                    if (c == '*' || c == '_' || c == '~') {
                        size_t run = 0;
                        while (i + run < text.size() && text[i + run] == c) {
                            run++;
                        }
                        if (c == '~' && run != 2) {
                            buffer.append(run, c);
                            i += run;
                            continue;
                        }
                        char32_t before = codeBefore(text, i);
                        char32_t after = codeAt(text, i + run);
                        bool left = !spaceCode(after) && (!punctCode(after) || spaceCode(before) || punctCode(before));
                        bool right = !spaceCode(before) && (!punctCode(before) || spaceCode(after) || punctCode(after));
                        pushText(nodes, buffer, state);
                        Node node;
                        node.delim = true;
                        node.mark = c;
                        node.count = (int) run;
                        node.original = (int) run;
                        if (c == '_') {
                            node.canOpen = left && (!right || punctCode(before));
                            node.canClose = right && (!left || punctCode(after));
                            if (isWord(before) && isWord(after) && before < 0x80 && after < 0x80) {
                                node.canOpen = node.canClose = false;
                            }
                        } else {
                            node.canOpen = left;
                            node.canClose = right;
                        }
                        node.span.style = state.html;
                        node.span.link = state.htmlLink >= 0 ? state.htmlLink : state.link;
                        nodes.push_back(std::move(node));
                        i += run;
                        continue;
                    }
                    if ((c == '!' && i + 1 < text.size() && text[i + 1] == '[') || c == '[') {
                        bool isImage = c == '!';
                        size_t open = isImage ? i + 1 : i;
                        size_t close = findBracket(text, open);
                        if (close != std::string_view::npos) {
                            std::string_view label = text.substr(open + 1, close - open - 1);
                            std::string url;
                            size_t end = 0;
                            bool ok = destination(text, close + 1, url, end);
                            if (!ok) {
                                ok = reference(text, close + 1, label, url, end);
                            }
                            if (ok) {
                                pushText(nodes, buffer, state);
                                if (isImage) {
                                    Node node;
                                    node.span.kind = Span::Kind::Image;
                                    node.span.src = url;
                                    InlineState inner;
                                    std::vector<Span> alt = resolve(scan(label, inner));
                                    node.span.text = plain(alt);
                                    node.span.style = state.html;
                                    node.span.link = state.htmlLink >= 0 ? state.htmlLink : state.link;
                                    nodes.push_back(std::move(node));
                                } else {
                                    InlineState inner = state;
                                    inner.htmlStack.clear();
                                    inner.link = out.link(url);
                                    inner.htmlLink = -1;
                                    for (Span& span : resolve(scan(label, inner))) {
                                        Node node;
                                        node.span = std::move(span);
                                        if (node.span.link < 0) {
                                            node.span.link = inner.link;
                                        }
                                        nodes.push_back(std::move(node));
                                    }
                                }
                                i = end;
                                continue;
                            }
                        }
                        buffer += c;
                        i++;
                        continue;
                    }
                    if (c == '<') {
                        size_t close = text.find('>', i);
                        if (close != std::string_view::npos) {
                            std::string_view inside = text.substr(i + 1, close - i - 1);
                            bool auto_ = inside.find(' ') == std::string_view::npos &&
                                    (inside.rfind("http://", 0) == 0 || inside.rfind("https://", 0) == 0 ||
                                     inside.rfind("mailto:", 0) == 0);
                            if (auto_) {
                                pushText(nodes, buffer, state);
                                Node node;
                                node.span.text = std::string(inside);
                                node.span.style = state.html;
                                node.span.link = out.link(std::string(inside));
                                nodes.push_back(std::move(node));
                                i = close + 1;
                                continue;
                            }
                        }
                        if (text.compare(i, 4, "<!--") == 0) {
                            size_t end = text.find("-->", i + 4);
                            i = end == std::string_view::npos ? text.size() : end + 3;
                            continue;
                        }
                        Tag tag;
                        size_t end = 0;
                        if (readTag(text, i, tag, end)) {
                            pushText(nodes, buffer, state);
                            inlineHtml(tag, nodes, state);
                            i = end;
                            continue;
                        }
                    }
                    if (c == '&') {
                        size_t end = 0;
                        if (entityAt(text, i, buffer, end)) {
                            i = end;
                            continue;
                        }
                    }
                    if (c == '\n') {
                        size_t spaces = 0;
                        while (spaces < buffer.size() && buffer[buffer.size() - 1 - spaces] == ' ') {
                            spaces++;
                        }
                        buffer.erase(buffer.size() - spaces);
                        if (spaces >= 2) {
                            pushText(nodes, buffer, state);
                            Node node;
                            node.span.kind = Span::Kind::Break;
                            nodes.push_back(std::move(node));
                        } else {
                            buffer += ' ';
                        }
                        i++;
                        while (i < text.size() && text[i] == ' ') {
                            i++;
                        }
                        continue;
                    }
                    if (state.link < 0 && state.htmlLink < 0 && (c == 'h' || c == 'w')) {
                        if (size_t length = bareUrl(text, i)) {
                            pushText(nodes, buffer, state);
                            std::string url(text.substr(i, length));
                            Node node;
                            node.span.text = url;
                            node.span.style = state.html;
                            node.span.link = out.link(url.rfind("www.", 0) == 0 ? "https://" + url : url);
                            nodes.push_back(std::move(node));
                            i += length;
                            continue;
                        }
                    }
                    buffer += c;
                    i++;
                }
                pushText(nodes, buffer, state);
                return nodes;
            }

            static std::vector<Span> resolve(std::vector<Node> nodes) {
                for (size_t c = 0; c < nodes.size(); c++) {
                    if (!nodes[c].delim || !nodes[c].canClose) {
                        continue;
                    }
                    while (nodes[c].count > 0) {
                        size_t found = SIZE_MAX;
                        for (size_t o = c; o-- > 0;) {
                            Node& opener = nodes[o];
                            if (!opener.delim || opener.mark != nodes[c].mark || !opener.canOpen || opener.count <= 0) {
                                continue;
                            }
                            if ((opener.canClose || nodes[c].canOpen) &&
                                (opener.original + nodes[c].original) % 3 == 0 &&
                                !(opener.original % 3 == 0 && nodes[c].original % 3 == 0)) {
                                continue;
                            }
                            found = o;
                            break;
                        }
                        if (found == SIZE_MAX) {
                            break;
                        }
                        Node& opener = nodes[found];
                        int use = nodes[c].mark == '~' ? 2 : (opener.count >= 2 && nodes[c].count >= 2 ? 2 : 1);
                        if (nodes[c].mark == '~' && (opener.count < 2 || nodes[c].count < 2)) {
                            break;
                        }
                        uint16_t style = nodes[c].mark == '~' ? STRIKE : use == 2 ? STRONG : EM;
                        for (size_t k = found + 1; k < c; k++) {
                            nodes[k].span.style |= style;
                            if (nodes[k].delim) {
                                nodes[k].canOpen = false;
                                nodes[k].canClose = false;
                            }
                        }
                        opener.count -= use;
                        nodes[c].count -= use;
                    }
                }
                std::vector<Span> out;
                for (Node& node : nodes) {
                    Span span = std::move(node.span);
                    if (node.delim) {
                        if (node.count <= 0) {
                            continue;
                        }
                        span.text = std::string((size_t) node.count, node.mark);
                    } else if (span.kind == Span::Kind::Text) {
                        span.text = clean(span.text);
                    }
                    if (span.kind == Span::Kind::Text && !out.empty() && out.back().kind == Span::Kind::Text &&
                        out.back().style == span.style && out.back().link == span.link) {
                        out.back().text += span.text;
                        continue;
                    }
                    out.push_back(std::move(span));
                }
                return out;
            }

        public:
            std::vector<Span> inlines(const std::string& text) {
                InlineState state;
                std::vector<Span> spans = resolve(scan(text, state));
                while (!spans.empty() && spans.back().kind == Span::Kind::Text && !spans.back().text.empty() &&
                       spans.back().text.back() == ' ') {
                    spans.back().text.pop_back();
                    if (spans.back().text.empty()) {
                        spans.pop_back();
                    }
                }
                return spans;
            }
        };

        std::vector<std::string> splitLines(std::string_view text) {
            std::vector<std::string> lines;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                std::string line(text.substr(start, end - start));
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(std::move(line));
                if (end == text.size()) {
                    break;
                }
                start = end + 1;
            }
            return lines;
        }
    }

    Doc parse(std::string_view text, Format format) {
        Doc doc;
        Builder builder(doc);
        if (format == Format::Html) {
            builder.html(text);
        } else {
            Parser parser(builder);
            std::vector<std::string> lines = splitLines(text);
            parser.collectRefs(lines);
            parser.blocks(lines);
        }
        builder.finish();
        return doc;
    }

    std::string plain(const std::vector<Span>& spans) {
        std::string out;
        for (const Span& span : spans) {
            if (span.kind == Span::Kind::Break) {
                out += ' ';
            } else {
                out += span.text;
            }
        }
        return out;
    }
}
