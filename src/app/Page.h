#pragma once

#include <cstdint>
#include <string>

namespace Page {

    constexpr float LEFT = 216.0f;
    constexpr float RIGHT_MARGIN = 28.0f;
    constexpr float TOP = 54.0f;
    constexpr float ROW_H = 40.0f;
    constexpr float TITLE_SIZE = 18.0f;
    constexpr float CAPTION_H = 26.0f;
    constexpr float GAP = 18.0f;
    constexpr float FIELD_H = 23.0f;
    constexpr float FIELD_Y = 8.0f;
    constexpr float SWATCH_Y = 12.0f;

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

    void title(const std::string& text, float x, float y);
    void caption(const std::string& text, float y);
    void card(float y, int rows);
    void label(const std::string& text, float y);
    void spinner(float cx, float cy, float radius, uint32_t color);
    void spinnerArc(float& from, float& to);
    bool button(const std::string& id, const std::string& text, float x, float y, float w, bool primary, bool blocked,
                int glyph = 0);
    float buttonWidth(const std::string& text, int glyph = 0);
    void rowHover(const std::string& id, float y, bool hovered, int corners);
    bool navRow(const std::string& id, const std::string& text, const std::string& value, float y, bool blocked,
                int corners, int glyph = 0);

    bool shown(float y, float h = ROW_H);

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
