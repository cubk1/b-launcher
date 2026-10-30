#include "ui/imgui/screens/shell/Logo.h"
#include "app/system/EmbeddedIcons.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"

#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Logo {

    namespace {
        constexpr unsigned int MID = 0x6366F1u;
        constexpr unsigned int DARK = 0x3730A3u;
        constexpr unsigned int LIGHT = 0xA5B4FCu;
        constexpr size_t CACHE_SIZE = 8;

        struct Entry {
            int pixels = 0;
            uint32_t accent = 0;
            uint32_t texture = 0;
        };

        std::vector<Entry> cache;
        NSVGimage* image = nullptr;
        NSVGrasterizer* rasterizer = nullptr;
        bool failed = false;

        bool load() {
            if (image != nullptr || failed) {
                return image != nullptr;
            }
            std::string text((const char*) EmbeddedIcons::logoSvg, EmbeddedIcons::logoSvgSize);
            image = nsvgParse(text.data(), "px", 96.0f);
            rasterizer = nsvgCreateRasterizer();
            failed = image == nullptr || rasterizer == nullptr || image->shapes == nullptr || image->width <= 0.0f;
            return !failed;
        }

        unsigned int rgbOf(unsigned int abgr) {
            return (abgr & 0xFFu) << 16 | (abgr & 0xFF00u) | (abgr >> 16 & 0xFFu);
        }

        unsigned int abgrOf(uint32_t argb) {
            return 0xFF000000u | (argb & 0xFFu) << 16 | (argb & 0xFF00u) | (argb >> 16 & 0xFFu);
        }

        unsigned int recolor(unsigned int abgr, uint32_t mid, uint32_t dark, uint32_t light) {
            unsigned int rgb = rgbOf(abgr);
            uint32_t target = rgb == MID ? mid : rgb == DARK ? dark : rgb == LIGHT ? light : 0;
            return target != 0 ? (abgr & 0xFF000000u) | (abgrOf(target) & 0x00FFFFFFu) : abgr;
        }

        std::vector<unsigned char> rasterize(int pixels, uint32_t accent) {
            uint32_t mid = accent | 0xFF000000u;
            uint32_t dark = UiRender::mix(mid, 0xFF000000u, 0.42f) | 0xFF000000u;
            uint32_t light = UiRender::mix(mid, 0xFFFFFFFFu, 0.45f) | 0xFF000000u;
            std::vector<unsigned int> fills;
            std::vector<unsigned int> strokes;
            std::vector<unsigned char> flags;
            for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
                fills.push_back(shape->fill.color);
                strokes.push_back(shape->stroke.color);
                flags.push_back(shape->flags);
                if (shape->fill.type == NSVG_PAINT_COLOR) {
                    shape->fill.color = recolor(shape->fill.color, mid, dark, light);
                }
                if (shape->stroke.type == NSVG_PAINT_COLOR) {
                    shape->stroke.color = recolor(shape->stroke.color, mid, dark, light);
                }
            }
            float scale = (float) pixels / image->width;
            size_t bytes = (size_t) pixels * pixels * 4;
            std::vector<unsigned char> rgba(bytes, 0);
            std::vector<unsigned char> mask(bytes, 0);
            nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, rgba.data(), pixels, pixels, pixels * 4);
            for (NSVGshape* shape = image->shapes->next; shape != nullptr; shape = shape->next) {
                shape->flags = (unsigned char) (shape->flags & ~NSVG_FLAGS_VISIBLE);
            }
            nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, mask.data(), pixels, pixels, pixels * 4);
            size_t index = 0;
            for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next, index++) {
                shape->fill.color = fills[index];
                shape->stroke.color = strokes[index];
                shape->flags = flags[index];
            }
            for (size_t i = 3; i < bytes; i += 4) {
                rgba[i] = (unsigned char) ((rgba[i] * mask[i] + 127) / 255);
            }
            return rgba;
        }

        uint32_t render(int pixels, uint32_t accent) {
            std::vector<unsigned char> rgba = rasterize(pixels, accent);
            return UiImages::upload(rgba.data(), pixels, pixels);
        }

        uint32_t texture(int pixels, uint32_t accent) {
            for (size_t i = 0; i < cache.size(); i++) {
                if (cache[i].pixels == pixels && cache[i].accent == accent) {
                    Entry hit = cache[i];
                    cache.erase(cache.begin() + (long long) i);
                    cache.push_back(hit);
                    return hit.texture;
                }
            }
            Entry entry;
            entry.pixels = pixels;
            entry.accent = accent;
            entry.texture = render(pixels, accent);
            if (cache.size() >= CACHE_SIZE) {
                GLuint old = cache.front().texture;
                if (old != 0) {
                    UiGL::DeleteTextures(1, &old);
                }
                cache.erase(cache.begin());
            }
            cache.push_back(entry);
            return entry.texture;
        }
    }

    std::vector<unsigned char> pixels(int size) {
        if (size <= 0 || !load()) {
            return {};
        }
        return rasterize(size, UiTheme::accent() | 0xFF000000u);
    }

    void draw(float x, float y, float size, float alpha) {
        if (size <= 0.0f || alpha <= 0.0f || !load()) {
            return;
        }
        float step = UiRender::pixelScale();
        float k = UiRender::uiScale * step;
        int pixels = std::clamp((int) std::lround(size * k / step) * (int) step, (int) step, 1024);
        uint32_t id = texture(pixels, UiTheme::accent() | 0xFF000000u);
        if (id == 0) {
            return;
        }
        float shown = (float) pixels / k;
        float dx = x + (size - shown) * 0.5f;
        float dy = y + (size - shown) * 0.5f;
        if (UiRender::scaleFactor == 1.0f) {
            dx = UiRender::snapLayout(dx);
            dy = UiRender::snapLayout(dy);
        }
        float saved = UiRender::alphaMul;
        UiRender::alphaMul = saved * alpha;
        UiRender::texture(id, dx, dy, shown, shown);
        UiRender::alphaMul = saved;
    }
}
