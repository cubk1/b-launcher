#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Page {

    constexpr float GUTTER = 28.0f;
    constexpr float RIGHT_MARGIN = 28.0f;
    constexpr float ROW_H = 40.0f;
    constexpr float TITLE_SIZE = 18.0f;
    constexpr float CAPTION_H = 26.0f;
    constexpr float GAP = 18.0f;
    constexpr float FIELD_H = 23.0f;
    constexpr float FIELD_Y = 8.0f;
    constexpr float SWATCH_Y = 12.0f;

    float top();
    float left();
    float right();
    float width();
    float controlRight();

    class Column {
    public:
        explicit Column(float left);
        ~Column();
        Column(const Column&) = delete;
        Column& operator=(const Column&) = delete;

    private:
        float saved;
    };
    float columnLeft();
    float columnWidth();

    void centredText(const std::string& text, float cx, float y, float h, uint32_t color, float size);
    void title(const std::string& text, float x, float y);
    void caption(const std::string& text, float y);
    void card(float x, float y, float w, float h);
    void listCard(float x, float y, float w, float rowHeight, int rows);
    void card(float y, int rows);
    void label(const std::string& text, float y);
    void reveal(const std::string& text);
    void collect(std::vector<std::string>* sink);
    float tag(const std::string& text, float x, float rowY, uint32_t fill, uint32_t ink, float padding = 6.0f);
    // Paint the supplied fraction; callers own animation and unknown-progress state.
    void progressRing(float cx, float cy, float fraction);
    void spinner(float cx, float cy, float radius, uint32_t color);
    void spinnerArc(float& from, float& to);
    // Shared controls in the main window's page coordinate space.
    void tooltip(const std::string& text, float x, float y, float w, float h);
    bool iconButton(const std::string& id, int glyph, const std::string& hint, float x, float y,
                    bool blocked = false);
    bool button(const std::string& id, const std::string& text, float x, float y, float w, bool primary, bool blocked,
                int glyph = 0, float h = FIELD_H);
    float buttonWidth(const std::string& text, int glyph = 0);
    int rowCorners(int index, int count);
    void rowHover(const std::string& id, float y, bool hovered, int corners);
    bool navRow(const std::string& id, const std::string& text, const std::string& value, float y, bool blocked,
                int corners, int glyph = 0);

    bool shown(float y, float h = ROW_H);
    float scrollOffset(const std::string& id);
    void scrollTo(const std::string& id, float target);
    void forgetHidden();

    // Separate scroll positions and scrollbar interaction IDs for another rendering of the same pages.
    class ScrollScope {
    public:
        explicit ScrollScope(const std::string& name);
        ~ScrollScope();
        ScrollScope(const ScrollScope&) = delete;
        ScrollScope& operator=(const ScrollScope&) = delete;

    private:
        std::string saved;
    };

    class Scroll {
    public:
        float top;
        float bottom;
        float offset;

        Scroll(const std::string& id, float top, float bottom, float contentHeight);
        Scroll(const std::string& id, float top, float bottom, float contentHeight, float left, float width);
        bool begin();
        void end();

    private:
        std::string id;
        float left = 0.0f;
        float width = 0.0f;
        float maxScroll = 0.0f;
    };
}
