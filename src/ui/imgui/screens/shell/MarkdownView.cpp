#include "ui/imgui/screens/shell/MarkdownView.h"
#include "ui/imgui/screens/home/WebImages.h"
#include "ui/imgui/screens/shell/Page.h"

#include "ui/imgui/gl/ShaderUtil.h"
#include "ui/imgui/panel/NewUi.h"
#include "ui/imgui/panel/Notifications.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/panel/UiCursor.h"
#include "ui/imgui/panel/UiFont.h"
#include "ui/imgui/panel/UiInput.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiSound.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/panel/UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace {
    using Markdown::Align;
    using Markdown::Block;
    using Markdown::Span;

    constexpr float LINE = 1.6f;
    constexpr float PARA_GAP = 12.0f;
    constexpr float ITEM_GAP = 4.0f;
    constexpr float CODE_SIZE = 12.0f;
    constexpr float CODE_PAD = 10.0f;
    constexpr float QUOTE_PAD = 14.0f;
    constexpr float CELL_X = 10.0f;
    constexpr float CELL_Y = 6.0f;
    constexpr float SUMMARY_H = 30.0f;
    constexpr float VIDEO_H = 36.0f;
    constexpr float CODE_GAP = 3.0f;
    constexpr int BODY = 0;
    constexpr int HEADING = 1;
    constexpr int QUOTE = 2;

    struct Token {
        size_t start = 0;
        size_t length = 0;
        float width = 0.0f;
        int type = 0;
    };

    struct Run {
        Span::Kind kind = Span::Kind::Text;
        std::string text;
        uint16_t style = 0;
        int link = -1;
        float size = 13.5f;
        int family = UiFont::MEDIUM;
        int tone = BODY;
        int group = 0;
        float gap = 0.0f;
        std::string src;
        float imageW = 0.0f;
        float imageH = 0.0f;
        float percent = 0.0f;
        std::vector<Token> tokens;
        float tokenScale = -1.0f;
    };

    struct Piece {
        int run = -1;
        int line = 0;
        std::string text;
        float x = 0.0f;
        float top = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct Flow {
        std::vector<Run> runs;
        Align align = Align::Start;
        float cachedWidth = -2.0f;
        float cachedScale = 0.0f;
        long long cachedGeneration = -1;
        std::vector<Piece> pieces;
        std::vector<float> lineWidths;
        float width = 0.0f;
        float height = 0.0f;
        float firstLine = 0.0f;
        bool figure = false;
    };

    struct Heading {
        int level = 0;
        std::string text;
    };

    struct Box {
        Block::Kind kind = Block::Kind::Paragraph;
        Align align = Align::Start;
        int tone = BODY;
        int level = 0;
        Flow flow;
        std::vector<Box> children;
        bool ordered = false;
        int start = 1;
        int checked = -1;
        std::string code;
        std::vector<std::string> lines;
        std::vector<std::vector<Flow>> cells;
        std::vector<Align> columns;
        int headerRows = 0;
        std::vector<float> colX;
        std::vector<float> colW;
        std::vector<float> rowY;
        std::vector<float> rowH;
        float tableW = 0.0f;
        bool open = false;
        bool paragraph = false;
        std::string url;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float marginBottom = 0.0f;
        float markerY = 0.0f;
    };

    char32_t nextCode(const std::string& text, size_t& i) {
        unsigned char c = (unsigned char) text[i++];
        if (c < 0x80) {
            return c;
        }
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        char32_t code = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : c & 0x1F;
        for (int k = 0; k < extra && i < text.size(); k++) {
            code = (code << 6) | ((unsigned char) text[i++] & 0x3F);
        }
        return code;
    }

    bool space(char32_t c) {
        return c == ' ' || c == '\t' || c == '\r';
    }

    bool cjk(char32_t c) {
        return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) ||
                (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x3000 && c <= 0x303F);
    }

    bool closing(char32_t c) {
        static const std::u32string_view marks = U"，。、；：？！）」』》〉】〕…,.;:?!)]}%”’";
        return marks.find(c) != std::u32string_view::npos;
    }

    std::string imageUrl(const std::string& src) {
        static const std::string shields = "https://img.shields.io/";
        if (src.rfind(shields, 0) == 0) {
            return "https://raster.shields.io/" + src.substr(shields.size());
        }
        return src;
    }

    float lineHeight(float size) {
        return std::round(size * LINE);
    }

    std::string trimmed(const std::string& text) {
        size_t from = text.find_first_not_of(" \t\r\n");
        if (from == std::string::npos) {
            return std::string();
        }
        size_t to = text.find_last_not_of(" \t\r\n");
        return text.substr(from, to - from + 1);
    }

    std::string slug(const std::string& text) {
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            size_t from = i;
            char32_t c = nextCode(text, i);
            if (c >= 0x80) {
                if (!closing(c) && c != U'\u3000') {
                    out += text.substr(from, i - from);
                }
            } else if (std::isalnum((int) c)) {
                out += (char) std::tolower((int) c);
            } else if (c == ' ' || c == '-') {
                out += '-';
            } else if (c == '_') {
                out += '_';
            }
        }
        return out;
    }

    std::string loose(const std::string& text) {
        std::string out;
        for (char c : text) {
            if (c != '-' && c != '_') {
                out += c;
            }
        }
        return out;
    }

    std::string decoded(const std::string& text) {
        std::string out;
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char) text[i + 1]) &&
                std::isxdigit((unsigned char) text[i + 2])) {
                out += (char) std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            } else {
                out += text[i];
            }
        }
        return out;
    }

    bool blank(const Run& run) {
        if (run.gap > 0.0f) {
            return true;
        }
        return run.kind == Span::Kind::Text && trimmed(run.text).empty();
    }

    bool hollow(const Flow& f) {
        return std::all_of(f.runs.begin(), f.runs.end(),
                           [](const Run& run) { return blank(run) || run.kind == Span::Kind::Break; });
    }

    bool figure(const Flow& f) {
        for (const Run& run : f.runs) {
            if (!blank(run)) {
                return run.kind == Span::Kind::Image;
            }
        }
        return false;
    }

    bool caption(Flow& f) {
        size_t i = 0;
        bool image = false;
        while (i < f.runs.size() && (f.runs[i].kind == Span::Kind::Image || blank(f.runs[i]))) {
            image = image || f.runs[i].kind == Span::Kind::Image;
            i++;
        }
        if (!image || i >= f.runs.size() || f.runs[i].kind != Span::Kind::Break) {
            return false;
        }
        bool text = false;
        for (size_t j = i + 1; j < f.runs.size(); j++) {
            const Run& run = f.runs[j];
            if (run.kind == Span::Kind::Image) {
                return false;
            }
            if (run.kind == Span::Kind::Text && !blank(run)) {
                if (!(run.style & Markdown::EM)) {
                    return false;
                }
                text = true;
            }
        }
        if (!text) {
            return false;
        }
        for (size_t j = i + 1; j < f.runs.size(); j++) {
            Run& run = f.runs[j];
            run.style = (uint16_t) (run.style & ~Markdown::EM);
            run.size *= 0.9f;
            run.tone = QUOTE;
        }
        return true;
    }

    uint32_t toneColor(int tone) {
        if (tone == HEADING) {
            return UiTheme::textPrimary();
        }
        if (tone == QUOTE) {
            return UiTheme::textMuted();
        }
        return UiTheme::textSecondary();
    }

    void tokenize(Run& run) {
        float scale = UiRender::uiScale;
        if (run.tokenScale == scale) {
            return;
        }
        run.tokens.clear();
        const std::string& text = run.text;
        size_t i = 0;
        while (i < text.size()) {
            Token token;
            token.start = i;
            char32_t c = nextCode(text, i);
            if (c == '\n') {
                token.type = 2;
            } else if (space(c)) {
                token.type = 1;
                while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
                    i++;
                }
            } else {
                if (!cjk(c)) {
                    while (i < text.size()) {
                        size_t j = i;
                        char32_t d = nextCode(text, j);
                        if (space(d) || d == '\n' || cjk(d)) {
                            break;
                        }
                        i = j;
                        if (d == '/' || d == '-') {
                            break;
                        }
                    }
                }
                while (i < text.size()) {
                    size_t j = i;
                    char32_t d = nextCode(text, j);
                    if (!closing(d)) {
                        break;
                    }
                    i = j;
                }
            }
            token.length = i - token.start;
            if (token.type != 2) {
                token.width = UiFont::width(text.substr(token.start, token.length), run.family, run.size);
            }
            run.tokens.push_back(token);
        }
        run.tokenScale = scale;
    }

    void imageSize(Run& run, float maxWidth, float& w, float& h) {
        const WebImages::Image& image = WebImages::get(imageUrl(run.src));
        w = run.imageW;
        h = run.imageH;
        if (run.percent > 0.0f) {
            w = maxWidth * run.percent / 100.0f;
        }
        if (image.texture != 0 && image.width > 0 && image.height > 0) {
            float nw = (float) image.width;
            float nh = (float) image.height;
            if (w > 0.0f && h <= 0.0f) {
                h = w * nh / nw;
            } else if (h > 0.0f && w <= 0.0f) {
                w = h * nw / nh;
            } else if (w <= 0.0f && h <= 0.0f) {
                w = nw;
                h = nh;
            }
        } else if (image.failed) {
            w = 0.0f;
            h = 0.0f;
        } else if (w > 0.0f && h <= 0.0f) {
            h = w * 0.5625f;
        } else if (w <= 0.0f) {
            h = 0.0f;
        }
        if (w > maxWidth && w > 0.0f) {
            h *= maxWidth / w;
            w = maxWidth;
        }
    }

    void layoutFlow(Flow& f, float maxWidth) {
        float scale = UiRender::uiScale;
        long long generation = WebImages::generation();
        if (f.cachedWidth == maxWidth && f.cachedScale == scale && f.cachedGeneration == generation) {
            return;
        }
        f.cachedWidth = maxWidth;
        f.cachedScale = scale;
        f.cachedGeneration = generation;
        struct Item {
            int run = -1;
            size_t start = 0;
            size_t length = 0;
            float w = 0.0f;
            float h = 0.0f;
            bool image = false;
        };
        struct Line {
            std::vector<Item> items;
            float width = 0.0f;
            float trailing = 0.0f;
        };
        std::vector<Line> lines(1);
        bool soft = false;
        float baseSize = 13.5f;
        for (const Run& run : f.runs) {
            if (run.kind == Span::Kind::Text && run.gap <= 0.0f) {
                baseSize = run.size;
                break;
            }
        }
        auto append = [&](int runIndex, size_t start, size_t count, float w) {
            Line& line = lines.back();
            if (!line.items.empty()) {
                Item& last = line.items.back();
                if (!last.image && last.run == runIndex && last.start + last.length == start) {
                    last.length += count;
                    last.w += w;
                    line.width += w;
                    return;
                }
            }
            line.items.push_back(Item{runIndex, start, count, w, 0.0f, false});
            line.width += w;
        };
        for (int ri = 0; ri < (int) f.runs.size(); ri++) {
            Run& run = f.runs[ri];
            if (run.kind == Span::Kind::Break) {
                lines.back().width -= lines.back().trailing;
                lines.back().trailing = 0.0f;
                lines.emplace_back();
                soft = false;
                continue;
            }
            if (run.kind == Span::Kind::Image) {
                const WebImages::Image& image = WebImages::get(imageUrl(run.src));
                if (image.failed && !run.text.empty()) {
                    run.kind = Span::Kind::Text;
                    run.text = "[" + run.text + "]";
                    run.style |= Markdown::SMALL;
                    run.size *= 0.85f;
                    run.tone = QUOTE;
                    run.tokenScale = -1.0f;
                } else {
                    float w = 0.0f;
                    float h = 0.0f;
                    imageSize(run, maxWidth, w, h);
                    if (w <= 0.0f || h <= 0.0f) {
                        continue;
                    }
                    if (lines.back().width - lines.back().trailing > 0.0f && lines.back().width + w > maxWidth + 0.5f) {
                        lines.back().width -= lines.back().trailing;
                        lines.emplace_back();
                    }
                    lines.back().items.push_back(Item{ri, 0, 0, w, h, true});
                    lines.back().width += w;
                    lines.back().trailing = 0.0f;
                    soft = false;
                    continue;
                }
            }
            if (run.gap > 0.0f) {
                lines.back().items.push_back(Item{ri, 0, 0, run.gap, 0.0f, false});
                lines.back().width += run.gap;
                continue;
            }
            tokenize(run);
            for (const Token& token : run.tokens) {
                if (token.type == 2) {
                    lines.back().width -= lines.back().trailing;
                    lines.back().trailing = 0.0f;
                    lines.emplace_back();
                    soft = false;
                    continue;
                }
                if (token.type == 1) {
                    if (soft && lines.back().items.empty()) {
                        continue;
                    }
                    if (lines.back().items.empty()) {
                        continue;
                    }
                    append(ri, token.start, token.length, token.width);
                    lines.back().trailing += token.width;
                    continue;
                }
                size_t peek = token.start;
                bool glued = lines.back().trailing <= 0.0f && closing(nextCode(run.text, peek));
                if (!glued && lines.back().width - lines.back().trailing > 0.0f &&
                    lines.back().width + token.width > maxWidth + 0.5f) {
                    lines.back().width -= lines.back().trailing;
                    lines.back().trailing = 0.0f;
                    lines.emplace_back();
                    soft = true;
                }
                if (token.width > maxWidth && lines.back().width <= 0.0f) {
                    size_t i = token.start;
                    size_t end = token.start + token.length;
                    while (i < end) {
                        size_t from = i;
                        nextCode(run.text, i);
                        float w = UiFont::width(run.text.substr(from, i - from), run.family, run.size);
                        if (lines.back().width > 0.0f && lines.back().width + w > maxWidth + 0.5f) {
                            lines.emplace_back();
                        }
                        append(ri, from, i - from, w);
                    }
                } else {
                    append(ri, token.start, token.length, token.width);
                }
                lines.back().trailing = 0.0f;
                soft = false;
            }
        }
        f.pieces.clear();
        f.lineWidths.clear();
        f.width = 0.0f;
        float y = 0.0f;
        for (size_t li = 0; li < lines.size(); li++) {
            Line& line = lines[li];
            float above = 0.0f;
            float below = 0.0f;
            bool any = false;
            for (const Item& item : line.items) {
                if (item.image) {
                    above = std::max(above, item.h + 2.0f);
                    below = std::max(below, 3.0f);
                } else {
                    const Run& run = f.runs[item.run];
                    float size = run.gap > 0.0f ? baseSize : run.size;
                    float lh = lineHeight(size);
                    float a = (lh - size) * 0.5f + 0.85f * size;
                    above = std::max(above, a);
                    below = std::max(below, lh - a);
                }
                any = true;
            }
            if (!any) {
                float lh = lineHeight(baseSize);
                above = (lh - baseSize) * 0.5f + 0.85f * baseSize;
                below = lh - above;
            }
            float baseline = y + above;
            float cursor = 0.0f;
            for (const Item& item : line.items) {
                Piece piece;
                piece.run = item.run;
                piece.line = (int) li;
                piece.x = cursor;
                piece.w = item.w;
                const Run& run = f.runs[item.run];
                if (item.image) {
                    piece.h = item.h;
                    piece.top = baseline - item.h;
                } else if (run.gap > 0.0f) {
                    piece.h = 0.0f;
                    piece.top = baseline;
                } else {
                    piece.text = run.text.substr(item.start, item.length);
                    piece.h = run.size;
                    piece.top = baseline - 0.85f * run.size;
                }
                cursor += item.w;
                f.pieces.push_back(std::move(piece));
            }
            float width = std::max(0.0f, line.width - line.trailing);
            f.lineWidths.push_back(width);
            f.width = std::max(f.width, width);
            if (li == 0) {
                f.firstLine = above + below;
            }
            y += above + below;
        }
        f.height = y;
    }

    float naturalWidth(Flow& f) {
        layoutFlow(f, 1e6f);
        return f.width;
    }

    float minimumWidth(Flow& f) {
        float widest = 0.0f;
        for (Run& run : f.runs) {
            if (run.kind == Span::Kind::Image) {
                float w = 0.0f;
                float h = 0.0f;
                imageSize(run, 1e6f, w, h);
                widest = std::max(widest, std::min(w, 160.0f));
                continue;
            }
            tokenize(run);
            for (const Token& token : run.tokens) {
                if (token.type == 0) {
                    widest = std::max(widest, std::min(token.width, 120.0f));
                }
            }
        }
        return widest;
    }
}

struct MarkdownView::State {
    std::string id;
    std::string source;
    Markdown::Format format = Markdown::Format::Markdown;
    Markdown::Doc doc;
    std::vector<Box> boxes;
    float baseSize = 13.5f;
    float cachedWidth = -1.0f;
    float cachedScale = 0.0f;
    long long cachedGeneration = -1;
    bool dirty = true;
    float height = 0.0f;
    int groups = 0;
    int hoverLink = -1;
    int hoverGroup = -1;
    int nextHoverLink = -1;
    int nextHoverGroup = -1;
    int counter = 0;
    std::vector<Heading> headings;
    int headingIndex = 0;

    void collect(const std::list<Block>& blocks) {
        for (const Block& block : blocks) {
            if (block.kind == Block::Kind::Heading) {
                std::string text = trimmed(Markdown::plain(block.spans));
                if (!text.empty()) {
                    headings.push_back({block.level, text});
                }
            }
            collect(block.children);
        }
    }

    Run textRun(const std::string& text, float size, int tone) {
        Run run;
        run.text = text;
        run.size = size;
        run.tone = tone;
        run.group = groups++;
        return run;
    }

    Run gapRun(float gap, float size) {
        Run run;
        run.gap = gap;
        run.size = size;
        run.group = groups++;
        return run;
    }

    Flow contents(float size) {
        Flow f;
        int top = 7;
        for (const Heading& heading : headings) {
            top = std::min(top, heading.level);
        }
        int major = 0;
        int minor = 0;
        for (size_t i = 0; i < headings.size(); i++) {
            int depth = headings[i].level - top;
            if (depth > 1) {
                continue;
            }
            std::string number;
            if (depth == 0) {
                major++;
                minor = 0;
                number = std::to_string(major);
            } else {
                minor++;
                number = std::to_string(major) + "." + std::to_string(minor);
            }
            if (!f.runs.empty()) {
                Run line;
                line.kind = Span::Kind::Break;
                line.size = size;
                f.runs.push_back(line);
            }
            if (depth > 0) {
                f.runs.push_back(gapRun(14.0f, size));
            }
            f.runs.push_back(textRun(number, size, QUOTE));
            f.runs.push_back(gapRun(7.0f, size));
            Run title = textRun(headings[i].text, size, BODY);
            title.link = (int) doc.links.size();
            doc.links.push_back("#toc:" + std::to_string(i));
            f.runs.push_back(std::move(title));
        }
        return f;
    }

    void tableContents(Box& box) {
        if (headings.size() < 2 || box.headerRows <= 0 || box.cells.empty()) {
            return;
        }
        const std::vector<Flow>& head = box.cells[0];
        for (size_t c = 0; c < head.size(); c++) {
            std::string label;
            for (const Run& run : head[c].runs) {
                if (run.kind == Span::Kind::Text) {
                    label += run.text;
                }
            }
            if (trimmed(label) != "目录") {
                continue;
            }
            for (size_t r = (size_t) box.headerRows; r < box.cells.size(); r++) {
                if (c < box.cells[r].size() && hollow(box.cells[r][c])) {
                    box.cells[r][c] = contents(baseSize * 0.9f);
                    return;
                }
            }
        }
    }

    bool anchorY(const std::vector<Box>& boxes, const std::string& wanted, int index, float& y) {
        for (const Box& b : boxes) {
            if (b.kind == Block::Kind::Heading && b.start >= 0) {
                bool hit = index >= 0 ? b.start == index
                                      : !wanted.empty() && (b.url == wanted || loose(b.url) == loose(wanted));
                if (hit) {
                    y = b.y;
                    return true;
                }
            }
            if (anchorY(b.children, wanted, index, y)) {
                return true;
            }
        }
        return false;
    }

    void jump(MarkdownView& view, const std::string& target) {
        if (!view.onAnchor) {
            return;
        }
        std::string name = decoded(target.substr(1));
        int index = -1;
        if (name.rfind("toc:", 0) == 0) {
            index = std::atoi(name.c_str() + 4);
        }
        float y = 0.0f;
        if (anchorY(boxes, slug(name), index, y)) {
            view.onAnchor(y);
        }
    }

    void addSpans(Flow& flow, const std::vector<Span>& spans, float size, int tone, uint16_t extra) {
        for (const Span& span : spans) {
            Run run;
            run.kind = span.kind;
            run.text = span.text;
            run.style = (uint16_t) (span.style | extra);
            run.link = span.link;
            run.tone = (run.style & Markdown::STRONG) && tone == BODY ? HEADING : tone;
            run.size = (run.style & Markdown::SMALL) ? size * 0.82f : size;
            run.group = groups++;
            run.src = span.src;
            run.imageW = span.width;
            run.imageH = span.height;
            run.percent = span.percent;
            bool code = (run.style & (Markdown::CODE | Markdown::KBD)) != 0 && run.kind == Span::Kind::Text;
            if (code) {
                run.family = UiFont::CODE;
                run.size *= 0.9f;
                Run pad;
                pad.gap = CODE_GAP;
                pad.size = size;
                pad.group = run.group;
                pad.style = run.style;
                flow.runs.push_back(pad);
                flow.runs.push_back(std::move(run));
                flow.runs.push_back(pad);
                continue;
            }
            flow.runs.push_back(std::move(run));
        }
    }

    Box build(const Block& block, int tone, Align align, bool inItem) {
        Box box;
        box.kind = block.kind;
        box.align = block.align != Align::Start ? block.align : align;
        box.tone = tone;
        box.level = block.level;
        box.paragraph = inItem;
        box.flow.align = box.align;
        switch (block.kind) {
            case Block::Kind::Paragraph:
                addSpans(box.flow, block.spans, baseSize, tone, 0);
                if (caption(box.flow) && box.align == Align::Start) {
                    box.align = Align::Center;
                    box.flow.align = Align::Center;
                }
                break;
            case Block::Kind::Heading: {
                static const float sizes[] = {0.0f, 1.5f, 1.3f, 1.14f, 1.0f, 0.94f, 0.88f};
                float size = baseSize * sizes[std::clamp(block.level, 1, 6)];
                addSpans(box.flow, block.spans, size, HEADING, Markdown::STRONG);
                std::string text = trimmed(Markdown::plain(block.spans));
                box.url = slug(text);
                box.start = text.empty() ? -1 : headingIndex++;
                break;
            }
            case Block::Kind::Code: box.code = block.text; break;
            case Block::Kind::Video: box.url = block.url; break;
            case Block::Kind::Details:
                box.open = block.open;
                addSpans(box.flow,
                         block.spans.empty() ? std::vector<Span>{Span{Span::Kind::Text, "详情"}} : block.spans,
                         baseSize, HEADING, 0);
                break;
            case Block::Kind::Table:
                box.columns = block.columns;
                box.headerRows = block.headerRows;
                for (const auto& row : block.rows) {
                    std::vector<Flow> cells;
                    for (size_t c = 0; c < row.size(); c++) {
                        Flow cell;
                        cell.align = c < block.columns.size() ? block.columns[c] : Align::Start;
                        addSpans(cell, row[c], baseSize * 0.94f, tone, 0);
                        caption(cell);
                        cell.figure = figure(cell);
                        cells.push_back(std::move(cell));
                    }
                    box.columns.resize(std::max(box.columns.size(), cells.size()), Align::Start);
                    box.cells.push_back(std::move(cells));
                }
                tableContents(box);
                for (size_t c = 0; c < box.columns.size(); c++) {
                    bool pictured = std::any_of(box.cells.begin(), box.cells.end(), [c](const std::vector<Flow>& row) {
                        return c < row.size() && row[c].figure;
                    });
                    for (size_t r = 0; r < box.cells.size(); r++) {
                        if (c < box.cells[r].size() && box.cells[r][c].align == Align::Start &&
                            (pictured || (int) r < box.headerRows)) {
                            box.cells[r][c].align = Align::Center;
                        }
                    }
                }
                break;
            case Block::Kind::List:
                box.ordered = block.ordered;
                box.start = block.start;
                break;
            case Block::Kind::Item: box.checked = block.checked; break;
            default: break;
        }
        int childTone = block.kind == Block::Kind::Quote ? QUOTE : tone;
        bool childItem = block.kind == Block::Kind::Item || inItem;
        for (const Block& child : block.children) {
            box.children.push_back(build(child, childTone, box.align, childItem));
        }
        return box;
    }

    float layoutBlocks(std::vector<Box>& boxes, float x, float y, float w) {
        float cursor = 0.0f;
        float pending = 0.0f;
        for (size_t i = 0; i < boxes.size(); i++) {
            Box& b = boxes[i];
            float top = i == 0 ? 0.0f : pending;
            if (b.kind == Block::Kind::Heading && i > 0) {
                top = std::max(top, b.level <= 2 ? 22.0f : 16.0f);
            }
            if (b.kind == Block::Kind::Rule && i > 0) {
                top = std::max(top, 14.0f);
            }
            cursor += top;
            layoutBox(b, x, y + cursor, w);
            cursor += b.h;
            pending = b.marginBottom;
        }
        return cursor;
    }

    void layoutBox(Box& b, float x, float y, float w) {
        b.x = x;
        b.y = y;
        b.w = w;
        float gap = b.paragraph ? ITEM_GAP + 2.0f : PARA_GAP;
        switch (b.kind) {
            case Block::Kind::Paragraph:
                layoutFlow(b.flow, w);
                b.h = b.flow.height;
                b.marginBottom = gap;
                break;
            case Block::Kind::Heading:
                layoutFlow(b.flow, w);
                b.h = b.flow.height + (b.level <= 2 ? 8.0f : 0.0f);
                b.marginBottom = b.level <= 2 ? 12.0f : 8.0f;
                break;
            case Block::Kind::Rule:
                b.h = 1.0f;
                b.marginBottom = 14.0f;
                break;
            case Block::Kind::Code: {
                b.lines.clear();
                float room = std::max(40.0f, w - CODE_PAD * 2.0f);
                size_t start = 0;
                while (start <= b.code.size()) {
                    size_t end = b.code.find('\n', start);
                    if (end == std::string::npos) {
                        end = b.code.size();
                    }
                    std::string line = b.code.substr(start, end - start);
                    size_t tab;
                    while ((tab = line.find('\t')) != std::string::npos) {
                        line.replace(tab, 1, "    ");
                    }
                    if (line.empty()) {
                        b.lines.emplace_back();
                    } else {
                        for (std::string& part : UiFont::wrap(line, UiFont::CODE, CODE_SIZE, room)) {
                            b.lines.push_back(std::move(part));
                        }
                    }
                    if (end == b.code.size()) {
                        break;
                    }
                    start = end + 1;
                }
                b.h = b.lines.size() * lineHeight(CODE_SIZE) * 0.9f + CODE_PAD * 2.0f;
                b.marginBottom = gap;
                break;
            }
            case Block::Kind::Quote:
                b.h = layoutBlocks(b.children, x + QUOTE_PAD, y + 4.0f, std::max(40.0f, w - QUOTE_PAD)) + 8.0f;
                b.marginBottom = gap;
                break;
            case Block::Kind::List: {
                float marker = 18.0f;
                if (b.ordered) {
                    std::string widest = std::to_string(b.start + (int) b.children.size() - 1) + ".";
                    marker = UiFont::width(widest, UiFont::MEDIUM, baseSize) + 8.0f;
                }
                float cursor = 0.0f;
                for (size_t i = 0; i < b.children.size(); i++) {
                    Box& item = b.children[i];
                    item.x = x;
                    item.y = y + cursor;
                    item.w = w;
                    float inner = layoutBlocks(item.children, x + marker, y + cursor, std::max(40.0f, w - marker));
                    item.h = std::max(inner, lineHeight(baseSize));
                    item.markerY = item.y;
                    if (!item.children.empty() && (item.children.front().kind == Block::Kind::Paragraph)) {
                        item.markerY = item.children.front().y;
                    }
                    cursor += item.h + (i + 1 < b.children.size() ? ITEM_GAP : 0.0f);
                }
                b.colW = {marker};
                b.h = cursor;
                b.marginBottom = gap;
                break;
            }
            case Block::Kind::Container: {
                b.h = layoutBlocks(b.children, x, y, w);
                float last = b.children.empty() ? 0.0f : b.children.back().marginBottom;
                b.marginBottom = std::max(last, b.open ? gap : 0.0f);
                break;
            }
            case Block::Kind::Item: b.h = layoutBlocks(b.children, x, y, w); break;
            case Block::Kind::Details: {
                layoutFlow(b.flow, std::max(40.0f, w - 24.0f));
                float head = std::max(SUMMARY_H, b.flow.height + 10.0f);
                b.rowH = {head};
                b.h = head;
                if (b.open) {
                    b.h += layoutBlocks(b.children, x + 16.0f, y + head + 6.0f, std::max(40.0f, w - 16.0f)) + 10.0f;
                }
                b.marginBottom = 8.0f;
                break;
            }
            case Block::Kind::Video:
                b.h = VIDEO_H;
                b.marginBottom = gap;
                break;
            case Block::Kind::Table: layoutTable(b, x, y, w); break;
        }
    }

    void layoutTable(Box& b, float x, float y, float w) {
        size_t columns = b.columns.size();
        std::vector<float> natural(columns, 0.0f);
        std::vector<float> minimum(columns, 0.0f);
        std::vector<bool> pictured(columns, false);
        for (auto& row : b.cells) {
            for (size_t c = 0; c < row.size(); c++) {
                Flow& cell = row[c];
                pictured[c] = pictured[c] || cell.figure;
                float most = std::ceil(naturalWidth(cell));
                float least = 0.0f;
                if (cell.figure && !cell.lineWidths.empty() && cell.lineWidths[0] > 0.0f) {
                    most = std::ceil(cell.lineWidths[0]);
                    least = most <= 96.0f ? most : std::max(96.0f, most * 0.2f);
                } else {
                    least = most <= 72.0f ? most : std::ceil(minimumWidth(cell));
                }
                natural[c] = std::max(natural[c], most);
                minimum[c] = std::max(minimum[c], least);
            }
        }
        float pad = CELL_X * 2.0f;
        float room = std::max(12.0f * (float) columns, w - pad * (float) columns);
        float naturalSum = 0.0f;
        float minimumSum = 0.0f;
        for (size_t c = 0; c < columns; c++) {
            natural[c] = std::max(natural[c], 12.0f);
            if (!pictured[c]) {
                minimum[c] = std::max(minimum[c], std::min(natural[c], std::floor(room * 0.28f)));
            }
            minimum[c] = std::clamp(minimum[c], 12.0f, natural[c]);
            naturalSum += natural[c];
            minimumSum += minimum[c];
        }
        b.colW.assign(columns, 0.0f);
        if (naturalSum <= room) {
            b.colW = natural;
        } else if (minimumSum >= room) {
            float rigid = 0.0f;
            float flexible = 0.0f;
            for (size_t c = 0; c < columns; c++) {
                (natural[c] <= 72.0f ? rigid : flexible) += minimum[c];
            }
            bool keep = rigid < room && flexible > 0.0f;
            for (size_t c = 0; c < columns; c++) {
                if (keep && natural[c] <= 72.0f) {
                    b.colW[c] = minimum[c];
                } else {
                    b.colW[c] = keep ? minimum[c] * (room - rigid) / flexible : minimum[c] * room / minimumSum;
                }
            }
        } else {
            float extra = room - minimumSum;
            float spread = naturalSum - minimumSum;
            for (size_t c = 0; c < columns; c++) {
                b.colW[c] = minimum[c] + (spread > 0.0f ? extra * (natural[c] - minimum[c]) / spread : 0.0f);
            }
        }
        for (size_t c = 0; c < columns; c++) {
            b.colW[c] += pad;
        }
        b.tableW = 0.0f;
        b.colX.assign(columns, 0.0f);
        for (size_t c = 0; c < columns; c++) {
            b.colX[c] = b.tableW;
            b.tableW += b.colW[c];
        }
        b.rowY.clear();
        b.rowH.clear();
        float cursor = 0.0f;
        for (auto& row : b.cells) {
            float h = 0.0f;
            for (size_t c = 0; c < row.size(); c++) {
                layoutFlow(row[c], std::max(10.0f, b.colW[c] - pad));
                h = std::max(h, row[c].height);
            }
            h = std::max(h, lineHeight(baseSize * 0.94f)) + CELL_Y * 2.0f + 2.0f;
            b.rowY.push_back(cursor);
            b.rowH.push_back(h);
            cursor += h;
        }
        b.x = x +
                (b.align == Align::Center        ? std::max(0.0f, (w - b.tableW) * 0.5f)
                         : b.align == Align::End ? std::max(0.0f, w - b.tableW)
                                                 : 0.0f);
        b.y = y;
        b.w = b.tableW;
        b.h = cursor;
        b.marginBottom = PARA_GAP;
    }

    struct Ctx {
        float ox = 0.0f;
        float oy = 0.0f;
        float clipTop = 0.0f;
        float clipBottom = 0.0f;
        bool blocked = false;
    };

    void drawText(const std::string& text, float x, float top, uint32_t color, const Run& run) {
        bool slanted = (run.style & Markdown::EM) != 0;
        if (slanted) {
            UiRender::flush();
        }
        ImDrawList* dl = UiRender::drawList();
        int start = dl != nullptr ? dl->VtxBuffer.Size : 0;
        UiFont::draw(text, x, top, color, run.family, run.size);
        if ((run.style & Markdown::STRONG) && run.family != UiFont::CODE) {
            UiFont::draw(text, x + 0.55f, top, color, run.family, run.size);
        }
        if (slanted && dl != nullptr && dl->VtxBuffer.Size > start) {
            float low = dl->VtxBuffer[start].pos.y;
            float high = low;
            for (int i = start; i < dl->VtxBuffer.Size; i++) {
                low = std::min(low, dl->VtxBuffer[i].pos.y);
                high = std::max(high, dl->VtxBuffer[i].pos.y);
            }
            float pivot = high - (high - low) * 0.2f;
            for (int i = start; i < dl->VtxBuffer.Size; i++) {
                ImDrawVert& v = dl->VtxBuffer[i];
                v.pos.x += (pivot - v.pos.y) * 0.18f;
            }
        }
    }

    void drawFlow(MarkdownView& view, Flow& f, float x, float y, float boxW, Ctx& ctx) {
        for (Piece& piece : f.pieces) {
            const Run& run = f.runs[piece.run];
            float lineW = f.lineWidths[piece.line];
            float offset = f.align == Align::Center ? (boxW - lineW) * 0.5f
                    : f.align == Align::End         ? boxW - lineW
                                                    : 0.0f;
            float px = x + std::max(0.0f, offset) + piece.x;
            float py = y + piece.top;
            if (py > ctx.clipBottom || py + std::max(piece.h, run.size) < ctx.clipTop) {
                continue;
            }
            bool linked = run.link >= 0 && run.link < (int) doc.links.size();
            float hitH = std::max(piece.h, run.size + 4.0f);
            float hitY = run.kind == Span::Kind::Image ? py : py - 2.0f;
            bool hovered = !ctx.blocked && piece.w > 0.0f &&
                    NewUi::hit(px, std::max(hitY, ctx.clipTop), piece.w, std::min(hitH, ctx.clipBottom - hitY));
            if (hovered && linked) {
                nextHoverLink = run.link;
                UiCursor::hand();
                const std::string& target = doc.links[run.link];
                bool anchor = !target.empty() && target[0] == '#';
                if (!anchor) {
                    Page::tooltip(target, px, py, piece.w, hitH);
                }
                if (NewUi::click(px, hitY, piece.w, hitH)) {
                    UiSound::request(UiSound::TICK);
                    if (anchor) {
                        jump(view, target);
                    } else if (view.onLink) {
                        view.onLink(target);
                    }
                }
            }
            if (hovered && (run.style & Markdown::SPOILER)) {
                nextHoverGroup = run.group;
            }
            if (run.kind == Span::Kind::Image) {
                const WebImages::Image& image = WebImages::get(imageUrl(run.src));
                if (image.texture == 0) {
                    UiRender::roundRect(px, py, piece.w, piece.h, 4.0f, UiTheme::surfaceRaised());
                    continue;
                }
                float radius = piece.h > 48.0f ? 6.0f : 0.0f;
                if (radius > 0.0f) {
                    ShaderUtil::roundTex(UiRender::transformX(px), UiRender::transformY(py),
                                         UiRender::transformW(piece.w), UiRender::transformW(piece.h),
                                         UiRender::transformW(radius), image.texture, 0.0f, 0.0f, 1.0f, 1.0f,
                                         UiRender::applyAlpha(0xFFFFFFFFu));
                } else {
                    UiRender::texture(image.texture, px, py, piece.w, piece.h);
                }
                if (linked && hoverLink == run.link) {
                    UiRender::roundRect(px, py, piece.w, piece.h, radius, UiTheme::overlay(14));
                }
                continue;
            }
            bool code = (run.style & (Markdown::CODE | Markdown::KBD)) != 0;
            if (run.gap > 0.0f) {
                continue;
            }
            if (code) {
                float bx = px - CODE_GAP + 1.0f;
                float bw = piece.w + CODE_GAP * 2.0f - 2.0f;
                UiRender::roundRect(bx, py - 2.0f, bw, run.size + 5.0f, 4.0f, UiTheme::overlay(16));
                if (run.style & Markdown::KBD) {
                    UiRender::outline(bx, py - 2.0f, bw, run.size + 5.0f, 4.0f, UiTheme::outlineSoft());
                }
            }
            uint32_t color = linked ? UiTheme::accentText() : code ? UiTheme::textPrimary() : toneColor(run.tone);
            if ((run.style & Markdown::SPOILER) && hoverGroup != run.group) {
                UiRender::roundRect(px - 1.0f, py - 1.0f, piece.w + 2.0f, run.size + 3.0f, 3.0f, UiTheme::textMuted());
                continue;
            }
            drawText(piece.text, px, py, color, run);
            if ((run.style & Markdown::STRIKE)) {
                UiRender::hLine(px, px + piece.w, py + run.size * 0.55f, color);
            }
            if ((run.style & Markdown::UNDERLINE) || (linked && hoverLink == run.link)) {
                UiRender::hLine(px, px + piece.w, py + run.size + 1.0f, color);
            }
        }
    }

    void drawBlocks(MarkdownView& view, std::vector<Box>& boxes, Ctx& ctx) {
        for (Box& b : boxes) {
            float top = ctx.oy + b.y;
            if (top > ctx.clipBottom) {
                break;
            }
            if (top + b.h < ctx.clipTop) {
                continue;
            }
            drawBox(view, b, ctx);
        }
    }

    void drawBox(MarkdownView& view, Box& b, Ctx& ctx) {
        float x = ctx.ox + b.x;
        float y = ctx.oy + b.y;
        switch (b.kind) {
            case Block::Kind::Paragraph: drawFlow(view, b.flow, x, y, b.w, ctx); break;
            case Block::Kind::Heading:
                drawFlow(view, b.flow, x, y, b.w, ctx);
                if (b.level <= 2) {
                    UiRender::hLine(x, x + b.w, y + b.flow.height + 5.0f, UiTheme::divider());
                }
                break;
            case Block::Kind::Rule: UiRender::hLine(x, x + b.w, y, UiTheme::divider()); break;
            case Block::Kind::Code: drawCode(b, x, y, ctx); break;
            case Block::Kind::Quote:
                UiRender::roundRect(x, y, 3.0f, b.h, 1.5f, UiTheme::outline());
                drawBlocks(view, b.children, ctx);
                break;
            case Block::Kind::List: drawList(view, b, ctx); break;
            case Block::Kind::Container:
            case Block::Kind::Item: drawBlocks(view, b.children, ctx); break;
            case Block::Kind::Details: drawDetails(view, b, x, y, ctx); break;
            case Block::Kind::Video: drawVideo(view, b, x, y, ctx); break;
            case Block::Kind::Table: drawTable(view, b, x, y, ctx); break;
        }
    }

    void drawList(MarkdownView& view, Box& b, Ctx& ctx) {
        float marker = b.colW.empty() ? 18.0f : b.colW[0];
        float lh = lineHeight(baseSize);
        for (size_t i = 0; i < b.children.size(); i++) {
            Box& item = b.children[i];
            float iy = ctx.oy + item.y;
            if (iy > ctx.clipBottom) {
                break;
            }
            if (iy + item.h < ctx.clipTop) {
                continue;
            }
            float mx = ctx.ox + item.x;
            float my = ctx.oy + item.markerY;
            uint32_t color = toneColor(b.tone);
            if (item.checked >= 0) {
                UiWidgets::icon(Ui::glyph(item.checked ? 0xf14a : 0xf0c8), mx + 6.0f, my + lh * 0.5f,
                                item.checked ? UiTheme::accent() : UiTheme::textFaint(), 11.0f);
            } else if (b.ordered) {
                std::string label = std::to_string(b.start + (int) i) + ".";
                float lw = UiFont::width(label, UiFont::MEDIUM, baseSize);
                UiRender::textY(label, mx + marker - 8.0f - lw, my, lh, color, UiFont::MEDIUM, baseSize);
            } else {
                float r = 2.5f;
                UiRender::circle(mx + 6.0f, my + lh * 0.5f, r, UiTheme::textMuted());
            }
            drawBlocks(view, item.children, ctx);
        }
    }

    void drawCode(Box& b, float x, float y, Ctx& ctx) {
        UiRender::roundRect(x, y, b.w, b.h, 8.0f, UiTheme::overlay(12));
        float lh = lineHeight(CODE_SIZE) * 0.9f;
        float ty = y + CODE_PAD;
        for (const std::string& line : b.lines) {
            if (ty > ctx.clipBottom) {
                break;
            }
            if (ty + lh >= ctx.clipTop && !line.empty()) {
                UiRender::textY(line, x + CODE_PAD, ty, lh, UiTheme::textPrimary(), UiFont::CODE, CODE_SIZE);
            }
            ty += lh;
        }
        bool hovered = !ctx.blocked && NewUi::hit(x, std::max(y, ctx.clipTop), b.w, b.h);
        if (hovered && y + 4.0f >= ctx.clipTop) {
            if (Page::iconButton("markdown#copy#" + id + "#" + std::to_string((long long) (b.y * 10.0f)), 0xf0c5,
                                 "复制", x + b.w - 32.0f, y + 4.0f, ctx.blocked)) {
                UiInput::setClipboard(b.code);
                Notifications::add(Notifications::INFO, "已复制代码", 1500);
            }
        }
    }

    void drawDetails(MarkdownView& view, Box& b, float x, float y, Ctx& ctx) {
        float head = b.rowH.empty() ? SUMMARY_H : b.rowH[0];
        bool hovered = !ctx.blocked && NewUi::hit(x, y, b.w, head);
        float lit = UiState::motion("markdown#details#" + id + "#" + std::to_string((long long) (b.y * 10.0f)),
                                    hovered ? 1.0f : 0.0f, 18.0f);
        if (lit > 0.001f) {
            UiRender::roundRect(x, y, b.w, head, 8.0f, UiTheme::overlay(Ui::round(10 * lit)));
        }
        UiWidgets::icon(Ui::glyph(b.open ? 0xf078 : 0xf054), x + 10.0f, y + head * 0.5f, UiTheme::textMuted(), 9.0f);
        drawFlow(view, b.flow, x + 24.0f, y + (head - b.flow.height) * 0.5f, b.w - 24.0f, ctx);
        if (hovered && NewUi::click(x, y, b.w, head)) {
            UiSound::request(UiSound::TICK);
            b.open = !b.open;
            dirty = true;
        }
        if (b.open) {
            UiRender::vLine(x + 10.0f, y + head + 2.0f, y + b.h - 6.0f, UiTheme::divider());
            drawBlocks(view, b.children, ctx);
        }
    }

    void drawVideo(MarkdownView& view, Box& b, float x, float y, Ctx& ctx) {
        std::string host = b.url;
        size_t scheme = host.find("://");
        if (scheme != std::string::npos) {
            host = host.substr(scheme + 3);
        }
        host = host.substr(0, host.find('/'));
        std::string label = "观看视频";
        float w = std::min(b.w, UiFont::width(label + host, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 64.0f);
        bool hovered = !ctx.blocked && NewUi::hit(x, y, w, b.h);
        UiRender::roundRect(x, y, w, b.h, 8.0f, hovered ? UiTheme::overlay(16) : UiTheme::overlay(10));
        UiWidgets::icon(Ui::glyph(0xf144), x + 18.0f, y + b.h * 0.5f, UiTheme::accent(), 14.0f);
        UiRender::textY(label, x + 34.0f, y, b.h, UiTheme::textPrimary(), UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
        float hostX = x + 34.0f + UiFont::width(label, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 8.0f;
        UiRender::textY(
                UiFont::fitEnd(host, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, std::max(0.0f, x + w - 10.0f - hostX)),
                hostX, y, b.h, UiTheme::textMuted(), UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
        if (hovered) {
            UiCursor::hand();
            if (NewUi::click(x, y, w, b.h) && view.onLink) {
                UiSound::request(UiSound::TICK);
                view.onLink(b.url);
            }
        }
    }

    void drawTable(MarkdownView& view, Box& b, float x, float y, Ctx& ctx) {
        float right = x + b.tableW;
        if (b.headerRows > 0 && b.headerRows <= (int) b.rowH.size()) {
            size_t last = (size_t) b.headerRows - 1;
            UiRender::roundRect(x, y, b.tableW, b.rowY[last] + b.rowH[last], 8.0f, UiTheme::overlay(9), 1 | 2);
        }
        for (size_t r = 0; r < b.cells.size(); r++) {
            float ry = y + b.rowY[r];
            if (ry > ctx.clipBottom) {
                break;
            }
            if (r > 0) {
                UiRender::hLine(x, right, ry, UiTheme::outlineSoft());
            }
            if (ry + b.rowH[r] < ctx.clipTop) {
                continue;
            }
            for (size_t c = 0; c < b.cells[r].size(); c++) {
                Flow& cell = b.cells[r][c];
                float inner = b.colW[c] - CELL_X * 2.0f;
                float cy = ry + CELL_Y + 1.0f;
                if (cell.figure) {
                    cy += std::max(0.0f, (b.rowH[r] - CELL_Y * 2.0f - 2.0f - cell.height) * 0.5f);
                }
                drawFlow(view, cell, x + b.colX[c] + CELL_X, cy, inner, ctx);
            }
        }
        for (size_t c = 1; c < b.colX.size(); c++) {
            UiRender::vLine(x + b.colX[c], y, y + b.h, UiTheme::outlineSoft());
        }
        UiRender::outline(x, y, b.tableW, b.h, 8.0f, UiTheme::outline());
    }
};

MarkdownView::MarkdownView() : state(std::make_unique<State>()) {}

MarkdownView::~MarkdownView() = default;

void MarkdownView::load(const std::string& text, Markdown::Format format, const std::string& id) {
    if (state->source == text && state->format == format && state->id == id && !state->boxes.empty()) {
        return;
    }
    state = std::make_unique<State>();
    state->id = id;
    state->source = text;
    state->format = format;
    state->baseSize = textSize;
    state->doc = Markdown::parse(text, format);
    state->collect(state->doc.blocks);
    for (const Block& block : state->doc.blocks) {
        state->boxes.push_back(state->build(block, BODY, Align::Start, false));
    }
}

bool MarkdownView::empty() const {
    return state->boxes.empty();
}

float MarkdownView::measure(float width) {
    float scale = UiRender::uiScale;
    long long generation = WebImages::generation();
    if (!state->dirty && state->cachedWidth == width && state->cachedScale == scale &&
        state->cachedGeneration == generation) {
        return state->height;
    }
    bool raw = UiFont::raw;
    UiFont::raw = true;
    state->dirty = false;
    state->cachedWidth = width;
    state->cachedScale = scale;
    state->cachedGeneration = generation;
    state->height = state->layoutBlocks(state->boxes, 0.0f, 0.0f, std::max(40.0f, width));
    UiFont::raw = raw;
    return state->height;
}

void MarkdownView::draw(float x, float y, float clipTop, float clipBottom, bool blocked) {
    bool raw = UiFont::raw;
    UiFont::raw = true;
    State::Ctx ctx;
    ctx.ox = x;
    ctx.oy = y;
    ctx.clipTop = clipTop;
    ctx.clipBottom = clipBottom;
    ctx.blocked = blocked;
    state->nextHoverLink = -1;
    state->nextHoverGroup = -1;
    state->drawBlocks(*this, state->boxes, ctx);
    state->hoverLink = state->nextHoverLink;
    state->hoverGroup = state->nextHoverGroup;
    UiFont::raw = raw;
}
