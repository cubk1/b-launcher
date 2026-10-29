#include "ui/imgui/screens/shell/Textures.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/UiImages.h"

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
}
