#include "ui/imgui/screens/theme/ThemeAssets.h"

#include "app/system/Pixels.h"
#include "ui/imgui/gl/UiGL.h"
#include "ui/imgui/screens/theme/ThemeStore.h"
#include "ui/imgui/theme/ThemePaintSpec.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace ThemeAssets {

    namespace {
        constexpr int MAX_SIDE = 4096;
        constexpr size_t BUDGET = (size_t) 128 << 20;

        struct Job {
            std::string source;
            ThemeStore::Source where;
        };

        struct Result {
            std::string source;
            Pixels pixels;
            bool ok = false;
        };

        std::map<std::string, Texture> textures;
        std::map<std::string, std::string> failures;
        size_t used = 0;
        unsigned seen = 0;
        bool collected = false;

        std::mutex lock;
        std::condition_variable wake;
        std::deque<Job> jobs;
        std::vector<Result> results;
        std::thread worker;
        bool stopping = false;

        size_t cost(int width, int height) {
            return (size_t) width * (size_t) height * 16 / 3;
        }

        std::string pathOf(const std::string& source) {
            std::string owner;
            std::string path;
            return ThemePaintSpec::splitSource(source, owner, path) ? owner + "/" + path : source;
        }

        Result decode(const Job& job) {
            Result result;
            result.source = job.source;
            std::optional<std::string> bytes = ThemeStore::contents(job.where);
            if (!bytes) {
                return result;
            }
            std::shared_ptr<const Pixels> pixels = Pixels::decode(bytes->data(), bytes->size());
            if (!pixels || pixels->width <= 0 || pixels->height <= 0) {
                return result;
            }
            int side = std::max(pixels->width, pixels->height);
            if (side > MAX_SIDE) {
                float scale = (float) MAX_SIDE / (float) side;
                int width = std::max(1, (int) std::lround(pixels->width * scale));
                int height = std::max(1, (int) std::lround(pixels->height * scale));
                result.pixels = Pixels::resized(*pixels, 0, 0, pixels->width, pixels->height, width, height);
            } else {
                result.pixels = *pixels;
            }
            result.ok = !result.pixels.rgba.empty();
            return result;
        }

        void run() {
            while (true) {
                Job job;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [] { return stopping || !jobs.empty(); });
                    if (stopping) {
                        return;
                    }
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                Result result = decode(job);
                std::lock_guard<std::mutex> guard(lock);
                results.push_back(std::move(result));
            }
        }

        uint32_t upload(const Pixels& pixels) {
            GLuint id = 0;
            UiGL::GenTextures(1, &id);
            UiGL::ActiveTexture(UGL_TEXTURE0);
            UiGL::BindTexture(UGL_TEXTURE_2D, id);
            UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MIN_FILTER, UGL_LINEAR_MIPMAP_LINEAR);
            UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_MAG_FILTER, UGL_LINEAR);
            UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_S, UGL_REPEAT);
            UiGL::TexParameteri(UGL_TEXTURE_2D, UGL_TEXTURE_WRAP_T, UGL_REPEAT);
            UiGL::PixelStorei(UGL_UNPACK_ROW_LENGTH, 0);
            UiGL::PixelStorei(UGL_UNPACK_ALIGNMENT, 4);
            UiGL::TexImage2D(UGL_TEXTURE_2D, 0, UGL_RGBA8, pixels.width, pixels.height, 0, UGL_RGBA, UGL_UNSIGNED_BYTE,
                             pixels.rgba.data());
            UiGL::GenerateMipmap(UGL_TEXTURE_2D);
            UiGL::BindTexture(UGL_TEXTURE_2D, 0);
            return id;
        }

        void release(Texture& texture) {
            if (texture.id != 0) {
                GLuint id = texture.id;
                UiGL::DeleteTextures(1, &id);
                used -= std::min(used, cost(texture.width, texture.height));
            }
            texture = Texture();
        }

        void collect() {
            unsigned generation = ThemePaintSpec::generation();
            if (collected && generation == seen) {
                return;
            }
            collected = true;
            seen = generation;
            std::set<std::string> wanted;
            const ThemePaintSpec::Spec& spec = ThemePaintSpec::current();
            for (const ThemePaintSpec::RegionSpec& region : spec.regions) {
                for (const ThemePaintSpec::Layer& layer : region.layers) {
                    if (!layer.source.empty() && layer.kind == ThemePaintSpec::Kind::Image) {
                        wanted.insert(layer.source);
                    }
                    wanted.insert(layer.textures.begin(), layer.textures.end());
                }
            }
            for (const ThemePaintSpec::Decoration& decoration : spec.decorations) {
                wanted.insert(decoration.source);
            }
            for (auto it = textures.begin(); it != textures.end();) {
                if (wanted.count(it->first) != 0) {
                    ++it;
                    continue;
                }
                release(it->second);
                failures.erase(it->first);
                it = textures.erase(it);
            }
            std::lock_guard<std::mutex> guard(lock);
            jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                      [&](const Job& job) { return wanted.count(job.source) == 0; }),
                       jobs.end());
        }
    }

    const Texture& get(const std::string& source) {
        auto found = textures.find(source);
        if (found != textures.end()) {
            return found->second;
        }
        Texture& texture = textures[source];
        std::string owner;
        std::string path;
        std::optional<ThemeStore::Source> where;
        if (ThemePaintSpec::splitSource(source, owner, path)) {
            where = ThemeStore::locate(owner, path);
        }
        if (!where) {
            texture.failed = true;
            failures[source] = "找不到贴图 " + pathOf(source);
            return texture;
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            jobs.push_back({source, *where});
            if (!worker.joinable()) {
                stopping = false;
                worker = std::thread(run);
            }
        }
        wake.notify_one();
        return texture;
    }

    void poll() {
        collect();
        std::vector<Result> finished;
        {
            std::lock_guard<std::mutex> guard(lock);
            finished.swap(results);
        }
        if (finished.empty() || (!UiGL::loaded() && !UiGL::load())) {
            return;
        }
        for (Result& result : finished) {
            auto found = textures.find(result.source);
            if (found == textures.end() || found->second.ready || found->second.failed) {
                continue;
            }
            Texture& texture = found->second;
            if (!result.ok) {
                texture.failed = true;
                failures[result.source] = "无法读取贴图 " + pathOf(result.source);
                continue;
            }
            size_t need = cost(result.pixels.width, result.pixels.height);
            if (used + need > BUDGET) {
                texture.failed = true;
                failures[result.source] = "贴图超出 128 MB 显存预算 " + pathOf(result.source);
                continue;
            }
            texture.id = upload(result.pixels);
            texture.width = result.pixels.width;
            texture.height = result.pixels.height;
            texture.ready = true;
            used += need;
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
            jobs.clear();
        }
        wake.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            results.clear();
        }
        for (auto& entry : textures) {
            release(entry.second);
        }
        textures.clear();
        failures.clear();
        used = 0;
        collected = false;
    }

    void reset() {
        {
            std::lock_guard<std::mutex> guard(lock);
            jobs.clear();
            results.clear();
        }
        for (auto& entry : textures) {
            release(entry.second);
        }
        textures.clear();
        failures.clear();
        used = 0;
    }

    std::vector<std::string> problems() {
        std::vector<std::string> out;
        for (const auto& entry : failures) {
            out.push_back(entry.second);
        }
        return out;
    }
}
