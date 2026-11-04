#include "ui/imgui/screens/shell/Background.h"
#include "app/system/Config.h"
#include "ui/imgui/screens/shell/Icons.h"
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
#include "app/system/Pixels.h"

#include <core/base/fs.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Background {

    namespace {
        constexpr int WINDOW_W = 820;
        constexpr int WINDOW_H = 510;
        constexpr int SCALE = 2;
        constexpr int SIDEBAR_W = 188;
        constexpr int SHELL_RADIUS = 14;
        constexpr float SIDEBAR_BLUR = 40.0f;
        constexpr float PANEL_BLUR = 12.0f;

        struct Image {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> rgba;
        };

        struct Prepared {
            bool ok = false;
            std::string error;
            Image sidebar;
            Image panel;
        };

        std::string chosen;
        bool loaded = false;
        uint32_t sidebarTexture = 0;
        uint32_t panelTexture = 0;
        bool working = false;
        std::thread worker;
        std::mutex lock;
        std::shared_ptr<Prepared> finished;
        std::string workingOn;

#if defined(_WIN32)
        bool decode(const std::wstring& file, int w, int h, Image& out, std::string& error) {
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
                double wanted = (double) w / h;
                WICRect crop = {0, 0, (INT) sw, (INT) sh};
                if ((double) sw / sh > wanted) {
                    crop.Width = (INT) std::lround(sh * wanted);
                    crop.X = ((INT) sw - crop.Width) / 2;
                } else {
                    crop.Height = (INT) std::lround(sw / wanted);
                    crop.Y = ((INT) sh - crop.Height) / 2;
                }
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
        bool decode(const std::filesystem::path& file, int w, int h, Image& out, std::string& error) {
            b_launcher_base::result<std::string> data = b_launcher_base::fs::read_file(file);
            std::shared_ptr<const Pixels> pixels = data ? Pixels::decode(data->data(), data->size()) : nullptr;
            if (!pixels || pixels->width <= 0 || pixels->height <= 0) {
                error = "无法读取这张图片";
                return false;
            }
            int sw = pixels->width;
            int sh = pixels->height;
            double wanted = (double) w / h;
            int x = 0;
            int y = 0;
            int cw = sw;
            int ch = sh;
            if ((double) sw / sh > wanted) {
                cw = (int) std::lround(sh * wanted);
                x = (sw - cw) / 2;
            } else {
                ch = (int) std::lround(sw / wanted);
                y = (sh - ch) / 2;
            }
            Pixels scaled = Pixels::resized(*pixels, x, y, cw, ch, w, h);
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

        void release() {
            if (sidebarTexture != 0 || panelTexture != 0) {
                const GLuint textures[] = {sidebarTexture, panelTexture};
                UiGL::DeleteTextures(2, textures);
            }
            sidebarTexture = 0;
            panelTexture = 0;
        }

        void start() {
            if (chosen.empty() || working) {
                return;
            }
            working = true;
            workingOn = chosen;
#if defined(_WIN32)
            std::wstring file = b_launcher_base::path_from_utf8(chosen).wstring();
#else
            std::filesystem::path file = b_launcher_base::path_from_utf8(chosen);
#endif
            if (worker.joinable()) {
                worker.join();
            }
            worker = std::thread([file] {
#if defined(_WIN32)
                HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
                auto result = std::make_shared<Prepared>();
                Image image;
                if (decode(file, WINDOW_W * SCALE, WINDOW_H * SCALE, image, result->error)) {
                    int split = SIDEBAR_W * SCALE;
                    result->sidebar = crop(blur(image, SIDEBAR_BLUR), 0, split + SHELL_RADIUS * SCALE);
                    result->panel = crop(blur(image, PANEL_BLUR), split, image.width);
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

        void load() {
            if (loaded) {
                return;
            }
            loaded = true;
            chosen = b_launcher_base::json_text(Config::data(), "backgroundPath");
            start();
        }
    }

    uint32_t sidebar() {
        load();
        return chosen.empty() ? Icons::backgroundSidebar() : sidebarTexture;
    }

    uint32_t panel() {
        load();
        return chosen.empty() ? Icons::backgroundPanel() : panelTexture;
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
        start();
    }

    bool preparing() {
        return working;
    }

    void poll() {
        // setPath() may run while this frame's draw list still references the
        // old background. Retire textures here, before the next frame is built.
        if (chosen.empty()) {
            release();
        }
        std::shared_ptr<Prepared> result;
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
            start();
            return;
        }
        if (!result->ok) {
            Notices::add(Notices::Kind::Failed, "背景图片：" + result->error, 4000);
            chosen.clear();
            Config::data()["backgroundPath"] = chosen;
            Config::save();
            release();
            return;
        }
        GLuint nextSidebar = UiImages::upload(result->sidebar.rgba.data(), result->sidebar.width, result->sidebar.height);
        GLuint nextPanel = UiImages::upload(result->panel.rgba.data(), result->panel.width, result->panel.height);
        release();
        sidebarTexture = nextSidebar;
        panelTexture = nextPanel;
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
        release();
    }
}
