#include "ui/imgui/screens/shell/Background.h"
#include "app/system/Config.h"
#include "app/system/EmbeddedIcons.h"
#include "app/system/Pixels.h"
#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/theme/Swatches.h"
#include "ui/imgui/screens/theme/ThemeRuntime.h"
#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/theme/ThemePaintSpec.h"
#include "app/system/Notices.h"

#include "ui/imgui/panel/UiImages.h"
#include "ui/imgui/gl/UiGL.h"

#include <core/base/json.hpp>
#include <core/base/strings.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#else
#include <core/base/fs.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Background {

    namespace {
        constexpr int WINDOW_W = 820;
        constexpr int WINDOW_H = 510;
        constexpr int SCALE = 2;
        constexpr int SIDEBAR_W = 188;
        constexpr int SHELL_RADIUS = 14;
        constexpr float DEFAULT_EXTRA_BLUR = 38.0f;

        struct Image {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> rgba;
        };

        struct Key {
            std::string file;
            std::string theme;
            bool stretch = false;
            float sidebarBlur = 40.0f;
            float panelBlur = 12.0f;

            bool operator==(const Key& other) const = default;
        };

        struct Prepared {
            Key key;
            bool whole = false;
            bool ok = false;
            std::string error;
            Image sidebar;
            Image panel;
            Image wholeSidebar;
            Image wholePanel;
            Swatches swatches;
        };

        std::string chosen;
        std::string failedTheme;
        bool loaded = false;
        bool wantWhole = false;
        uint32_t sidebarTexture = 0;
        uint32_t panelTexture = 0;
        uint32_t wholeSidebarTexture = 0;
        uint32_t wholePanelTexture = 0;
        std::optional<Key> shownKey;
        std::optional<Key> wholeKey;
        bool working = false;
        std::thread worker;
        std::mutex lock;
        std::shared_ptr<Prepared> finished;

        bool cropFor(int sw, int sh, int w, int h, bool stretch, int& x, int& y, int& cw, int& ch) {
            x = 0;
            y = 0;
            cw = sw;
            ch = sh;
            if (stretch) {
                return true;
            }
            double wanted = (double) w / h;
            if ((double) sw / sh > wanted) {
                cw = (int) std::lround(sh * wanted);
                x = (sw - cw) / 2;
            } else {
                ch = (int) std::lround(sw / wanted);
                y = (sh - ch) / 2;
            }
            return true;
        }

        bool fit(const Pixels& pixels, int w, int h, bool stretch, Image& out, std::string& error) {
            if (pixels.width <= 0 || pixels.height <= 0) {
                error = "无法读取这张图片";
                return false;
            }
            int x = 0;
            int y = 0;
            int cw = 0;
            int ch = 0;
            cropFor(pixels.width, pixels.height, w, h, stretch, x, y, cw, ch);
            Pixels scaled = Pixels::resized(pixels, x, y, cw, ch, w, h);
            if (scaled.rgba.empty()) {
                error = "无法处理这张图片";
                return false;
            }
            out.width = w;
            out.height = h;
            out.rgba = std::move(scaled.rgba);
            return true;
        }

#if defined(_WIN32)
        bool decode(const std::wstring& file, int w, int h, bool stretch, Image& out, std::string& error) {
            IWICImagingFactory* factory = nullptr;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory)))) {
                error = "无法读取图片";
                return false;
            }
            bool ok = false;
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICBitmapClipper* clipper = nullptr;
            IWICBitmapScaler* scaler = nullptr;
            IWICFormatConverter* converter = nullptr;
            UINT sw = 0;
            UINT sh = 0;
            if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnDemand, &decoder)) ||
                FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&sw, &sh)) || sw == 0 || sh == 0) {
                error = "无法读取这张图片";
            } else {
                int x = 0;
                int y = 0;
                int cw = 0;
                int ch = 0;
                cropFor((int) sw, (int) sh, w, h, stretch, x, y, cw, ch);
                WICRect crop = {x, y, cw, ch};
                if (SUCCEEDED(factory->CreateBitmapClipper(&clipper)) && SUCCEEDED(clipper->Initialize(frame, &crop)) &&
                    SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(clipper, w, h, WICBitmapInterpolationModeHighQualityCubic)) &&
                    SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                                    nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    out.width = w;
                    out.height = h;
                    out.rgba.resize((size_t) w * h * 4);
                    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, (UINT) out.rgba.size(), out.rgba.data()));
                }
                if (!ok) {
                    error = "无法处理这张图片";
                }
            }
            if (converter) converter->Release();
            if (scaler) scaler->Release();
            if (clipper) clipper->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            factory->Release();
            return ok;
        }
#else
        bool decode(const std::filesystem::path& file, int w, int h, bool stretch, Image& out, std::string& error) {
            b_launcher_base::result<std::string> data = b_launcher_base::fs::read_file(file);
            std::shared_ptr<const Pixels> pixels = data ? Pixels::decode(data->data(), data->size()) : nullptr;
            if (!pixels) {
                error = "无法读取这张图片";
                return false;
            }
            return fit(*pixels, w, h, stretch, out, error);
        }
#endif

        bool decodeBytes(const ThemeStore::Source& source, int w, int h, bool stretch, Image& out, std::string& error) {
            std::optional<std::string> bytes = ThemeStore::contents(source);
            std::shared_ptr<const Pixels> pixels = bytes ? Pixels::decode(bytes->data(), bytes->size()) : nullptr;
            if (!pixels) {
                error = "无法读取这张图片";
                return false;
            }
            return fit(*pixels, w, h, stretch, out, error);
        }

        std::vector<int> boxes(float sigma) {
            const int n = 3;
            double ideal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
            int lower = (int) std::floor(ideal);
            if (lower % 2 == 0) {
                lower--;
            }
            int upper = lower + 2;
            double split =
                    (12.0 * sigma * sigma - n * lower * lower - 4.0 * n * lower - 3.0 * n) / (-4.0 * lower - 4.0);
            int m = (int) std::lround(split);
            std::vector<int> out;
            for (int i = 0; i < n; i++) {
                out.push_back(i < m ? lower : upper);
            }
            return out;
        }

        void boxPass(const std::vector<float>& in, std::vector<float>& out, int width, int height, int r, bool rows) {
            int lines = rows ? height : width;
            int length = rows ? width : height;
            size_t stride = rows ? 1 : (size_t) width;
            float scale = 1.0f / (2 * r + 1);
            for (int line = 0; line < lines; line++) {
                size_t start = rows ? (size_t) line * width : (size_t) line;
                auto at = [&](int i) { return in[start + (size_t) std::clamp(i, 0, length - 1) * stride]; };
                float sum = 0.0f;
                for (int i = -r; i <= r; i++) {
                    sum += at(i);
                }
                for (int i = 0; i < length; i++) {
                    out[start + (size_t) i * stride] = sum * scale;
                    sum += at(i + r + 1) - at(i - r);
                }
            }
        }

        Image blur(const Image& image, float sigma) {
            Image out = image;
            size_t count = (size_t) image.width * image.height;
            std::vector<float> channel(count);
            std::vector<float> scratch(count);
            for (int c = 0; c < 3; c++) {
                for (size_t i = 0; i < count; i++) {
                    channel[i] = image.rgba[i * 4 + c];
                }
                for (int size : boxes(sigma)) {
                    int r = (size - 1) / 2;
                    boxPass(channel, scratch, image.width, image.height, r, true);
                    boxPass(scratch, channel, image.width, image.height, r, false);
                }
                for (size_t i = 0; i < count; i++) {
                    out.rgba[i * 4 + c] = (unsigned char) std::clamp((int) std::lround(channel[i]), 0, 255);
                }
            }
            for (size_t i = 0; i < count; i++) {
                out.rgba[i * 4 + 3] = 255;
            }
            return out;
        }

        Image crop(const Image& image, int x0, int x1) {
            Image out;
            out.width = x1 - x0;
            out.height = image.height;
            out.rgba.resize((size_t) out.width * out.height * 4);
            for (int y = 0; y < image.height; y++) {
                std::copy(image.rgba.begin() + ((size_t) y * image.width + x0) * 4,
                          image.rgba.begin() + ((size_t) y * image.width + x1) * 4,
                          out.rgba.begin() + (size_t) y * out.width * 4);
            }
            return out;
        }

        bool stitchDefault(Image& out) {
            std::shared_ptr<const Pixels> side =
                    Pixels::decode(EmbeddedIcons::backgroundSidebar, EmbeddedIcons::backgroundSidebarSize);
            std::shared_ptr<const Pixels> main =
                    Pixels::decode(EmbeddedIcons::backgroundPanel, EmbeddedIcons::backgroundPanelSize);
            int width = WINDOW_W * SCALE;
            int height = WINDOW_H * SCALE;
            int split = SIDEBAR_W * SCALE;
            if (!side || !main || side->height != height || main->height != height || side->width < split ||
                main->width != width - split) {
                return false;
            }
            out.width = width;
            out.height = height;
            out.rgba.resize((size_t) width * height * 4);
            for (int y = 0; y < height; y++) {
                unsigned char* row = out.rgba.data() + (size_t) y * width * 4;
                std::copy_n(side->rgba.data() + (size_t) y * side->width * 4, (size_t) split * 4, row);
                std::copy_n(main->rgba.data() + (size_t) y * main->width * 4, (size_t) main->width * 4,
                            row + (size_t) split * 4);
            }
            return true;
        }

        void releaseShown() {
            if (sidebarTexture != 0 || panelTexture != 0) {
                const GLuint textures[] = {sidebarTexture, panelTexture};
                UiGL::DeleteTextures(2, textures);
            }
            sidebarTexture = 0;
            panelTexture = 0;
        }

        void releaseWhole() {
            if (wholeSidebarTexture != 0 || wholePanelTexture != 0) {
                const GLuint textures[] = {wholeSidebarTexture, wholePanelTexture};
                UiGL::DeleteTextures(2, textures);
            }
            wholeSidebarTexture = 0;
            wholePanelTexture = 0;
            wholeKey.reset();
        }

        Key currentKey() {
            const ThemePaintSpec::BackgroundSpec& spec = ThemePaintSpec::current().background;
            Key key;
            key.file = chosen;
            if (chosen.empty() && spec.image != failedTheme) {
                key.theme = spec.image;
            }
            key.stretch = spec.stretch;
            key.sidebarBlur = spec.sidebarBlur;
            key.panelBlur = spec.panelBlur;
            return key;
        }

        bool builtIn(const Key& key) {
            return key.file.empty() && key.theme.empty();
        }

        void prepare(Prepared& result, const std::optional<ThemeStore::Source>& source) {
            const Key& key = result.key;
            int width = WINDOW_W * SCALE;
            int height = WINDOW_H * SCALE;
            if (builtIn(key)) {
                std::shared_ptr<const Pixels> pixels =
                        Pixels::decode(EmbeddedIcons::backgroundPanel, EmbeddedIcons::backgroundPanelSize);
                if (pixels && pixels->width > 0 && pixels->height > 0) {
                    result.swatches =
                            Swatches::of(Swatches::thumbnail(pixels->rgba.data(), pixels->width, pixels->height));
                    result.ok = true;
                }
                Image stitched;
                if (result.whole && stitchDefault(stitched)) {
                    result.wholeSidebar = blur(stitched, DEFAULT_EXTRA_BLUR);
                    result.wholePanel = std::move(stitched);
                }
                return;
            }
            Image image;
            bool decoded = false;
            if (!key.file.empty()) {
#if defined(_WIN32)
                decoded = decode(b_launcher_base::path_from_utf8(key.file).wstring(), width, height, key.stretch, image,
                                 result.error);
#else
                decoded = decode(b_launcher_base::path_from_utf8(key.file), width, height, key.stretch, image,
                                 result.error);
#endif
            } else if (source) {
                decoded = decodeBytes(*source, width, height, key.stretch, image, result.error);
            } else {
                result.error = "找不到主题的背景图片";
            }
            if (!decoded) {
                return;
            }
            int split = SIDEBAR_W * SCALE;
            Image strong = blur(image, key.sidebarBlur);
            Image light = blur(image, key.panelBlur);
            result.sidebar = crop(strong, 0, split + SHELL_RADIUS * SCALE);
            result.panel = crop(light, split, image.width);
            if (result.whole) {
                result.wholeSidebar = std::move(strong);
                result.wholePanel = std::move(light);
            }
            result.swatches = Swatches::of(Swatches::thumbnail(image.rgba.data(), image.width, image.height));
            result.ok = true;
        }

        void start(const Key& key, bool whole) {
            std::optional<ThemeStore::Source> source;
            if (key.file.empty() && !key.theme.empty()) {
                std::string owner;
                std::string file;
                if (ThemePaintSpec::splitSource(key.theme, owner, file)) {
                    source = ThemeStore::locate(owner, file);
                }
            }
            working = true;
            if (worker.joinable()) {
                worker.join();
            }
            worker = std::thread([key, whole, source] {
#if defined(_WIN32)
                HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
                auto result = std::make_shared<Prepared>();
                result->key = key;
                result->whole = whole;
                prepare(*result, source);
#if defined(_WIN32)
                if (SUCCEEDED(com)) {
                    CoUninitialize();
                }
#endif
                std::lock_guard<std::mutex> guard(lock);
                finished = result;
            });
        }

        void load() {
            if (loaded) {
                return;
            }
            loaded = true;
            chosen = b_launcher_base::json_text(Config::data(), "backgroundPath");
        }

        void accept(Prepared& result) {
            const Key& key = result.key;
            if (!result.ok) {
                if (!key.file.empty()) {
                    Notices::add(Notices::Kind::Failed, "背景图片：" + result.error, 4000);
                    chosen.clear();
                    Config::data()["backgroundPath"] = chosen;
                    Config::save();
                } else if (!key.theme.empty()) {
                    Notices::add(Notices::Kind::Failed, "主题背景图片：" + result.error, 4000);
                    failedTheme = key.theme;
                } else {
                    shownKey = key;
                    if (result.whole) {
                        wholeKey = key;
                    }
                }
                return;
            }
            ThemeRuntime::setImageSwatches(result.swatches);
            if (!builtIn(key)) {
                GLuint nextSidebar =
                        UiImages::upload(result.sidebar.rgba.data(), result.sidebar.width, result.sidebar.height);
                GLuint nextPanel = UiImages::upload(result.panel.rgba.data(), result.panel.width, result.panel.height);
                releaseShown();
                sidebarTexture = nextSidebar;
                panelTexture = nextPanel;
            }
            shownKey = key;
            if (!result.whole) {
                return;
            }
            GLuint nextSidebar = 0;
            GLuint nextPanel = 0;
            if (!result.wholeSidebar.rgba.empty() && !result.wholePanel.rgba.empty()) {
                nextSidebar = UiImages::upload(result.wholeSidebar.rgba.data(), result.wholeSidebar.width,
                                               result.wholeSidebar.height);
                nextPanel = UiImages::upload(result.wholePanel.rgba.data(), result.wholePanel.width,
                                             result.wholePanel.height);
            }
            releaseWhole();
            wholeSidebarTexture = nextSidebar;
            wholePanelTexture = nextPanel;
            wholeKey = key;
        }
    }

    uint32_t sidebar() {
        load();
        return builtIn(currentKey()) ? Icons::backgroundSidebar() : sidebarTexture;
    }

    uint32_t panel() {
        load();
        return builtIn(currentKey()) ? Icons::backgroundPanel() : panelTexture;
    }

    uint32_t wholeSidebar() {
        load();
        wantWhole = true;
        return wholeSidebarTexture;
    }

    uint32_t wholePanel() {
        load();
        wantWhole = true;
        return wholePanelTexture;
    }

    bool themeImage() {
        load();
        return !currentKey().theme.empty();
    }

    std::string path() {
        load();
        return chosen;
    }

    void setPath(const std::string& value) {
        load();
        if (value == chosen) {
            return;
        }
        chosen = value;
        Config::data()["backgroundPath"] = chosen;
        Config::save();
    }

    bool preparing() {
        return working;
    }

    void poll() {
        load();
        Key key = currentKey();
        // setPath() may run while this frame's draw list still references the
        // old background. Retire textures here, before the next frame is built.
        if (builtIn(key)) {
            releaseShown();
        }
        std::shared_ptr<Prepared> result;
        {
            std::lock_guard<std::mutex> guard(lock);
            result.swap(finished);
        }
        if (result) {
            working = false;
            if (worker.joinable()) {
                worker.join();
            }
            if (result->key == key) {
                accept(*result);
                key = currentKey();
            }
        }
        if (working) {
            return;
        }
        bool stale = !shownKey || !(*shownKey == key);
        bool wholeStale = wantWhole && (!wholeKey || !(*wholeKey == key));
        if (stale || wholeStale) {
            start(key, wantWhole);
        }
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
        releaseShown();
        releaseWhole();
        shownKey.reset();
    }
}
