#include "WebImages.h"
#include "Icons.h"
#include "Platform.h"

#include "ui/panel/UiImages.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>
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
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace WebImages {

    namespace blb = b_launcher_base;

    namespace {
        constexpr UINT MAX_SIDE = 2048;
        constexpr auto CACHE_AGE = std::chrono::hours(24 * 7);

        struct Job {
            std::string key;
            std::string source;
            std::string fallback;
        };

        struct Decoded {
            std::string key;
            std::vector<unsigned char> rgba;
            int width = 0;
            int height = 0;
        };

        std::map<std::string, Image> images;
        long long generationCount = 0;
        std::mutex lock;
        std::condition_variable wake;
        std::deque<Job> jobs;
        std::deque<Decoded> finished;
        std::thread worker;
        bool stopping = false;
        blb::cancellation_source cancel;

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            return text;
        }

        bool remote(const std::string& source) {
            std::string head = lower(source.substr(0, 8));
            return head.rfind("http://", 0) == 0 || head.rfind("https://", 0) == 0;
        }

        std::optional<Image> builtin(const std::string& source) {
            std::string text = lower(source);
            if (text.rfind("pack://", 0) != 0) {
                return std::nullopt;
            }
            size_t slash = text.find_last_of('/');
            std::string name = slash == std::string::npos ? text : text.substr(slash + 1);
            size_t dot = name.find('.');
            if (dot != std::string::npos) {
                name.resize(dot);
            }
            using b_launcher_base::install::loader_type;
            uint32_t texture = Icons::vanilla();
            if (name == "fabric") {
                texture = Icons::loader(loader_type::fabric);
            } else if (name == "neoforge") {
                texture = Icons::loader(loader_type::neoforge);
            } else if (name == "anvil") {
                texture = Icons::forge();
            } else if (name == "quilt") {
                texture = Icons::loader(loader_type::quilt);
            } else if (name == "optifine") {
                texture = Icons::loader(loader_type::optifine);
            }
            Image image;
            image.done = true;
            image.failed = texture == 0;
            image.texture = texture;
            image.width = texture != 0 ? 32 : 0;
            image.height = texture != 0 ? 32 : 0;
            return image;
        }

        std::filesystem::path cacheFile(const std::string& url) {
            std::filesystem::path cache = Platform::cacheDirectory();
            if (cache.empty()) {
                return {};
            }
            return cache / "homepage" / "images" / blb::to_hex(blb::sha1::digest(url));
        }

        bool fresh(const std::filesystem::path& file) {
            std::error_code error;
            auto written = std::filesystem::last_write_time(file, error);
            if (error) {
                return false;
            }
            return std::filesystem::file_time_type::clock::now() - written < CACHE_AGE;
        }

        std::optional<std::string> bytesOf(const std::string& source) {
            if (source.empty()) {
                return std::nullopt;
            }
            if (!remote(source)) {
                std::filesystem::path path = blb::path_from_utf8(source);
                if (path.is_relative()) {
                    path = Platform::exeDirectory() / path;
                }
                if (auto data = blb::fs::read_file(path)) {
                    return std::move(*data);
                }
                return std::nullopt;
            }
            std::filesystem::path file = cacheFile(source);
            if (!file.empty() && fresh(file)) {
                if (auto data = blb::fs::read_file(file)) {
                    return std::move(*data);
                }
            }
            blb::net::http_request request;
            request.url = source;
            request.headers.push_back({"User-Agent", Platform::USER_AGENT});
            request.timeout = std::chrono::seconds(30);
            auto response = blb::net::http_send(request, cancel.token());
            std::optional<std::string> data;
            if (response && response->ok()) {
                data = std::move(response->body);
            }
            if (!data) {
                if (!file.empty()) {
                    if (auto stale = blb::fs::read_file(file)) {
                        return std::move(*stale);
                    }
                }
                return std::nullopt;
            }
            if (!file.empty() && blb::fs::create_directories(file.parent_path())) {
                (void) blb::fs::write_file_atomic(file, *data);
            }
            return std::move(*data);
        }

        bool decode(IWICImagingFactory* factory, const std::string& data, Decoded& out) {
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
                float fit = std::min(1.0f, (float) MAX_SIDE / (float) std::max(w, h));
                UINT sw = std::max(1u, (UINT) (w * fit + 0.5f));
                UINT sh = std::max(1u, (UINT) (h * fit + 0.5f));
                IWICBitmapSource* input = frame;
                if ((sw != w || sh != h) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeHighQualityCubic))) {
                    input = scaler;
                } else {
                    sw = w;
                    sh = h;
                }
                if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(input, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                                    nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    out.rgba.resize((size_t) sw * sh * 4);
                    if (SUCCEEDED(converter->CopyPixels(nullptr, sw * 4, (UINT) out.rgba.size(), out.rgba.data()))) {
                        out.width = (int) sw;
                        out.height = (int) sh;
                        ok = true;
                    }
                }
            }
            if (converter) converter->Release();
            if (scaler) scaler->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (stream) stream->Release();
            return ok;
        }

        void run() {
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            IWICImagingFactory* factory = nullptr;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            while (true) {
                Job job;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [] { return stopping || !jobs.empty(); });
                    if (stopping) {
                        break;
                    }
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                Decoded result;
                result.key = job.key;
                bool ok = false;
                if (factory != nullptr) {
                    for (const std::string& source : {job.source, job.fallback}) {
                        if (std::optional<std::string> data = bytesOf(source); data && decode(factory, *data, result)) {
                            ok = true;
                            break;
                        }
                    }
                }
                if (!ok) {
                    result.rgba.clear();
                    result.width = 0;
                    result.height = 0;
                }
                std::lock_guard<std::mutex> guard(lock);
                finished.push_back(std::move(result));
            }
            if (factory != nullptr) {
                factory->Release();
            }
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
        }
    }

    const Image& get(const std::string& source, const std::string& fallback) {
        std::string key = source + '\x1f' + fallback;
        auto found = images.find(key);
        if (found != images.end()) {
            return found->second;
        }
        Image& image = images[key];
        if (std::optional<Image> local = builtin(source)) {
            image = *local;
            if (image.failed && !fallback.empty()) {
                image = Image();
            } else {
                return image;
            }
        }
        std::lock_guard<std::mutex> guard(lock);
        if (!worker.joinable() && !stopping) {
            worker = std::thread(run);
        }
        jobs.push_back(Job{key, source, fallback});
        wake.notify_one();
        return image;
    }

    long long generation() {
        return generationCount;
    }

    void poll() {
        std::deque<Decoded> ready;
        {
            std::lock_guard<std::mutex> guard(lock);
            ready.swap(finished);
        }
        for (Decoded& result : ready) {
            Image& image = images[result.key];
            image.done = true;
            if (result.rgba.empty()) {
                image.failed = true;
            } else {
                image.texture = UiImages::upload(result.rgba.data(), result.width, result.height);
                image.width = result.width;
                image.height = result.height;
                image.failed = image.texture == 0;
            }
            generationCount++;
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
            jobs.clear();
        }
        cancel.cancel();
        wake.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
    }
}
