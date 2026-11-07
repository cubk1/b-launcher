#include "app/downloads/Addons.h"
#include "app/downloads/Aggregate.h"
#include "app/downloads/Authors.h"
#include "app/downloads/CurseForge.h"
#include "app/downloads/Dependencies.h"
#include "app/downloads/DownloadSources.h"
#include "app/downloads/Mcmod.h"
#include "app/downloads/ModNames.h"
#include "app/downloads/Modrinth.h"
#include "app/downloads/PackContents.h"
#include "app/downloads/Projects.h"
#include "app/downloads/Recent.h"
#include "app/system/CallbackQueue.h"

#include <core/base/cancellation.hpp>
#include <core/network/http_client.hpp>

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

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Addons {

    namespace blb = b_launcher_base;

    namespace {
        constexpr int ICON_SIZE = 64;
        // 16 KB each: the icons used last, the categories' and a few pages of results.
        constexpr size_t KEPT_ICONS = 400;
        constexpr int ICON_WORKERS = 4;

        CallbackQueue finished;
        blb::cancellation_source stopping;

        struct Icon {
            std::shared_ptr<const Pixels> pixels;
            bool requested = false;
        };
        std::unordered_map<std::string, Icon> icons;
        Recent iconUse;
        std::mutex iconLock;
        std::condition_variable iconWake;
        std::deque<std::string> iconQueue;
        std::vector<std::thread> iconWorkers;
        bool iconsStopping = false;

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

#if defined(_WIN32)
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
#else
        bool decodeIcon(const std::string& data, std::vector<unsigned char>& rgba) {
            std::shared_ptr<const Pixels> source = Pixels::decode(data.data(), data.size());
            if (!source || source->width <= 0 || source->height <= 0) {
                return false;
            }
            int w = source->width;
            int h = source->height;
            float fit = (float) ICON_SIZE / (float) std::max(w, h);
            int sw = std::max(1, (int) (w * fit + 0.5f));
            int sh = std::max(1, (int) (h * fit + 0.5f));
            std::vector<unsigned char> scaled;
            if (std::max(w, h) <= 32) {
                scaled.resize((size_t) sw * sh * 4);
                for (int y = 0; y < sh; y++) {
                    for (int x = 0; x < sw; x++) {
                        const unsigned char* from =
                                source->rgba.data() + ((size_t) (y * h / sh) * w + (size_t) (x * w / sw)) * 4;
                        std::copy(from, from + 4, scaled.begin() + ((size_t) y * sw + x) * 4);
                    }
                }
            } else {
                scaled = Pixels::resized(*source, 0, 0, w, h, sw, sh).rgba;
            }
            if (scaled.size() != (size_t) sw * sh * 4) {
                return false;
            }
            rgba.assign((size_t) ICON_SIZE * ICON_SIZE * 4, 0);
            int ox = (ICON_SIZE - sw) / 2;
            int oy = (ICON_SIZE - sh) / 2;
            for (int row = 0; row < sh; row++) {
                std::copy(scaled.begin() + (size_t) row * sw * 4, scaled.begin() + (size_t) (row + 1) * sw * 4,
                          rgba.begin() + ((size_t) (oy + row) * ICON_SIZE + ox) * 4);
            }
            roundCorners(rgba);
            return true;
        }
#endif

        void iconWorker() {
#if defined(_WIN32)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
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
                    finished.post([url, rgba] {
                        icons[url].pixels = std::make_shared<const Pixels>(Pixels{ICON_SIZE, ICON_SIZE, *rgba});
                    });
                }
            }
#if defined(_WIN32)
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
#endif
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

    Fetched fetchVersion(Source source, const std::string& versionId, blb::cancellation_token token) {
        return source == Source::CurseForge ? CurseForge::fetchVersion(versionId, token)
                                            : Modrinth::fetchVersion(versionId, token);
    }

    Fetched fetchProjectVersions(Source source, const std::string& projectId, const std::string& gameVersion,
                                 const std::string& loader, blb::cancellation_token token) {
        return source == Source::CurseForge ? CurseForge::fetchProjectVersions(projectId, gameVersion, loader, token)
                                            : Modrinth::fetchProjectVersions(projectId, gameVersion, loader, token);
    }

    std::shared_ptr<const Pixels> icon(const std::string& url) {
        if (url.empty()) {
            return nullptr;
        }
        iconUse.use(url);
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
        return entry.pixels;
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
        std::vector<std::string> official = {url};
        const char* const edge = "https://edge.forgecdn.net";
        if (url.starts_with(edge)) {
            official.push_back("https://mediafilez.forgecdn.net" + url.substr(std::strlen(edge)));
        }
        return DownloadSources::provider()->resolve_all(official);
    }

    void preload() {
        Modrinth::preload();
        CurseForge::preload();
    }

    void poll() {
        Modrinth::poll();
        CurseForge::poll();
        Aggregate::poll();
        ModNames::poll();
        Projects::poll();
        Mcmod::poll();
        Authors::poll();
        PackContents::poll();
        Dependencies::poll();
        finished.poll();
        // An icon still on its way, or that failed, is only its address.
        iconUse.trim(icons, KEPT_ICONS, [](const Icon& icon) { return icon.pixels != nullptr; });
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
        Aggregate::shutdown();
        ModNames::shutdown();
        Projects::shutdown();
        Mcmod::shutdown();
        Authors::shutdown();
        PackContents::shutdown();
        Dependencies::shutdown();
    }
}
