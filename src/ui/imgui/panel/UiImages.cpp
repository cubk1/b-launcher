#include "UiImages.h"
#include "UiCommon.h"
#include "UiHttp.h"
#include "../gl/UiGL.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#include <algorithm>
#include <deque>
#include <utility>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace UiImages {

    bool promoted = false;
    std::function<bool(const std::string&, Resolved&)> itemResolver;
    std::function<bool(const std::string&, Resolved&)> textureResolver;
    std::function<bool(const std::string&, Resolved&)> silhouetteResolver;

    static std::mutex lock;
    static std::unordered_map<std::string, int> kinds;
    static std::unordered_map<std::string, std::vector<unsigned char>> pending;
    struct Frame {
        uint32_t texture;
        int until;
    };
    struct Image {
        Resolved resolved{0, 16, 16};
        std::vector<Frame> frames;
    };
    static std::unordered_map<std::string, Image> images;
    static std::deque<std::string> queue;
    static bool working = false;

    static int kindOf(const std::string& source, bool& known) {
        std::lock_guard<std::mutex> guard(lock);
        auto it = kinds.find(source);
        known = it != kinds.end();
        return known ? it->second : KIND_MISSING;
    }

    static void setKind(const std::string& source, int kind) {
        std::lock_guard<std::mutex> guard(lock);
        kinds[source] = kind;
    }

    int resolve(const std::string& source) {
        if (source.empty()) {
            return KIND_MISSING;
        }
        bool known = false;
        int kind = kindOf(source, known);
        if (known) {
            if (kind == KIND_LOADING) {
                promote(source);
                kind = kindOf(source, known);
            }
            return kind;
        }
        if (Ui::startsWith(source, "item:")) {
            return resolveItem(source, source.substr(5));
        }
        if (Ui::startsWith(source, "mc:")) {
            return resolveTexture(source, source.substr(3));
        }
        if (Ui::startsWith(source, "http://") || Ui::startsWith(source, "https://")) {
            setKind(source, KIND_LOADING);
            startFetch(source);
            return KIND_LOADING;
        }
        setKind(source, KIND_MISSING);
        return KIND_MISSING;
    }

    int resolveItem(const std::string& source, const std::string& name) {
        (void) name;
        Resolved resolved{0, 16, 16};
        if (!itemResolver || !itemResolver(source, resolved) || resolved.texture == 0) {
            setKind(source, KIND_MISSING);
            return KIND_MISSING;
        }
        images[source].resolved = {resolved.texture, 16, 16};
        setKind(source, KIND_ITEM);
        return KIND_ITEM;
    }

    int resolveTexture(const std::string& source, const std::string& path) {
        (void) path;
        Resolved resolved{0, 64, 64};
        if (!textureResolver || !textureResolver(source, resolved) || resolved.texture == 0) {
            setKind(source, KIND_MISSING);
            return KIND_MISSING;
        }
        images[source].resolved = {resolved.texture, 64, 64};
        setKind(source, KIND_TEXTURE);
        return KIND_TEXTURE;
    }

    void startFetch(const std::string& source) {
        {
            std::lock_guard<std::mutex> guard(lock);
            queue.push_back(source);
            if (working) {
                return;
            }
            working = true;
        }
        std::thread worker(drainQueue);
        worker.detach();
    }

    void drainQueue() {
        while (true) {
            std::string source;
            {
                std::lock_guard<std::mutex> guard(lock);
                if (queue.empty()) {
                    working = false;
                    return;
                }
                source = std::move(queue.front());
                queue.pop_front();
            }
            fetchOne(source);
        }
    }

    void fetchOne(const std::string& source) {
        std::vector<unsigned char> data;
        int status = 0;
        bool ok = UiHttp::get(source, {}, 15000, 4194304, data, status);
        if (!ok || status != 200) {
            setKind(source, KIND_FAILED);
            return;
        }
        std::lock_guard<std::mutex> guard(lock);
        pending[source] = std::move(data);
    }

    uint32_t upload(const unsigned char* rgba, int width, int height) {
        if (!UiGL::loaded() && !UiGL::load()) {
            return 0;
        }
        GLuint id = 0;
        UiGL::GenTextures(1, &id);
        UiGL::BindTexture(UGL_TEXTURE_2D, id);
        UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MIN_FILTER, UGL_LINEAR);
        UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MAG_FILTER, UGL_LINEAR);
        UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_S, UGL_CLAMP_TO_EDGE);
        UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_T, UGL_CLAMP_TO_EDGE);
        UiGL::PixelStorei(UGL_UNPACK_ROW_LENGTH, 0);
        UiGL::PixelStorei(UGL_UNPACK_ALIGNMENT, 4);
        UiGL::TexImage2D(UGL_TEXTURE_2D, 0, UGL_RGBA8, width, height, 0, UGL_RGBA, UGL_UNSIGNED_BYTE, rgba);
        return id;
    }

    uint32_t uploadEncoded(const unsigned char* data, int size, int* width, int* height) {
        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load_from_memory(data, size, &w, &h, &channels, 4);
        if (pixels == nullptr) {
            return 0;
        }
        uint32_t texture = upload(pixels, w, h);
        stbi_image_free(pixels);
        if (width != nullptr) {
            *width = w;
        }
        if (height != nullptr) {
            *height = h;
        }
        return texture;
    }

    void promote(const std::string& source) {
        std::vector<unsigned char> data;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = pending.find(source);
            if (it == pending.end()) {
                return;
            }
            data = std::move(it->second);
            pending.erase(it);
        }
        if (isGif(data) && promoteGif(source, data)) {
            return;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load_from_memory(data.data(), (int) data.size(), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            setKind(source, KIND_FAILED);
            return;
        }
        uint32_t id = upload(pixels, width, height);
        stbi_image_free(pixels);
        if (id == 0) {
            setKind(source, KIND_FAILED);
            return;
        }
        images[source].resolved = {id, width, height};
        setKind(source, KIND_TEXTURE);
        promoted = true;
    }

    bool isGif(const std::vector<unsigned char>& data) {
        return data.size() > 3 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F';
    }

    bool promoteGif(const std::string& source, const std::vector<unsigned char>& data) {
        int* frameDelays = nullptr;
        int width = 0;
        int height = 0;
        int count = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load_gif_from_memory(data.data(), (int) data.size(), &frameDelays, &width, &height,
                                                          &count, &channels, 4);
        if (pixels == nullptr) {
            return false;
        }
        if (count <= 1) {
            stbi_image_free(pixels);
            if (frameDelays != nullptr) {
                stbi_image_free(frameDelays);
            }
            return false;
        }
        std::vector<Frame> frames;
        frames.reserve((size_t) count);
        int total = 0;
        size_t frameBytes = (size_t) width * (size_t) height * 4;
        for (int i = 0; i < count; i++) {
            uint32_t id = upload(pixels + frameBytes * (size_t) i, width, height);
            int millis = std::max(20, frameDelays != nullptr ? frameDelays[i] : 100);
            total += millis;
            frames.push_back(Frame{id, total});
        }
        stbi_image_free(pixels);
        if (frameDelays != nullptr) {
            stbi_image_free(frameDelays);
        }
        Image& image = images[source];
        image.resolved = {frames.front().texture, width, height};
        image.frames = std::move(frames);
        setKind(source, KIND_TEXTURE);
        promoted = true;
        return true;
    }

    int widthOf(const std::string& source) {
        auto it = images.find(source);
        return it == images.end() ? 16 : it->second.resolved.width;
    }

    int heightOf(const std::string& source) {
        auto it = images.find(source);
        return it == images.end() ? 16 : it->second.resolved.height;
    }

    uint32_t textureOf(const std::string& source) {
        auto it = images.find(source);
        if (it == images.end()) return 0;
        const Image& image = it->second;
        if (image.frames.empty()) return image.resolved.texture;
        int total = image.frames.back().until;
        if (total <= 0) return image.frames.front().texture;
        long long now = Ui::millis() % total;
        for (const Frame& frame : image.frames) {
            if (now < frame.until) return frame.texture;
        }
        return image.frames.back().texture;
    }
}
