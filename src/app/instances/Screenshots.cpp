#include "app/instances/Screenshots.h"
#include "app/instances/Instances.h"
#include "app/system/AsyncTasks.h"
#include "app/system/Notices.h"
#include "app/system/Platform.h"

#include <core/base/cancellation.hpp>
#include <core/base/fs.hpp>
#include <core/base/strings.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace Screenshots {

    namespace blb = b_launcher_base;

    namespace {
        using Continuation = AsyncTasks::Continuation;

        constexpr uint32_t THUMB_SIDE = 320;
        constexpr size_t MAX_CACHED = 192;

        AsyncTasks tasks;
        std::map<std::string, List> lists;
        int nextSerial = 1;
        const List noList;

        struct Thumb {
            std::shared_ptr<const Pixels> pixels;
            std::int64_t modified = 0;
            uint64_t used = 0;
            bool requested = false;
            bool failed = false;
        };
        std::map<std::string, Thumb> thumbs;
        uint64_t thumbClock = 0;

        struct Full {
            std::string key;
            std::shared_ptr<const Pixels> pixels;
            bool requested = false;
            bool failed = false;
        };
        Full fullOne;

        bool isImage(const std::filesystem::path& path) {
            std::string ext = blb::to_lower(blb::path_to_utf8(path.extension()));
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
        }

        std::string thumbKey(const std::filesystem::path& path, std::int64_t modified) {
            return blb::path_to_utf8(path) + "#" + std::to_string(modified);
        }

        std::filesystem::path folderOf(const std::string& instanceKey) {
            const Instances::Entry* entry = Instances::find(instanceKey);
            return entry != nullptr ? entry->gameDirectory / "screenshots" : std::filesystem::path();
        }

        std::shared_ptr<const Pixels> scale(const Pixels& source, uint32_t maxSide) {
            uint32_t srcMax = std::max((uint32_t) source.width, (uint32_t) source.height);
            if (srcMax <= maxSide) {
                return std::make_shared<Pixels>(source);
            }
            double ratio = (double) maxSide / srcMax;
            int newW = std::max(1, (int) std::lround(source.width * ratio));
            int newH = std::max(1, (int) std::lround(source.height * ratio));
            auto out = std::make_shared<Pixels>();
            out->width = newW;
            out->height = newH;
            out->rgba.resize((size_t) newW * newH * 4);
            double stepX = (double) source.width / newW;
            double stepY = (double) source.height / newH;
            for (int y = 0; y < newH; y++) {
                int sy0 = std::max(0, (int) (y * stepY));
                int sy1 = std::min(source.height, std::max(sy0 + 1, (int) ((y + 1) * stepY)));
                for (int x = 0; x < newW; x++) {
                    int sx0 = std::max(0, (int) (x * stepX));
                    int sx1 = std::min(source.width, std::max(sx0 + 1, (int) ((x + 1) * stepX)));
                    uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int sy = sy0; sy < sy1; sy++) {
                        const unsigned char* row = source.rgba.data() + ((size_t) sy * source.width + sx0) * 4;
                        for (int sx = sx0; sx < sx1; sx++) {
                            r += row[0];
                            g += row[1];
                            b += row[2];
                            a += row[3];
                            n++;
                            row += 4;
                        }
                    }
                    unsigned char* to = out->rgba.data() + ((size_t) y * newW + x) * 4;
                    if (n == 0) {
                        to[0] = to[1] = to[2] = to[3] = 0;
                    } else {
                        to[0] = (unsigned char) (r / n);
                        to[1] = (unsigned char) (g / n);
                        to[2] = (unsigned char) (b / n);
                        to[3] = (unsigned char) (a / n);
                    }
                }
            }
            return out;
        }

        void trimCache() {
            while (thumbs.size() > MAX_CACHED) {
                auto victim = std::min_element(thumbs.begin(), thumbs.end(), [](const auto& a, const auto& b) {
                    return a.second.used < b.second.used;
                });
                if (victim == thumbs.end()) {
                    break;
                }
                thumbs.erase(victim);
            }
        }
    }

    const List& of(const std::string& instanceKey) {
        auto it = lists.find(instanceKey);
        return it != lists.end() ? it->second : noList;
    }

    void scan(const std::string& instanceKey) {
        List& list = lists[instanceKey];
        if (list.loading) {
            list.serial = nextSerial++;
            return;
        }
        std::filesystem::path folder = folderOf(instanceKey);
        if (folder.empty()) {
            return;
        }
        list.loading = true;
        int serial = list.serial;
        tasks.run(
                [folder, instanceKey, serial](blb::cancellation_token token) -> Continuation {
                    std::vector<Entry> found;
                    auto map = blb::fs::list_directory(folder);
                    for (const auto& [name, stat] : map) {
                        if (token.is_cancelled()) {
                            return nullptr;
                        }
                        if (stat.is_directory || stat.is_link) {
                            continue;
                        }
                        std::filesystem::path path = folder / blb::path_from_utf8(name);
                        if (!isImage(path)) {
                            continue;
                        }
                        Entry e;
                        e.path = path;
                        e.filename = name;
                        e.size = stat.size;
                        e.modified = stat.mtime;
                        found.push_back(std::move(e));
                    }
                    std::sort(found.begin(), found.end(),
                              [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
                    return [instanceKey, serial, found = std::move(found)]() mutable {
                        List& list = lists[instanceKey];
                        list.loading = false;
                        if (list.serial != serial) {
                            scan(instanceKey);
                            return;
                        }
                        for (const Entry& entry : found) {
                            auto cached = thumbs.find(thumbKey(entry.path, entry.modified));
                            if (cached != thumbs.end() && cached->second.failed) {
                                thumbs.erase(cached);
                            }
                        }
                        list.entries = std::move(found);
                        list.serial = nextSerial++;
                    };
                },
                [instanceKey, serial](const std::string& error) -> Continuation {
                    return [instanceKey, serial, error] {
                        List& list = lists[instanceKey];
                        list.loading = false;
                        if (list.serial != serial) {
                            scan(instanceKey);
                            return;
                        }
                        Notices::add(Notices::Kind::Failed, "无法读取截图：" + error, 5000);
                    };
                });
    }

    std::shared_ptr<const Pixels> thumbnail(const std::filesystem::path& path, std::int64_t modified) {
        std::string key = thumbKey(path, modified);
        Thumb& thumb = thumbs[key];
        thumb.used = ++thumbClock;
        if (thumb.pixels || thumb.failed) {
            return thumb.pixels;
        }
        if (!thumb.requested) {
            thumb.requested = true;
            thumb.modified = modified;
            std::filesystem::path copied = path;
            tasks.run(
                    [key, copied](blb::cancellation_token token) -> Continuation {
                        auto data = blb::fs::read_file(copied);
                        if (!data || token.is_cancelled()) {
                            return [key] { thumbs[key].failed = true; };
                        }
                        std::shared_ptr<const Pixels> full = Pixels::decode(data->data(), data->size());
                        if (!full) {
                            return [key] { thumbs[key].failed = true; };
                        }
                        std::shared_ptr<const Pixels> small = scale(*full, THUMB_SIDE);
                        return [key, small] { thumbs[key].pixels = small; };
                    },
                    [key](const std::string&) -> Continuation { return [key] { thumbs[key].failed = true; }; });
        }
        return nullptr;
    }

    std::shared_ptr<const Pixels> fullImage(const std::filesystem::path& path, std::int64_t modified) {
        std::string key = thumbKey(path, modified);
        if (fullOne.key != key) {
            fullOne = {};
            fullOne.key = key;
        }
        if (fullOne.pixels || fullOne.failed) {
            return fullOne.pixels;
        }
        if (!fullOne.requested) {
            fullOne.requested = true;
            std::filesystem::path copied = path;
            std::string captured = key;
            tasks.run(
                    [captured, copied](blb::cancellation_token token) -> Continuation {
                        auto data = blb::fs::read_file(copied);
                        if (!data || token.is_cancelled()) {
                            return [captured] {
                                if (fullOne.key == captured) {
                                    fullOne.failed = true;
                                }
                            };
                        }
                        std::shared_ptr<const Pixels> pixels = Pixels::decode(data->data(), data->size());
                        return [captured, pixels] {
                            if (fullOne.key != captured) {
                                return;
                            }
                            if (pixels) {
                                fullOne.pixels = pixels;
                            } else {
                                fullOne.failed = true;
                            }
                        };
                    },
                    [captured](const std::string&) -> Continuation {
                        return [captured] {
                            if (fullOne.key == captured) {
                                fullOne.failed = true;
                            }
                        };
                    });
        }
        return nullptr;
    }

    bool fullImageFailed(const std::filesystem::path& path, std::int64_t modified) {
        return fullOne.key == thumbKey(path, modified) && fullOne.failed;
    }

    void forgetFullImage() {
        fullOne = {};
    }

    void copy(const std::filesystem::path& path) {
        std::filesystem::path copied = path;
        tasks.run(
                [copied](blb::cancellation_token token) -> Continuation {
                    auto data = blb::fs::read_file(copied);
                    if (!data) {
                        return [] { Notices::add(Notices::Kind::Failed, "无法读取截图", 4000); };
                    }
                    if (token.is_cancelled()) {
                        return nullptr;
                    }
                    std::shared_ptr<const Pixels> pixels = Pixels::decode(data->data(), data->size());
                    if (!pixels) {
                        return [] { Notices::add(Notices::Kind::Failed, "无法解码截图", 4000); };
                    }
                    return [pixels] {
                        bool ok = Platform::setClipboardImage(*pixels);
                        if (ok) {
                            Notices::add(Notices::Kind::Done, "已复制到剪贴板", 2500);
                        } else {
                            Notices::add(Notices::Kind::Failed, "无法复制到剪贴板", 4000);
                        }
                    };
                },
                nullptr);
    }

    void remove(const std::string& instanceKey, const std::filesystem::path& path) {
        if (!Platform::recycle(path)) {
            Notices::add(Notices::Kind::Failed, "无法删除截图", 4000);
            return;
        }
        List& list = lists[instanceKey];
        list.entries.erase(std::remove_if(list.entries.begin(), list.entries.end(),
                                          [&path](const Entry& e) { return e.path == path; }),
                           list.entries.end());
        list.serial = nextSerial++;
    }

    bool rename(const std::string& instanceKey, const std::filesystem::path& path, const std::string& newName) {
        if (!blb::fs::safe_filename(newName)) {
            Notices::add(Notices::Kind::Failed, "文件名无效", 4000);
            return false;
        }
        std::filesystem::path target = path.parent_path() / blb::path_from_utf8(newName);
        if (target == path) {
            return true;
        }
        if (blb::fs::stat(target).exists) {
            Notices::add(Notices::Kind::Failed, "已存在同名文件", 4000);
            return false;
        }
        auto result = blb::fs::rename_file(path, target);
        if (!result) {
            Notices::add(Notices::Kind::Failed, "无法重命名：" + result.err().message, 4000);
            return false;
        }
        scan(instanceKey);
        return true;
    }

    void poll() {
        tasks.poll();
        trimCache();
    }

    void shutdown() {
        tasks.stop();
    }
}
