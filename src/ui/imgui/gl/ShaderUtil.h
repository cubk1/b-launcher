#pragma once

#include <cstdint>
#include <vector>

struct ImDrawList;

namespace ShaderUtil {

    enum Kind {
        ROUND_RECT = 0,
        RING = 1,
        SHADOW = 2,
        BLOOM = 3,
        KAWASE = 4,
        BLUR = 5,
        PICKER = 6,
        ROUND_TEXTURE = 7,
        ROUND_TEX = 8,
        CHECK = 9,
        ARC = 10,
        ICON = 11,
        SHELL = 12
    };

    struct Prim {
        int kind;
        float f[12];
        uint32_t c[2];
        int i[3];
        uint32_t tex;
    };

    struct Header {
        float displayPosX;
        float displayPosY;
        float displaySizeX;
        float displaySizeY;
        float fbScaleX;
        float fbScaleY;
        float unit;
        int frame;
        int count;
    };

    void submit(const Prim& prim);
    void flush(ImDrawList* drawList);
    bool pending();

    void rect(float x, float y, float width, float height, uint32_t rgba);
    void roundRect(float x, float y, float width, float height, float radius, uint32_t rgba);
    void roundRect(float x, float y, float width, float height, float radius, uint32_t rgba, int cornerMask);
    void roundRect(float x, float y, float width, float height, float radius, uint32_t rgba, uint32_t rgbaB,
                   float gradient, int cornerMask);
    void roundRectRadii(float x, float y, float width, float height, float radius, uint32_t rgba, uint32_t rgbaB,
                        float gradient, float topLeft, float topRight, float bottomLeft, float bottomRight);
    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgba,
                       int cornerMask);
    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgbaTop,
                       uint32_t rgbaBottom, int cornerMask);
    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgbaTop,
                       uint32_t rgbaBottom, int cornerMask, float fadeEnd, bool horizontal);
    void roundRectRing(float x, float y, float width, float height, float radius, float thickness, uint32_t rgbaTop,
                       uint32_t rgbaBottom, int cornerMask, float fadeEnd, bool horizontal, float dash, float gap);
    void roundRectShadow(float x, float y, float width, float height, float radius, float spread, float offsetY,
                         uint32_t rgba);
    void roundRectBloom(float x, float y, float width, float height, float radius, float spread, float intensity,
                        uint32_t rgba);
    void kawaseBlur(float x, float y, float w, float h, float cornerRadius, float strength, uint32_t tintRgba,
                    int cornerMask, bool recapture);
    void blur(float x, float y, float w, float h, float cornerRadius, float blurRadius);
    void blur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba);
    void blur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba,
              int cornerMask, float gamma);
    void pickerSurface(float x, float y, float width, float height, float radius, float hue, int mode, float alpha);
    void pickerSurface(float x, float y, float width, float height, float radius, float hue, float sat, float val,
                       int mode, float alpha);
    void roundTexture(uint32_t texture, float x, float y, float width, float height, float radius, int cornerMask,
                      float alpha, float fadeWidth, uint32_t fadeRgba, float uvScaleX, float uvScaleY);
    void roundTex(float x, float y, float width, float height, float radius, uint32_t texture, float u0, float v0,
                  float u1, float v1, uint32_t tintRgba);
    void roundTex(float x, float y, float width, float height, float radius, uint32_t texture, float u0, float v0,
                  float u1, float v1, uint32_t tintRgba, int cornerMask);
    void checkMark(float cx, float cy, float size, float thickness, uint32_t rgba);
    void arc(float cx, float cy, float radius, float thickness, float from, float to, uint32_t rgba);
    void icon(float x, float y, float size, int kind, uint32_t rgba);
    void shell(float x, float y, float width, float height, float split, float radius, uint32_t plateRgba,
               uint32_t mainRgba);
    void shell(float x, float y, float width, float height, float split, float radius, uint32_t plateRgba,
               uint32_t mainRgba, float outerRadius);

    void queueBlur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba,
                   int cornerMask, float gamma);
    void queueBlur(float x, float y, float w, float h, float cornerRadius, float blurRadius, uint32_t tintRgba,
                   int cornerMask, float gamma, bool recapture);
    void queueBlurCorners(float x, float y, float w, float h, float topLeft, float topRight, float bottomLeft,
                          float bottomRight, float blurRadius, uint32_t tintRgba, float gamma);
    void flushBlur();
    void queueBloom(float x, float y, float width, float height, float radius, float spread, float intensity,
                    uint32_t rgba);
    void flushBloom();

    void invalidateCapture();
    void outlineRect(float x, float y, float w, float h, float thickness, uint32_t color);

    extern std::vector<Prim> blurQueue;
    extern std::vector<Prim> bloomQueue;
}
