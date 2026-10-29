#include "UiRender.h"
#include "UiState.h"
#include "UiFont.h"
#include "UiImages.h"
#include "UiCommon.h"
#include "../gl/ShaderUtil.h"
#include "../gl/RenderUtil.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace UiRender {

    float alphaMul = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scaleFactor = 1.0f;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
    float uiScale = 1.0f;
    float rasterScale = 1.0f;
    int guiScaleOption = 2;

    static ImDrawList* target = nullptr;

    static ImU32 toIm(uint32_t argb) {
        return ((argb >> 24) & 0xFF) << IM_COL32_A_SHIFT | ((argb >> 16) & 0xFF) << IM_COL32_R_SHIFT |
                ((argb >> 8) & 0xFF) << IM_COL32_G_SHIFT | (argb & 0xFF) << IM_COL32_B_SHIFT;
    }

    void begin(ImDrawList* drawList) {
        flush();
        target = drawList;
    }

    void end() {
        flush();
        target = nullptr;
    }

    ImDrawList* drawList() {
        if (target != nullptr) {
            return target;
        }
        return ImGui::GetForegroundDrawList();
    }

    void flush() {
        if (ShaderUtil::pending()) {
            ShaderUtil::flush(drawList());
        }
    }

    void reset() {
        alphaMul = 1.0f;
        offsetX = 0;
        offsetY = 0;
        scaleFactor = 1.0f;
        pivotX = 0;
        pivotY = 0;
    }

    int guiScale() {
        ImGuiIO& io = ImGui::GetIO();
        int width = (int) (io.DisplaySize.x * io.DisplayFramebufferScale.x);
        int height = (int) (io.DisplaySize.y * io.DisplayFramebufferScale.y);
        int scaleFactor = 1;
        int k = guiScaleOption;
        if (k == 0) {
            k = 1000;
        }
        while (scaleFactor < k && width / (scaleFactor + 1) >= 320 && height / (scaleFactor + 1) >= 240) {
            ++scaleFactor;
        }
        return scaleFactor;
    }

    float unit() {
        float framebuffer = ImGui::GetIO().DisplayFramebufferScale.x;
        return guiScale() / (framebuffer > 0.0f ? framebuffer : 1.0f);
    }

    float guiWidth() {
        return ImGui::GetIO().DisplaySize.x / unit();
    }

    float guiHeight() {
        return ImGui::GetIO().DisplaySize.y / unit();
    }

    float menuScale(float shellWidth, float shellHeight, const std::string& scaleMode) {
        ImGuiIO& io = ImGui::GetIO();
        float physicalWidth = io.DisplaySize.x * io.DisplayFramebufferScale.x;
        float physicalHeight = io.DisplaySize.y * io.DisplayFramebufferScale.y;
        float scale = (float) std::max(1, guiScale());
        float fit = std::min(physicalWidth / (shellWidth * 1.12f), physicalHeight / (shellHeight * 1.12f));
        float target = clamp(fit, 0.75f, 2.0f);
        if (!scaleMode.empty() && scaleMode != "Auto") {
            int percent = 100;
            Ui::parseInt(Ui::replaceAll(scaleMode, "%", ""), percent);
            float configured = percent / 100.0f;
            float maxFit = std::min(physicalWidth / shellWidth, physicalHeight / shellHeight);
            target = std::min(configured, maxFit);
        }
        return target / scale;
    }

    float pixelScale() {
        return (float) std::max(1, guiScale());
    }

    float scale() {
        return uiScale * scaleFactor;
    }

    float stableScale() {
        return uiScale;
    }

    namespace {
        bool scaling() {
            return std::fabs(scaleFactor - 1.0f) >= 0.001f;
        }

        float edge(float gui, float pixel) {
            return scaling() ? gui : Ui::round(gui * pixel) / pixel;
        }

        float edgeDown(float gui, float pixel) {
            return scaling() ? gui : (float) std::floor(gui * pixel) / pixel;
        }
    }

    float transformX(float x) {
        return pivotX + (x * uiScale + offsetX - pivotX) * scaleFactor;
    }

    float transformY(float y) {
        return pivotY + (y * uiScale + offsetY - pivotY) * scaleFactor;
    }

    float transformW(float w) {
        return w * uiScale * scaleFactor;
    }

    uint32_t applyAlpha(uint32_t rgba) {
        if (alphaMul >= 0.999f) {
            return rgba;
        }
        int a = (rgba >> 24) & 0xFF;
        return (rgba & 0x00FFFFFFu) | ((uint32_t) Ui::round(a * std::max(0.0f, std::min(1.0f, alphaMul))) << 24);
    }

    void rect(float x, float y, float w, float h, uint32_t rgba) {
        roundRect(x, y, w, h, 0, rgba, 0xF);
    }

    void roundRect(float x, float y, float w, float h, float radius, uint32_t rgba) {
        roundRect(x, y, w, h, radius, rgba, 0xF);
    }

    void roundRect(float x, float y, float w, float h, float radius, uint32_t rgba, int cornerMask) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRect(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), color,
                              cornerMask);
    }

    void outline(float x, float y, float w, float h, float radius, uint32_t rgba) {
        outline(x, y, w, h, radius, rgba, 0xF, 1.0f);
    }

    void outline(float x, float y, float w, float h, float radius, uint32_t rgba, int cornerMask, float thickness) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        float pixel = pixelScale();
        float left = edge(transformX(x), pixel);
        float top = edge(transformY(y), pixel);
        float right = edge(transformX(x + w), pixel);
        float bottom = edge(transformY(y + h), pixel);
        float line = std::max(1, Ui::round(thickness)) / pixel;
        ShaderUtil::roundRectRing(left, top, right - left, bottom - top, transformW(radius), line, color, cornerMask);
    }

    void outlineGradient(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom,
                         float thickness) {
        uint32_t a = applyAlpha(top);
        uint32_t b = applyAlpha(bottom);
        if (((a >> 24) & 0xFF) == 0 && ((b >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRectRing(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                                  std::max(1, Ui::round(thickness)) / pixelScale(), a, b, 0xF);
    }

    void shell(float x, float y, float w, float h, float split, float radius, uint32_t plateRgba, uint32_t mainRgba) {
        ShaderUtil::shell(transformX(x), transformY(y), transformW(w), transformW(h), transformW(split),
                          transformW(radius), applyAlpha(plateRgba), applyAlpha(mainRgba));
    }

    void shell(float x, float y, float w, float h, float split, float radius, uint32_t plateRgba, uint32_t mainRgba,
               float outerRadius) {
        ShaderUtil::shell(transformX(x), transformY(y), transformW(w), transformW(h), transformW(split),
                          transformW(radius), applyAlpha(plateRgba), applyAlpha(mainRgba), transformW(outerRadius));
    }

    void cliIcon(float cx, float cy, float size, int kind, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::icon(transformX(cx - size * 0.5f), transformY(cy - size * 0.5f), transformW(size), kind, color);
    }

    void checkMark(float cx, float cy, float size, float thickness, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::checkMark(transformX(cx), transformY(cy), transformW(size), transformW(thickness), color);
    }

    void outlineFade(float x, float y, float w, float h, float radius, uint32_t rgba, float thickness, float fadeEnd) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRectRing(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                                  std::max(1, Ui::round(thickness)) / pixelScale(), color, color & 0xFFFFFFu, 0xF,
                                  fadeEnd, true);
    }

    void shadow(float x, float y, float w, float h, float radius, float spread, float offsetY, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRectShadow(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                                    transformW(spread), transformW(offsetY), color);
    }

    void circle(float cx, float cy, float radius, uint32_t rgba) {
        roundRect(cx - radius, cy - radius, radius * 2, radius * 2, radius, rgba, 0xF);
    }

    void hLine(float x0, float x1, float y, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        float scale = pixelScale();
        float py = edgeDown(transformY(y), scale);
        float thick = 1.0f / scale;
        ShaderUtil::roundRect(transformX(x0), py, transformW(x1 - x0), std::max(thick, transformW(0) + thick), 0, color,
                              0);
    }

    void vLine(float x, float y0, float y1, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        float scale = pixelScale();
        float px = edgeDown(transformX(x), scale);
        float thick = 1.0f / scale;
        ShaderUtil::roundRect(px, transformY(y0), std::max(thick, thick), transformW(y1 - y0), 0, color, 0);
    }

    void dashedOutline(float x, float y, float w, float h, float radius, uint32_t rgba, float dash, float gap) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        float pixel = pixelScale();
        float left = edge(transformX(x), pixel);
        float top = edge(transformY(y), pixel);
        float right = edge(transformX(x + w), pixel);
        float bottom = edge(transformY(y + h), pixel);
        ShaderUtil::roundRectRing(left, top, right - left, bottom - top, transformW(radius), 1.0f / pixel, color, color,
                                  0xF, 1.0f, false, transformW(dash), transformW(gap));
    }

    void line(float x0, float y0, float x1, float y1, uint32_t rgba, float thickness) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        float dx = x1 - x0;
        float dy = y1 - y0;
        float length = (float) std::sqrt(dx * dx + dy * dy);
        if (length < 0.0001f) {
            return;
        }
        float nx = -dy / length * thickness * 0.5f;
        float ny = dx / length * thickness * 0.5f;
        RenderUtil::triangle(transformX(x0 + nx), transformY(y0 + ny), transformX(x1 + nx), transformY(y1 + ny),
                             transformX(x1 - nx), transformY(y1 - ny), color);
        RenderUtil::triangle(transformX(x0 + nx), transformY(y0 + ny), transformX(x1 - nx), transformY(y1 - ny),
                             transformX(x0 - nx), transformY(y0 - ny), color);
    }

    void blur(float x, float y, float w, float h, float radius, float blurRadius) {
        blur(x, y, w, h, radius, blurRadius, false);
    }

    void blur(float x, float y, float w, float h, float radius, float blurRadius, bool recapture) {
        blur(x, y, w, h, radius, blurRadius, recapture, 0xF);
    }

    void blur(float x, float y, float w, float h, float radius, float blurRadius, bool recapture, int cornerMask) {
        int alpha = Ui::round(255 * std::max(0.0f, std::min(1.0f, alphaMul)));
        if (alpha == 0) {
            return;
        }
        ShaderUtil::kawaseBlur(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                               blurRadius, ((uint32_t) alpha << 24) | 0xFFFFFFu, cornerMask, recapture);
    }

    void gaussianBlur(float x, float y, float w, float h, float radius, float blurRadius) {
        int alpha = Ui::round(255 * std::max(0.0f, std::min(1.0f, alphaMul)));
        if (alpha == 0) {
            return;
        }
        ShaderUtil::blur(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), blurRadius,
                         ((uint32_t) alpha << 24) | 0xFFFFFFu, 0xF, 6.0f);
    }

    void queueBlur(float x, float y, float w, float h, float radius, float blurRadius, bool recapture, int cornerMask) {
        int alpha = Ui::round(255 * std::max(0.0f, std::min(1.0f, alphaMul)));
        if (alpha == 0) {
            return;
        }
        ShaderUtil::queueBlur(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                              blurRadius, ((uint32_t) alpha << 24) | 0xFFFFFFu, cornerMask, 6.0f, recapture);
    }

    void bloom(float x, float y, float w, float h, float radius, float spread, float intensity, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRectBloom(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                                   transformW(spread), intensity, color);
    }

    void queueBloom(float x, float y, float w, float h, float radius, float spread, float intensity, uint32_t rgba) {
        int alpha = Ui::round(((rgba >> 24) & 0xFF) * std::max(0.0f, std::min(1.0f, alphaMul)));
        if (alpha == 0) {
            return;
        }
        ShaderUtil::queueBloom(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius),
                               transformW(spread), intensity, ((uint32_t) alpha << 24) | (rgba & 0xFFFFFFu));
    }

    void svField(float x, float y, float w, float h, float radius, float hue) {
        ShaderUtil::pickerSurface(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), hue,
                                  0, std::max(0.0f, std::min(1.0f, alphaMul)));
    }

    void hueField(float x, float y, float w, float h, float radius) {
        ShaderUtil::pickerSurface(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), 0, 1,
                                  std::max(0.0f, std::min(1.0f, alphaMul)));
    }

    void alphaField(float x, float y, float w, float h, float radius, float hue, float sat, float val) {
        ShaderUtil::pickerSurface(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), hue,
                                  sat, val, 2, std::max(0.0f, std::min(1.0f, alphaMul)));
    }

    void roundRectGradient(float x, float y, float w, float h, float radius, uint32_t left, uint32_t right) {
        uint32_t a = applyAlpha(left);
        uint32_t b = applyAlpha(right);
        if (((a >> 24) & 0xFF) == 0 && ((b >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRect(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), a, b,
                              1.0f, 0xF);
    }

    void roundRectGradientV(float x, float y, float w, float h, float radius, uint32_t top, uint32_t bottom) {
        uint32_t a = applyAlpha(top);
        uint32_t b = applyAlpha(bottom);
        if (((a >> 24) & 0xFF) == 0 && ((b >> 24) & 0xFF) == 0) {
            return;
        }
        ShaderUtil::roundRect(transformX(x), transformY(y), transformW(w), transformW(h), transformW(radius), a, b,
                              -1.0f, 0xF);
    }

    void gradientH(float x, float y, float w, float h, uint32_t left, uint32_t right) {
        uint32_t a = applyAlpha(left);
        uint32_t b = applyAlpha(right);
        if (((a >> 24) & 0xFF) == 0 && ((b >> 24) & 0xFF) == 0) {
            return;
        }
        RenderUtil::gradientH(transformX(x), transformY(y), transformW(w), transformW(h), a, b);
    }

    void texture(uint32_t location, float x, float y, float w, float h) {
        texture(location, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
    }

    void texture(uint32_t location, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
        texture(location, x, y, w, h, u0, v0, u1, v1, 0xFFFFFFFFu);
    }

    void texture(uint32_t location, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 uint32_t tint) {
        if (location == 0) {
            return;
        }
        int alpha = Ui::round(std::max(0.0f, std::min(1.0f, alphaMul)) * (float) ((tint >> 24) & 0xFF));
        if (alpha == 0) {
            return;
        }
        flush();
        ImDrawList* dl = drawList();
        float drawX = transformX(x);
        float drawY = transformY(y);
        float drawW = scaling() ? transformW(w) : (float) Ui::round(transformW(w));
        float drawH = scaling() ? transformW(h) : (float) Ui::round(transformW(h));
        float u = unit();
        dl->AddImage(ImTextureRef((ImTextureID) (intptr_t) location), ImVec2(drawX * u, drawY * u),
                     ImVec2((drawX + drawW) * u, (drawY + drawH) * u), ImVec2(u0, v0), ImVec2(u1, v1),
                     IM_COL32((tint >> 16) & 0xFF, (tint >> 8) & 0xFF, tint & 0xFF, alpha));
    }

    void roundTexture(uint32_t location, float x, float y, float w, float h, float radius, float fadeWidth,
                      uint32_t fadeRgba, float textureW, float textureH) {
        roundTexture(location, x, y, w, h, radius, fadeWidth, fadeRgba, textureW, textureH, 0xF);
    }

    void roundTexture(uint32_t location, float x, float y, float w, float h, float radius, float fadeWidth,
                      uint32_t fadeRgba, float textureW, float textureH, int cornerMask) {
        if (location == 0) {
            return;
        }
        float alpha = std::max(0.0f, std::min(1.0f, alphaMul));
        if (alpha <= 0.0f) {
            return;
        }
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (textureW > 0 && textureH > 0 && w > 0 && h > 0) {
            float boxAspect = w / h;
            float texAspect = textureW / textureH;
            if (texAspect > boxAspect) {
                scaleX = boxAspect / texAspect;
            } else {
                scaleY = texAspect / boxAspect;
            }
        }
        ShaderUtil::roundTexture(location, transformX(x), transformY(y), transformW(w), transformW(h),
                                 transformW(radius), cornerMask, alpha, transformW(fadeWidth), fadeRgba, scaleX,
                                 scaleY);
    }

    void item(const std::string& stack, float x, float y) {
        item(stack, x, y, 1.0f);
    }

    void itemSmooth(const std::string& stack, float cx, float cy, float itemScale) {
        uint32_t tex = UiImages::textureOf(stack);
        if (tex == 0) {
            return;
        }
        int alpha = Ui::round(std::max(0.0f, std::min(1.0f, alphaMul)) * 255);
        if (alpha == 0) {
            return;
        }
        float sc = scale() * itemScale;
        float sx = transformX(cx) - sc * 8;
        float sy = transformY(cy) - sc * 8;
        flush();
        float u = unit();
        drawList()->AddImage(ImTextureRef((ImTextureID) (intptr_t) tex), ImVec2(sx * u, sy * u),
                             ImVec2((sx + 16 * sc) * u, (sy + 16 * sc) * u), ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32(255, 255, 255, alpha));
    }

    void itemSilhouette(const std::string& stack, float cx, float cy, float itemScale, uint32_t color, bool flip,
                        bool shadow) {
        UiImages::Resolved silhouette{0, 0, 0};
        if (!UiImages::silhouetteResolver || !UiImages::silhouetteResolver(stack, silhouette) ||
            silhouette.texture == 0) {
            return;
        }
        float size = scale() * itemScale * 16;
        float x = transformX(cx) - size / 2;
        float y = transformY(cy) - size / 2;
        float u0 = flip ? 1.0f : 0.0f;
        float u1 = flip ? 0.0f : 1.0f;
        if (shadow) {
            float offset = std::max(0.5f, size * 0.06f);
            ShaderUtil::roundTex(x + offset, y + offset, size, size, 0, silhouette.texture, u0, 0.0f, u1, 1.0f,
                                 applyAlpha(0xb0000000u));
        }
        ShaderUtil::roundTex(x, y, size, size, 0, silhouette.texture, u0, 0.0f, u1, 1.0f, applyAlpha(color));
    }

    void item(const std::string& stack, float cx, float cy, float itemScale) {
        uint32_t tex = UiImages::textureOf(stack);
        if (tex == 0) {
            return;
        }
        float pixel = pixelScale();
        float raw = scale() * itemScale;
        float physical = raw * pixel;
        float snapped = std::max(1.0f, (float) Ui::round(physical));
        float sc = snapped / pixel;
        float sx = Ui::round(transformX(cx) * pixel - snapped * 8) / pixel;
        float sy = Ui::round(transformY(cy) * pixel - snapped * 8) / pixel;
        int alpha = Ui::round(std::max(0.0f, std::min(1.0f, alphaMul)) * 255);
        if (alpha == 0) {
            return;
        }
        flush();
        float u = unit();
        drawList()->AddImage(ImTextureRef((ImTextureID) (intptr_t) tex), ImVec2(sx * u, sy * u),
                             ImVec2((sx + 16 * sc) * u, (sy + 16 * sc) * u), ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32(255, 255, 255, alpha));
    }

    void text(const std::string& value, float x, float y, uint32_t rgba, int family, float size) {
        UiFont::draw(value, x, y, rgba, family, size);
    }

    void textClipped(const std::string& value, float x, float y, float h, float maxWidth, uint32_t rgba, int family,
                     float size) {
        textClipped(value, x, y, h, maxWidth, rgba, family, size, 0);
    }

    void textClipped(const std::string& value, float x, float y, float h, float maxWidth, uint32_t rgba, int family,
                     float size, uint32_t fadeBackground) {
        float textWidth = UiFont::width(value, family, size);
        bool overflow = textWidth > maxWidth;
        int scale = std::max(1, guiScale());
        int sx = (int) (transformX(x) * scale);
        int sy = (int) (transformY(y) * scale);
        int ex = overflow && fadeBackground != 0 ? (int) std::floor(transformX(x + maxWidth) * scale)
                                                 : (int) std::ceil(transformX(x + maxWidth) * scale);
        int ey = (int) std::ceil(transformY(y + h) * scale);
        enableScissor(sx / scale, sy / scale, overflow && fadeBackground != 0 ? ex / scale : (ex + scale - 1) / scale,
                      ey / scale);
        UiFont::draw(value, x, y + (h - size) * 0.5f, rgba, family, size);
        disableScissor();
        if (overflow && fadeBackground != 0) {
            float fadeW = std::min(18.0f, maxWidth * 0.45f);
            roundRectGradient(x + maxWidth - fadeW, y + 1, fadeW, h - 2, 0, fadeBackground & 0x00FFFFFFu,
                              fadeBackground);
        }
    }

    void textY(const std::string& value, float x, float y, float h, uint32_t rgba, int family, float size) {
        UiFont::draw(value, x, y + (h - size) * 0.5f, rgba, family, size);
    }

    uint32_t over(uint32_t top, uint32_t bottom) {
        float topAlpha = ((top >> 24) & 0xFF) / 255.0f;
        float bottomAlpha = ((bottom >> 24) & 0xFF) / 255.0f * (1.0f - topAlpha);
        float alpha = topAlpha + bottomAlpha;
        if (alpha <= 0.0f) {
            return 0;
        }
        int r = Ui::round((((top >> 16) & 0xFF) * topAlpha + ((bottom >> 16) & 0xFF) * bottomAlpha) / alpha);
        int g = Ui::round((((top >> 8) & 0xFF) * topAlpha + ((bottom >> 8) & 0xFF) * bottomAlpha) / alpha);
        int b = Ui::round(((top & 0xFF) * topAlpha + (bottom & 0xFF) * bottomAlpha) / alpha);
        return ((uint32_t) Ui::round(alpha * 255.0f) << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
    }

    uint32_t mix(uint32_t a, uint32_t b, float t) {
        float f = std::max(0.0f, std::min(1.0f, t));
        int aa = (a >> 24) & 0xFF, ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
        int ba = (b >> 24) & 0xFF, br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        int ra = Ui::round(aa + (ba - aa) * f);
        int rr = Ui::round(ar + (br - ar) * f);
        int rg = Ui::round(ag + (bg - ag) * f);
        int rb = Ui::round(ab + (bb - ab) * f);
        return ((uint32_t) ra << 24) | ((uint32_t) rr << 16) | ((uint32_t) rg << 8) | (uint32_t) rb;
    }

    uint32_t color(int r, int g, int b, int a) {
        return ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
    }

    uint32_t color(int r, int g, int b) {
        return color(r, g, b, 255);
    }

    float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    float clamp(float value, float min, float max) {
        return value < min ? min : (value > max ? max : value);
    }

    float saturate(float value) {
        return clamp(value, 0, 1);
    }

    float deltaTime() {
        return std::min(1.0f / 30.0f, ImGui::GetIO().DeltaTime);
    }

    float approach01(float current, float target, float speed) {
        float next = lerp(current, target, 1.0f - (float) std::exp(-speed * deltaTime()));
        if (std::fabs(next - target) < 0.002f) {
            return target;
        }
        UiState::keepAnimating();
        return next;
    }

    float snapLayout(float value) {
        float pixels = uiScale * pixelScale();
        return pixels > 0.0f ? std::round(value * pixels) / pixels : value;
    }

    float approach(float current, float target, float speed) {
        float next = lerp(current, target, 1.0f - (float) std::exp(-speed * deltaTime()));
        if (std::fabs(next - target) < 0.25f) {
            return target;
        }
        UiState::keepAnimating();
        return next;
    }

    void triangle(float cx, float cy, float size, float angle, uint32_t rgba) {
        uint32_t color = applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        double radians = angle * 3.14159265358979323846 / 180.0;
        float cosv = (float) std::cos(radians);
        float sinv = (float) std::sin(radians);
        float tx = transformX(cx);
        float ty = transformY(cy);
        float s = transformW(size);
        float x1 = tx + s * sinv;
        float y1 = ty - s * cosv;
        float x2 = tx - s * cosv - s * sinv;
        float y2 = ty - s * sinv + s * cosv;
        float x3 = tx + s * cosv - s * sinv;
        float y3 = ty + s * sinv + s * cosv;
        RenderUtil::smoothTriangle(x1, y1, x2, y2, x3, y3, color);
    }

    void scissor(float x, float y, float w, float h) {
        int sx = (int) std::floor(transformX(x));
        int sy = (int) std::floor(transformY(y));
        int ex = (int) std::ceil(transformX(x + w));
        int ey = (int) std::ceil(transformY(y + h));
        enableScissor(sx, sy, std::max(sx, ex), std::max(sy, ey));
    }

    void unscissor() {
        disableScissor();
    }

    void enableScissor(int x0, int y0, int x1, int y1) {
        flush();
        float u = unit();
        drawList()->PushClipRect(ImVec2(x0 * u, y0 * u), ImVec2(std::max(x0, x1) * u, std::max(y0, y1) * u), true);
    }

    void disableScissor() {
        flush();
        drawList()->PopClipRect();
    }

    float screenWidth() {
        return guiWidth();
    }

    float screenHeight() {
        return guiHeight();
    }
}
