#include "ui/imgui/screens/theme/ThemePaint.h"

#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiState.h"
#include "ui/imgui/panel/UiTheme.h"
#include "ui/imgui/screens/theme/ThemeAssets.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeShaders.h"
#include "ui/imgui/panel/UiCommon.h"
#include "ui/imgui/theme/ColorMath.h"

#include <algorithm>
#include <cmath>

namespace ThemePaint {

    namespace {
        using ThemePaintSpec::Blend;
        using ThemePaintSpec::Fit;
        using ThemePaintSpec::Kind;
        using ThemePaintSpec::Layer;
        using ThemePaintSpec::Motion;
        using ThemePaintSpec::Region;

        constexpr float PI = 3.14159265358979f;

        uint32_t standIn(Region region) {
            switch (region) {
                case ThemePaintSpec::WINDOW: return UiTheme::shell();
                case ThemePaintSpec::TITLEBAR:
                case ThemePaintSpec::SIDEBAR: return UiTheme::sidebar();
                case ThemePaintSpec::CONTENT: return UiTheme::panel();
                case ThemePaintSpec::MENU: return UiTheme::menu();
                case ThemePaintSpec::PRIMARY_BUTTON: return UiTheme::accent();
                case ThemePaintSpec::NAV_ACTIVE: return UiTheme::navHover();
                default: return UiTheme::surface();
            }
        }

        float phase(const Layer& layer) {
            double cycles = ThemeRuntime::time() / std::max(0.5f, layer.period);
            return (float) (cycles - std::floor(cycles));
        }

        int blendOf(Blend blend) {
            switch (blend) {
                case Blend::Multiply: return ShaderUtil::BLEND_MULTIPLY;
                case Blend::Screen: return ShaderUtil::BLEND_SCREEN;
                case Blend::Add: return ShaderUtil::BLEND_ADD;
                default: return ShaderUtil::BLEND_NORMAL;
            }
        }

        void colors(const Layer& layer, ShaderUtil::Paint& paint) {
            float hue = layer.motion == Motion::Hue ? phase(layer) * 360.0f : 0.0f;
            paint.count = (int) std::min(layer.stops.size(), (size_t) ShaderUtil::PAINT_STOPS);
            for (int k = 0; k < paint.count; k++) {
                uint32_t color = layer.stops[(size_t) k].color;
                paint.colors[k] = hue != 0.0f ? ColorMath::rotate(color, hue) : color;
                paint.stops[k] = layer.stops[(size_t) k].at;
            }
            if (layer.motion == Motion::Shift) {
                paint.cycle = true;
                paint.shift = phase(layer);
            }
        }

        bool image(const Layer& layer, const float rect[4], ShaderUtil::Paint& paint) {
            const ThemeAssets::Texture& texture = ThemeAssets::get(layer.source);
            if (!texture.ready || texture.width <= 0 || texture.height <= 0) {
                return false;
            }
            float unit = UiRender::transformW(1.0f);
            float width = rect[2];
            float height = rect[3];
            float drawW = texture.width * unit * layer.scale;
            float drawH = texture.height * unit * layer.scale;
            if (layer.fit == Fit::Cover || layer.fit == Fit::Contain) {
                float fit = layer.fit == Fit::Cover ? std::max(width / texture.width, height / texture.height)
                                                    : std::min(width / texture.width, height / texture.height);
                drawW = texture.width * fit * layer.scale;
                drawH = texture.height * fit * layer.scale;
            } else if (layer.fit == Fit::Stretch) {
                drawW = width * layer.scale;
                drawH = height * layer.scale;
            }
            if (drawW <= 0.0f || drawH <= 0.0f) {
                return false;
            }
            float originX = rect[0] + (width - drawW) * layer.align[0] + layer.offset[0] * unit;
            float originY = rect[1] + (height - drawH) * layer.align[1] + layer.offset[1] * unit;
            if (layer.fit == Fit::Tile && layer.motion == Motion::Shift) {
                float angle = layer.angle * PI / 180.0f;
                float travel = phase(layer);
                originX += std::sin(angle) * travel * drawW;
                originY -= std::cos(angle) * travel * drawH;
            }
            paint.kind = ShaderUtil::PAINT_IMAGE;
            paint.texture = texture.id;
            std::copy(rect, rect + 4, paint.frame);
            paint.uv[0] = width / drawW;
            paint.uv[1] = height / drawH;
            paint.uv[2] = (rect[0] - originX) / drawW;
            paint.uv[3] = (rect[1] - originY) / drawH;
            paint.tile = layer.fit == Fit::Tile;
            paint.bounded = layer.fit == Fit::Contain;
            paint.tint = layer.tint;
            if (layer.motion == Motion::Hue) {
                paint.tint = ColorMath::rotate(layer.tint, phase(layer) * 360.0f);
            }
            return true;
        }

        void gradientAxis(const Layer& layer, const float rect[4], ShaderUtil::Paint& paint) {
            float cx = rect[0] + rect[2] * 0.5f;
            float cy = rect[1] + rect[3] * 0.5f;
            if (layer.kind == Kind::Radial) {
                paint.kind = ShaderUtil::PAINT_RADIAL;
                paint.axis[0] = rect[0] + rect[2] * layer.center[0];
                paint.axis[1] = rect[1] + rect[3] * layer.center[1];
                paint.axis[2] = layer.radius * std::max(rect[2], rect[3]);
                return;
            }
            paint.kind = ShaderUtil::PAINT_LINEAR;
            float degrees = layer.angle + (layer.motion == Motion::Rotate ? phase(layer) * 360.0f : 0.0f);
            float angle = degrees * PI / 180.0f;
            float dx = std::sin(angle);
            float dy = -std::cos(angle);
            float half = (std::fabs(rect[2] * dx) + std::fabs(rect[3] * dy)) * 0.5f;
            paint.axis[0] = cx - dx * half;
            paint.axis[1] = cy - dy * half;
            paint.axis[2] = cx + dx * half;
            paint.axis[3] = cy + dy * half;
        }
    }

    bool custom(Region region) {
        return ThemePaintSpec::custom(region) && !ShaderUtil::paintBroken();
    }

    void layers(Region region, const ShaderUtil::Shape& shape, const ShaderUtil::Shape* clip,
                const ShaderUtil::Shape* hole, float opacity) {
        if (opacity <= 0.0f) {
            return;
        }
        float bounds[4] = {shape.rect[0], shape.rect[1], shape.rect[2], shape.rect[3]};
        if (clip != nullptr) {
            float left = std::max(bounds[0], clip->rect[0]);
            float top = std::max(bounds[1], clip->rect[1]);
            float right = std::min(bounds[0] + bounds[2], clip->rect[0] + clip->rect[2]);
            float bottom = std::min(bounds[1] + bounds[3], clip->rect[1] + clip->rect[3]);
            bounds[0] = left;
            bounds[1] = top;
            bounds[2] = std::max(0.0f, right - left);
            bounds[3] = std::max(0.0f, bottom - top);
        }
        for (const Layer& layer : ThemePaintSpec::current().regions[region].layers) {
            ShaderUtil::Paint paint;
            paint.blend = blendOf(layer.blend);
            paint.opacity = layer.opacity * std::min(1.0f, opacity);
            paint.dither = layer.dither;
            if (layer.kind == Kind::Shader) {
                if (!ThemeShaders::draw(layer, shape, clip, paint.opacity)) {
                    paint.count = 1;
                    paint.colors[0] = layer.stops.empty() ? standIn(region) : layer.stops[0].color;
                    ShaderUtil::paint(shape, clip, hole, paint);
                }
                continue;
            }
            if (layer.kind == Kind::Image) {
                if (!image(layer, bounds, paint)) {
                    continue;
                }
            } else {
                colors(layer, paint);
                if (layer.kind == Kind::Solid) {
                    paint.kind = ShaderUtil::PAINT_SOLID;
                } else {
                    gradientAxis(layer, bounds, paint);
                }
            }
            if (layer.inset > 0.0f) {
                float cut = layer.inset * UiRender::transformW(1.0f);
                ShaderUtil::Shape inner = shape;
                inner.rect[0] += cut;
                inner.rect[1] += cut;
                inner.rect[2] = std::max(0.0f, inner.rect[2] - 2.0f * cut);
                inner.rect[3] = std::max(0.0f, inner.rect[3] - 2.0f * cut);
                for (float& radius : inner.radii) {
                    radius = std::max(0.0f, radius - cut);
                }
                ShaderUtil::paint(inner, clip, hole, paint);
                continue;
            }
            ShaderUtil::paint(shape, clip, hole, paint);
        }
    }

    void paintRect(Region region, float x, float y, float w, float h, float radius, int cornerMask, float opacity) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        ShaderUtil::Shape shape =
                ShaderUtil::roundShape(UiRender::transformX(x), UiRender::transformY(y), UiRender::transformW(w),
                                       UiRender::transformW(h), UiRender::transformW(radius), cornerMask);
        layers(region, shape, nullptr, nullptr, opacity * std::clamp(UiRender::alphaMul, 0.0f, 1.0f));
    }

    void fill(Region region, float x, float y, float w, float h, float radius, int cornerMask, uint32_t fallback) {
        if (!ThemePaint::custom(region)) {
            UiRender::roundRect(x, y, w, h, radius, fallback, cornerMask);
            return;
        }
        paintRect(region, x, y, w, h, radius, cornerMask, 1.0f);
    }

    float blur(Region region) {
        return ThemePaintSpec::current().regions[region].blur;
    }

    void primaryFill(float x, float y, float w, float h, float radius, float lift) {
        if (!ThemePaint::custom(ThemePaintSpec::PRIMARY_BUTTON)) {
            UiRender::roundRect(x, y, w, h, radius, UiRender::mix(UiTheme::accent(), UiTheme::accentHover(), lift));
            return;
        }
        paintRect(ThemePaintSpec::PRIMARY_BUTTON, x, y, w, h, radius, 0xF, 1.0f);
        int alpha = Ui::round(255.0f * std::clamp(lift, 0.0f, 1.0f));
        if (alpha > 0) {
            UiRender::roundRect(x, y, w, h, radius, (UiTheme::accentHover() & 0x00FFFFFFu) | ((uint32_t) alpha << 24));
        }
    }

    void decorations(bool above, const std::string& page, const Frames& frames, const ShaderUtil::Shape& windowClip,
                     const ShaderUtil::Shape& shellClip) {
        using ThemePaintSpec::Extent;
        float opacity = std::clamp(UiRender::alphaMul, 0.0f, 1.0f);
        if (opacity <= 0.0f || ShaderUtil::paintBroken()) {
            return;
        }
        for (const ThemePaintSpec::Decoration& item : ThemePaintSpec::current().decorations) {
            if (item.above != above ||
                (!item.pages.empty() && std::find(item.pages.begin(), item.pages.end(), page) == item.pages.end())) {
                continue;
            }
            const ThemeAssets::Texture& texture = ThemeAssets::get(item.source);
            if (!texture.ready || texture.width <= 0 || texture.height <= 0) {
                continue;
            }
            const float* frame = frames.rects[item.region];
            float size[2] = {(float) texture.width, (float) texture.height};
            for (int k = 0; k < 2; k++) {
                if (item.extent[k] == Extent::Units) {
                    size[k] = item.size[k];
                } else if (item.extent[k] == Extent::Percent) {
                    size[k] = item.size[k] * frame[2 + k];
                }
            }
            float aspect = (float) texture.width / (float) texture.height;
            if (item.extent[0] == Extent::Auto && item.extent[1] != Extent::Auto) {
                size[0] = size[1] * aspect;
            } else if (item.extent[1] == Extent::Auto && item.extent[0] != Extent::Auto) {
                size[1] = size[0] / aspect;
            }
            float x = frame[0] + (frame[2] - size[0]) * item.anchor[0] + item.offset[0];
            float y = frame[1] + (frame[3] - size[1]) * item.anchor[1] + item.offset[1];
            ShaderUtil::Shape shape =
                    ShaderUtil::roundShape(UiRender::transformX(x), UiRender::transformY(y),
                                           UiRender::transformW(size[0]), UiRender::transformW(size[1]), 0.0f, 0);
            ShaderUtil::Paint paint;
            paint.kind = ShaderUtil::PAINT_IMAGE;
            paint.texture = texture.id;
            std::copy(shape.rect, shape.rect + 4, paint.frame);
            paint.bounded = true;
            paint.opacity = item.opacity * opacity;
            ShaderUtil::paint(shape, item.region == ThemePaintSpec::WINDOW ? &windowClip : &shellClip, nullptr, paint);
        }
    }

    void selectedFill(Region region, const std::string& id, float x, float y, float w, float h, float radius,
                      bool selected, bool hovered, float lit) {
        if (!ThemePaint::custom(region)) {
            if (lit > 0.001f) {
                UiRender::roundRect(x, y, w, h, radius, UiRender::mix(UiTheme::clear(), UiTheme::navHover(), lit));
            }
            return;
        }
        float hover = UiState::motion(id + "#paintHover", !selected && hovered ? 0.48f : 0.0f, 16.0f);
        float chosen = UiState::motion(id + "#paintChosen", selected ? 1.0f : 0.0f, 16.0f);
        if (hover > 0.001f) {
            UiRender::roundRect(x, y, w, h, radius, UiRender::mix(UiTheme::clear(), UiTheme::navHover(), hover));
        }
        if (chosen > 0.001f) {
            paintRect(region, x, y, w, h, radius, 0xF, chosen);
        }
    }
}
