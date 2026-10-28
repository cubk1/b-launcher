#include "Xaml.h"

#include <algorithm>
#include <cstdint>

namespace Xaml {

    namespace {
        const size_t MAX_DEPTH = 256;
        const size_t MAX_ENTITY = 32;

        bool isSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        bool isNameChar(char c) {
            unsigned char u = static_cast<unsigned char>(c);
            return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_' || u == '-' ||
                    u == '.' || u == ':' || u >= 0x80;
        }

        bool isEntityChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#';
        }

        int digitValue(char c, bool hex) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (hex && c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (hex && c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        void appendUtf8(std::string& out, uint32_t code) {
            if (code == 0 || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) {
                code = 0xFFFD;
            }
            if (code < 0x80) {
                out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }

        size_t decodeEntity(std::string& out, std::string_view raw, size_t at) {
            size_t limit = std::min(raw.size(), at + MAX_ENTITY);
            size_t end = at + 1;
            while (end < limit && isEntityChar(raw[end])) {
                ++end;
            }
            if (end >= limit || raw[end] != ';' || end == at + 1) {
                return 0;
            }
            std::string_view body = raw.substr(at + 1, end - at - 1);
            if (body[0] == '#') {
                bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
                size_t first = hex ? 2 : 1;
                if (first >= body.size()) {
                    return 0;
                }
                uint32_t code = 0;
                for (size_t i = first; i < body.size(); ++i) {
                    int value = digitValue(body[i], hex);
                    if (value < 0) {
                        return 0;
                    }
                    if (code <= 0x10FFFF) {
                        code = code * (hex ? 16 : 10) + static_cast<uint32_t>(value);
                    }
                }
                appendUtf8(out, code);
                return end - at + 1;
            }
            char c = 0;
            if (body == "lt") {
                c = '<';
            } else if (body == "gt") {
                c = '>';
            } else if (body == "amp") {
                c = '&';
            } else if (body == "quot") {
                c = '"';
            } else if (body == "apos") {
                c = '\'';
            }
            if (c == 0) {
                return 0;
            }
            out.push_back(c);
            return end - at + 1;
        }

        void decode(std::string& out, std::string_view raw, bool attribute) {
            size_t run = 0;
            size_t i = 0;
            while (i < raw.size()) {
                char c = raw[i];
                if (c == '&') {
                    out.append(raw.data() + run, i - run);
                    size_t used = decodeEntity(out, raw, i);
                    if (used == 0) {
                        out.push_back('&');
                        used = 1;
                    }
                    i += used;
                    run = i;
                } else if (c == '\r') {
                    out.append(raw.data() + run, i - run);
                    out.push_back(attribute ? ' ' : '\n');
                    i += i + 1 < raw.size() && raw[i + 1] == '\n' ? 2 : 1;
                    run = i;
                } else if (attribute && (c == '\n' || c == '\t')) {
                    out.append(raw.data() + run, i - run);
                    out.push_back(' ');
                    ++i;
                    run = i;
                } else {
                    ++i;
                }
            }
            out.append(raw.data() + run, raw.size() - run);
        }

        void appendLines(std::string& out, std::string_view raw) {
            size_t run = 0;
            size_t i = 0;
            while (i < raw.size()) {
                if (raw[i] == '\r') {
                    out.append(raw.data() + run, i - run);
                    out.push_back('\n');
                    i += i + 1 < raw.size() && raw[i + 1] == '\n' ? 2 : 1;
                    run = i;
                } else {
                    ++i;
                }
            }
            out.append(raw.data() + run, raw.size() - run);
        }

        struct Parser {
            std::string_view source;
            size_t position = 0;
            size_t counted = 0;
            int countedLine = 1;
            Document document;
            std::vector<Node*> open;
            std::string pending;
            int pendingLine = 0;
            bool failed = false;

            int lineAt(size_t at) {
                while (counted < at) {
                    char c = source[counted];
                    if (c == '\n' || (c == '\r' && (counted + 1 >= source.size() || source[counted + 1] != '\n'))) {
                        ++countedLine;
                    }
                    ++counted;
                }
                return countedLine;
            }

            void failLine(int line, const std::string& message) {
                document.error = "第 " + std::to_string(line) + " 行：" + message;
                document.errorLine = line;
                failed = true;
            }

            void fail(size_t at, const std::string& message) {
                failLine(lineAt(std::min(at, source.size())), message);
            }

            size_t skipSpaces(size_t at) const {
                while (at < source.size() && isSpace(source[at])) {
                    ++at;
                }
                return at;
            }

            size_t scanName(size_t at) const {
                while (at < source.size() && isNameChar(source[at])) {
                    ++at;
                }
                return at;
            }

            void addText(size_t begin, size_t end) {
                if (open.empty() || begin >= end) {
                    return;
                }
                if (pending.empty()) {
                    pendingLine = lineAt(begin);
                }
                decode(pending, source.substr(begin, end - begin), false);
            }

            void flush() {
                if (pending.empty()) {
                    return;
                }
                Node& node = open.back()->children.emplace_back();
                node.text = std::move(pending);
                node.line = pendingLine;
                pending.clear();
            }

            void skipUntil(std::string_view opening, std::string_view closing, const char* message) {
                size_t end = source.find(closing, position + opening.size());
                if (end == std::string_view::npos) {
                    fail(position, message);
                    return;
                }
                position = end + closing.size();
            }

            void cdata() {
                const std::string_view OPEN = "<![CDATA[";
                size_t end = source.find("]]>", position + OPEN.size());
                if (end == std::string_view::npos) {
                    fail(position, "CDATA 没有结束");
                    return;
                }
                if (!open.empty()) {
                    if (pending.empty()) {
                        pendingLine = lineAt(position);
                    }
                    size_t begin = position + OPEN.size();
                    appendLines(pending, source.substr(begin, end - begin));
                }
                position = end + 3;
            }

            void declaration() {
                int depth = 0;
                char quote = 0;
                for (size_t at = position + 2; at < source.size(); ++at) {
                    char c = source[at];
                    if (quote != 0) {
                        if (c == quote) {
                            quote = 0;
                        }
                    } else if (c == '"' || c == '\'') {
                        quote = c;
                    } else if (c == '[') {
                        ++depth;
                    } else if (c == ']') {
                        depth = std::max(depth - 1, 0);
                    } else if (c == '>' && depth == 0) {
                        position = at + 1;
                        return;
                    }
                }
                fail(position, "声明 <! 没有结束");
            }

            void endTag() {
                size_t start = position;
                size_t nameStart = position + 2;
                size_t nameEnd = scanName(nameStart);
                std::string name(source.substr(nameStart, nameEnd - nameStart));
                size_t at = skipSpaces(nameEnd);
                if (name.empty()) {
                    fail(start, "结束标签 </ 后面缺少标签名");
                    return;
                }
                if (at >= source.size() || source[at] != '>') {
                    fail(start, "结束标签 </" + name + "> 不完整");
                    return;
                }
                if (open.empty()) {
                    fail(start, "多余的结束标签 </" + name + ">");
                    return;
                }
                if (open.back()->name != name) {
                    fail(start, "结束标签 </" + name + "> 与 <" + open.back()->name + "> 不匹配");
                    return;
                }
                flush();
                open.pop_back();
                position = at + 1;
            }

            void startTag() {
                size_t start = position;
                size_t nameStart = position + 1;
                size_t nameEnd = scanName(nameStart);
                if (nameEnd == nameStart) {
                    fail(start, "< 后面缺少标签名");
                    return;
                }
                int line = lineAt(start);
                if (open.size() >= MAX_DEPTH) {
                    failLine(line, "标签嵌套层级过深");
                    return;
                }
                flush();
                std::vector<Node>& siblings = open.empty() ? document.roots : open.back()->children;
                Node& node = siblings.emplace_back();
                node.name.assign(source.data() + nameStart, nameEnd - nameStart);
                node.line = line;
                size_t at = nameEnd;
                while (true) {
                    at = skipSpaces(at);
                    if (at >= source.size()) {
                        failLine(line, "标签 <" + node.name + "> 不完整");
                        return;
                    }
                    char c = source[at];
                    if (c == '>') {
                        open.push_back(&node);
                        position = at + 1;
                        return;
                    }
                    if (c == '/') {
                        if (at + 1 < source.size() && source[at + 1] == '>') {
                            position = at + 2;
                            return;
                        }
                        fail(at, "标签 <" + node.name + "> 中的 / 后面缺少 >");
                        return;
                    }
                    if (c == '<') {
                        failLine(line, "标签 <" + node.name + "> 缺少 >");
                        return;
                    }
                    size_t keyEnd = scanName(at);
                    if (keyEnd == at) {
                        fail(at, "标签 <" + node.name + "> 中有无法识别的字符");
                        return;
                    }
                    std::string key(source.substr(at, keyEnd - at));
                    at = skipSpaces(keyEnd);
                    if (at >= source.size() || source[at] != '=') {
                        fail(at, "属性 " + key + " 缺少 =");
                        return;
                    }
                    at = skipSpaces(at + 1);
                    if (at >= source.size() || (source[at] != '"' && source[at] != '\'')) {
                        fail(at, "属性 " + key + " 的值缺少引号");
                        return;
                    }
                    size_t close = source.find(source[at], at + 1);
                    if (close == std::string_view::npos) {
                        fail(at, "属性 " + key + " 的值没有结束");
                        return;
                    }
                    auto& attribute = node.attributes.emplace_back();
                    attribute.first = std::move(key);
                    decode(attribute.second, source.substr(at + 1, close - at - 1), true);
                    at = close + 1;
                }
            }

            void markup() {
                std::string_view rest = source.substr(position);
                if (rest.starts_with("<!--")) {
                    skipUntil("<!--", "-->", "注释没有结束");
                } else if (rest.starts_with("<![CDATA[")) {
                    cdata();
                } else if (rest.starts_with("<?")) {
                    skipUntil("<?", "?>", "处理指令 <? 没有结束");
                } else if (rest.starts_with("<!")) {
                    declaration();
                } else if (rest.starts_with("</")) {
                    endTag();
                } else {
                    startTag();
                }
            }

            void run() {
                if (source.starts_with("\xEF\xBB\xBF")) {
                    position = 3;
                }
                while (!failed && position < source.size()) {
                    size_t next = source.find('<', position);
                    if (next == std::string_view::npos) {
                        addText(position, source.size());
                        position = source.size();
                        break;
                    }
                    addText(position, next);
                    position = next;
                    markup();
                }
                if (!failed && !open.empty()) {
                    flush();
                    failLine(open.back()->line, "标签 <" + open.back()->name + "> 没有闭合");
                }
            }
        };
    }

    bool Node::isText() const {
        return name.empty();
    }

    const std::string* Node::attribute(std::string_view key) const {
        for (const auto& entry : attributes) {
            if (entry.first == key) {
                return &entry.second;
            }
        }
        return nullptr;
    }

    Document parse(std::string_view source) {
        Parser parser;
        parser.source = source;
        parser.run();
        return std::move(parser.document);
    }
}
