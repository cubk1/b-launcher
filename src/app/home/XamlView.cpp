#include "app/home/XamlView.h"
#include "app/shell/MainWindow.h"
#include "app/shell/Page.h"
#include "app/shell/SvgPath.h"
#include "app/home/WebImages.h"
#include "app/home/Xaml.h"

#include "ui/gl/ShaderUtil.h"
#include "ui/panel/NewUi.h"
#include "ui/panel/UiCommon.h"
#include "ui/panel/UiFont.h"
#include "ui/panel/UiRender.h"
#include "ui/panel/UiSound.h"
#include "ui/panel/UiState.h"
#include "ui/panel/UiTheme.h"
#include "ui/panel/UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>

namespace XamlView {

    namespace {
        constexpr float INF = std::numeric_limits<float>::infinity();
        constexpr float CARD_HEADER = 40.0f;
        constexpr float CARD_RADIUS = 12.0f;
        constexpr float TEXT_SIZE = 13.5f;
        constexpr float CONTROL_SIZE = 13.0f;
        constexpr float LINE_FACTOR = 1.38f;

        enum Align { STRETCH, START, CENTER, END };
        enum Stretch { NONE, FILL, UNIFORM, UNIFORM_FILL };

        enum class Kind {
            Stack,
            Wrap,
            Grid,
            Border,
            Card,
            Text,
            Flow,
            Image,
            Path,
            Line,
            Shape,
            Hint,
            Button,
            TextButton,
            IconTextButton,
            IconButton,
            ListItem,
            Loading,
            Code,
            Rule
        };

        struct Thickness {
            float l = 0.0f;
            float t = 0.0f;
            float r = 0.0f;
            float b = 0.0f;

            float h() const {
                return l + r;
            }

            float v() const {
                return t + b;
            }
        };

        struct Brush {
            int kind = 0;
            uint32_t argb = 0;
            int level = 0;
            uint32_t argb2 = 0;
            bool vertical = false;
            std::string image;
            int stretch = FILL;

            bool set() const {
                return kind != 0;
            }
        };

        struct Def {
            int type = 2;
            float value = 1.0f;
            float min = 0.0f;
            float max = INF;
        };

        struct Element;

        struct Token {
            size_t start = 0;
            size_t length = 0;
            float width = 0.0f;
            int type = 0;
        };

        struct Inline {
            std::string text;
            Brush color;
            float size = TEXT_SIZE;
            int family = UiFont::MEDIUM;
            bool underline = false;
            bool lineBreak = false;
            Element* element = nullptr;
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
            Element* element = nullptr;
        };

        struct Rich {
            std::vector<Inline> runs;
            std::vector<std::unique_ptr<Element>> owned;
            Brush foreground;
            int align = 0;
            bool wrap = true;
            float lineHeight = 0.0f;
            float cachedWidth = -2.0f;
            float cachedScale = 0.0f;
            long long cachedGeneration = -1;
            std::vector<Piece> pieces;
            std::vector<float> lineWidths;
            float width = 0.0f;
            float height = 0.0f;
            float firstLine = 0.0f;
        };

        struct Block {
            int type = 0;
            Rich rich;
            Thickness margin;
            std::vector<std::vector<Block>> items;
            std::vector<float> itemTops;
            std::vector<float> markerYs;
            Thickness itemMargin;
            std::vector<Block> blocks;
            std::unique_ptr<Element> ui;
            Brush marker;
            float markerSize = TEXT_SIZE;
            int markerStyle = 1;
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
        };

        struct Element {
            Kind kind = Kind::Stack;
            std::string id;
            Thickness margin;
            Thickness padding;
            bool paddingSet = false;
            float width = NAN;
            float height = NAN;
            float minWidth = 0.0f;
            float minHeight = 0.0f;
            float maxWidth = INF;
            float maxHeight = INF;
            int hAlign = STRETCH;
            int vAlign = STRETCH;
            bool collapsed = false;
            bool hidden = false;
            float opacity = 1.0f;
            std::string toolTip;
            bool hitTest = true;
            int column = 0;
            int row = 0;
            int columnSpan = 1;
            int rowSpan = 1;
            std::vector<std::unique_ptr<Element>> children;

            bool horizontal = false;
            std::vector<Def> columns;
            std::vector<Def> rows;
            std::vector<float> colSizes;
            std::vector<float> rowSizes;
            Brush background;
            Brush borderBrush;
            Brush foreground;
            Thickness borderThickness;
            float corners[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float clipRadius = 0.0f;
            std::string title;
            bool canSwap = false;
            bool swapped = false;
            float open = 1.0f;
            float contentH = 0.0f;
            Rich rich;
            std::vector<Block> blocks;
            std::string source;
            std::string fallback;
            int stretch = UNIFORM;
            std::string data;
            float logoScale = 1.0f;
            Brush fill;
            Brush stroke;
            float strokeThickness = 1.0f;
            float x1 = 0.0f;
            float y1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            bool ellipse = false;
            bool groupBox = false;
            float radius = 0.0f;
            std::string text;
            std::string info;
            int theme = 0;
            std::vector<Action> actions;
            bool clickable = false;
            float fontSize = CONTROL_SIZE;
            bool block = false;
            std::vector<std::string> codeLines;
            float codeWidth = -1.0f;

            float desiredW = 0.0f;
            float desiredH = 0.0f;
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
        };

        struct Ctx {
            float clipTop = 0.0f;
            float clipBottom = 0.0f;
            bool blocked = false;
            bool glass = false;
            bool captured = false;
            std::vector<Action>* pending = nullptr;
        };

        std::string trim(const std::string& text) {
            size_t start = text.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return std::string();
            }
            size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(start, end - start + 1);
        }

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return text;
        }

        bool isTrue(const std::string& value) {
            return lower(trim(value)) == "true";
        }

        float number(const std::string& value, float fallback) {
            std::string text = trim(value);
            if (text.empty()) {
                return fallback;
            }
            char* end = nullptr;
            float parsed = std::strtof(text.c_str(), &end);
            return end == text.c_str() ? fallback : parsed;
        }

        float length(const std::string& value) {
            std::string text = lower(trim(value));
            if (text.empty() || text == "auto" || text == "nan") {
                return NAN;
            }
            return number(text, NAN);
        }

        std::vector<float> numbers(const std::string& value) {
            std::vector<float> out;
            std::string text = value;
            for (char& c : text) {
                if (c == ',') {
                    c = ' ';
                }
            }
            const char* cursor = text.c_str();
            while (*cursor != 0) {
                char* end = nullptr;
                float parsed = std::strtof(cursor, &end);
                if (end == cursor) {
                    cursor++;
                    continue;
                }
                out.push_back(parsed);
                cursor = end;
            }
            return out;
        }

        Thickness thickness(const std::string& value) {
            std::vector<float> v = numbers(value);
            Thickness out;
            if (v.size() == 1) {
                out = {v[0], v[0], v[0], v[0]};
            } else if (v.size() == 2) {
                out = {v[0], v[1], v[0], v[1]};
            } else if (v.size() >= 4) {
                out = {v[0], v[1], v[2], v[3]};
            }
            return out;
        }

        int alignment(const std::string& value) {
            std::string text = lower(trim(value));
            if (text == "left" || text == "top") {
                return START;
            }
            if (text == "center") {
                return CENTER;
            }
            if (text == "right" || text == "bottom") {
                return END;
            }
            return STRETCH;
        }

        int textAlignment(const std::string& value) {
            std::string text = lower(trim(value));
            return text == "center" ? 1 : (text == "right" ? 2 : 0);
        }

        int stretchOf(const std::string& value, int fallback) {
            std::string text = lower(trim(value));
            if (text == "none") {
                return NONE;
            }
            if (text == "fill") {
                return FILL;
            }
            if (text == "uniform") {
                return UNIFORM;
            }
            if (text == "uniformtofill") {
                return UNIFORM_FILL;
            }
            return fallback;
        }

        bool bold(const std::string& value) {
            std::string text = lower(trim(value));
            return text == "bold" || text == "semibold" || text == "demibold" || text == "extrabold" ||
                    text == "black" || text == "heavy" || text == "ultrabold";
        }

        uint32_t hexColor(const std::string& digits, bool& ok) {
            uint32_t value = 0;
            for (char c : digits) {
                int nibble = std::isdigit((unsigned char) c) ? c - '0'
                        : (c >= 'a' && c <= 'f')             ? c - 'a' + 10
                        : (c >= 'A' && c <= 'F')             ? c - 'A' + 10
                                                             : -1;
                if (nibble < 0) {
                    ok = false;
                    return 0;
                }
                value = value * 16 + (uint32_t) nibble;
            }
            ok = true;
            switch (digits.size()) {
                case 3:
                    return 0xFF000000u | ((value >> 8 & 0xF) * 0x11u) << 16 | ((value >> 4 & 0xF) * 0x11u) << 8 |
                            (value & 0xF) * 0x11u;
                case 4:
                    return ((value >> 12 & 0xF) * 0x11u) << 24 | ((value >> 8 & 0xF) * 0x11u) << 16 |
                            ((value >> 4 & 0xF) * 0x11u) << 8 | (value & 0xF) * 0x11u;
                case 6: return 0xFF000000u | value;
                case 8: return value;
                default: ok = false; return 0;
            }
        }

        Brush brushOf(const std::string& raw) {
            std::string value = trim(raw);
            Brush out;
            if (value.empty()) {
                return out;
            }
            if (value.front() == '{') {
                out.kind = 2;
                out.level = 3;
                size_t at = value.find("ColorBrush");
                if (at != std::string::npos) {
                    int n = std::atoi(value.c_str() + at + 10);
                    if (n >= 1 && n <= 8) {
                        out.level = n;
                    }
                }
                return out;
            }
            if (value.front() == '#') {
                bool ok = false;
                uint32_t color = hexColor(value.substr(1), ok);
                if (ok) {
                    out.kind = 1;
                    out.argb = color;
                }
                return out;
            }
            static const std::map<std::string, uint32_t> named = {
                    {"transparent", 0x00000000u}, {"white", 0xFFFFFFFFu},     {"black", 0xFF000000u},
                    {"red", 0xFFFF0000u},         {"green", 0xFF008000u},     {"blue", 0xFF0000FFu},
                    {"yellow", 0xFFFFFF00u},      {"orange", 0xFFFFA500u},    {"gray", 0xFF808080u},
                    {"grey", 0xFF808080u},        {"lightgray", 0xFFD3D3D3u}, {"darkgray", 0xFFA9A9A9u},
                    {"silver", 0xFFC0C0C0u},      {"purple", 0xFF800080u},    {"pink", 0xFFFFC0CBu},
                    {"brown", 0xFFA52A2Au},       {"gold", 0xFFFFD700u},      {"cyan", 0xFF00FFFFu},
                    {"magenta", 0xFFFF00FFu},     {"lime", 0xFF00FF00u},      {"navy", 0xFF000080u},
                    {"teal", 0xFF008080u},        {"skyblue", 0xFF87CEEBu},   {"dodgerblue", 0xFF1E90FFu},
                    {"crimson", 0xFFDC143Cu},     {"darkred", 0xFF8B0000u},   {"darkgreen", 0xFF006400u},
                    {"darkblue", 0xFF00008Bu},    {"orangered", 0xFFFF4500u}, {"deepskyblue", 0xFF00BFFFu},
            };
            auto found = named.find(lower(value));
            if (found != named.end()) {
                out.kind = 1;
                out.argb = found->second;
            }
            return out;
        }

        float linear(float v) {
            return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        }

        float luminance(uint32_t c) {
            return 0.2126f * linear(((c >> 16) & 0xFF) / 255.0f) + 0.7152f * linear(((c >> 8) & 0xFF) / 255.0f) +
                    0.0722f * linear((c & 0xFF) / 255.0f);
        }

        uint32_t invertLightness(uint32_t c) {
            float r = ((c >> 16) & 0xFF) / 255.0f;
            float g = ((c >> 8) & 0xFF) / 255.0f;
            float b = (c & 0xFF) / 255.0f;
            float hi = std::max({r, g, b});
            float lo = std::min({r, g, b});
            float l = (hi + lo) * 0.5f;
            float target = 1.0f - l;
            float shift = target - l;
            auto move = [shift](float v) { return std::clamp(v + shift, 0.0f, 1.0f); };
            return (c & 0xFF000000u) | (uint32_t) std::lround(move(r) * 255.0f) << 16 |
                    (uint32_t) std::lround(move(g) * 255.0f) << 8 | (uint32_t) std::lround(move(b) * 255.0f);
        }

        uint32_t readable(uint32_t c) {
            float a = luminance(c);
            float b = luminance(UiTheme::surface());
            float ratio = (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
            return ratio < 2.2f ? invertLightness(c) : c;
        }

        uint32_t level(int n) {
            static const float stops[] = {0.04f, 0.24f, 0.5f, 0.62f, 0.74f, 0.84f, 0.91f, 0.95f};
            float position = stops[std::clamp(n, 1, 8) - 1];
            uint32_t accent = UiTheme::accent() | 0xFF000000u;
            uint32_t ink = UiRender::mix(UiTheme::textPrimary() | 0xFF000000u, accent, 0.18f);
            uint32_t paper = UiTheme::surface() | 0xFF000000u;
            return position <= 0.5f ? UiRender::mix(ink, accent, position / 0.5f)
                                    : UiRender::mix(accent, paper, (position - 0.5f) / 0.5f);
        }

        uint32_t paint(const Brush& brush, uint32_t fallback, bool text) {
            if (brush.kind == 1 || brush.kind == 3) {
                return text ? readable(brush.argb) : brush.argb;
            }
            if (brush.kind == 2) {
                return level(brush.level);
            }
            if (brush.kind == 5) {
                return UiTheme::textMuted();
            }
            return fallback;
        }

        std::string clean(const std::string& text) {
            std::string out;
            out.reserve(text.size());
            size_t i = 0;
            bool dropped = false;
            while (i < text.size()) {
                size_t from = i;
                unsigned char c = (unsigned char) text[i];
                int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 0;
                char32_t code = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
                i++;
                for (int k = 0; k < extra && i < text.size(); k++, i++) {
                    code = (code << 6) | ((unsigned char) text[i] & 0x3F);
                }
                if (code > 0xFFFF || code == 0xFE0F || code == 0x200D || code == 0x20E3) {
                    dropped = true;
                    continue;
                }
                if (dropped && code == ' ') {
                    dropped = false;
                    continue;
                }
                dropped = false;
                out.append(text, from, i - from);
            }
            return out;
        }

        char32_t nextCode(const std::string& text, size_t& i) {
            unsigned char c = (unsigned char) text[i];
            int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 0;
            char32_t code = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
            i++;
            for (int k = 0; k < extra && i < text.size(); k++, i++) {
                code = (code << 6) | ((unsigned char) text[i] & 0x3F);
            }
            return code;
        }

        bool space(char32_t c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool cjk(char32_t c) {
            return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) ||
                    (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x20000 && c <= 0x2FFFF) ||
                    (c >= 0x1F000 && c <= 0x1FAFF);
        }

        bool closing(char32_t c) {
            static const std::u32string_view marks = U"，。、；：？！）」』》〉】〕…,.;:?!)]}%";
            return marks.find(c) != std::u32string_view::npos;
        }

        float lineHeightOf(float size, float custom) {
            return std::max(std::round(size * LINE_FACTOR), custom);
        }

        void measure(Element& e, float aw, float ah);
        void arrange(Element& e, float x, float y, float w, float h);
        void draw(Element& e, Ctx& ctx);

        void tokenize(Inline& run) {
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
                    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) {
                        i++;
                    }
                } else {
                    if (!cjk(c)) {
                        while (i < text.size()) {
                            size_t j = i;
                            char32_t d = nextCode(text, j);
                            if (space(d) || cjk(d)) {
                                break;
                            }
                            i = j;
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

        void layoutRich(Rich& r, float maxWidth) {
            float scale = UiRender::uiScale;
            long long generation = WebImages::generation();
            if (r.cachedWidth == maxWidth && r.cachedScale == scale && r.cachedGeneration == generation) {
                return;
            }
            r.cachedWidth = maxWidth;
            r.cachedScale = scale;
            r.cachedGeneration = generation;

            struct Item {
                int run = -1;
                size_t start = 0;
                size_t length = 0;
                float w = 0.0f;
                float h = 0.0f;
                Element* element = nullptr;
            };
            struct Line {
                std::vector<Item> items;
                float width = 0.0f;
                float trailing = 0.0f;
                float emptySize = 0.0f;
            };
            std::vector<Line> lines(1);
            bool wrap = r.wrap && std::isfinite(maxWidth);
            bool soft = false;
            float defaultSize = TEXT_SIZE;
            for (const Inline& run : r.runs) {
                if (run.element == nullptr && !run.lineBreak) {
                    defaultSize = run.size;
                    break;
                }
            }
            auto newLine = [&](float size) {
                if (lines.back().items.empty()) {
                    lines.back().emptySize = size;
                }
                lines.emplace_back();
            };
            auto append = [&](int runIndex, size_t start, size_t count, float w) {
                Line& line = lines.back();
                if (!line.items.empty()) {
                    Item& last = line.items.back();
                    if (last.element == nullptr && last.run == runIndex && last.start + last.length == start) {
                        last.length += count;
                        last.w += w;
                        line.width += w;
                        return;
                    }
                }
                line.items.push_back(Item{runIndex, start, count, w, 0.0f, nullptr});
                line.width += w;
            };
            for (int ri = 0; ri < (int) r.runs.size(); ri++) {
                Inline& run = r.runs[ri];
                if (run.lineBreak) {
                    newLine(run.size);
                    soft = false;
                    continue;
                }
                if (run.element != nullptr) {
                    measure(*run.element, wrap ? maxWidth : INF, INF);
                    float w = run.element->desiredW;
                    if (wrap && lines.back().width > 0.0f && lines.back().width + w > maxWidth) {
                        newLine(run.size);
                    }
                    lines.back().items.push_back(Item{ri, 0, 0, w, run.element->desiredH, run.element});
                    lines.back().width += w;
                    lines.back().trailing = 0.0f;
                    soft = false;
                    continue;
                }
                tokenize(run);
                for (const Token& token : run.tokens) {
                    if (token.type == 2) {
                        newLine(run.size);
                        soft = false;
                        continue;
                    }
                    if (token.type == 1) {
                        if (soft && lines.back().items.empty()) {
                            continue;
                        }
                        append(ri, token.start, token.length, token.width);
                        lines.back().trailing += token.width;
                        continue;
                    }
                    if (wrap && lines.back().width - lines.back().trailing > 0.0f &&
                        lines.back().width + token.width > maxWidth) {
                        lines.back().width -= lines.back().trailing;
                        newLine(run.size);
                        soft = true;
                    }
                    if (wrap && token.width > maxWidth && lines.back().width <= 0.0f) {
                        size_t i = token.start;
                        size_t end = token.start + token.length;
                        while (i < end) {
                            size_t from = i;
                            nextCode(run.text, i);
                            float w = UiFont::width(run.text.substr(from, i - from), run.family, run.size);
                            if (lines.back().width > 0.0f && lines.back().width + w > maxWidth) {
                                newLine(run.size);
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

            r.pieces.clear();
            r.lineWidths.clear();
            r.width = 0.0f;
            float y = 0.0f;
            for (size_t li = 0; li < lines.size(); li++) {
                Line& line = lines[li];
                float textSize = 0.0f;
                for (const Item& item : line.items) {
                    if (item.element == nullptr) {
                        textSize = std::max(textSize, r.runs[item.run].size);
                    }
                }
                float shown = textSize > 0.0f ? textSize : (line.emptySize > 0.0f ? line.emptySize : defaultSize);
                float above = 0.0f;
                float below = 0.0f;
                auto textMetrics = [&](float size) {
                    float lh = lineHeightOf(size, r.lineHeight);
                    float a = (lh - size) * 0.5f + 0.85f * size;
                    above = std::max(above, a);
                    below = std::max(below, lh - a);
                };
                bool any = false;
                for (const Item& item : line.items) {
                    if (item.element == nullptr) {
                        textMetrics(r.runs[item.run].size);
                        any = true;
                    } else {
                        float a = item.h * 0.5f + 0.35f * shown;
                        above = std::max(above, a);
                        below = std::max(below, item.h - a);
                        any = true;
                    }
                }
                if (!any) {
                    textMetrics(shown);
                }
                float baseline = y + above;
                float cursor = 0.0f;
                for (const Item& item : line.items) {
                    Piece piece;
                    piece.run = item.run;
                    piece.line = (int) li;
                    piece.x = cursor;
                    piece.w = item.w;
                    if (item.element != nullptr) {
                        piece.element = item.element;
                        piece.h = item.h;
                        piece.top = baseline - 0.35f * shown - item.h * 0.5f;
                    } else {
                        const Inline& run = r.runs[item.run];
                        piece.text = run.text.substr(item.start, item.length);
                        piece.h = run.size;
                        piece.top = baseline - 0.85f * run.size;
                    }
                    cursor += item.w;
                    r.pieces.push_back(std::move(piece));
                }
                float width = std::max(0.0f, line.width - line.trailing);
                r.lineWidths.push_back(width);
                r.width = std::max(r.width, width);
                if (li == 0) {
                    r.firstLine = above + below;
                }
                y += above + below;
            }
            r.height = y;
        }

        void drawRich(Rich& r, float x, float y, float boxW, uint32_t fallback, Ctx& ctx) {
            uint32_t base = paint(r.foreground, fallback, true);
            for (Piece& piece : r.pieces) {
                float lineW = r.lineWidths[piece.line];
                float offset = r.align == 1 ? (boxW - lineW) * 0.5f : r.align == 2 ? boxW - lineW : 0.0f;
                float px = x + std::max(0.0f, offset) + piece.x;
                float py = y + piece.top;
                if (py > ctx.clipBottom || py + piece.h < ctx.clipTop) {
                    continue;
                }
                if (piece.element != nullptr) {
                    arrange(*piece.element, px, py, piece.w, piece.h);
                    draw(*piece.element, ctx);
                    continue;
                }
                const Inline& run = r.runs[piece.run];
                uint32_t color = paint(run.color, base, true);
                UiFont::draw(piece.text, px, py, color, run.family, run.size);
                if (run.underline) {
                    UiRender::hLine(px, px + piece.w, py + run.size + 1.0f, color);
                }
            }
        }

        void tooltip(const std::string& text, float x, float y, float w, float h) {
            float tipW = UiFont::width(text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL) + 22.0f;
            NewUi::tooltipText = text;
            NewUi::tooltipX = UiRender::clamp(x + (w - tipW) * 0.5f, 6.0f, MainWindow::width() - tipW - 6.0f);
            NewUi::tooltipY = y + h + 6.0f;
        }

        void fire(const Element& e, Ctx& ctx) {
            if (ctx.pending != nullptr) {
                ctx.pending->insert(ctx.pending->end(), e.actions.begin(), e.actions.end());
            }
        }

        float hover(const Element& e, bool hovered) {
            return UiState::motion(e.id + "#hover", hovered ? 1.0f : 0.0f, 18.0f);
        }

        int cornerMask(const Element& e) {
            return (e.corners[0] > 0.0f ? 1 : 0) | (e.corners[1] > 0.0f ? 2 : 0) | (e.corners[2] > 0.0f ? 8 : 0) |
                    (e.corners[3] > 0.0f ? 4 : 0);
        }

        float cornerRadius(const Element& e) {
            return std::max({e.corners[0], e.corners[1], e.corners[2], e.corners[3]});
        }

        uint32_t hintColor(int theme) {
            if (theme == 1) {
                return UiTheme::isLight() ? 0xFFB36B00u : 0xFFF0B04Cu;
            }
            if (theme == 2) {
                return UiTheme::danger();
            }
            return UiTheme::accent();
        }

        float measureBlocks(std::vector<Block>& blocks, float width);

        void measureCore(Element& e, float iw, float ih, float& cw, float& ch) {
            cw = 0.0f;
            ch = 0.0f;
            switch (e.kind) {
                case Kind::Stack:
                    for (auto& child : e.children) {
                        if (e.horizontal) {
                            measure(*child, INF, ih);
                            cw += child->desiredW;
                            ch = std::max(ch, child->desiredH);
                        } else {
                            measure(*child, iw, INF);
                            cw = std::max(cw, child->desiredW);
                            ch += child->desiredH;
                        }
                    }
                    break;
                case Kind::Wrap: {
                    float lineW = 0.0f;
                    float lineH = 0.0f;
                    for (auto& child : e.children) {
                        measure(*child, iw, ih);
                        if (lineW > 0.0f && lineW + child->desiredW > iw) {
                            cw = std::max(cw, lineW);
                            ch += lineH;
                            lineW = 0.0f;
                            lineH = 0.0f;
                        }
                        lineW += child->desiredW;
                        lineH = std::max(lineH, child->desiredH);
                    }
                    cw = std::max(cw, lineW);
                    ch += lineH;
                    break;
                }
                case Kind::Grid: {
                    std::vector<Def> cols = e.columns.empty() ? std::vector<Def>{Def{}} : e.columns;
                    std::vector<Def> rows = e.rows.empty() ? std::vector<Def>{Def{}} : e.rows;
                    size_t n = cols.size();
                    size_t m = rows.size();
                    std::vector<float> colW(n, 0.0f);
                    std::vector<float> rowH(m, 0.0f);
                    float starTotal = 0.0f;
                    for (size_t j = 0; j < n; j++) {
                        if (cols[j].type == 0) {
                            colW[j] = std::clamp(cols[j].value, cols[j].min, std::max(cols[j].min, cols[j].max));
                        } else if (cols[j].type == 2) {
                            starTotal += cols[j].value;
                        }
                    }
                    bool stars = std::isfinite(iw) && starTotal > 0.0f;
                    for (auto& child : e.children) {
                        size_t col = (size_t) std::clamp(child->column, 0, (int) n - 1);
                        if (child->columnSpan == 1 && (cols[col].type == 1 || (cols[col].type == 2 && !stars))) {
                            measure(*child, INF, INF);
                            colW[col] = std::max(colW[col], std::max(cols[col].min, child->desiredW));
                        }
                    }
                    if (stars) {
                        float fixed = 0.0f;
                        for (size_t j = 0; j < n; j++) {
                            if (cols[j].type != 2) {
                                fixed += colW[j];
                            }
                        }
                        float remaining = std::max(0.0f, iw - fixed);
                        std::vector<bool> settled(n, false);
                        for (size_t pass = 0; pass <= n; pass++) {
                            float weight = 0.0f;
                            for (size_t j = 0; j < n; j++) {
                                if (cols[j].type == 2 && !settled[j]) {
                                    weight += cols[j].value;
                                }
                            }
                            if (weight <= 0.0f) {
                                break;
                            }
                            bool changed = false;
                            for (size_t j = 0; j < n; j++) {
                                if (cols[j].type == 2 && !settled[j] &&
                                    remaining / weight * cols[j].value < cols[j].min) {
                                    colW[j] = cols[j].min;
                                    settled[j] = true;
                                    remaining = std::max(0.0f, remaining - cols[j].min);
                                    changed = true;
                                }
                            }
                            if (!changed) {
                                for (size_t j = 0; j < n; j++) {
                                    if (cols[j].type == 2 && !settled[j]) {
                                        colW[j] = std::min(remaining / weight * cols[j].value, cols[j].max);
                                    }
                                }
                                break;
                            }
                        }
                    }
                    for (size_t k = 0; k < m; k++) {
                        if (rows[k].type == 0) {
                            rowH[k] = rows[k].value;
                        }
                    }
                    for (auto& child : e.children) {
                        size_t col = (size_t) std::clamp(child->column, 0, (int) n - 1);
                        size_t row = (size_t) std::clamp(child->row, 0, (int) m - 1);
                        float cellW = 0.0f;
                        for (size_t j = col; j < std::min(n, col + (size_t) std::max(1, child->columnSpan)); j++) {
                            cellW += colW[j];
                        }
                        float cellH = rows[row].type == 0 ? rowH[row] : INF;
                        measure(*child, cellW, cellH);
                        if (rows[row].type != 0 && child->rowSpan <= 1) {
                            rowH[row] = std::max(rowH[row], child->desiredH);
                        }
                    }
                    e.colSizes = colW;
                    e.rowSizes = rowH;
                    for (float w : colW) {
                        cw += w;
                    }
                    for (float h : rowH) {
                        ch += h;
                    }
                    break;
                }
                case Kind::Border: {
                    float bw = e.borderThickness.h() + e.padding.h();
                    float bh = e.borderThickness.v() + e.padding.v();
                    for (auto& child : e.children) {
                        measure(*child, std::max(0.0f, iw - bw), std::max(0.0f, ih - bh));
                        cw = std::max(cw, child->desiredW);
                        ch = std::max(ch, child->desiredH);
                    }
                    cw += bw;
                    ch += bh;
                    break;
                }
                case Kind::Card: {
                    float content = 0.0f;
                    for (auto& child : e.children) {
                        measure(*child, iw, INF);
                        cw = std::max(cw, child->desiredW);
                        content = std::max(content, child->desiredH);
                    }
                    float header = !e.title.empty() || e.canSwap ? CARD_HEADER : 0.0f;
                    e.contentH = std::max(content, header);
                    ch = header + (e.contentH - header) * e.open;
                    break;
                }
                case Kind::Text:
                    layoutRich(e.rich, iw - e.padding.h());
                    cw = e.rich.width + e.padding.h();
                    ch = e.rich.height + e.padding.v();
                    break;
                case Kind::Flow:
                    ch = measureBlocks(e.blocks, std::isfinite(iw) ? iw : 600.0f);
                    cw = std::isfinite(iw) ? iw : 600.0f;
                    break;
                case Kind::Image: {
                    const WebImages::Image& image = WebImages::get(e.source, e.fallback);
                    float nw = (float) image.width;
                    float nh = (float) image.height;
                    if (image.texture == 0 || nw <= 0.0f || nh <= 0.0f) {
                        if (e.stretch == UNIFORM_FILL && std::isfinite(iw) && std::isfinite(ih)) {
                            cw = iw;
                            ch = ih;
                        } else {
                            cw = std::isnan(e.width) ? (std::isnan(e.height) ? 0.0f : e.height) : e.width;
                            ch = std::isnan(e.height) ? (std::isnan(e.width) ? 0.0f : e.width) : e.height;
                        }
                        break;
                    }
                    switch (e.stretch) {
                        case NONE:
                            cw = nw;
                            ch = nh;
                            break;
                        case FILL:
                            cw = std::isfinite(iw) ? iw : nw;
                            ch = std::isfinite(ih) ? ih : nh;
                            break;
                        case UNIFORM_FILL:
                            if (std::isfinite(iw) && std::isfinite(ih)) {
                                cw = iw;
                                ch = ih;
                            } else if (std::isfinite(iw)) {
                                cw = iw;
                                ch = nh * iw / nw;
                            } else if (std::isfinite(ih)) {
                                cw = nw * ih / nh;
                                ch = ih;
                            } else {
                                cw = nw;
                                ch = nh;
                            }
                            break;
                        default: {
                            float s = std::min(iw / nw, ih / nh);
                            if (!std::isfinite(s)) {
                                s = 1.0f;
                            }
                            cw = nw * s;
                            ch = nh * s;
                            break;
                        }
                    }
                    break;
                }
                case Kind::Path: {
                    float bx = 0.0f;
                    float by = 0.0f;
                    float bw = 0.0f;
                    float bh = 0.0f;
                    if (SvgPath::bounds(e.data, bx, by, bw, bh)) {
                        cw = std::min(bw, std::isfinite(iw) ? iw : bw);
                        ch = std::min(bh, std::isfinite(ih) ? ih : bh);
                    }
                    break;
                }
                case Kind::Line:
                    if (e.stretch == FILL) {
                        cw = std::isfinite(iw) ? iw : std::fabs(e.x2 - e.x1);
                        ch = std::max(e.strokeThickness, std::fabs(e.y2 - e.y1));
                    } else {
                        cw = std::fabs(e.x2 - e.x1) + e.strokeThickness;
                        ch = std::fabs(e.y2 - e.y1) + e.strokeThickness;
                    }
                    break;
                case Kind::Shape: break;
                case Kind::Hint:
                    layoutRich(e.rich, iw - 28.0f);
                    cw = std::isfinite(iw) ? iw : e.rich.width + 28.0f;
                    ch = e.rich.height + 18.0f;
                    break;
                case Kind::Button: {
                    float textW = UiFont::width(e.text, UiFont::MEDIUM, e.fontSize);
                    cw = textW + (e.paddingSet ? e.padding.h() : 26.0f);
                    ch = 32.0f;
                    break;
                }
                case Kind::TextButton:
                    cw = UiFont::width(e.text, UiFont::MEDIUM, e.fontSize) + 4.0f;
                    ch = lineHeightOf(e.fontSize, 0.0f);
                    break;
                case Kind::IconTextButton: {
                    float icon = 13.0f;
                    cw = 8.0f + icon +
                            (e.text.empty() ? 0.0f : 6.0f + UiFont::width(e.text, UiFont::MEDIUM, e.fontSize)) + 10.0f;
                    ch = 28.0f;
                    break;
                }
                case Kind::IconButton:
                    cw = 24.0f;
                    ch = 24.0f;
                    break;
                case Kind::ListItem: {
                    float textW = UiFont::width(e.title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    if (!e.info.empty()) {
                        textW += 10.0f + UiFont::width(e.info, UiFont::MEDIUM, UiWidgets::TEXT_SMALL);
                    }
                    cw = std::isfinite(iw) ? iw : (e.source.empty() ? 12.0f : 46.0f) + textW + 12.0f;
                    ch = 40.0f;
                    break;
                }
                case Kind::Loading:
                    cw = std::isfinite(iw) ? iw : 120.0f;
                    ch = e.text.empty() ? 40.0f : 64.0f;
                    break;
                case Kind::Code:
                    if (e.block) {
                        float inner = std::isfinite(iw) ? std::max(40.0f, iw - 32.0f) : 600.0f;
                        if (e.codeWidth != inner) {
                            e.codeWidth = inner;
                            e.codeLines.clear();
                            size_t start = 0;
                            while (start <= e.text.size()) {
                                size_t end = e.text.find('\n', start);
                                std::string line = e.text.substr(
                                        start, end == std::string::npos ? std::string::npos : end - start);
                                std::vector<std::string> wrapped = UiFont::wrap(line, UiFont::CODE, e.fontSize, inner);
                                if (wrapped.empty()) {
                                    wrapped.push_back(std::string());
                                }
                                e.codeLines.insert(e.codeLines.end(), wrapped.begin(), wrapped.end());
                                if (end == std::string::npos) {
                                    break;
                                }
                                start = end + 1;
                            }
                        }
                        cw = std::isfinite(iw) ? iw : inner + 32.0f;
                        ch = e.codeLines.size() * lineHeightOf(e.fontSize, 0.0f) + 20.0f;
                    } else {
                        cw = UiFont::width(e.text, UiFont::CODE, e.fontSize) + 8.0f;
                        ch = lineHeightOf(e.fontSize, 0.0f);
                    }
                    break;
                case Kind::Rule:
                    cw = std::isfinite(iw) ? iw : 0.0f;
                    ch = 1.0f;
                    break;
            }
        }

        void measure(Element& e, float aw, float ah) {
            if (e.collapsed) {
                e.desiredW = 0.0f;
                e.desiredH = 0.0f;
                return;
            }
            float maxW = std::max(e.minWidth, e.maxWidth);
            float maxH = std::max(e.minHeight, e.maxHeight);
            float iw = std::isnan(e.width) ? aw - e.margin.h() : e.width;
            float ih = std::isnan(e.height) ? ah - e.margin.v() : e.height;
            iw = std::max(0.0f, std::clamp(iw, e.minWidth, maxW));
            ih = std::max(0.0f, std::clamp(ih, e.minHeight, maxH));
            float cw = 0.0f;
            float ch = 0.0f;
            measureCore(e, iw, ih, cw, ch);
            float w = std::isnan(e.width) ? std::clamp(cw, e.minWidth, maxW) : e.width;
            float h = std::isnan(e.height) ? std::clamp(ch, e.minHeight, maxH) : e.height;
            e.desiredW = w + e.margin.h();
            e.desiredH = h + e.margin.v();
        }

        void arrangeCore(Element& e) {
            switch (e.kind) {
                case Kind::Stack: {
                    float cursor = e.horizontal ? e.x : e.y;
                    for (auto& child : e.children) {
                        if (e.horizontal) {
                            arrange(*child, cursor, e.y, child->desiredW, e.h);
                            cursor += child->desiredW;
                        } else {
                            arrange(*child, e.x, cursor, e.w, child->desiredH);
                            cursor += child->desiredH;
                        }
                    }
                    break;
                }
                case Kind::Wrap: {
                    float cx = e.x;
                    float cy = e.y;
                    float lineH = 0.0f;
                    size_t lineStart = 0;
                    std::vector<std::pair<size_t, float>> rows;
                    for (size_t i = 0; i < e.children.size(); i++) {
                        Element& child = *e.children[i];
                        if (cx > e.x && cx + child.desiredW > e.x + e.w + 0.5f) {
                            rows.push_back({lineStart, lineH});
                            lineStart = i;
                            cx = e.x;
                            lineH = 0.0f;
                        }
                        cx += child.desiredW;
                        lineH = std::max(lineH, child.desiredH);
                    }
                    rows.push_back({lineStart, lineH});
                    size_t row = 0;
                    cx = e.x;
                    for (size_t i = 0; i < e.children.size(); i++) {
                        if (row + 1 < rows.size() && i == rows[row + 1].first) {
                            cy += rows[row].second;
                            row++;
                            cx = e.x;
                        }
                        Element& child = *e.children[i];
                        arrange(child, cx, cy, child.desiredW, rows[row].second);
                        cx += child.desiredW;
                    }
                    break;
                }
                case Kind::Grid: {
                    std::vector<float> colW = e.colSizes;
                    std::vector<float> rowH = e.rowSizes;
                    if (colW.empty()) {
                        colW.push_back(e.w);
                    }
                    if (rowH.empty()) {
                        rowH.push_back(e.h);
                    }
                    float sum = 0.0f;
                    float starTotal = 0.0f;
                    for (size_t j = 0; j < colW.size(); j++) {
                        sum += colW[j];
                        if (j < e.columns.size() ? e.columns[j].type == 2 : true) {
                            starTotal += j < e.columns.size() ? e.columns[j].value : 1.0f;
                        }
                    }
                    if (e.w - sum > 0.5f && starTotal > 0.0f) {
                        float extra = e.w - sum;
                        for (size_t j = 0; j < colW.size(); j++) {
                            if (j < e.columns.size() ? e.columns[j].type == 2 : true) {
                                colW[j] += extra * (j < e.columns.size() ? e.columns[j].value : 1.0f) / starTotal;
                            }
                        }
                    }
                    float rowSum = 0.0f;
                    for (float h : rowH) {
                        rowSum += h;
                    }
                    if (rowH.size() == 1 && e.h > rowSum) {
                        rowH[0] = e.h;
                    }
                    std::vector<float> colX(colW.size() + 1, e.x);
                    for (size_t j = 0; j < colW.size(); j++) {
                        colX[j + 1] = colX[j] + colW[j];
                    }
                    std::vector<float> rowY(rowH.size() + 1, e.y);
                    for (size_t k = 0; k < rowH.size(); k++) {
                        rowY[k + 1] = rowY[k] + rowH[k];
                    }
                    for (auto& child : e.children) {
                        size_t col = (size_t) std::clamp(child->column, 0, (int) colW.size() - 1);
                        size_t row = (size_t) std::clamp(child->row, 0, (int) rowH.size() - 1);
                        size_t colEnd = std::min(colW.size(), col + (size_t) std::max(1, child->columnSpan));
                        size_t rowEnd = std::min(rowH.size(), row + (size_t) std::max(1, child->rowSpan));
                        arrange(*child, colX[col], rowY[row], colX[colEnd] - colX[col], rowY[rowEnd] - rowY[row]);
                    }
                    break;
                }
                case Kind::Border: {
                    float x = e.x + e.borderThickness.l + e.padding.l;
                    float y = e.y + e.borderThickness.t + e.padding.t;
                    float w = std::max(0.0f, e.w - e.borderThickness.h() - e.padding.h());
                    float h = std::max(0.0f, e.h - e.borderThickness.v() - e.padding.v());
                    float inner = std::max(0.0f, cornerRadius(e) - std::max(e.borderThickness.l, e.borderThickness.t));
                    for (auto& child : e.children) {
                        if (child->kind == Kind::Image) {
                            child->clipRadius = inner;
                        }
                        arrange(*child, x, y, w, h);
                    }
                    break;
                }
                case Kind::Card:
                    for (auto& child : e.children) {
                        if (child->kind == Kind::Border && std::fabs(child->margin.l) < 2.0f &&
                            std::fabs(child->margin.t) < 2.0f && std::fabs(child->margin.r) < 2.0f &&
                            std::fabs(child->margin.b) < 2.0f) {
                            for (float& corner : child->corners) {
                                corner = std::max(corner, CARD_RADIUS);
                            }
                        }
                        arrange(*child, e.x, e.y, e.w, std::max(child->desiredH, e.contentH));
                    }
                    break;
                default: break;
            }
        }

        void arrange(Element& e, float sx, float sy, float sw, float sh) {
            if (e.collapsed) {
                e.x = sx;
                e.y = sy;
                e.w = 0.0f;
                e.h = 0.0f;
                return;
            }
            float aw = std::max(0.0f, sw - e.margin.h());
            float ah = std::max(0.0f, sh - e.margin.v());
            float dw = e.desiredW - e.margin.h();
            float dh = e.desiredH - e.margin.v();
            float maxW = std::max(e.minWidth, e.maxWidth);
            float maxH = std::max(e.minHeight, e.maxHeight);
            float w = !std::isnan(e.width) ? e.width : (e.hAlign == STRETCH ? aw : std::min(dw, aw));
            float h = !std::isnan(e.height) ? e.height : (e.vAlign == STRETCH ? ah : std::min(dh, ah));
            if (std::isnan(e.width) && e.hAlign != STRETCH && dw > aw) {
                w = dw;
            }
            if (std::isnan(e.height) && e.vAlign != STRETCH && dh > ah) {
                h = dh;
            }
            w = std::clamp(w, e.minWidth, maxW);
            h = std::clamp(h, e.minHeight, maxH);
            float x = sx + e.margin.l;
            float y = sy + e.margin.t;
            int ha = e.hAlign == STRETCH && w < aw ? CENTER : e.hAlign;
            int va = e.vAlign == STRETCH && h < ah ? CENTER : e.vAlign;
            if (ha == CENTER) {
                x += (aw - w) * 0.5f;
            } else if (ha == END) {
                x += aw - w;
            }
            if (va == CENTER) {
                y += (ah - h) * 0.5f;
            } else if (va == END) {
                y += ah - h;
            }
            e.x = x;
            e.y = y;
            e.w = w;
            e.h = h;
            arrangeCore(e);
        }

        float measureBlocks(std::vector<Block>& blocks, float width) {
            float y = 0.0f;
            float previous = 0.0f;
            bool first = true;
            for (Block& block : blocks) {
                y += first ? block.margin.t : std::max(previous, block.margin.t);
                block.y = y;
                block.x = block.margin.l;
                block.w = std::max(0.0f, width - block.margin.h());
                switch (block.type) {
                    case 0:
                        layoutRich(block.rich, block.w);
                        block.h = block.rich.height;
                        break;
                    case 1: {
                        float iy = 0.0f;
                        float last = 0.0f;
                        block.itemTops.assign(block.items.size(), 0.0f);
                        block.markerYs.assign(block.items.size(), 0.0f);
                        for (size_t i = 0; i < block.items.size(); i++) {
                            iy += i == 0 ? block.itemMargin.t : std::max(last, block.itemMargin.t);
                            block.itemTops[i] = iy;
                            float itemH = measureBlocks(block.items[i], block.w);
                            float marker = lineHeightOf(block.markerSize, 0.0f) * 0.5f;
                            if (!block.items[i].empty() && block.items[i].front().type == 0) {
                                marker = block.items[i].front().y + block.items[i].front().rich.firstLine * 0.5f;
                            }
                            block.markerYs[i] = iy + marker;
                            iy += itemH;
                            last = block.itemMargin.b;
                        }
                        block.h = iy + last;
                        break;
                    }
                    case 2:
                        if (block.ui) {
                            measure(*block.ui, block.w, INF);
                            block.h = block.ui->desiredH;
                        } else {
                            block.h = 0.0f;
                        }
                        break;
                    default: block.h = measureBlocks(block.blocks, block.w); break;
                }
                y += block.h;
                previous = block.margin.b;
                first = false;
            }
            return y + previous;
        }

        void drawBlocks(std::vector<Block>& blocks, float ox, float oy, uint32_t fallback, Ctx& ctx) {
            for (Block& block : blocks) {
                float bx = ox + block.x;
                float by = oy + block.y;
                if (by > ctx.clipBottom) {
                    break;
                }
                if (by + block.h < ctx.clipTop) {
                    continue;
                }
                switch (block.type) {
                    case 0: drawRich(block.rich, bx, by, block.w, fallback, ctx); break;
                    case 1:
                        for (size_t i = 0; i < block.items.size(); i++) {
                            float iy = by + block.itemTops[i];
                            if (iy > ctx.clipBottom) {
                                break;
                            }
                            float my = by + block.markerYs[i];
                            uint32_t color = paint(block.marker, UiTheme::textMuted(), true);
                            if (block.markerStyle == 2) {
                                std::string number = std::to_string(i + 1) + ".";
                                float nw = UiFont::width(number, UiFont::MEDIUM, block.markerSize);
                                UiFont::draw(number, bx - nw - 6.0f, my - block.markerSize * 0.5f, color,
                                             UiFont::MEDIUM, block.markerSize);
                            } else if (block.markerStyle != 0) {
                                UiRender::roundRect(bx - 11.0f, my - 2.5f, 5.0f, 5.0f, 2.5f, color);
                            }
                            drawBlocks(block.items[i], bx, iy, fallback, ctx);
                        }
                        break;
                    case 2:
                        if (block.ui) {
                            arrange(*block.ui, bx, by, block.w, block.h);
                            draw(*block.ui, ctx);
                        }
                        break;
                    default: drawBlocks(block.blocks, bx, by, fallback, ctx); break;
                }
            }
        }

        void imageQuad(const WebImages::Image& image, int stretch, float x, float y, float w, float h, float radius) {
            if (image.texture == 0 || image.width <= 0 || image.height <= 0 || w <= 0.0f || h <= 0.0f) {
                return;
            }
            float nw = (float) image.width;
            float nh = (float) image.height;
            float dx = x;
            float dy = y;
            float dw = w;
            float dh = h;
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 1.0f;
            float v1 = 1.0f;
            if (stretch == UNIFORM || stretch == NONE) {
                float s = stretch == NONE ? 1.0f : std::min(w / nw, h / nh);
                dw = nw * s;
                dh = nh * s;
                dx = x + (w - dw) * 0.5f;
                dy = y + (h - dh) * 0.5f;
            } else if (stretch == UNIFORM_FILL) {
                float s = std::max(w / nw, h / nh);
                float uSpan = w / (nw * s);
                float vSpan = h / (nh * s);
                u0 = (1.0f - uSpan) * 0.5f;
                u1 = u0 + uSpan;
                v0 = (1.0f - vSpan) * 0.5f;
                v1 = v0 + vSpan;
            }
            if (dw <= 0.0f || dh <= 0.0f) {
                return;
            }
            if (radius > 0.0f) {
                ShaderUtil::roundTex(UiRender::transformX(dx), UiRender::transformY(dy), UiRender::transformW(dw),
                                     UiRender::transformW(dh), UiRender::transformW(radius), image.texture, u0, v0, u1,
                                     v1, UiRender::applyAlpha(0xFFFFFFFFu));
            } else {
                UiRender::texture(image.texture, dx, dy, dw, dh, u0, v0, u1, v1);
            }
        }

        void drawImage(Element& e) {
            const WebImages::Image& image = WebImages::get(e.source, e.fallback);
            if (image.texture == 0 || image.width <= 0 || image.height <= 0) {
                if (!image.done && e.w > 0.0f && e.h > 0.0f) {
                    UiRender::roundRect(e.x, e.y, e.w, e.h, std::max(4.0f, e.clipRadius), UiTheme::surfaceRaised());
                }
                return;
            }
            imageQuad(image, e.stretch, e.x, e.y, e.w, e.h, e.clipRadius);
        }

        void fillArea(float x, float y, float w, float h, float radius, int mask, const Brush& brush) {
            if (!brush.set() || w <= 0.0f || h <= 0.0f) {
                return;
            }
            if (brush.kind == 3) {
                if (brush.vertical) {
                    UiRender::roundRectGradientV(x, y, w, h, radius, brush.argb, brush.argb2);
                } else {
                    UiRender::roundRectGradient(x, y, w, h, radius, brush.argb, brush.argb2);
                }
                return;
            }
            if (brush.kind == 4) {
                imageQuad(WebImages::get(brush.image), brush.stretch, x, y, w, h, radius);
                return;
            }
            uint32_t color = paint(brush, 0, false);
            if (radius > 0.0f) {
                UiRender::roundRect(x, y, w, h, radius, color, mask != 0 ? mask : 0xF);
            } else {
                UiRender::rect(x, y, w, h, color);
            }
        }

        void drawBorder(Element& e) {
            float r = cornerRadius(e);
            int mask = cornerMask(e);
            fillArea(e.x, e.y, e.w, e.h, r, mask, e.background);
            if (e.groupBox) {
                UiRender::outline(e.x, e.y, e.w, e.h, r, UiTheme::outlineSoft());
            }
            if (!e.borderBrush.set()) {
                return;
            }
            const Thickness& t = e.borderThickness;
            if (t.l <= 0.0f && t.t <= 0.0f && t.r <= 0.0f && t.b <= 0.0f) {
                return;
            }
            uint32_t color = paint(e.borderBrush, UiTheme::outline(), false);
            if (t.l == t.t && t.t == t.r && t.r == t.b) {
                float pixels = t.l * UiRender::uiScale * UiRender::pixelScale();
                UiRender::outline(e.x, e.y, e.w, e.h, r, color, mask != 0 ? mask : 0xF, pixels);
                return;
            }
            auto band = [&](float x, float y, float w, float h) {
                if (w <= 0.0f || h <= 0.0f) {
                    return;
                }
                UiRender::scissor(x, y, w, h);
                UiRender::roundRect(e.x, e.y, e.w, e.h, r, color, mask);
                UiRender::unscissor();
            };
            band(e.x, e.y, e.w, t.t);
            band(e.x, e.y + e.h - t.b, e.w, t.b);
            band(e.x, e.y, t.l, e.h);
            band(e.x + e.w - t.r, e.y, t.r, e.h);
        }

        void drawCard(Element& e, Ctx& ctx) {
            bool header = !e.title.empty() || e.canSwap;
            if (e.canSwap) {
                e.open = UiState::motion(e.id + "#open", e.swapped ? 0.0f : 1.0f, 14.0f);
            }
            if (ctx.glass) {
                UiRender::blur(e.x, e.y, e.w, e.h, CARD_RADIUS, 8, !ctx.captured);
                ctx.captured = true;
                uint32_t veil = UiTheme::isLight() ? 0xA6000000u : 0x8C000000u;
                UiRender::roundRect(e.x, e.y, e.w, e.h, CARD_RADIUS, (UiTheme::surface() & 0x00FFFFFFu) | veil);
            } else {
                UiRender::roundRect(e.x, e.y, e.w, e.h, CARD_RADIUS, UiTheme::surface());
            }
            UiRender::outline(e.x, e.y, e.w, e.h, CARD_RADIUS, UiTheme::outlineSoft());
            if (header) {
                bool hovered = e.canSwap && !ctx.blocked && NewUi::hit(e.x, e.y, e.w, CARD_HEADER);
                float lit = hover(e, hovered);
                float right = e.canSwap ? 40.0f : 16.0f;
                std::string title = UiFont::fitEnd(e.title, UiFont::MEDIUM, UiWidgets::TEXT_TITLE,
                                                   std::max(0.0f, e.w - 16.0f - right));
                uint32_t titleInk = e.canSwap ? UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), lit)
                                              : UiTheme::textPrimary();
                UiRender::textY(title, e.x + 16.0f, e.y, CARD_HEADER, titleInk, UiFont::MEDIUM, UiWidgets::TEXT_TITLE);
                if (e.canSwap) {
                    uint32_t tint = UiRender::mix(UiTheme::textMuted(), UiTheme::textPrimary(), lit);
                    float saved = UiRender::alphaMul;
                    UiRender::alphaMul = saved * e.open;
                    UiWidgets::icon(Ui::glyph(0xf077), e.x + e.w - 22.0f, e.y + CARD_HEADER * 0.5f, tint, 9.0f);
                    UiRender::alphaMul = saved * (1.0f - e.open);
                    UiWidgets::icon(Ui::glyph(0xf078), e.x + e.w - 22.0f, e.y + CARD_HEADER * 0.5f, tint, 9.0f);
                    UiRender::alphaMul = saved;
                    if (!ctx.blocked && NewUi::click(e.x, e.y, e.w, CARD_HEADER)) {
                        e.swapped = !e.swapped;
                        UiSound::request(UiSound::TICK);
                    }
                }
            }
            if (header && e.open <= 0.001f) {
                return;
            }
            bool clip = header && e.open < 0.999f;
            UiRender::scissor(e.x, e.y, e.w, e.h);
            float savedTop = ctx.clipTop;
            float savedBottom = ctx.clipBottom;
            ctx.clipTop = std::max(ctx.clipTop, e.y);
            ctx.clipBottom = std::min(ctx.clipBottom, e.y + e.h);
            float saved = UiRender::alphaMul;
            if (clip) {
                UiRender::alphaMul = saved * std::clamp(e.open * 1.4f - 0.2f, 0.0f, 1.0f);
            }
            for (auto& child : e.children) {
                draw(*child, ctx);
            }
            UiRender::alphaMul = saved;
            ctx.clipTop = savedTop;
            ctx.clipBottom = savedBottom;
            UiRender::unscissor();
        }

        void drawIcon(const std::string& data, float cx, float cy, float size, uint32_t color) {
            if (data.empty()) {
                return;
            }
            SvgPath::draw(data, cx - size * 0.5f, cy - size * 0.5f, size, size, color, true);
        }

        void drawContent(Element& e, Ctx& ctx) {
            bool blocked = ctx.blocked;
            switch (e.kind) {
                case Kind::Stack:
                case Kind::Wrap:
                case Kind::Grid:
                    for (auto& child : e.children) {
                        if (child->y > ctx.clipBottom || child->y + child->h < ctx.clipTop) {
                            if (e.kind == Kind::Stack && !e.horizontal && child->y > ctx.clipBottom) {
                                break;
                            }
                            continue;
                        }
                        draw(*child, ctx);
                    }
                    break;
                case Kind::Border:
                    drawBorder(e);
                    for (auto& child : e.children) {
                        draw(*child, ctx);
                    }
                    break;
                case Kind::Card: drawCard(e, ctx); break;
                case Kind::Text:
                    fillArea(e.x, e.y, e.w, e.h, 0.0f, 0, e.background);
                    drawRich(e.rich, e.x + e.padding.l, e.y + e.padding.t, e.w - e.padding.h(), UiTheme::textPrimary(),
                             ctx);
                    break;
                case Kind::Flow: drawBlocks(e.blocks, e.x, e.y, UiTheme::textPrimary(), ctx); break;
                case Kind::Image: drawImage(e); break;
                case Kind::Path:
                    SvgPath::draw(e.data, e.x, e.y, e.w, e.h, paint(e.fill, UiTheme::textPrimary(), false),
                                  e.stretch != FILL);
                    break;
                case Kind::Line: {
                    uint32_t color = paint(e.stroke, UiTheme::outline(), false);
                    if (e.stretch == FILL) {
                        float cy = e.y + e.h * 0.5f;
                        UiRender::line(e.x, cy, e.x + e.w, cy, color, e.strokeThickness);
                    } else {
                        UiRender::line(e.x + e.x1, e.y + e.y1, e.x + e.x2, e.y + e.y2, color, e.strokeThickness);
                    }
                    break;
                }
                case Kind::Shape: {
                    float r = e.ellipse ? std::min(e.w, e.h) * 0.5f : e.radius;
                    fillArea(e.x, e.y, e.w, e.h, r, 0xF, e.fill);
                    if (e.stroke.set()) {
                        UiRender::outline(e.x, e.y, e.w, e.h, r, paint(e.stroke, 0, false), 0xF,
                                          e.strokeThickness * UiRender::uiScale * UiRender::pixelScale());
                    }
                    break;
                }
                case Kind::Hint: {
                    uint32_t color = hintColor(e.theme);
                    UiRender::roundRect(e.x, e.y, e.w, e.h, 8.0f, (color & 0x00FFFFFFu) | 0x24000000u);
                    drawRich(e.rich, e.x + 14.0f, e.y + 9.0f, e.w - 28.0f,
                             UiRender::mix(color, UiTheme::textPrimary(), 0.25f), ctx);
                    break;
                }
                case Kind::Button: {
                    bool hovered = !blocked && NewUi::hit(e.x, e.y, e.w, e.h);
                    float lit = hover(e, hovered);
                    float radius = std::min(8.0f, e.h * 0.5f);
                    uint32_t ink = UiTheme::textPrimary();
                    if (e.theme == 1) {
                        UiRender::roundRect(e.x, e.y, e.w, e.h, radius, UiTheme::accentSoft(30 + Ui::round(24 * lit)));
                        UiRender::outline(e.x, e.y, e.w, e.h, radius, (UiTheme::accent() & 0x00FFFFFFu) | 0xA0000000u);
                        ink = UiTheme::accentText();
                    } else if (e.theme == 2) {
                        UiRender::roundRect(e.x, e.y, e.w, e.h, radius, UiTheme::surfaceRaised());
                        if (lit > 0.001f) {
                            UiRender::roundRect(e.x, e.y, e.w, e.h, radius, UiTheme::overlay(Ui::round(14 * lit)));
                        }
                        UiRender::outline(e.x, e.y, e.w, e.h, radius, (UiTheme::danger() & 0x00FFFFFFu) | 0xA0000000u);
                        ink = UiTheme::danger();
                    } else {
                        UiRender::roundRect(e.x, e.y, e.w, e.h, radius, UiTheme::surfaceRaised());
                        if (lit > 0.001f) {
                            UiRender::roundRect(e.x, e.y, e.w, e.h, radius, UiTheme::overlay(Ui::round(14 * lit)));
                        }
                        UiRender::outline(e.x, e.y, e.w, e.h, radius, UiTheme::outlineSoft());
                    }
                    std::string text = UiFont::fitEnd(e.text, UiFont::MEDIUM, e.fontSize, std::max(0.0f, e.w - 12.0f));
                    float textW = UiFont::width(text, UiFont::MEDIUM, e.fontSize);
                    UiRender::textY(text, e.x + (e.w - textW) * 0.5f, e.y, e.h, ink, UiFont::MEDIUM, e.fontSize);
                    if (!blocked && NewUi::click(e.x, e.y, e.w, e.h)) {
                        fire(e, ctx);
                    }
                    break;
                }
                case Kind::TextButton: {
                    bool hovered = !blocked && NewUi::hit(e.x, e.y, e.w, e.h);
                    float lit = hover(e, hovered);
                    uint32_t ink = UiRender::mix(UiTheme::accentText(), UiTheme::textPrimary(), lit * 0.35f);
                    UiRender::textY(e.text, e.x + 2.0f, e.y, e.h, ink, UiFont::MEDIUM, e.fontSize);
                    if (lit > 0.001f) {
                        float textW = UiFont::width(e.text, UiFont::MEDIUM, e.fontSize);
                        float saved = UiRender::alphaMul;
                        UiRender::alphaMul = saved * lit;
                        UiRender::hLine(e.x + 2.0f, e.x + 2.0f + textW, e.y + (e.h + e.fontSize) * 0.5f + 1.0f, ink);
                        UiRender::alphaMul = saved;
                    }
                    if (!blocked && NewUi::click(e.x, e.y, e.w, e.h)) {
                        fire(e, ctx);
                    }
                    break;
                }
                case Kind::IconTextButton: {
                    bool hovered = !blocked && NewUi::hit(e.x, e.y, e.w, e.h);
                    float lit = hover(e, hovered);
                    if (lit > 0.001f) {
                        UiRender::roundRect(e.x, e.y, e.w, e.h, 7.0f, UiTheme::overlay(Ui::round(12 * lit)));
                    }
                    uint32_t ink = e.theme == 1 ? UiTheme::accentText()
                                                : UiRender::mix(UiTheme::textSecondary(), UiTheme::textPrimary(), lit);
                    float icon = 13.0f;
                    drawIcon(e.data, e.x + 8.0f + icon * 0.5f, e.y + e.h * 0.5f, icon * e.logoScale, ink);
                    if (!e.text.empty()) {
                        UiRender::textY(e.text, e.x + 8.0f + icon + 6.0f, e.y, e.h, ink, UiFont::MEDIUM, e.fontSize);
                    }
                    if (!blocked && NewUi::click(e.x, e.y, e.w, e.h)) {
                        fire(e, ctx);
                    }
                    break;
                }
                case Kind::IconButton: {
                    bool hovered = !blocked && NewUi::hit(e.x, e.y, e.w, e.h);
                    float lit = hover(e, hovered);
                    float size = std::min(e.w, e.h);
                    if (lit > 0.001f) {
                        UiRender::roundRect(e.x + (e.w - size) * 0.5f, e.y + (e.h - size) * 0.5f, size, size,
                                            size * 0.5f, UiTheme::overlay(Ui::round(14 * lit)));
                    }
                    uint32_t ink = e.theme == 1 ? 0xFFFFFFFFu
                            : e.theme == 2      ? UiTheme::textPrimary()
                            : e.theme == 3      ? UiTheme::danger()
                                                : UiTheme::accent();
                    drawIcon(e.data, e.x + e.w * 0.5f, e.y + e.h * 0.5f, size * 0.62f * e.logoScale, ink);
                    if (!blocked && NewUi::click(e.x, e.y, e.w, e.h)) {
                        fire(e, ctx);
                    }
                    break;
                }
                case Kind::ListItem: {
                    bool hovered = e.clickable && !blocked && NewUi::hit(e.x, e.y, e.w, e.h);
                    float lit = hover(e, hovered);
                    if (lit > 0.001f) {
                        UiRender::roundRect(e.x, e.y, e.w, e.h, 8.0f, UiTheme::overlay(Ui::round(12 * lit)));
                    }
                    float textX = e.x + 12.0f;
                    if (!e.source.empty()) {
                        float logo = 26.0f * std::clamp(e.logoScale, 0.4f, 1.4f);
                        const WebImages::Image& image = WebImages::get(e.source);
                        if (image.texture != 0 && image.width > 0 && image.height > 0) {
                            float s = std::min(logo / image.width, logo / image.height);
                            float lw = image.width * s;
                            float lh = image.height * s;
                            UiRender::texture(image.texture, e.x + 12.0f + (26.0f - lw) * 0.5f, e.y + (e.h - lh) * 0.5f,
                                              lw, lh);
                        } else if (image.failed) {
                            UiWidgets::icon(Ui::glyph(0xf1b2), e.x + 25.0f, e.y + e.h * 0.5f, UiTheme::textMuted(),
                                            12.0f);
                        }
                        textX = e.x + 48.0f;
                    }
                    float room = std::max(0.0f, e.x + e.w - 12.0f - textX);
                    std::string title = UiFont::fitEnd(e.title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL, room);
                    float titleW = UiFont::width(title, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    UiRender::textY(title, textX, e.y, e.h, UiTheme::textPrimary(), UiFont::MEDIUM,
                                    UiWidgets::TEXT_CONTROL);
                    if (!e.info.empty() && room - titleW > 40.0f) {
                        std::string info =
                                UiFont::fitEnd(e.info, UiFont::MEDIUM, UiWidgets::TEXT_SMALL, room - titleW - 10.0f);
                        UiRender::textY(info, textX + titleW + 10.0f, e.y, e.h, UiTheme::textMuted(), UiFont::MEDIUM,
                                        UiWidgets::TEXT_SMALL);
                    }
                    if (e.clickable && !blocked && NewUi::click(e.x, e.y, e.w, e.h)) {
                        fire(e, ctx);
                    }
                    break;
                }
                case Kind::Loading: {
                    float cy = e.y + (e.text.empty() ? e.h * 0.5f : 18.0f);
                    Page::spinner(e.x + e.w * 0.5f, cy, 9.0f, UiTheme::accent());
                    if (!e.text.empty()) {
                        float textW = UiFont::width(e.text, UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                        UiRender::textY(e.text, e.x + (e.w - textW) * 0.5f, e.y + 36.0f, 20.0f, UiTheme::accentText(),
                                        UiFont::MEDIUM, UiWidgets::TEXT_CONTROL);
                    }
                    break;
                }
                case Kind::Code: {
                    float lh = lineHeightOf(e.fontSize, 0.0f);
                    if (e.block) {
                        UiRender::roundRect(e.x, e.y, e.w, e.h, 6.0f, UiTheme::surfaceSunken());
                        float y = e.y + 10.0f;
                        for (const std::string& line : e.codeLines) {
                            if (y > ctx.clipBottom) {
                                break;
                            }
                            if (y + lh >= ctx.clipTop) {
                                UiRender::textY(line, e.x + 16.0f, y, lh, UiTheme::textPrimary(), UiFont::CODE,
                                                e.fontSize);
                            }
                            y += lh;
                        }
                    } else {
                        UiRender::roundRect(e.x, e.y + 1.0f, e.w, std::max(0.0f, e.h - 2.0f), 5.0f,
                                            UiTheme::accentSoft(36));
                        UiRender::textY(e.text, e.x + 4.0f, e.y, e.h, UiTheme::accentText(), UiFont::CODE, e.fontSize);
                    }
                    break;
                }
                case Kind::Rule: UiRender::hLine(e.x, e.x + e.w, e.y, UiTheme::divider()); break;
            }
        }

        void draw(Element& e, Ctx& ctx) {
            if (e.collapsed || e.hidden) {
                return;
            }
            float saved = UiRender::alphaMul;
            if (e.opacity < 1.0f) {
                UiRender::alphaMul = saved * std::max(0.0f, e.opacity);
            }
            bool savedBlocked = ctx.blocked;
            if (!e.hitTest) {
                ctx.blocked = true;
            }
            if (!e.toolTip.empty() && !ctx.blocked && NewUi::hit(e.x, e.y, e.w, e.h)) {
                tooltip(e.toolTip, e.x, e.y, e.w, e.h);
            }
            drawContent(e, ctx);
            ctx.blocked = savedBlocked;
            UiRender::alphaMul = saved;
        }

        struct Scope {
            std::map<std::string, const Xaml::Node*> keyed;
            std::map<std::string, const Xaml::Node*> implicit;
        };

        struct Props {
            std::map<std::string, std::string> values;

            bool has(const std::string& key) const {
                return values.count(key) != 0;
            }

            std::string get(const std::string& key, const std::string& fallback = std::string()) const {
                auto found = values.find(key);
                return found == values.end() ? fallback : found->second;
            }
        };

        struct TextStyle {
            Brush color;
            float size = TEXT_SIZE;
            int family = UiFont::MEDIUM;
            bool underline = false;
        };

        std::string typeOf(const std::string& name) {
            size_t colon = name.find(':');
            return colon == std::string::npos ? name : name.substr(colon + 1);
        }

        std::string textOf(const Xaml::Node& node) {
            std::string out;
            for (const Xaml::Node& child : node.children) {
                if (child.isText()) {
                    out += child.text;
                } else {
                    out += textOf(child);
                }
            }
            return out;
        }

        bool markupParts(const std::string& raw, std::string& word, std::string& argument) {
            std::string value = trim(raw);
            if (value.size() < 2 || value.front() != '{' || value.back() != '}' || value.rfind("{}", 0) == 0) {
                return false;
            }
            std::string inner = trim(value.substr(1, value.size() - 2));
            size_t gap = inner.find_first_of(" \t");
            word = gap == std::string::npos ? inner : inner.substr(0, gap);
            argument = gap == std::string::npos ? std::string() : trim(inner.substr(gap + 1));
            return true;
        }

        struct Builder {
            std::string prefix;
            int counter = 0;
            std::vector<Scope> scopes;
            std::string content;

            const Xaml::Node* keyed(const std::string& key) const {
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                    auto found = it->keyed.find(key);
                    if (found != it->keyed.end()) {
                        return found->second;
                    }
                }
                return nullptr;
            }

            const Xaml::Node* implicitStyle(const std::string& type) const {
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                    auto found = it->implicit.find(type);
                    if (found != it->implicit.end()) {
                        return found->second;
                    }
                }
                return nullptr;
            }

            std::string resolve(const std::string& raw) const {
                if (raw.rfind("{}", 0) == 0) {
                    return raw.substr(2);
                }
                std::string word;
                std::string argument;
                if (!markupParts(raw, word, argument)) {
                    return raw;
                }
                if (word == "StaticResource") {
                    const Xaml::Node* node = keyed(argument);
                    if (node == nullptr) {
                        return std::string();
                    }
                    std::string type = typeOf(node->name);
                    if (type == "String" || type == "Geometry" || type == "StreamGeometry") {
                        return trim(textOf(*node));
                    }
                    if (type == "PathGeometry") {
                        const std::string* figures = node->attribute("Figures");
                        return figures != nullptr ? *figures : trim(textOf(*node));
                    }
                    if (type == "SolidColorBrush") {
                        const std::string* color = node->attribute("Color");
                        return color != nullptr ? *color : std::string();
                    }
                    return raw;
                }
                if (word == "TemplateBinding") {
                    return content;
                }
                if (word == "DynamicResource") {
                    return raw;
                }
                return std::string();
            }

            const Xaml::Node* resource(const std::string& raw) const {
                std::string word;
                std::string argument;
                if (!markupParts(raw, word, argument) || word != "StaticResource") {
                    return nullptr;
                }
                return keyed(argument);
            }
        };

        void applyStyle(const Builder& b, const Xaml::Node* style, Props& props, int depth) {
            if (style == nullptr || depth > 8 || typeOf(style->name) != "Style") {
                return;
            }
            if (const std::string* based = style->attribute("BasedOn")) {
                applyStyle(b, b.resource(*based), props, depth + 1);
            }
            for (const Xaml::Node& setter : style->children) {
                if (setter.isText() || typeOf(setter.name) != "Setter") {
                    continue;
                }
                const std::string* property = setter.attribute("Property");
                const std::string* value = setter.attribute("Value");
                if (property != nullptr && value != nullptr) {
                    props.values[*property] = b.resolve(*value);
                }
            }
        }

        Props propsOf(const Builder& b, const Xaml::Node& node, const std::string& type) {
            Props props;
            if (const std::string* style = node.attribute("Style")) {
                applyStyle(b, b.resource(*style), props, 0);
            } else {
                applyStyle(b, b.implicitStyle(type), props, 0);
            }
            for (const auto& [key, value] : node.attributes) {
                if (key == "Style" || key.rfind("xmlns", 0) == 0) {
                    continue;
                }
                props.values[key] = b.resolve(value);
            }
            return props;
        }

        bool pushResources(Builder& b, const Xaml::Node& node) {
            const Xaml::Node* resources = nullptr;
            for (const Xaml::Node& child : node.children) {
                if (!child.isText() && child.name.size() > 10 &&
                    child.name.compare(child.name.size() - 10, 10, ".Resources") == 0) {
                    resources = &child;
                    break;
                }
            }
            if (resources == nullptr) {
                return false;
            }
            Scope scope;
            std::vector<const Xaml::Node*> entries;
            for (const Xaml::Node& entry : resources->children) {
                if (entry.isText()) {
                    continue;
                }
                if (typeOf(entry.name) == "ResourceDictionary") {
                    for (const Xaml::Node& inner : entry.children) {
                        if (!inner.isText()) {
                            entries.push_back(&inner);
                        }
                    }
                } else {
                    entries.push_back(&entry);
                }
            }
            for (const Xaml::Node* entry : entries) {
                const std::string* key = entry->attribute("x:Key");
                if (typeOf(entry->name) == "Style" && key == nullptr) {
                    if (const std::string* target = entry->attribute("TargetType")) {
                        std::string word;
                        std::string argument;
                        std::string name = markupParts(*target, word, argument) ? argument : *target;
                        scope.implicit[typeOf(trim(name))] = entry;
                    }
                } else if (key != nullptr) {
                    scope.keyed[*key] = entry;
                }
            }
            b.scopes.push_back(std::move(scope));
            return true;
        }

        Brush brushNode(const Builder& b, const Xaml::Node& node) {
            std::string type = typeOf(node.name);
            Brush out;
            if (type == "SolidColorBrush") {
                const std::string* color = node.attribute("Color");
                out = color != nullptr ? brushOf(b.resolve(*color)) : Brush{};
                if (out.kind == 1) {
                    if (const std::string* opacity = node.attribute("Opacity")) {
                        float alpha = std::clamp(number(*opacity, 1.0f), 0.0f, 1.0f);
                        uint32_t a = (uint32_t) std::lround(((out.argb >> 24) & 0xFF) * alpha);
                        out.argb = (out.argb & 0x00FFFFFFu) | a << 24;
                    }
                }
                return out;
            }
            if (type == "LinearGradientBrush" || type == "RadialGradientBrush") {
                std::vector<std::pair<float, uint32_t>> stops;
                std::vector<const Xaml::Node*> pending = {&node};
                while (!pending.empty()) {
                    const Xaml::Node* current = pending.back();
                    pending.pop_back();
                    for (const Xaml::Node& child : current->children) {
                        if (child.isText()) {
                            continue;
                        }
                        if (typeOf(child.name) == "GradientStop") {
                            const std::string* color = child.attribute("Color");
                            const std::string* offset = child.attribute("Offset");
                            Brush stop = color != nullptr ? brushOf(b.resolve(*color)) : Brush{};
                            if (stop.kind == 1) {
                                stops.push_back({offset != nullptr ? number(*offset, 0.0f) : 0.0f, stop.argb});
                            }
                        } else {
                            pending.push_back(&child);
                        }
                    }
                }
                if (stops.empty()) {
                    return out;
                }
                std::stable_sort(stops.begin(), stops.end(),
                                 [](const auto& a, const auto& c) { return a.first < c.first; });
                out.kind = 3;
                out.argb = stops.front().second;
                out.argb2 = stops.back().second;
                std::vector<float> start =
                        numbers(node.attribute("StartPoint") ? *node.attribute("StartPoint") : "0,0");
                std::vector<float> end = numbers(node.attribute("EndPoint") ? *node.attribute("EndPoint") : "1,1");
                if (start.size() >= 2 && end.size() >= 2) {
                    out.vertical = std::fabs(end[1] - start[1]) > std::fabs(end[0] - start[0]);
                }
                return out;
            }
            if (type == "ImageBrush") {
                const std::string* source = node.attribute("ImageSource");
                if (source != nullptr) {
                    out.kind = 4;
                    out.image = trim(b.resolve(*source));
                    const std::string* stretch = node.attribute("Stretch");
                    out.stretch = stretchOf(stretch != nullptr ? *stretch : std::string(), FILL);
                }
                return out;
            }
            return out;
        }

        Brush brushProp(const Builder& b, const Xaml::Node& node, const Props& p, const std::string& name) {
            std::string suffix = "." + name;
            for (const Xaml::Node& child : node.children) {
                if (child.isText() || child.name.size() <= suffix.size() ||
                    child.name.compare(child.name.size() - suffix.size(), suffix.size(), suffix) != 0) {
                    continue;
                }
                for (const Xaml::Node& inner : child.children) {
                    if (!inner.isText()) {
                        return brushNode(b, inner);
                    }
                }
            }
            std::string value = p.get(name);
            if (const Xaml::Node* resource = b.resource(value)) {
                return brushNode(b, *resource);
            }
            return brushOf(value);
        }

        TextStyle derive(const TextStyle& base, const Props& p) {
            TextStyle out = base;
            if (p.has("FontSize")) {
                out.size = std::clamp(number(p.get("FontSize"), base.size), 6.0f, 72.0f);
            }
            if (p.has("FontWeight")) {
                out.family = bold(p.get("FontWeight")) ? UiFont::BOLD : UiFont::MEDIUM;
            }
            if (p.has("FontFamily")) {
                std::string family = lower(p.get("FontFamily"));
                if (family.find("consolas") != std::string::npos || family.find("mono") != std::string::npos ||
                    family.find("courier") != std::string::npos) {
                    out.family = UiFont::CODE;
                }
            }
            if (p.has("Foreground")) {
                Brush color = brushOf(p.get("Foreground"));
                if (color.set()) {
                    out.color = color;
                }
            }
            if (p.has("TextDecorations")) {
                out.underline = lower(p.get("TextDecorations")).find("underline") != std::string::npos;
            }
            return out;
        }

        std::unique_ptr<Element> build(Builder& b, const Xaml::Node& node);

        Inline textRun(const std::string& text, const TextStyle& style) {
            Inline run;
            run.text = clean(text);
            run.color = style.color;
            run.size = style.size;
            run.family = style.family;
            run.underline = style.underline;
            return run;
        }

        std::unique_ptr<Element> textElement(Builder& b, const std::string& text, float size, int family,
                                             const Brush& color) {
            auto element = std::make_unique<Element>();
            element->kind = Kind::Text;
            element->id = b.prefix + "#" + std::to_string(b.counter++);
            TextStyle style;
            style.size = size;
            style.family = family;
            style.color = color;
            element->rich.foreground = color;
            element->rich.runs.push_back(textRun(text, style));
            return element;
        }

        void collectInlines(Builder& b, const Xaml::Node& node, const TextStyle& style, Rich& rich) {
            for (const Xaml::Node& child : node.children) {
                if (child.isText()) {
                    rich.runs.push_back(textRun(child.text, style));
                    continue;
                }
                if (child.name.find('.') != std::string::npos) {
                    continue;
                }
                std::string type = typeOf(child.name);
                Props p = propsOf(b, child, type);
                if (type == "Run") {
                    TextStyle s = derive(style, p);
                    rich.runs.push_back(textRun(p.has("Text") ? p.get("Text") : textOf(child), s));
                } else if (type == "Bold") {
                    TextStyle s = derive(style, p);
                    s.family = UiFont::BOLD;
                    collectInlines(b, child, s, rich);
                } else if (type == "Underline") {
                    TextStyle s = derive(style, p);
                    s.underline = true;
                    collectInlines(b, child, s, rich);
                } else if (type == "Italic" || type == "Span") {
                    collectInlines(b, child, derive(style, p), rich);
                } else if (type == "LineBreak") {
                    Inline run = textRun(std::string(), style);
                    run.lineBreak = true;
                    rich.runs.push_back(std::move(run));
                } else if (type == "Hyperlink") {
                    auto link = std::make_unique<Element>();
                    link->kind = Kind::TextButton;
                    link->id = b.prefix + "#" + std::to_string(b.counter++);
                    link->text = trim(textOf(child));
                    link->fontSize = style.size;
                    std::string uri = p.get("NavigateUri");
                    if (!uri.empty()) {
                        link->actions.push_back(Action{"打开网页", uri});
                    }
                    Inline run = textRun(std::string(), style);
                    run.element = link.get();
                    rich.owned.push_back(std::move(link));
                    rich.runs.push_back(std::move(run));
                } else if (type == "InlineUIContainer") {
                    for (const Xaml::Node& inner : child.children) {
                        if (auto built = build(b, inner)) {
                            Inline run = textRun(std::string(), style);
                            run.element = built.get();
                            rich.owned.push_back(std::move(built));
                            rich.runs.push_back(std::move(run));
                        }
                    }
                } else if (auto built = build(b, child)) {
                    if (built->kind == Kind::Code) {
                        built->fontSize = style.size;
                    }
                    Inline run = textRun(std::string(), style);
                    run.element = built.get();
                    rich.owned.push_back(std::move(built));
                    rich.runs.push_back(std::move(run));
                }
            }
        }

        void normalize(Rich& rich) {
            bool pending = false;
            bool pendingNewline = false;
            bool haveOutput = false;
            char32_t previous = 0;
            Inline* lastText = nullptr;
            for (Inline& run : rich.runs) {
                if (run.lineBreak) {
                    pending = false;
                    pendingNewline = false;
                    haveOutput = false;
                    lastText = nullptr;
                    continue;
                }
                if (run.element != nullptr) {
                    if (pending && haveOutput && lastText != nullptr) {
                        lastText->text += ' ';
                    }
                    pending = false;
                    pendingNewline = false;
                    haveOutput = true;
                    previous = 'x';
                    lastText = nullptr;
                    continue;
                }
                std::string out;
                size_t i = 0;
                while (i < run.text.size()) {
                    size_t from = i;
                    char32_t c = nextCode(run.text, i);
                    if (space(c)) {
                        pending = true;
                        pendingNewline = pendingNewline || c == '\n';
                        continue;
                    }
                    if (pending && haveOutput && !(pendingNewline && cjk(previous) && cjk(c))) {
                        out += ' ';
                    }
                    pending = false;
                    pendingNewline = false;
                    out.append(run.text, from, i - from);
                    haveOutput = true;
                    previous = c;
                }
                run.text = std::move(out);
                if (!run.text.empty()) {
                    lastText = &run;
                }
            }
            std::vector<Inline> kept;
            for (Inline& run : rich.runs) {
                if (run.element == nullptr && !run.lineBreak && run.text.empty()) {
                    continue;
                }
                kept.push_back(std::move(run));
            }
            rich.runs = std::move(kept);
        }

        void setupRich(Builder& b, const Xaml::Node& node, const Props& p, const TextStyle& base, Rich& rich,
                       bool useTextAttribute) {
            TextStyle style = derive(base, p);
            rich.foreground = style.color;
            rich.align = textAlignment(p.get("TextAlignment"));
            rich.wrap = lower(p.get("TextWrapping")) != "nowrap";
            rich.lineHeight = number(p.get("LineHeight"), 0.0f);
            if (useTextAttribute && p.has("Text")) {
                rich.runs.push_back(textRun(p.get("Text"), style));
                return;
            }
            collectInlines(b, node, style, rich);
            normalize(rich);
        }

        void buildBlocks(Builder& b, const Xaml::Node& node, const TextStyle& base, std::vector<Block>& out) {
            for (const Xaml::Node& child : node.children) {
                if (child.isText() || child.name.find('.') != std::string::npos) {
                    continue;
                }
                std::string type = typeOf(child.name);
                Props p = propsOf(b, child, type);
                Block block;
                if (type == "Paragraph") {
                    block.type = 0;
                    block.margin = p.has("Margin") ? thickness(p.get("Margin")) : Thickness{0.0f, 6.0f, 0.0f, 6.0f};
                    setupRich(b, child, p, base, block.rich, false);
                } else if (type == "List") {
                    block.type = 1;
                    block.margin = p.has("Margin") ? thickness(p.get("Margin")) : Thickness{20.0f, 6.0f, 0.0f, 6.0f};
                    block.marker = brushOf(p.get("Foreground"));
                    block.markerSize = base.size;
                    std::string marker = lower(p.get("MarkerStyle"));
                    block.markerStyle = marker == "none" || marker == "0" ? 0
                            : marker == "decimal" || marker == "9"        ? 2
                                                                          : 1;
                    for (const Xaml::Node& item : child.children) {
                        if (item.isText() || typeOf(item.name) != "ListItem") {
                            continue;
                        }
                        Props ip = propsOf(b, item, "ListItem");
                        if (ip.has("Margin")) {
                            block.itemMargin = thickness(ip.get("Margin"));
                        }
                        std::vector<Block> blocks;
                        buildBlocks(b, item, derive(base, ip), blocks);
                        block.items.push_back(std::move(blocks));
                    }
                } else if (type == "Section" || type == "ListItem") {
                    block.type = 3;
                    block.margin = thickness(p.get("Margin"));
                    buildBlocks(b, child, derive(base, p), block.blocks);
                } else if (type == "BlockUIContainer") {
                    block.type = 2;
                    block.margin = thickness(p.get("Margin"));
                    for (const Xaml::Node& inner : child.children) {
                        if (auto built = build(b, inner)) {
                            if (built->kind == Kind::Code) {
                                built->block = true;
                            }
                            block.ui = std::move(built);
                            break;
                        }
                    }
                } else if (type == "Table") {
                    continue;
                } else {
                    block.type = 2;
                    block.ui = build(b, child);
                    if (!block.ui) {
                        continue;
                    }
                }
                out.push_back(std::move(block));
            }
        }

        void common(Element& e, const Props& p) {
            e.margin = thickness(p.get("Margin"));
            if (p.has("Padding")) {
                e.padding = thickness(p.get("Padding"));
                e.paddingSet = true;
            }
            e.width = length(p.get("Width"));
            e.height = length(p.get("Height"));
            e.minWidth = std::max(0.0f, number(p.get("MinWidth"), 0.0f));
            e.minHeight = std::max(0.0f, number(p.get("MinHeight"), 0.0f));
            e.maxWidth = number(p.get("MaxWidth"), INF);
            e.maxHeight = number(p.get("MaxHeight"), INF);
            e.hAlign = alignment(p.get("HorizontalAlignment"));
            e.vAlign = alignment(p.get("VerticalAlignment"));
            std::string visibility = lower(trim(p.get("Visibility")));
            e.collapsed = visibility == "collapsed";
            e.hidden = visibility == "hidden";
            e.opacity = std::clamp(number(p.get("Opacity"), 1.0f), 0.0f, 1.0f);
            e.toolTip = p.get("ToolTip");
            e.hitTest = lower(trim(p.get("IsHitTestVisible"))) != "false";
            e.column = std::max(0, (int) number(p.get("Grid.Column"), 0.0f));
            e.row = std::max(0, (int) number(p.get("Grid.Row"), 0.0f));
            e.columnSpan = std::max(1, (int) number(p.get("Grid.ColumnSpan"), 1.0f));
            e.rowSpan = std::max(1, (int) number(p.get("Grid.RowSpan"), 1.0f));
        }

        void actionsOf(Builder& b, const Xaml::Node& node, const Props& p, Element& e) {
            std::string type = trim(p.get("EventType"));
            if (!type.empty()) {
                e.actions.push_back(Action{type, p.get("EventData")});
            }
            for (const Xaml::Node& child : node.children) {
                if (child.isText() || child.name.size() < 7 ||
                    child.name.compare(child.name.size() - 7, 7, ".Events") != 0) {
                    continue;
                }
                std::vector<const Xaml::Node*> events;
                for (const Xaml::Node& inner : child.children) {
                    if (inner.isText()) {
                        continue;
                    }
                    if (typeOf(inner.name) == "CustomEvent") {
                        events.push_back(&inner);
                    } else {
                        for (const Xaml::Node& event : inner.children) {
                            if (!event.isText() && typeOf(event.name) == "CustomEvent") {
                                events.push_back(&event);
                            }
                        }
                    }
                }
                for (const Xaml::Node* event : events) {
                    const std::string* eventType = event->attribute("Type");
                    const std::string* eventData = event->attribute("Data");
                    if (eventType != nullptr) {
                        e.actions.push_back(
                                Action{trim(b.resolve(*eventType)), eventData != nullptr ? b.resolve(*eventData) : ""});
                    }
                }
            }
        }

        void addChildren(Builder& b, const Xaml::Node& node, Element& e) {
            for (const Xaml::Node& child : node.children) {
                if (auto built = build(b, child)) {
                    e.children.push_back(std::move(built));
                }
            }
        }

        std::vector<Def> definitions(Builder& b, const Xaml::Node& node, const std::string& suffix, bool columns) {
            std::vector<Def> out;
            for (const Xaml::Node& child : node.children) {
                if (child.isText() || child.name.size() < suffix.size() ||
                    child.name.compare(child.name.size() - suffix.size(), suffix.size(), suffix) != 0) {
                    continue;
                }
                for (const Xaml::Node& def : child.children) {
                    if (def.isText()) {
                        continue;
                    }
                    Props p = propsOf(b, def, typeOf(def.name));
                    std::string size = lower(trim(p.get(columns ? "Width" : "Height", "*")));
                    Def d;
                    if (size == "auto") {
                        d.type = 1;
                    } else if (!size.empty() && size.back() == '*') {
                        d.type = 2;
                        d.value = std::max(0.0f, number(size.substr(0, size.size() - 1), 1.0f));
                    } else {
                        d.type = 0;
                        d.value = number(size, 0.0f);
                    }
                    d.min = std::max(0.0f, number(p.get(columns ? "MinWidth" : "MinHeight"), 0.0f));
                    d.max = number(p.get(columns ? "MaxWidth" : "MaxHeight"), INF);
                    out.push_back(d);
                }
            }
            return out;
        }

        const Xaml::Node* findChild(const Xaml::Node& node, const std::string& type) {
            for (const Xaml::Node& child : node.children) {
                if (child.isText()) {
                    continue;
                }
                if (typeOf(child.name) == type) {
                    return &child;
                }
                if (const Xaml::Node* inner = findChild(child, type)) {
                    return inner;
                }
            }
            return nullptr;
        }

        std::unique_ptr<Element> build(Builder& b, const Xaml::Node& node) {
            if (node.isText() || node.name.find('.') != std::string::npos) {
                return nullptr;
            }
            std::string type = typeOf(node.name);
            if (type == "String" || type == "Style" || type == "ControlTemplate" || type == "DataTemplate" ||
                type == "SolidColorBrush" || type == "ResourceDictionary" || type == "CustomEvent" ||
                type == "CustomEventCollection" || type == "Setter" || type == "Geometry" || type == "StreamGeometry" ||
                type == "PathGeometry" || type == "LinearGradientBrush" || type == "RadialGradientBrush" ||
                type == "ImageBrush" || type == "GradientStop" || type == "DropShadowEffect" || type == "BlurEffect" ||
                type == "Storyboard") {
                return nullptr;
            }
            bool scoped = pushResources(b, node);
            Props p = propsOf(b, node, type);
            auto e = std::make_unique<Element>();
            e->id = b.prefix + "#" + std::to_string(b.counter++);
            common(*e, p);
            actionsOf(b, node, p, *e);
            TextStyle base;
            base.size = TEXT_SIZE;

            if (type == "StackPanel" || type == "DockPanel" || type == "VirtualizingStackPanel") {
                e->kind = Kind::Stack;
                e->horizontal = lower(trim(p.get("Orientation"))) == "horizontal";
                addChildren(b, node, *e);
            } else if (type == "WrapPanel") {
                e->kind = lower(trim(p.get("Orientation"))) == "vertical" ? Kind::Stack : Kind::Wrap;
                addChildren(b, node, *e);
            } else if (type == "Grid" || type == "Canvas") {
                e->kind = Kind::Grid;
                e->columns = definitions(b, node, ".ColumnDefinitions", true);
                e->rows = definitions(b, node, ".RowDefinitions", false);
                addChildren(b, node, *e);
            } else if (type == "UniformGrid") {
                e->kind = Kind::Grid;
                addChildren(b, node, *e);
                int count = (int) e->children.size();
                int cols = (int) number(p.get("Columns"), 0.0f);
                int rows = (int) number(p.get("Rows"), 0.0f);
                if (cols <= 0) {
                    cols = rows > 0 ? (count + rows - 1) / rows
                                    : std::max(1, (int) std::ceil(std::sqrt((float) std::max(1, count))));
                }
                cols = std::max(1, cols);
                int rowCount = std::max(1, (count + cols - 1) / cols);
                e->columns.assign((size_t) cols, Def{});
                e->rows.assign((size_t) rowCount, Def{1, 1.0f, 0.0f, INF});
                for (int i = 0; i < count; i++) {
                    e->children[(size_t) i]->column = i % cols;
                    e->children[(size_t) i]->row = i / cols;
                    e->children[(size_t) i]->columnSpan = 1;
                    e->children[(size_t) i]->rowSpan = 1;
                }
            } else if (type == "GroupBox") {
                e->kind = Kind::Border;
                e->groupBox = true;
                e->background = brushProp(b, node, p, "Background");
                std::fill(std::begin(e->corners), std::end(e->corners), 8.0f);
                if (!e->paddingSet) {
                    e->padding = Thickness{10.0f, 6.0f, 10.0f, 8.0f};
                }
                if (!p.has("Margin")) {
                    e->margin = Thickness{0.0f, 0.0f, 8.0f, 8.0f};
                }
                e->minWidth = std::max(e->minWidth, 64.0f);
                auto stack = std::make_unique<Element>();
                stack->kind = Kind::Stack;
                stack->id = b.prefix + "#" + std::to_string(b.counter++);
                std::string header = p.get("Header");
                if (!header.empty()) {
                    Brush muted;
                    muted.kind = 5;
                    stack->children.push_back(textElement(b, header, UiWidgets::TEXT_SMALL, UiFont::MEDIUM, muted));
                }
                if (p.has("Content")) {
                    stack->children.push_back(textElement(b, p.get("Content"), 16.0f, UiFont::BOLD, Brush{}));
                } else {
                    addChildren(b, node, *stack);
                }
                e->children.push_back(std::move(stack));
            } else if (type == "Border" || type == "ScrollViewer" || type == "Viewbox" || type == "Expander") {
                e->kind = Kind::Border;
                e->background = brushProp(b, node, p, "Background");
                e->borderBrush = brushProp(b, node, p, "BorderBrush");
                e->borderThickness = thickness(p.get("BorderThickness"));
                std::vector<float> radius = numbers(p.get("CornerRadius"));
                if (radius.size() == 1) {
                    std::fill(std::begin(e->corners), std::end(e->corners), radius[0]);
                } else if (radius.size() >= 4) {
                    std::copy(radius.begin(), radius.begin() + 4, std::begin(e->corners));
                }
                if (!e->paddingSet) {
                    e->padding = Thickness{};
                }
                addChildren(b, node, *e);
            } else if (type == "ContentControl" || type == "ContentPresenter" || type == "Label") {
                e->kind = Kind::Border;
                std::string content = p.get("Content");
                const Xaml::Node* templ = nullptr;
                if (const std::string* raw = node.attribute("Template")) {
                    templ = b.resource(*raw);
                }
                if (templ != nullptr && typeOf(templ->name) == "ControlTemplate") {
                    std::string saved = b.content;
                    b.content = content;
                    addChildren(b, *templ, *e);
                    b.content = saved;
                } else if (!content.empty()) {
                    auto text = std::make_unique<Element>();
                    text->kind = Kind::Text;
                    text->id = b.prefix + "#" + std::to_string(b.counter++);
                    TextStyle style = derive(base, p);
                    text->rich.foreground = style.color;
                    text->rich.runs.push_back(textRun(content, style));
                    e->children.push_back(std::move(text));
                } else {
                    addChildren(b, node, *e);
                }
            } else if (type == "MyCard") {
                e->kind = Kind::Card;
                e->title = clean(p.get("Title"));
                e->canSwap = isTrue(p.get("CanSwap"));
                e->swapped = e->canSwap && (isTrue(p.get("IsSwapped")) || isTrue(p.get("IsSwaped")));
                e->open = e->swapped ? 0.0f : 1.0f;
                addChildren(b, node, *e);
            } else if (type == "TextBlock") {
                e->kind = Kind::Text;
                e->background = brushProp(b, node, p, "Background");
                setupRich(b, node, p, base, e->rich, true);
            } else if (type == "FlowDocumentScrollViewer" || type == "FlowDocumentReader" ||
                       type == "FlowDocumentPageViewer" || type == "RichTextBox" || type == "FlowDocument") {
                const Xaml::Node* doc = type == "FlowDocument" ? &node : findChild(node, "FlowDocument");
                if (doc == nullptr) {
                    if (scoped) {
                        b.scopes.pop_back();
                    }
                    return nullptr;
                }
                e->kind = Kind::Flow;
                Props dp = doc == &node ? p : propsOf(b, *doc, "FlowDocument");
                TextStyle style = derive(base, dp);
                if (!dp.has("FontSize")) {
                    style.size = 14.0f;
                }
                buildBlocks(b, *doc, style, e->blocks);
            } else if (type == "MyImage" || type == "Image") {
                e->kind = Kind::Image;
                e->source = trim(p.get("Source"));
                e->fallback = trim(p.get("FallbackSource"));
                e->stretch = stretchOf(p.get("Stretch"), UNIFORM);
            } else if (type == "Path") {
                e->kind = Kind::Path;
                e->data = p.get("Data");
                e->fill = brushProp(b, node, p, "Fill");
                if (!e->fill.set()) {
                    e->fill = brushProp(b, node, p, "Stroke");
                }
                e->stretch = stretchOf(p.get("Stretch"), UNIFORM);
            } else if (type == "Line") {
                e->kind = Kind::Line;
                e->x1 = number(p.get("X1"), 0.0f);
                e->y1 = number(p.get("Y1"), 0.0f);
                e->x2 = number(p.get("X2"), 0.0f);
                e->y2 = number(p.get("Y2"), 0.0f);
                e->stroke = brushProp(b, node, p, "Stroke");
                e->strokeThickness = std::max(0.5f, number(p.get("StrokeThickness"), 1.0f));
                e->stretch = stretchOf(p.get("Stretch"), NONE);
            } else if (type == "Rectangle" || type == "Ellipse") {
                e->kind = Kind::Shape;
                e->ellipse = type == "Ellipse";
                e->fill = brushProp(b, node, p, "Fill");
                e->stroke = brushProp(b, node, p, "Stroke");
                e->strokeThickness = number(p.get("StrokeThickness"), 1.0f);
                e->radius = number(p.get("RadiusX"), 0.0f);
            } else if (type == "MyHint") {
                e->kind = Kind::Hint;
                std::string theme = lower(trim(p.get("Theme")));
                bool warn = !p.has("IsWarn") || isTrue(p.get("IsWarn"));
                e->theme = theme == "blue" ? 0 : theme == "yellow" ? 1 : theme == "red" ? 2 : (warn ? 2 : 0);
                TextStyle style = base;
                style.size = CONTROL_SIZE;
                e->rich.runs.push_back(textRun(p.get("Text"), style));
            } else if (type == "MyButton" || type == "Button") {
                e->kind = Kind::Button;
                e->text = clean(p.has("Text") ? p.get("Text") : p.get("Content"));
                std::string color = lower(trim(p.get("ColorType")));
                e->theme = color == "highlight" ? 1 : color == "red" ? 2 : 0;
            } else if (type == "MyTextButton") {
                e->kind = Kind::TextButton;
                e->text = clean(p.has("Text") ? p.get("Text") : trim(textOf(node)));
                e->fontSize = number(p.get("FontSize"), CONTROL_SIZE);
            } else if (type == "MyIconTextButton") {
                e->kind = Kind::IconTextButton;
                e->text = clean(p.get("Text"));
                e->data = p.get("Logo");
                e->logoScale = std::clamp(number(p.get("LogoScale"), 1.0f), 0.2f, 3.0f);
                e->theme = lower(trim(p.get("ColorType"))) == "highlight" ? 1 : 0;
            } else if (type == "MyIconButton") {
                e->kind = Kind::IconButton;
                e->data = p.get("Logo");
                e->logoScale = std::clamp(number(p.get("LogoScale"), 1.0f), 0.2f, 3.0f);
                std::string theme = lower(trim(p.get("Theme")));
                e->theme = theme == "white" ? 1 : theme == "black" ? 2 : theme == "red" ? 3 : 0;
            } else if (type == "MyListItem") {
                e->kind = Kind::ListItem;
                e->source = trim(p.get("Logo"));
                e->logoScale = number(p.get("LogoScale"), 1.0f);
                e->title = clean(p.get("Title"));
                e->info = clean(p.get("Info"));
                e->clickable = lower(trim(p.get("Type"))) == "clickable" && !e->actions.empty();
                if (e->title.empty() && !e->actions.empty()) {
                    std::string data = e->actions.front().data;
                    size_t slash = data.find_last_of("/\\");
                    e->title = slash == std::string::npos ? data : data.substr(slash + 1);
                    size_t dot = e->title.rfind(".json");
                    if (dot != std::string::npos) {
                        e->title.resize(dot);
                    }
                    e->clickable = true;
                }
            } else if (type == "MyLoading") {
                e->kind = Kind::Loading;
                e->text = clean(p.get("Text"));
            } else if (type == "TextBox") {
                e->kind = Kind::Code;
                e->text = p.has("Text") ? p.get("Text") : textOf(node);
                e->fontSize = std::clamp(number(p.get("FontSize"), CONTROL_SIZE), 8.0f, 32.0f);
                e->block = lower(p.get("TextWrapping")) == "wrap" || isTrue(p.get("AcceptsReturn"));
                e->height = NAN;
            } else if (type == "Separator") {
                e->kind = Kind::Rule;
            } else if (type == "MyCheckBox" || type == "MyRadioBox" || type == "CheckBox" || type == "RadioButton") {
                e->kind = Kind::Text;
                TextStyle style = derive(base, p);
                e->rich.runs.push_back(textRun(p.has("Text") ? p.get("Text") : p.get("Content"), style));
            } else {
                e->kind = Kind::Stack;
                addChildren(b, node, *e);
                if (e->children.empty()) {
                    if (scoped) {
                        b.scopes.pop_back();
                    }
                    return nullptr;
                }
            }
            if (scoped) {
                b.scopes.pop_back();
            }
            return e;
        }
    }

    struct Document::State {
        std::unique_ptr<Element> root;
        std::string error;
        std::vector<Action> pending;
        float width = 0.0f;
    };

    Document::Document() : state(std::make_unique<State>()) {}

    Document::~Document() = default;

    bool Document::load(const std::string& source, const std::string& id) {
        state->root.reset();
        state->error.clear();
        Xaml::Document parsed = Xaml::parse(source);
        if (!parsed.error.empty()) {
            state->error = parsed.error;
            return false;
        }
        Builder b;
        b.prefix = id;
        auto root = std::make_unique<Element>();
        root->kind = Kind::Stack;
        root->id = id;
        for (const Xaml::Node& node : parsed.roots) {
            if (auto built = build(b, node)) {
                root->children.push_back(std::move(built));
            }
        }
        state->root = std::move(root);
        return true;
    }

    const std::string& Document::error() const {
        return state->error;
    }

    bool Document::empty() const {
        return state->root == nullptr || state->root->children.empty();
    }

    float Document::measure(float width) {
        if (!state->root) {
            return 0.0f;
        }
        state->width = width;
        XamlView::measure(*state->root, width, INF);
        return state->root->desiredH;
    }

    void Document::draw(float x, float y, float clipTop, float clipBottom, bool blocked) {
        if (!state->root) {
            return;
        }
        Element& root = *state->root;
        arrange(root, x, y, std::max(root.desiredW, state->width), root.desiredH);
        Ctx ctx;
        ctx.clipTop = clipTop;
        ctx.clipBottom = clipBottom;
        ctx.blocked = blocked;
        ctx.glass = glass;
        ctx.pending = &state->pending;
        XamlView::draw(root, ctx);
        if (!state->pending.empty()) {
            std::vector<Action> actions = std::move(state->pending);
            state->pending.clear();
            if (onActions) {
                onActions(actions);
            }
        }
    }
}
