#pragma once

#include <cstdint>
#include <string>

namespace UiWidgets {

    constexpr float TEXT_BODY = 13.5f;
    constexpr float TEXT_CONTROL = 13.0f;
    constexpr float TEXT_SMALL = 11.0f;
    constexpr float TEXT_CAPTION = 9.5f;
    constexpr float TEXT_TITLE = 14.5f;
    constexpr float TEXT_ICON = 12.0f;
    constexpr float TEXT_PILL = 11.0f;

    void dots(float x, float y, uint32_t color);
    void icon(const std::string& glyph, float cx, float cy, uint32_t color, float size);
    void iconBeside(const std::string& glyph, float cx, const std::string& label, int family, float labelSize, float y,
                    float h, uint32_t color, float size);
    void chevron(float x, float y, uint32_t color);
    void check(float cx, float cy, uint32_t color);
    void check(float cx, float cy, uint32_t color, float size);
    void knobShadow(float cx, float cy, float radius);
    void card(float x, float y, float w, float h, const std::string& title);
    void toggle(const std::string& id, float x, float y, bool value, bool enabled, bool hovered);
    void select(const std::string& id, float x, float y, float w, const std::string& value, bool hovered, bool open);
    void slider(const std::string& id, float x, float y, float amount, const std::string& valueText, float cardRight,
                bool hovered, bool active, bool editing);
    void slider(const std::string& id, float x, float y, float trackWidth, float amount, const std::string& valueText,
                float cardRight, bool hovered, bool active, bool editing);
    float pillWidth(const std::string& text);
    void chevronRow(const std::string& id, float cardX, float rowY, float width, bool hovered, bool firstRow,
                    bool lastRow);
    void anchorGlyph(const std::string& anchor, float cx, float cy, uint32_t color);
    void checkerBoard(float x, float y, float w, float h, float radius, float cell);
    void colorSwatch(float x, float y, uint32_t rgba);
    void dotsButton(const std::string& id, float x, float y, bool open, bool hovered);
    void menuPanel(float x, float y, float w, float h);
    void overlayPanel(float x, float y, float w, float h, float radius, uint32_t fill, int cornerMask);
    void menuRowHover(const std::string& id, float x, float y, float w, float h, bool hovered);
    void subPanel(float x, float y, float w, float h);
    void subGroup(float x, float y, float w, float h);
    void subRowHover(const std::string& id, float x, float y, float w, float h, bool hovered);
    void subMode(const std::string& id, float rightX, float y, float h, const std::string& value, bool hovered,
                 bool open, float maxWidth);
    float subModeWidth(const std::string& value);
    void subSlider(const std::string& id, float x, float y, float trackWidth, float amount, bool hovered, bool active,
                   const std::string& valueText);
    void subToggle(const std::string& id, float x, float y, bool value);
}
