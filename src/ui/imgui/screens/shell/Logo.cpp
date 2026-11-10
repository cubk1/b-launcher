#include "ui/imgui/screens/shell/Logo.h"
#include "app/system/Config.h"
#include "app/system/Customization.h"
#include "app/system/EmbeddedIcons.h"
#include "app/system/IconFile.h"
#include "app/system/Notices.h"

#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/panel/UiRender.h"
#include "ui/imgui/panel/UiTheme.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#include "app/system/Pixels.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#endif

#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

        constexpr int CUSTOM_SIDE = 768;

        struct Level {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> rgba;
        };

        struct Custom {
            bool ok = false;
            std::string error;
            std::vector<Level> levels;
        };

        std::string chosen;
        bool configured = false;
        uint32_t customTexture = 0;
        int customWidth = 0;
        int customHeight = 0;
        Pixels customImage;
        int revision = 0;
        Pixels packImage;
        bool packRead = false;

        const Pixels& packIcon() {
            if (!packRead) {
                packRead = true;
                std::string name = Customization::iconImage();
                std::optional<std::string> data = name.empty() ? std::nullopt : Customization::read(name);
                if (data) {
                    if (std::shared_ptr<const Pixels> decoded = Pixels::decode(data->data(), data->size())) {
                        packImage = *decoded;
                    }
                }
            }
            return packImage;
        }
        bool working = false;
        std::thread worker;
        std::mutex lock;
        std::shared_ptr<Custom> finished;
        std::string workingOn;

#if defined(_WIN32)
        bool decodeRaster(const std::wstring& file, Level& out, std::string& error) {
            IWICImagingFactory* factory = nullptr;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory)))) {
                error = "无法读取图片";
                return false;
            }
            bool ok = false;
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICBitmapScaler* scaler = nullptr;
            IWICFormatConverter* converter = nullptr;
            UINT sw = 0;
            UINT sh = 0;
            if (SUCCEEDED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnDemand, &decoder))) {
                GUID container = {};
                UINT count = 0;
                bool icon = SUCCEEDED(decoder->GetContainerFormat(&container)) &&
                        IsEqualGUID(container, GUID_ContainerFormatIco);
                if (FAILED(decoder->GetFrameCount(&count))) {
                    count = 0;
                }
                for (UINT i = 0; i < (icon ? count : std::min(count, 1u)); i++) {
                    IWICBitmapFrameDecode* candidate = nullptr;
                    UINT cw = 0;
                    UINT ch = 0;
                    if (FAILED(decoder->GetFrame(i, &candidate))) {
                        continue;
                    }
                    if (SUCCEEDED(candidate->GetSize(&cw, &ch)) && (UINT64) cw * ch > (UINT64) sw * sh) {
                        if (frame) frame->Release();
                        frame = candidate;
                        sw = cw;
                        sh = ch;
                    } else {
                        candidate->Release();
                    }
                }
            }
            if (frame == nullptr || sw == 0 || sh == 0) {
                error = "无法读取这张图片";
            } else {
                double scale = std::min(1.0, (double) CUSTOM_SIDE / std::max(sw, sh));
                UINT w = std::max(1u, (UINT) std::lround(sw * scale));
                UINT h = std::max(1u, (UINT) std::lround(sh * scale));
                if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame, w, h, WICBitmapInterpolationModeHighQualityCubic)) &&
                    SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                                    nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    out.width = (int) w;
                    out.height = (int) h;
                    out.rgba.resize((size_t) w * h * 4);
                    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, (UINT) out.rgba.size(), out.rgba.data()));
                }
                if (!ok) {
                    error = "无法处理这张图片";
                }
            }
            if (converter) converter->Release();
            if (scaler) scaler->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            factory->Release();
            return ok;
        }
#else
        bool decodeRaster(const std::filesystem::path& file, Level& out, std::string& error) {
            std::ifstream in(file, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::shared_ptr<const Pixels> pixels = Pixels::decode(data.data(), data.size());
            if (!pixels || pixels->width <= 0 || pixels->height <= 0) {
                error = "无法读取这张图片";
                return false;
            }
            double scale = std::min(1.0, (double) CUSTOM_SIDE / std::max(pixels->width, pixels->height));
            int w = std::max(1, (int) std::lround(pixels->width * scale));
            int h = std::max(1, (int) std::lround(pixels->height * scale));
            Pixels scaled = Pixels::resized(*pixels, 0, 0, pixels->width, pixels->height, w, h);
            if (scaled.rgba.empty()) {
                error = "无法处理这张图片";
                return false;
            }
            out.width = w;
            out.height = h;
            out.rgba = std::move(scaled.rgba);
            return true;
        }
#endif

        bool decodeVector(const std::filesystem::path& file, Level& out, std::string& error) {
            std::ifstream in(file, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!in.good() && !in.eof()) {
                error = "无法读取这张图片";
                return false;
            }
            NSVGimage* svg = text.empty() ? nullptr : nsvgParse(text.data(), "px", 96.0f);
            if (svg == nullptr || svg->width <= 0.0f || svg->height <= 0.0f || svg->shapes == nullptr) {
                if (svg != nullptr) {
                    nsvgDelete(svg);
                }
                error = "无法解析这个 SVG";
                return false;
            }
            NSVGrasterizer* raster = nsvgCreateRasterizer();
            float scale = (float) CUSTOM_SIDE / std::max(svg->width, svg->height);
            out.width = std::max(1, (int) std::lround(svg->width * scale));
            out.height = std::max(1, (int) std::lround(svg->height * scale));
            out.rgba.assign((size_t) out.width * out.height * 4, 0);
            if (raster != nullptr) {
                nsvgRasterize(raster, svg, 0.0f, 0.0f, scale, out.rgba.data(), out.width, out.height, out.width * 4);
                nsvgDeleteRasterizer(raster);
            }
            nsvgDelete(svg);
            if (raster == nullptr) {
                error = "无法处理这个 SVG";
                return false;
            }
            return true;
        }

        Level halve(const Level& source) {
            Level out;
            out.width = std::max(1, source.width / 2);
            out.height = std::max(1, source.height / 2);
            out.rgba.resize((size_t) out.width * out.height * 4);
            for (int y = 0; y < out.height; y++) {
                for (int x = 0; x < out.width; x++) {
                    float color[3] = {0.0f, 0.0f, 0.0f};
                    float alpha = 0.0f;
                    for (int dy = 0; dy < 2; dy++) {
                        for (int dx = 0; dx < 2; dx++) {
                            int sx = std::min(x * 2 + dx, source.width - 1);
                            int sy = std::min(y * 2 + dy, source.height - 1);
                            const unsigned char* p = &source.rgba[((size_t) sy * source.width + sx) * 4];
                            float a = p[3];
                            color[0] += p[0] * a;
                            color[1] += p[1] * a;
                            color[2] += p[2] * a;
                            alpha += a;
                        }
                    }
                    unsigned char* q = &out.rgba[((size_t) y * out.width + x) * 4];
                    for (int c = 0; c < 3; c++) {
                        q[c] = alpha > 0.0f ? (unsigned char) std::clamp((int) std::lround(color[c] / alpha), 0, 255)
                                            : 0;
                    }
                    q[3] = (unsigned char) std::lround(alpha * 0.25f);
                }
            }
            return out;
        }

        bool vectorFile(const std::filesystem::path& file) {
            std::string extension = b_launcher_base::path_to_utf8(file.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return extension == ".svg";
        }

        void releaseCustom() {
            if (customTexture != 0) {
                GLuint old = customTexture;
                UiGL::DeleteTextures(1, &old);
            }
            if (!customImage.rgba.empty()) {
                revision++;
            }
            customTexture = 0;
            customWidth = 0;
            customHeight = 0;
            customImage = Pixels();
        }

        uint32_t uploadLevels(const std::vector<Level>& levels) {
            uint32_t id = UiImages::upload(levels[0].rgba.data(), levels[0].width, levels[0].height);
            if (id == 0) {
                return 0;
            }
            for (size_t i = 1; i < levels.size(); i++) {
                UiGL::TexImage2D(UGL_TEXTURE_2D, (GLint) i, UGL_RGBA8, levels[i].width, levels[i].height, 0, UGL_RGBA,
                                 UGL_UNSIGNED_BYTE, levels[i].rgba.data());
            }
            UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MIN_FILTER, UGL_LINEAR_MIPMAP_LINEAR);
            return id;
        }

        void startCustom() {
            if (chosen.empty() || working) {
                return;
            }
            working = true;
            workingOn = chosen;
            std::filesystem::path file = b_launcher_base::path_from_utf8(chosen);
            if (worker.joinable()) {
                worker.join();
            }
            worker = std::thread([file] {
#if defined(_WIN32)
                HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
                auto result = std::make_shared<Custom>();
                Level base;
#if defined(_WIN32)
                bool ok = vectorFile(file) ? decodeVector(file, base, result->error)
                                           : decodeRaster(file.wstring(), base, result->error);
#else
                bool ok = vectorFile(file) ? decodeVector(file, base, result->error)
                                           : decodeRaster(file, base, result->error);
#endif
                if (ok) {
                    result->levels.push_back(std::move(base));
                    while (result->levels.back().width > 1 || result->levels.back().height > 1) {
                        result->levels.push_back(halve(result->levels.back()));
                    }
                    result->ok = true;
                }
#if defined(_WIN32)
                if (SUCCEEDED(com)) {
                    CoUninitialize();
                }
#endif
                std::lock_guard<std::mutex> guard(lock);
                finished = result;
            });
        }

        void configure() {
            if (configured) {
                return;
            }
            configured = true;
            chosen = b_launcher_base::json_text(Config::data(), "logoPath");
            startCustom();
        }
    }

    std::vector<unsigned char> pixels(int size) {
        if (size <= 0 || !load()) {
            return {};
        }
        return rasterize(size, UiTheme::accent() | 0xFF000000u);
    }

    std::vector<unsigned char> iconPixels(int size, bool custom) {
        const Pixels& pack = packIcon();
        if (size > 0 && !pack.rgba.empty()) {
            return IconFile::square(pack.rgba, pack.width, pack.height, size);
        }
        if (!custom || size <= 0 || customImage.rgba.empty()) {
            return pixels(size);
        }
        float fit = (float) size / (float) std::max(customImage.width, customImage.height);
        int w = std::clamp((int) std::lround(customImage.width * fit), 1, size);
        int h = std::clamp((int) std::lround(customImage.height * fit), 1, size);
        Pixels scaled = Pixels::resized(customImage, 0, 0, customImage.width, customImage.height, w, h);
        if (scaled.rgba.empty()) {
            return pixels(size);
        }
        std::vector<unsigned char> out((size_t) size * size * 4, 0);
        int left = (size - w) / 2;
        int top = (size - h) / 2;
        for (int row = 0; row < h; row++) {
            std::copy_n(scaled.rgba.data() + (size_t) row * w * 4, (size_t) w * 4,
                        out.data() + ((size_t) (top + row) * size + left) * 4);
        }
        return out;
    }

    int iconRevision() {
        return revision;
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

    void brand(float x, float y, float size, float alpha) {
        configure();
        if (chosen.empty()) {
            draw(x, y, size, alpha);
            return;
        }
        if (customTexture == 0 || size <= 0.0f || alpha <= 0.0f) {
            return;
        }
        float fit = size / (float) std::max(customWidth, customHeight);
        float w = customWidth * fit;
        float h = customHeight * fit;
        float dx = x + (size - w) * 0.5f;
        float dy = y + (size - h) * 0.5f;
        if (UiRender::scaleFactor == 1.0f) {
            dx = UiRender::snapLayout(dx);
            dy = UiRender::snapLayout(dy);
        }
        float saved = UiRender::alphaMul;
        UiRender::alphaMul = saved * alpha;
        UiRender::texture(customTexture, dx, dy, w, h);
        UiRender::alphaMul = saved;
    }

    std::string customPath() {
        configure();
        return chosen;
    }

    void setCustomPath(const std::string& path) {
        configure();
        if (path == chosen) {
            return;
        }
        chosen = path;
        Config::data()["logoPath"] = chosen;
        Config::save();
        startCustom();
    }

    bool preparing() {
        return working;
    }

    void poll() {
        configure();
        if (chosen.empty()) {
            releaseCustom();
        }
        std::shared_ptr<Custom> result;
        {
            std::lock_guard<std::mutex> guard(lock);
            result.swap(finished);
        }
        if (!result) {
            return;
        }
        working = false;
        if (worker.joinable()) {
            worker.join();
        }
        if (workingOn != chosen) {
            startCustom();
            return;
        }
        if (!result->ok) {
            Notices::add(Notices::Kind::Failed, "启动器标志：" + result->error, 4000);
            chosen.clear();
            Config::data()["logoPath"] = chosen;
            Config::save();
            releaseCustom();
            return;
        }
        uint32_t next = uploadLevels(result->levels);
        releaseCustom();
        customTexture = next;
        customWidth = result->levels.front().width;
        customHeight = result->levels.front().height;
        customImage.width = customWidth;
        customImage.height = customHeight;
        customImage.rgba = std::move(result->levels.front().rgba);
        revision++;
    }

    void shutdown() {
        if (worker.joinable()) {
            worker.join();
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            finished.reset();
        }
        working = false;
        releaseCustom();
    }
}
