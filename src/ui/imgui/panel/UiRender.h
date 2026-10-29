#pragma once

#include <cstdint>
#include <string>

struct ImDrawList;

namespace UiRender {

    extern float alphaMul;
    extern float offsetX;
    extern float offsetY;
    extern float scaleFactor;
    extern float pivotX;
    extern float pivotY;
    extern float uiScale;
    extern float rasterScale;
    extern int guiScaleOption;

    void begin(ImDrawList* drawList);
    void end();
    ImDrawList* drawList();
    void flush();

    void reset();
    int guiScale();
    float unit();
    float guiWidth();
    float guiHeight();
    float menuScale(float shellWidth, float shellHeight, const std::string& scaleMode);
    float pixelScale();
    float scale();
    float stableScale();
    float transformX(float x);
    float transformY(float y);
    float transformW(float w);
    uint32_t applyAlpha(uint32_t rgba);

    void rect(float x, float y, float w, float h, uint32_t rgba);
    void roundRect(float x, float y, float w, float h, float radius, uint32_t rgba);
    void roundRect(float x, float y, float w, float h, float radius, uint32_t rgba, int cornerMask);
    void outline(float x, float y, float w, float h, float radius, uint32_t rgba);
    void outline(float x, float y, float w, float h, float radius, uint32_t rgba, int cornerMask, float thickness);
    void outlineGradient(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom,
                         float thickness);
    void shell(float x, float y, float w, float h, float split, float radius, uint32_t plateRgba, uint32_t mainRgba,
               float outerRadius);
    void shell(float x, float y, float w, float h, float split, float radius, uint32_t plateRgba, uint32_t mainRgba);
    void cliIcon(float cx, float cy, float size, int kind, uint32_t rgba);
    void checkMark(float cx, float cy, float size, float thickness, uint32_t rgba);
    void outlineFade(float x, float y, float w, float h, float radius, uint32_t rgba, float thickness, float fadeEnd);
    void shadow(float x, float y, float w, float h, float radius, float spread, float offsetY, uint32_t rgba);
    void circle(float cx, float cy, float radius, uint32_t rgba);
    void hLine(float x0, float x1, float y, uint32_t rgba);
    void vLine(float x, float y0, float y1, uint32_t rgba);
    void dashedOutline(float x, float y, float w, float h, float radius, uint32_t rgba, float dash, float gap);
    void line(float x0, float y0, float x1, float y1, uint32_t rgba, float thickness);
    void blur(float x, float y, float w, float h, float radius, float blurRadius);
    void blur(float x, float y, float w, float h, float radius, float blurRadius, bool recapture);
    void blur(float x, float y, float w, float h, float radius, float blurRadius, bool recapture, int cornerMask);
    void gaussianBlur(float x, float y, float w, float h, float radius, float blurRadius);
    void queueBlur(float x, float y, float w, float h, float radius, float blurRadius, bool recapture, int cornerMask);
    void bloom(float x, float y, float w, float h, float radius, float spread, float intensity, uint32_t rgba);
    void queueBloom(float x, float y, float w, float h, float radius, float spread, float intensity, uint32_t rgba);
    void svField(float x, float y, float w, float h, float radius, float hue);
    void hueField(float x, float y, float w, float h, float radius);
    void alphaField(float x, float y, float w, float h, float radius, float hue, float sat, float val);
    void roundRectGradient(float x, float y, float w, float h, float radius, uint32_t left, uint32_t right);
    void roundRectGradientV(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom);
    void gradientH(float x, float y, float w, float h, uint32_t left, uint32_t right);
    void texture(uint32_t location, float x, float y, float w, float h);
    void texture(uint32_t location, float x, float y, float w, float h, float u0, float v0, float u1, float v1);
    void texture(uint32_t location, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 uint32_t tint);
    void roundTexture(uint32_t location, float x, float y, float w, float h, float radius, float fadeWidth,
                      uint32_t fadeRgba, float textureW, float textureH);
    void roundTexture(uint32_t location, float x, float y, float w, float h, float radius, float fadeWidth,
                      uint32_t fadeRgba, float textureW, float textureH, int cornerMask);
    void item(const std::string& stack, float x, float y);
    void item(const std::string& stack, float cx, float cy, float itemScale);
    void itemSmooth(const std::string& stack, float cx, float cy, float itemScale);
    void itemSilhouette(const std::string& stack, float cx, float cy, float itemScale, uint32_t color, bool flip,
                        bool shadow);
    void text(const std::string& value, float x, float y, uint32_t rgba, int family, float size);
    void textClipped(const std::string& value, float x, float y, float h, float maxWidth, uint32_t rgba, int family,
                     float size);
    void textClipped(const std::string& value, float x, float y, float h, float maxWidth, uint32_t rgba, int family,
                     float size, uint32_t fadeBackground);
    void textY(const std::string& value, float x, float y, float h, uint32_t rgba, int family, float size);
    uint32_t over(uint32_t top, uint32_t bottom);
    uint32_t mix(uint32_t a, uint32_t b, float t);
    uint32_t color(int r, int g, int b, int a);
    uint32_t color(int r, int g, int b);
    float lerp(float a, float b, float t);
    float clamp(float value, float min, float max);
    float saturate(float value);
    float deltaTime();
    float approach01(float current, float target, float speed);
    float approach(float current, float target, float speed);
    float snapLayout(float value);
    void triangle(float cx, float cy, float size, float angle, uint32_t rgba);
    void scissor(float x, float y, float w, float h);
    void unscissor();

    void enableScissor(int x0, int y0, int x1, int y1);
    void disableScissor();
    float screenWidth();
    float screenHeight();
}
