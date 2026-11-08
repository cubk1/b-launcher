#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiRender.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Textures {

    namespace {
        struct Entry {
            std::weak_ptr<const Pixels> pixels;
            uint32_t texture = 0;
        };

        std::unordered_map<const Pixels*, Entry> uploaded;

        void forgetReleased() {
            for (auto it = uploaded.begin(); it != uploaded.end();) {
                if (it->second.pixels.expired()) {
                    GLuint texture = it->second.texture;
                    UiGL::DeleteTextures(1, &texture);
                    it = uploaded.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    uint32_t of(const std::shared_ptr<const Pixels>& pixels) {
        if (pixels == nullptr || pixels->rgba.empty()) {
            return 0;
        }
        auto found = uploaded.find(pixels.get());
        if (found != uploaded.end() && !found->second.pixels.expired()) {
            return found->second.texture;
        }
        forgetReleased();
        uint32_t texture = UiImages::upload(pixels->rgba.data(), pixels->width, pixels->height);
        uploaded[pixels.get()] = Entry{pixels, texture};
        return texture;
    }

    void sharp(const std::shared_ptr<const Pixels>& pixels, float x, float y, float w, float h, float radius) {
        uint32_t texture = of(pixels);
        if (texture == 0) {
            return;
        }
        float pixel = UiRender::pixelScale();
        float per = UiRender::transformW(1.0f) * pixel;
        float width = (float) pixels->width;
        float height = (float) pixels->height;
        bool native = per > 0.0f && width <= w * per + 1.5f && height <= h * per + 1.5f &&
                (std::fabs(width - w * per) < 1.5f || std::fabs(height - h * per) < 1.5f);
        float drawW = native ? width / per : w;
        float drawH = native ? height / per : h;
        if (!native) {
            float fit = std::min(w / width, h / height);
            drawW = width * fit;
            drawH = height * fit;
        }
        float left = x + (w - drawW) * 0.5f;
        float top = y + (h - drawH) * 0.5f;
        if (native) {
            float screenX = UiRender::transformX(left) * pixel;
            float screenY = UiRender::transformY(top) * pixel;
            left += (std::round(screenX) - screenX) / per;
            top += (std::round(screenY) - screenY) / per;
        }
        if (radius > 0.0f) {
            UiRender::roundTexture(texture, left, top, drawW, drawH, radius, 0.0f, 0u, width, height);
        } else {
            UiRender::texture(texture, left, top, drawW, drawH);
        }
    }
}
