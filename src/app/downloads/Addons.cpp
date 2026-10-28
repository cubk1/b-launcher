#include "app/downloads/Addons.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/Modrinth.h"

#include "ui/panel/UiImages.h"

#include <core/base/cancellation.hpp>
#include <core/network/http_client.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Addons {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = std::function<void()>;

        constexpr int ICON_SIZE = 64;
        constexpr int ICON_WORKERS = 4;
        const char* const MIRROR = "https://mod.mcimirror.top";

        std::mutex lock;
        std::deque<Continuation> finished;
        blb::cancellation_source stopping;

        struct Icon {
            uint32_t texture = 0;
            bool requested = false;
        };
        std::unordered_map<std::string, Icon> icons;
        std::mutex iconLock;
        std::condition_variable iconWake;
        std::deque<std::string> iconQueue;
        std::vector<std::thread> iconWorkers;
        bool iconsStopping = false;

        void hand(Continuation next) {
            std::lock_guard<std::mutex> guard(lock);
            finished.push_back(std::move(next));
        }

        void roundCorners(std::vector<unsigned char>& rgba) {
            const float radius = ICON_SIZE * 0.22f;
            for (int y = 0; y < ICON_SIZE; y++) {
                for (int x = 0; x < ICON_SIZE; x++) {
                    float px = x + 0.5f;
                    float py = y + 0.5f;
                    float cx = std::clamp(px, radius, ICON_SIZE - radius);
                    float cy = std::clamp(py, radius, ICON_SIZE - radius);
                    float distance = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
                    float cover = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
                    if (cover < 1.0f) {
                        unsigned char& alpha = rgba[((size_t) y * ICON_SIZE + x) * 4 + 3];
                        alpha = (unsigned char) std::lround(alpha * cover);
                    }
                }
            }
        }

        bool decodeIcon(const std::string& data, std::vector<unsigned char>& rgba) {
            IWICImagingFactory* factory = nullptr;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory)))) {
                return false;
            }
            bool ok = false;
            IWICStream* stream = nullptr;
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICBitmapScaler* scaler = nullptr;
            IWICFormatConverter* converter = nullptr;
            UINT w = 0;
            UINT h = 0;
            if (SUCCEEDED(factory->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory((BYTE*) data.data(), (DWORD) data.size())) &&
                SUCCEEDED(
                        factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0) {
                float fit = (float) ICON_SIZE / (float) std::max(w, h);
                UINT sw = std::max(1u, (UINT) (w * fit + 0.5f));
                UINT sh = std::max(1u, (UINT) (h * fit + 0.5f));
                WICBitmapInterpolationMode mode = std::max(w, h) <= 32 ? WICBitmapInterpolationModeNearestNeighbor
                                                                       : WICBitmapInterpolationModeHighQualityCubic;
                if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame, sw, sh, mode)) &&
                    SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                                    nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    std::vector<unsigned char> scaled((size_t) sw * sh * 4);
                    if (SUCCEEDED(converter->CopyPixels(nullptr, sw * 4, (UINT) scaled.size(), scaled.data()))) {
                        rgba.assign((size_t) ICON_SIZE * ICON_SIZE * 4, 0);
                        UINT ox = (ICON_SIZE - sw) / 2;
                        UINT oy = (ICON_SIZE - sh) / 2;
                        for (UINT row = 0; row < sh; row++) {
                            std::copy(scaled.begin() + (size_t) row * sw * 4,
                                      scaled.begin() + (size_t) (row + 1) * sw * 4,
                                      rgba.begin() + ((size_t) (oy + row) * ICON_SIZE + ox) * 4);
                        }
                        roundCorners(rgba);
                        ok = true;
                    }
                }
            }
            if (converter) converter->Release();
            if (scaler) scaler->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (stream) stream->Release();
            factory->Release();
            return ok;
        }

        void iconWorker() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            blb::cancellation_token token = stopping.token();
            while (true) {
                std::string url;
                {
                    std::unique_lock<std::mutex> guard(iconLock);
                    iconWake.wait(guard, [] { return iconsStopping || !iconQueue.empty(); });
                    if (iconsStopping) {
                        break;
                    }
                    url = iconQueue.front();
                    iconQueue.pop_front();
                }
                auto data = blb::net::http_get(fileUrls(url), token);
                auto rgba = std::make_shared<std::vector<unsigned char>>();
                if (data && decodeIcon(*data, *rgba)) {
                    hand([url, rgba] { icons[url].texture = UiImages::upload(rgba->data(), ICON_SIZE, ICON_SIZE); });
                }
            }
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
        }

        bool startsWith(const std::string& text, const char* head) {
            return text.compare(0, std::strlen(head), head) == 0;
        }
    }

    const File* Version::file() const {
        for (const File& f : files) {
            if (f.primary) {
                return &f;
            }
        }
        return files.empty() ? nullptr : &files.front();
    }

    const char* sourceName(Source source) {
        return source == Source::CurseForge ? "CurseForge" : "Modrinth";
    }

    const Results& search(Source source, const Query& query) {
        return source == Source::CurseForge ? CurseForge::search(query) : Modrinth::search(query);
    }

    void refresh(Source source, const Query& query) {
        if (source == Source::CurseForge) {
            CurseForge::refresh(query);
        } else {
            Modrinth::refresh(query);
        }
    }

    const std::vector<Category>& categories(Source source, Kind kind) {
        return source == Source::CurseForge ? CurseForge::categories(kind) : Modrinth::categories(kind);
    }

    const Versions& versions(Source source, const std::string& projectId) {
        return source == Source::CurseForge ? CurseForge::versions(projectId) : Modrinth::versions(projectId);
    }

    void refreshVersions(Source source, const std::string& projectId) {
        if (source == Source::CurseForge) {
            CurseForge::refreshVersions(projectId);
        } else {
            Modrinth::refreshVersions(projectId);
        }
    }

    Fetched fetchVersion(Source source, const std::string& versionId) {
        return source == Source::CurseForge ? CurseForge::fetchVersion(versionId) : Modrinth::fetchVersion(versionId);
    }

    Fetched fetchProjectVersions(Source source, const std::string& projectId, const std::string& gameVersion,
                                 const std::string& loader) {
        return source == Source::CurseForge ? CurseForge::fetchProjectVersions(projectId, gameVersion, loader)
                                            : Modrinth::fetchProjectVersions(projectId, gameVersion, loader);
    }

    uint32_t icon(const std::string& url) {
        if (url.empty()) {
            return 0;
        }
        Icon& entry = icons[url];
        if (!entry.requested) {
            entry.requested = true;
            std::lock_guard<std::mutex> guard(iconLock);
            if (iconWorkers.empty() && !iconsStopping) {
                for (int i = 0; i < ICON_WORKERS; i++) {
                    iconWorkers.emplace_back(iconWorker);
                }
            }
            iconQueue.push_back(url);
            iconWake.notify_one();
        }
        return entry.texture;
    }

    std::string count(long long value) {
        char text[32];
        if (value >= 100000000LL) {
            std::snprintf(text, sizeof(text), "%.1f亿", value / 100000000.0);
        } else if (value >= 10000LL) {
            std::snprintf(text, sizeof(text), "%.1f万", value / 10000.0);
        } else {
            std::snprintf(text, sizeof(text), "%lld", value);
        }
        return text;
    }

    std::vector<std::string> fileUrls(const std::string& url) {
        std::vector<std::string> out = {url};
        const char* const modrinth = "https://cdn.modrinth.com";
        if (startsWith(url, modrinth)) {
            out.push_back(std::string(MIRROR) + url.substr(std::strlen(modrinth)));
        }
        for (const char* host : {"https://edge.forgecdn.net", "https://mediafilez.forgecdn.net"}) {
            if (!startsWith(url, host)) {
                continue;
            }
            std::string rest = url.substr(std::strlen(host));
            if (std::strcmp(host, "https://edge.forgecdn.net") == 0) {
                out.push_back("https://mediafilez.forgecdn.net" + rest);
            }
            out.push_back(std::string(MIRROR) + rest);
        }
        return out;
    }

    void preload() {
        Modrinth::preload();
        CurseForge::preload();
    }

    void poll() {
        Modrinth::poll();
        CurseForge::poll();
        std::deque<Continuation> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Continuation& next : ready) {
            if (next) {
                next();
            }
        }
    }

    void shutdown() {
        stopping.cancel();
        {
            std::lock_guard<std::mutex> guard(iconLock);
            iconsStopping = true;
        }
        iconWake.notify_all();
        for (std::thread& worker : iconWorkers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        iconWorkers.clear();
        Modrinth::shutdown();
        CurseForge::shutdown();
    }
}
