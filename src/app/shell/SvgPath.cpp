#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "app/shell/SvgPath.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiRender.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace SvgPath {

    namespace {

        constexpr int MAX_SIZE = 512;
        constexpr size_t MAX_TEXTURES = 256;
        constexpr size_t MAX_SHAPES = 512;
        constexpr float TOLERANCE = 0.04f;

        struct Raster {
            int width = 0;
            int height = 0;
            int textureWidth = 0;
            int textureHeight = 0;
            bool uniform = true;
            uint32_t texture = 0;
            uint64_t used = 0;
        };

        struct Shape {
            NSVGimage* image = nullptr;
            bool evenOdd = true;
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            uint64_t used = 0;
            std::vector<Raster> rasters;
        };

        std::unordered_map<std::string, Shape> shapes;
        size_t textureCount = 0;
        uint64_t tick = 0;
        NSVGrasterizer* rasterizer = nullptr;

        bool isSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        bool isDigit(char c) {
            return c >= '0' && c <= '9';
        }

        bool isCommand(char c) {
            return c != '\0' && std::strchr("MmLlHhVvCcSsQqTtAaZz", c) != nullptr;
        }

        std::string sanitize(const std::string& data, bool& evenOdd) {
            evenOdd = true;
            size_t i = 0;
            while (i < data.size() && isSpace(data[i])) {
                i++;
            }
            if (i < data.size() && data[i] == 'F') {
                size_t j = i + 1;
                while (j < data.size() && isSpace(data[j])) {
                    j++;
                }
                if (j >= data.size() || (data[j] != '0' && data[j] != '1')) {
                    return std::string();
                }
                evenOdd = data[j] == '0';
                i = j + 1;
            }
            std::string out;
            out.reserve(data.size() - i);
            char previous = ' ';
            for (; i < data.size(); i++) {
                char c = data[i];
                bool number = isDigit(c) || c == '.' || c == '+' || c == '-';
                bool exponent = (c == 'e' || c == 'E') && (isDigit(previous) || previous == '.');
                if (!number && !exponent && !isSpace(c) && c != ',' && !isCommand(c)) {
                    break;
                }
                out.push_back(isSpace(c) ? ' ' : c);
                previous = c;
            }
            return out;
        }

        void parse(Shape& shape, const std::string& data) {
            std::string path = sanitize(data, shape.evenOdd);
            if (path.find_first_not_of(' ') == std::string::npos) {
                return;
            }
            std::string document =
                    "<svg width=\"1\" height=\"1\" viewBox=\"0 0 1 1\"><path fill=\"#ffffff\" fill-rule=\"";
            document += shape.evenOdd ? "evenodd" : "nonzero";
            document += "\" d=\"";
            document += path;
            document += "\"/></svg>";
            NSVGimage* image = nsvgParse(document.data(), "px", 96.0f);
            if (image == nullptr) {
                return;
            }
            bool any = false;
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            for (NSVGshape* item = image->shapes; item != nullptr; item = item->next) {
                if (item->paths == nullptr) {
                    continue;
                }
                minX = any ? std::min(minX, item->bounds[0]) : item->bounds[0];
                minY = any ? std::min(minY, item->bounds[1]) : item->bounds[1];
                maxX = any ? std::max(maxX, item->bounds[2]) : item->bounds[2];
                maxY = any ? std::max(maxY, item->bounds[3]) : item->bounds[3];
                any = true;
            }
            if (!any || !(maxX - minX > 0.0f) || !(maxY - minY > 0.0f)) {
                nsvgDelete(image);
                return;
            }
            shape.image = image;
            shape.x = minX;
            shape.y = minY;
            shape.w = maxX - minX;
            shape.h = maxY - minY;
        }

        void release(uint32_t texture) {
            if (texture != 0 && UiGL::DeleteTextures != nullptr) {
                GLuint id = texture;
                UiGL::DeleteTextures(1, &id);
            }
        }

        void evictShape() {
            auto victim = shapes.end();
            for (auto it = shapes.begin(); it != shapes.end(); ++it) {
                if (victim == shapes.end() || it->second.used < victim->second.used) {
                    victim = it;
                }
            }
            if (victim == shapes.end()) {
                return;
            }
            for (Raster& raster : victim->second.rasters) {
                release(raster.texture);
                textureCount--;
            }
            if (victim->second.image != nullptr) {
                nsvgDelete(victim->second.image);
            }
            shapes.erase(victim);
        }

        void evictRaster() {
            Shape* owner = nullptr;
            size_t index = 0;
            for (auto& [key, shape] : shapes) {
                for (size_t i = 0; i < shape.rasters.size(); i++) {
                    if (owner == nullptr || shape.rasters[i].used < owner->rasters[index].used) {
                        owner = &shape;
                        index = i;
                    }
                }
            }
            if (owner == nullptr) {
                return;
            }
            release(owner->rasters[index].texture);
            owner->rasters.erase(owner->rasters.begin() + (std::ptrdiff_t) index);
            textureCount--;
        }

        Shape& lookup(const std::string& data) {
            tick++;
            auto it = shapes.find(data);
            if (it == shapes.end()) {
                if (shapes.size() >= MAX_SHAPES) {
                    evictShape();
                }
                it = shapes.emplace(data, Shape()).first;
                parse(it->second, data);
            }
            it->second.used = tick;
            return it->second;
        }

        void rasterize(Shape& shape, int width, int height, int stride, bool uniform, unsigned char* out) {
            if (rasterizer == nullptr) {
                rasterizer = nsvgCreateRasterizer();
                if (rasterizer == nullptr) {
                    return;
                }
                rasterizer->tessTol = TOLERANCE;
            }
            float sx = (float) width / shape.w;
            float sy = (float) height / shape.h;
            float ox = 0.0f;
            float oy = 0.0f;
            if (uniform) {
                float s = std::min(sx, sy);
                ox = ((float) width - shape.w * s) * 0.5f;
                oy = ((float) height - shape.h * s) * 0.5f;
                sx = s;
                sy = s;
            }
            ox -= shape.x * sx;
            oy -= shape.y * sy;
            size_t total = 0;
            for (NSVGshape* item = shape.image->shapes; item != nullptr; item = item->next) {
                for (NSVGpath* path = item->paths; path != nullptr; path = path->next) {
                    total += (size_t) path->npts * 2;
                }
            }
            std::vector<float> points(total);
            std::vector<float*> originals;
            size_t at = 0;
            for (NSVGshape* item = shape.image->shapes; item != nullptr; item = item->next) {
                for (NSVGpath* path = item->paths; path != nullptr; path = path->next) {
                    float* target = points.data() + at;
                    for (int i = 0; i < path->npts; i++) {
                        target[i * 2] = path->pts[i * 2] * sx + ox;
                        target[i * 2 + 1] = path->pts[i * 2 + 1] * sy + oy;
                    }
                    originals.push_back(path->pts);
                    path->pts = target;
                    at += (size_t) path->npts * 2;
                }
            }
            nsvgRasterize(rasterizer, shape.image, 0.0f, 0.0f, 1.0f, out, width, height, stride);
            size_t restored = 0;
            for (NSVGshape* item = shape.image->shapes; item != nullptr; item = item->next) {
                for (NSVGpath* path = item->paths; path != nullptr; path = path->next) {
                    path->pts = originals[restored++];
                }
            }
        }

        uint32_t render(Shape& shape, int width, int height, int textureWidth, int textureHeight, bool uniform) {
            for (Raster& raster : shape.rasters) {
                if (raster.width == width && raster.height == height && raster.textureWidth == textureWidth &&
                    raster.textureHeight == textureHeight && raster.uniform == uniform) {
                    raster.used = tick;
                    return raster.texture;
                }
            }
            if (textureCount >= MAX_TEXTURES) {
                evictRaster();
            }
            std::vector<unsigned char> pixels((size_t) textureWidth * (size_t) textureHeight * 4, 0);
            rasterize(shape, width, height, textureWidth * 4, uniform, pixels.data());
            for (size_t i = 0; i < pixels.size(); i += 4) {
                pixels[i] = 255;
                pixels[i + 1] = 255;
                pixels[i + 2] = 255;
            }
            uint32_t texture = UiImages::upload(pixels.data(), textureWidth, textureHeight);
            if (texture == 0) {
                return 0;
            }
            Raster raster;
            raster.width = width;
            raster.height = height;
            raster.textureWidth = textureWidth;
            raster.textureHeight = textureHeight;
            raster.uniform = uniform;
            raster.texture = texture;
            raster.used = tick;
            shape.rasters.push_back(raster);
            textureCount++;
            return texture;
        }

        int pixels(float value) {
            return (int) std::lround(std::clamp(value, 1.0f, (float) MAX_SIZE));
        }

        int padded(int value, int step) {
            return (value + step - 1) / step * step;
        }
    }

    bool valid(const std::string& data) {
        return lookup(data).image != nullptr;
    }

    bool bounds(const std::string& data, float& x, float& y, float& w, float& h) {
        Shape& shape = lookup(data);
        if (shape.image == nullptr) {
            return false;
        }
        x = shape.x;
        y = shape.y;
        w = shape.w;
        h = shape.h;
        return true;
    }

    void draw(const std::string& data, float x, float y, float w, float h, uint32_t rgba, bool uniform) {
        if (!(w > 0.0f) || !(h > 0.0f) || ((rgba >> 24) & 0xFF) == 0) {
            return;
        }
        Shape& shape = lookup(data);
        if (shape.image == nullptr) {
            return;
        }
        float grid = UiRender::pixelScale();
        float k = UiRender::uiScale * grid;
        if (!(k > 0.0f)) {
            return;
        }
        float rawW = w * k;
        float rawH = h * k;
        int width = pixels(rawW);
        int height = pixels(rawH);
        int step = std::max(1, (int) std::lround(grid));
        int textureWidth = padded(width, step);
        int textureHeight = padded(height, step);
        uint32_t texture = render(shape, width, height, textureWidth, textureHeight, uniform);
        if (texture == 0) {
            return;
        }
        bool scaling = std::fabs(UiRender::scaleFactor - 1.0f) >= 0.001f;
        bool exact = rawW >= 0.5f && rawH >= 0.5f && rawW < (float) MAX_SIZE + 0.5f && rawH < (float) MAX_SIZE + 0.5f;
        if (scaling || !exact) {
            UiRender::texture(texture, x, y, w, h, 0.0f, 0.0f, (float) width / (float) textureWidth,
                              (float) height / (float) textureHeight, rgba);
            return;
        }
        float shiftX = UiRender::offsetX / UiRender::uiScale;
        float shiftY = UiRender::offsetY / UiRender::uiScale;
        float left = UiRender::snapLayout(x + shiftX) - shiftX;
        float top = UiRender::snapLayout(y + shiftY) - shiftY;
        UiRender::texture(texture, left, top, (float) textureWidth / k, (float) textureHeight / k, 0.0f, 0.0f, 1.0f,
                          1.0f, rgba);
    }
}
