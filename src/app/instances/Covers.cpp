#include "app/instances/Covers.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Catalog.h"

#include <core/base/fs.hpp>
#include <core/base/zip.hpp>
#include <core/modpack/remote_zip.hpp>
#include <core/network/http_client.hpp>

#include <zlib.h>

#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Covers {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;
        using Found = std::vector<std::pair<std::string, std::filesystem::path>>;
        using Decoded = std::vector<std::pair<std::string, std::shared_ptr<const Pixels>>>;

        const char* const URL = "https://b-launcher-cdn.cubk.cn/dist.zip";
        const std::string EXTENSION = ".jpg";
        const std::string VECTOR = ".svg";
        const char* const MISSING = "missing.txt";
        const char* const CHECKED = "checked.txt";
        constexpr auto RECHECK = std::chrono::hours(24);
        constexpr size_t WORKERS = 4;

        const char* const APRIL_PREFIX = "april:";
        const char* const STAGE_PREFIX = "stage:";
        const char* const SNAPSHOT_GROUP = "snapshot";

        AsyncTasks tasks;
        std::map<std::string, std::shared_ptr<const Pixels>> images;
        std::map<std::string, std::shared_ptr<const Pixels>> thumbs;
        std::map<std::string, std::shared_ptr<const Pixels>> recent;
        std::set<std::string> thumbing;
        std::map<std::string, std::pair<int, std::shared_ptr<const Pixels>>> icons;
        std::set<std::string> drawing;
        std::set<std::string> settled;
        int pending = 0;
        int wanted = 0;
        int fitted = 0;
        std::set<std::string> asked;
        bool revalidated = false;

        std::string leadingVersion(const std::string& id) {
            size_t end = 0;
            while (end < id.size() && (std::isdigit((unsigned char) id[end]) || id[end] == '.')) {
                end++;
            }
            return id.substr(0, end);
        }

        std::string familyOf(const std::string& base) {
            size_t first = base.find('.');
            if (first == std::string::npos || first == 0) {
                return std::string();
            }
            if (base.compare(0, 2, "1.") == 0) {
                size_t second = base.find('.', first + 1);
                return second == std::string::npos ? base : base.substr(0, second);
            }
            return base.substr(0, first);
        }

        bool weekly(const std::string& id) {
            return id.size() >= 6 && std::isdigit((unsigned char) id[0]) && std::isdigit((unsigned char) id[1]) &&
                    id[2] == 'w';
        }

        std::string stageOf(const std::string& id) {
            bool numbered = id.size() > 1 && std::isdigit((unsigned char) id[1]);
            if (id.rfind("rd-", 0) == 0) {
                return "Pre-Classic";
            }
            if (id.rfind("inf-", 0) == 0) {
                return "Infdev";
            }
            if (id.rfind("in-", 0) == 0) {
                return "Indev";
            }
            if (numbered && id[0] == 'c') {
                return "Classic";
            }
            if (numbered && id[0] == 'a') {
                return "Alpha";
            }
            if (numbered && id[0] == 'b') {
                return "Beta";
            }
            return "远古版";
        }

        std::filesystem::path folder() {
            std::error_code error;
            std::filesystem::path temp = std::filesystem::temp_directory_path(error);
            return error || temp.empty() ? std::filesystem::path() : temp / "b-launcher" / "covers";
        }

        bool plain(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                    c == '-';
        }

        std::string fileName(const std::string& key) {
            std::string out;
            bool gap = false;
            for (char c : key) {
                c = c == ':' ? '-' : c;
                if (plain(c)) {
                    out.push_back(c);
                    gap = false;
                } else if (!gap) {
                    out.push_back('_');
                    gap = true;
                }
            }
            return out + EXTENSION;
        }

        std::string baseName(const std::string& entry) {
            size_t slash = entry.find_last_of("/\\");
            return slash == std::string::npos ? entry : entry.substr(slash + 1);
        }

        bool endsWith(const std::string& name, const std::string& suffix) {
            return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        bool coverName(const std::string& name) {
            return name.front() != '.' && (endsWith(name, EXTENSION) || endsWith(name, VECTOR)) &&
                    std::all_of(name.begin(), name.end(), plain);
        }

        bool present(const std::filesystem::path& file) {
            std::error_code error;
            return std::filesystem::is_regular_file(file, error);
        }

        bool cached(const std::filesystem::path& dir) {
            std::error_code error;
            for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
                if (it->path().extension() == EXTENSION && present(it->path())) {
                    return true;
                }
            }
            return false;
        }

        uint32_t checksum(const std::string& data) {
            return (uint32_t) crc32(crc32(0L, Z_NULL, 0), (const Bytef*) data.data(), (uInt) data.size());
        }

        void unpack(const std::filesystem::path& dir, blb::cancellation_token token) {
            auto data = blb::net::http_get(URL, token);
            if (!data) {
                return;
            }
            auto zip = blb::zip_reader::open_memory(std::move(*data));
            if (!zip) {
                return;
            }
            (void) zip->for_each([&](const blb::zip_entry& entry, blb::zip_reader& reader) {
                std::string name = baseName(entry.name);
                if (!entry.directory && coverName(name)) {
                    if (auto bytes = reader.read_current()) {
                        (void) blb::fs::write_file_atomic(dir / name, *bytes);
                    }
                }
                return !token.is_cancelled();
            });
            if (!token.is_cancelled()) {
                (void) blb::fs::write_file_atomic(dir / CHECKED, std::string());
            }
        }

        bool stale(const std::filesystem::path& marker) {
            std::error_code error;
            auto written = std::filesystem::last_write_time(marker, error);
            return error || std::filesystem::file_time_type::clock::now() - written > RECHECK;
        }

        bool checkedRecently(const std::filesystem::path& marker, const std::set<std::string>& absent) {
            std::error_code error;
            auto written = std::filesystem::last_write_time(marker, error);
            if (error || std::filesystem::file_time_type::clock::now() - written > RECHECK) {
                return false;
            }
            auto text = blb::fs::read_file(marker);
            if (!text) {
                return false;
            }
            std::set<std::string> known;
            std::istringstream lines(*text);
            for (std::string line; std::getline(lines, line);) {
                known.insert(line);
            }
            return std::includes(known.begin(), known.end(), absent.begin(), absent.end());
        }

        int update(const std::filesystem::path& dir, const std::set<std::string>& absent, blb::cancellation_token token,
                   bool everything = false) {
            auto zip = blb::modpack::remote_zip::open({URL}, 0, token);
            if (!zip) {
                return -1;
            }
            int written = 0;
            std::vector<blb::modpack::remote_entry> entries = zip->entries();
            for (const blb::modpack::remote_entry& entry : entries) {
                std::string name = baseName(entry.name);
                if (token.is_cancelled()) {
                    return -1;
                }
                if (!coverName(name)) {
                    continue;
                }
                std::filesystem::path file = dir / name;
                if (absent.count(name) == 0) {
                    auto local = blb::fs::read_file(file);
                    if (local ? checksum(*local) == entry.crc : !everything) {
                        continue;
                    }
                }
                if (auto bytes = zip->read(entry.name, token); bytes && blb::fs::write_file_atomic(file, *bytes)) {
                    written++;
                }
            }
            return written;
        }

        void fill(const std::filesystem::path& dir, const std::set<std::string>& absent,
                  blb::cancellation_token token) {
            if (!cached(dir)) {
                unpack(dir, token);
            } else if (!absent.empty() && !checkedRecently(dir / MISSING, absent) && update(dir, absent, token) >= 0) {
                std::string listed;
                for (const std::string& name : absent) {
                    if (!present(dir / name)) {
                        listed += name + "\n";
                    }
                }
                (void) blb::fs::write_file_atomic(dir / MISSING, listed);
            }
        }

        bool ensure(const std::string& name, blb::cancellation_token token) {
            std::filesystem::path dir = folder();
            if (dir.empty() || !blb::fs::create_directories(dir)) {
                return false;
            }
            if (!present(dir / name)) {
                fill(dir, {name}, token);
            }
            return present(dir / name);
        }

        std::shared_ptr<Pixels> rasterize(const std::filesystem::path& file, int side) {
            auto text = blb::fs::read_file(file);
            if (!text || text->empty()) {
                return nullptr;
            }
            std::string data = *text;
            NSVGimage* image = nsvgParse(data.data(), "px", 96.0f);
            if (image == nullptr || image->width <= 0.0f || image->height <= 0.0f || image->shapes == nullptr) {
                if (image != nullptr) {
                    nsvgDelete(image);
                }
                return nullptr;
            }
            float scale = (float) side / std::max(image->width, image->height);
            auto out = std::make_shared<Pixels>();
            out->width = std::max(1, (int) std::lround(image->width * scale));
            out->height = std::max(1, (int) std::lround(image->height * scale));
            out->rgba.assign((size_t) out->width * out->height * 4, 0);
            NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
            if (rasterizer != nullptr) {
                nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, out->rgba.data(), out->width, out->height,
                              out->width * 4);
                nsvgDeleteRasterizer(rasterizer);
            }
            nsvgDelete(image);
            return rasterizer != nullptr ? out : nullptr;
        }

        void lighten(Pixels& pixels) {
            for (size_t i = 0; i + 3 < pixels.rgba.size(); i += 4) {
                unsigned char* pixel = pixels.rgba.data() + i;
                float luma = 0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2];
                if (pixel[3] > 0 && luma < 64.0f) {
                    pixel[0] = 232;
                    pixel[1] = 232;
                    pixel[2] = 232;
                }
            }
        }

        Found locate(const std::vector<std::string>& keys, bool remote, blb::cancellation_token token) {
            Found found;
            std::filesystem::path dir = folder();
            if (dir.empty() || !blb::fs::create_directories(dir)) {
                return found;
            }
            if (remote) {
                std::set<std::string> absent;
                for (const std::string& key : keys) {
                    if (!present(dir / fileName(key))) {
                        absent.insert(fileName(key));
                    }
                }
                fill(dir, absent, token);
            }
            for (const std::string& key : keys) {
                std::filesystem::path file = dir / fileName(key);
                if (present(file)) {
                    found.emplace_back(key, file);
                }
            }
            return found;
        }

        struct Taps {
            int first = 0;
            std::vector<float> weights;
        };

        float cubic(float x) {
            x = std::fabs(x);
            if (x < 1.0f) {
                return (1.5f * x - 2.5f) * x * x + 1.0f;
            }
            if (x < 2.0f) {
                return ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f;
            }
            return 0.0f;
        }

        std::vector<Taps> taps(int from, int to) {
            std::vector<Taps> out((size_t) to);
            float scale = (float) from / (float) to;
            float stretch = std::max(1.0f, scale);
            for (int i = 0; i < to; i++) {
                float center = (i + 0.5f) * scale;
                int first = std::max(0, (int) std::floor(center - 2.0f * stretch));
                int last = std::min(from - 1, (int) std::ceil(center + 2.0f * stretch));
                Taps& tap = out[(size_t) i];
                tap.first = first;
                float total = 0.0f;
                for (int k = first; k <= last; k++) {
                    float weight = cubic((k + 0.5f - center) / stretch);
                    tap.weights.push_back(weight);
                    total += weight;
                }
                for (float& weight : tap.weights) {
                    weight /= total;
                }
            }
            return out;
        }

        std::shared_ptr<const Pixels> shrink(const Pixels& source, int width, int height) {
            std::vector<Taps> across = taps(source.width, width);
            std::vector<Taps> down = taps(source.height, height);
            std::vector<float> rows((size_t) width * source.height * 3);
            for (int y = 0; y < source.height; y++) {
                const unsigned char* in = source.rgba.data() + (size_t) y * source.width * 4;
                float* to = rows.data() + (size_t) y * width * 3;
                for (int x = 0; x < width; x++, to += 3) {
                    const Taps& tap = across[(size_t) x];
                    for (size_t k = 0; k < tap.weights.size(); k++) {
                        const unsigned char* pixel = in + (size_t) (tap.first + (int) k) * 4;
                        for (int c = 0; c < 3; c++) {
                            to[c] += pixel[c] * tap.weights[k];
                        }
                    }
                }
            }
            auto out = std::make_shared<Pixels>();
            out->width = width;
            out->height = height;
            out->rgba.resize((size_t) width * height * 4);
            for (int y = 0; y < height; y++) {
                const Taps& tap = down[(size_t) y];
                unsigned char* to = out->rgba.data() + (size_t) y * width * 4;
                for (int x = 0; x < width; x++, to += 4) {
                    float sum[3] = {};
                    for (size_t k = 0; k < tap.weights.size(); k++) {
                        const float* pixel = rows.data() + ((size_t) (tap.first + (int) k) * width + x) * 3;
                        for (int c = 0; c < 3; c++) {
                            sum[c] += pixel[c] * tap.weights[k];
                        }
                    }
                    for (int c = 0; c < 3; c++) {
                        to[c] = (unsigned char) std::lround(std::clamp(sum[c], 0.0f, 255.0f));
                    }
                    to[3] = 255;
                }
            }
            return out;
        }

        std::shared_ptr<const Pixels> decode(const std::filesystem::path& file, int height) {
            auto data = blb::fs::read_file(file);
            if (!data) {
                return nullptr;
            }
            std::shared_ptr<const Pixels> full = Pixels::decode(data->data(), data->size());
            if (full == nullptr || full->width <= 0 || full->height <= 0) {
                std::error_code error;
                std::filesystem::remove(file, error);
                return nullptr;
            }
            if (full->height <= height || full->rgba.size() < (size_t) full->width * full->height * 4) {
                return full;
            }
            int width = std::max(1, (int) std::lround((double) full->width * height / full->height));
            return shrink(*full, width, height);
        }

        void settle(const Found& part) {
            for (const auto& entry : part) {
                settled.insert(entry.first);
            }
        }

        void decodeAll(const Found& found, int height) {
            fitted = height;
            size_t slices = std::min(WORKERS, found.size());
            for (size_t slice = 0; slice < slices; slice++) {
                Found part;
                for (size_t i = slice; i < found.size(); i += slices) {
                    part.push_back(found[i]);
                }
                pending++;
                tasks.run(
                        [part, height](blb::cancellation_token token) -> Continuation {
                            Decoded done;
                            for (const auto& [key, file] : part) {
                                if (token.is_cancelled()) {
                                    break;
                                }
                                done.emplace_back(key, decode(file, height));
                            }
                            return [part, done] {
                                pending--;
                                for (const auto& [key, pixels] : done) {
                                    if (pixels != nullptr) {
                                        images[key] = pixels;
                                    }
                                }
                                settle(part);
                            };
                        },
                        [part](const std::string&) -> Continuation {
                            return [part] {
                                pending--;
                                settle(part);
                            };
                        });
            }
        }

        void fetch(const std::vector<std::string>& keys) {
            bool remote = false;
            for (const std::string& key : keys) {
                remote = asked.insert(key).second || remote;
            }
            pending++;
            tasks.run(
                    [keys, remote](blb::cancellation_token token) -> Continuation {
                        Found found = locate(keys, remote, token);
                        return [keys, found] {
                            pending--;
                            std::set<std::string> located;
                            for (const auto& entry : found) {
                                located.insert(entry.first);
                            }
                            for (const std::string& key : keys) {
                                if (located.count(key) == 0) {
                                    settled.insert(key);
                                }
                            }
                            decodeAll(found, wanted);
                        };
                    },
                    [keys](const std::string&) -> Continuation {
                        return [keys] {
                            pending--;
                            settled.insert(keys.begin(), keys.end());
                        };
                    });
        }

        void revalidate() {
            if (revalidated) {
                return;
            }
            revalidated = true;
            tasks.run(
                    [](blb::cancellation_token token) -> Continuation {
                        std::filesystem::path dir = folder();
                        if (dir.empty() || !cached(dir) || !stale(dir / CHECKED)) {
                            return nullptr;
                        }
                        int changed = update(dir, {}, token, true);
                        if (changed < 0) {
                            return nullptr;
                        }
                        (void) blb::fs::write_file_atomic(dir / CHECKED, std::string());
                        if (changed == 0) {
                            return nullptr;
                        }
                        return [] {
                            fitted = 0;
                            thumbs.clear();
                            icons.clear();
                        };
                    },
                    [](const std::string&) -> Continuation { return nullptr; });
        }

        void refit() {
            std::filesystem::path dir = folder();
            Found found;
            for (const auto& entry : images) {
                found.emplace_back(entry.first, dir / fileName(entry.first));
            }
            decodeAll(found, wanted);
        }
    }

    std::string keyOf(const blb::meta::manifest_version& version, std::string& upcoming) {
        Catalog::Kind kind = Catalog::kindOf(version);
        std::string key;
        if (kind == Catalog::Kind::Release) {
            key = familyOf(version.id);
            upcoming = key;
        } else if (kind == Catalog::Kind::AprilFools) {
            key = APRIL_PREFIX + version.id;
        } else if (kind == Catalog::Kind::Old) {
            key = STAGE_PREFIX + stageOf(version.id);
        } else {
            key = weekly(version.id) ? upcoming : familyOf(leadingVersion(version.id));
        }
        if (key.empty()) {
            key = kind == Catalog::Kind::Release ? version.id : SNAPSHOT_GROUP;
        }
        return key;
    }

    std::string keyOf(const std::string& minecraft) {
        std::string upcoming;
        for (const blb::meta::manifest_version& version : Catalog::minecraft().list) {
            std::string key = keyOf(version, upcoming);
            if (version.id == minecraft) {
                return key;
            }
        }
        return familyOf(leadingVersion(minecraft));
    }

    std::shared_ptr<const Pixels> thumb(const std::string& key, int height) {
        revalidate();
        std::string id = key + "#" + std::to_string(height);
        auto found = thumbs.find(id);
        if (found != thumbs.end() && found->second != nullptr) {
            return found->second;
        }
        if (found == thumbs.end() && !key.empty() && height > 0 && thumbing.insert(id).second) {
            std::filesystem::path file = folder() / fileName(key);
            bool remote = asked.insert(key).second;
            tasks.run(
                    [key, id, file, height, remote](blb::cancellation_token token) -> Continuation {
                        if (remote && !present(file)) {
                            (void) ensure(fileName(key), token);
                        }
                        std::shared_ptr<const Pixels> pixels = decode(file, height);
                        return [key, id, pixels] {
                            thumbing.erase(id);
                            thumbs[id] = pixels;
                            if (pixels != nullptr) {
                                recent[key] = pixels;
                            }
                        };
                    },
                    [id](const std::string&) -> Continuation { return [id] { thumbing.erase(id); }; });
        }
        auto last = recent.find(key);
        return last == recent.end() ? nullptr : last->second;
    }

    std::shared_ptr<const Pixels> icon(const std::string& name, int side, bool dark) {
        revalidate();
        std::string id = dark ? name + "#dark" : name;
        auto found = icons.find(id);
        bool fresh = found != icons.end() && found->second.first == side;
        if (!fresh && !name.empty() && side > 0 && drawing.insert(id).second) {
            std::string file = name + VECTOR;
            bool remote = asked.insert(file).second;
            tasks.run(
                    [id, file, side, dark, remote](blb::cancellation_token token) -> Continuation {
                        std::shared_ptr<Pixels> pixels;
                        if (remote ? ensure(file, token) : present(folder() / file)) {
                            pixels = rasterize(folder() / file, side);
                        }
                        if (pixels != nullptr && dark) {
                            lighten(*pixels);
                        }
                        return [id, side, pixels] {
                            drawing.erase(id);
                            std::shared_ptr<const Pixels> kept =
                                    pixels != nullptr || icons.count(id) == 0 ? pixels : icons[id].second;
                            icons[id] = {side, kept};
                        };
                    },
                    [id](const std::string&) -> Continuation { return [id] { drawing.erase(id); }; });
        }
        return found == icons.end() ? nullptr : found->second.second;
    }

    bool ready(const std::vector<std::string>& keys, int height) {
        revalidate();
        wanted = std::max(1, height);
        std::vector<std::string> missing;
        for (const std::string& key : keys) {
            if (settled.count(key) == 0) {
                missing.push_back(key);
            }
        }
        if (pending == 0) {
            if (!missing.empty()) {
                fetch(missing);
            } else if (wanted != fitted && !images.empty()) {
                refit();
            }
        }
        return missing.empty();
    }

    std::shared_ptr<const Pixels> get(const std::string& key) {
        auto found = images.find(key);
        return found == images.end() ? nullptr : found->second;
    }

    void poll() {
        tasks.poll();
    }

    void shutdown() {
        tasks.stop();
    }
}
