#include "app/instances/Shortcuts.h"
#include "app/instances/Instances.h"
#include "app/system/EmbeddedIcons.h"
#include "app/system/Platform.h"

#include <core/base/fs.hpp>
#include <core/base/hash.hpp>
#include <core/base/strings.hpp>
#include <core/installer/loader.hpp>
#include <core/meta/game_layout.hpp>

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>

namespace Shortcuts {

    namespace blb = b_launcher_base;

    namespace {
        constexpr int BASE_PIXELS = 256;
        const int ICON_SIZES[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};

        struct Image {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> rgba;
        };

        struct Encoded {
            const unsigned char* data = nullptr;
            unsigned int size = 0;
        };

        std::map<std::string, Draft> drafts;
        std::filesystem::path lastFolder;

        bool isIco(const std::filesystem::path& file) {
            return blb::iequals(blb::path_to_utf8(file.extension()), ".ico");
        }

        std::string safeName(const std::string& name) {
            std::string out;
            for (char c : name) {
                bool bad = (unsigned char) c < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos;
                out += bad ? '_' : c;
            }
            return out.empty() ? std::string("Minecraft") : out;
        }

        Image decode(const unsigned char* data, size_t size) {
            Image image;
            int channels = 0;
            unsigned char* pixels = stbi_load_from_memory(data, (int) size, &image.width, &image.height, &channels, 4);
            if (pixels != nullptr) {
                image.rgba.assign(pixels, pixels + (size_t) image.width * image.height * 4);
                stbi_image_free(pixels);
            }
            return image;
        }

        Image load(const std::filesystem::path& file) {
            auto bytes = blb::fs::read_file(file);
            if (!bytes || bytes->empty()) {
                return {};
            }
            return decode((const unsigned char*) bytes->data(), bytes->size());
        }

        std::vector<unsigned char> resample(const Image& image, int size) {
            std::vector<unsigned char> out((size_t) size * size * 4, 0);
            if (image.width <= 0 || image.height <= 0) {
                return out;
            }
            double scale = (double) size / std::max(image.width, image.height);
            double offsetX = (size - image.width * scale) * 0.5;
            double offsetY = (size - image.height * scale) * 0.5;
            double area = 1.0 / (scale * scale);
            for (int y = 0; y < size; y++) {
                double top = std::max(0.0, (y - offsetY) / scale);
                double bottom = std::min((double) image.height, (y + 1 - offsetY) / scale);
                if (bottom <= top) {
                    continue;
                }
                for (int x = 0; x < size; x++) {
                    double left = std::max(0.0, (x - offsetX) / scale);
                    double right = std::min((double) image.width, (x + 1 - offsetX) / scale);
                    if (right <= left) {
                        continue;
                    }
                    double sum[4] = {0.0, 0.0, 0.0, 0.0};
                    for (int sy = (int) top; sy < bottom; sy++) {
                        double wy = std::min(bottom, sy + 1.0) - std::max(top, (double) sy);
                        for (int sx = (int) left; sx < right; sx++) {
                            double wx = std::min(right, sx + 1.0) - std::max(left, (double) sx);
                            const unsigned char* p = &image.rgba[((size_t) sy * image.width + sx) * 4];
                            double weight = wx * wy * p[3];
                            sum[0] += p[0] * weight;
                            sum[1] += p[1] * weight;
                            sum[2] += p[2] * weight;
                            sum[3] += weight;
                        }
                    }
                    if (sum[3] <= 0.0) {
                        continue;
                    }
                    unsigned char* q = &out[((size_t) y * size + x) * 4];
                    q[0] = (unsigned char) std::lround(std::min(255.0, sum[0] / sum[3]));
                    q[1] = (unsigned char) std::lround(std::min(255.0, sum[1] / sum[3]));
                    q[2] = (unsigned char) std::lround(std::min(255.0, sum[2] / sum[3]));
                    q[3] = (unsigned char) std::lround(std::min(255.0, sum[3] / area));
                }
            }
            return out;
        }

        Image reduced(Image image) {
            if (image.width <= BASE_PIXELS && image.height <= BASE_PIXELS) {
                return image;
            }
            Image out;
            out.width = BASE_PIXELS;
            out.height = BASE_PIXELS;
            out.rgba = resample(image, BASE_PIXELS);
            return out;
        }

        void put16(std::string& out, uint32_t value) {
            out.push_back((char) (value & 0xFF));
            out.push_back((char) ((value >> 8) & 0xFF));
        }

        void put32(std::string& out, uint32_t value) {
            put16(out, value & 0xFFFF);
            put16(out, value >> 16);
        }

        std::string ico(const Image& source) {
            Image image = reduced(source);
            std::vector<std::string> images;
            for (int size : ICON_SIZES) {
                std::vector<unsigned char> rgba = resample(image, size);
                uint32_t stride = (uint32_t) ((size + 31) / 32) * 4;
                std::string dib;
                put32(dib, 40);
                put32(dib, (uint32_t) size);
                put32(dib, (uint32_t) size * 2);
                put16(dib, 1);
                put16(dib, 32);
                put32(dib, 0);
                put32(dib, (uint32_t) size * size * 4 + stride * size);
                put32(dib, 0);
                put32(dib, 0);
                put32(dib, 0);
                put32(dib, 0);
                for (int y = size - 1; y >= 0; y--) {
                    for (int x = 0; x < size; x++) {
                        const unsigned char* p = &rgba[((size_t) y * size + x) * 4];
                        dib.push_back((char) p[2]);
                        dib.push_back((char) p[1]);
                        dib.push_back((char) p[0]);
                        dib.push_back((char) p[3]);
                    }
                }
                for (int y = size - 1; y >= 0; y--) {
                    std::string row(stride, '\0');
                    for (int x = 0; x < size; x++) {
                        if (rgba[((size_t) y * size + x) * 4 + 3] == 0) {
                            row[x / 8] = (char) (row[x / 8] | (0x80 >> (x % 8)));
                        }
                    }
                    dib += row;
                }
                images.push_back(std::move(dib));
            }
            std::string out;
            put16(out, 0);
            put16(out, 1);
            put16(out, (uint32_t) images.size());
            uint32_t offset = 6 + 16 * (uint32_t) images.size();
            for (size_t i = 0; i < images.size(); i++) {
                int size = ICON_SIZES[i];
                out.push_back((char) (size >= 256 ? 0 : size));
                out.push_back((char) (size >= 256 ? 0 : size));
                out.push_back(0);
                out.push_back(0);
                put16(out, 1);
                put16(out, 32);
                put32(out, (uint32_t) images[i].size());
                put32(out, offset);
                offset += (uint32_t) images[i].size();
            }
            for (const std::string& image : images) {
                out += image;
            }
            return out;
        }

        Encoded loaderIcon(blb::install::loader_type type) {
            using blb::install::loader_type;
            switch (type) {
                case loader_type::forge: return {EmbeddedIcons::forge, EmbeddedIcons::forgeSize};
                case loader_type::neoforge: return {EmbeddedIcons::neoforge, EmbeddedIcons::neoforgeSize};
                case loader_type::cleanroom: return {EmbeddedIcons::cleanroom, EmbeddedIcons::cleanroomSize};
                case loader_type::fabric:
                case loader_type::fabric_api: return {EmbeddedIcons::fabric, EmbeddedIcons::fabricSize};
                case loader_type::legacy_fabric:
                case loader_type::legacy_fabric_api:
                    return {EmbeddedIcons::legacyFabric, EmbeddedIcons::legacyFabricSize};
                case loader_type::quilt:
                case loader_type::quilt_api: return {EmbeddedIcons::quilt, EmbeddedIcons::quiltSize};
                case loader_type::optifine: return {EmbeddedIcons::optifine, EmbeddedIcons::optifineSize};
                case loader_type::liteloader: return {EmbeddedIcons::liteloader, EmbeddedIcons::liteloaderSize};
            }
            return {};
        }

        // The icon of the instance's main loader, of another one, or of vanilla.
        Encoded instanceIcon(const Instances::Entry& entry) {
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                if (blb::install::is_primary_loader(loader.type)) {
                    if (Encoded found = loaderIcon(loader.type); found.data != nullptr) {
                        return found;
                    }
                }
            }
            for (const blb::install::loader_info& loader : entry.info.loaders) {
                if (Encoded found = loaderIcon(loader.type); found.data != nullptr) {
                    return found;
                }
            }
            return {EmbeddedIcons::vanilla, EmbeddedIcons::vanillaSize};
        }

        std::string iconBytes(const Instances::Entry& entry, const Draft& draft) {
            if (draft.icon == Icon::Custom && isIco(draft.custom)) {
                auto read = blb::fs::read_file(draft.custom);
                return read ? std::move(*read) : std::string();
            }
            Image image;
            if (draft.icon == Icon::Custom) {
                image = load(draft.custom);
            } else {
                Encoded encoded = instanceIcon(entry);
                image = decode(encoded.data, encoded.size);
            }
            return image.rgba.empty() ? std::string() : ico(image);
        }

        std::optional<std::filesystem::path> storeIcon(const Instances::Entry& entry, const std::string& bytes,
                                                       std::string& problem) {
            const Instances::Source* source = Instances::source(entry.source);
            if (source == nullptr) {
                problem = "找不到实例所在的游戏目录";
                return std::nullopt;
            }
            std::filesystem::path folder = blb::meta::game_layout(source->root).cache() / L"shortcuts";
            std::string name = blb::to_hex(blb::sha1::digest(bytes)).substr(0, 16) + ".ico";
            std::filesystem::path file = folder / blb::path_from_utf8(name);
            if (blb::fs::exists(file)) {
                return file;
            }
            auto made = blb::fs::create_directories(folder);
            auto written = made ? blb::fs::write_file_atomic(file, bytes) : made;
            if (!written) {
                problem = "无法保存图标：" + written.err().message;
                return std::nullopt;
            }
            return file;
        }
    }

    Draft& draft(const std::string& instanceKey) {
        return drafts[instanceKey];
    }

    bool create(const std::string& instanceKey) {
        const Instances::Entry* found = Instances::find(instanceKey);
        if (found == nullptr) {
            return false;
        }
        // The instances may be read again while the file dialog is open, so the entry is copied before it.
        const Instances::Entry entry = *found;
        const Draft draft = drafts[instanceKey];
        if (draft.icon == Icon::Custom && draft.custom.empty()) {
            Instances::notify("先选择图标文件", true);
            return false;
        }
        std::filesystem::path folder = lastFolder.empty() ? Platform::desktopDirectory() : lastFolder;
        std::optional<std::filesystem::path> target =
                Platform::pickShortcut(folder / blb::path_from_utf8(safeName(entry.info.id) + ".lnk"));
        if (!target) {
            return false;
        }
        lastFolder = target->parent_path();
        Platform::Shortcut shortcut;
        shortcut.file = *target;
        shortcut.arguments = "--launch \"" + entry.key + "\"" + (draft.headless ? " --headless" : "");
        shortcut.description = "用 B Launcher 启动 " + entry.info.id;
        shortcut.relative = Platform::insideLauncher(*target);
        if (draft.icon != Icon::Launcher) {
            std::string bytes = iconBytes(entry, draft);
            if (bytes.empty()) {
                Instances::notify("无法读取图标 " + Platform::display(draft.custom), true);
                return false;
            }
            std::string problem;
            std::optional<std::filesystem::path> icon = storeIcon(entry, bytes, problem);
            if (!icon) {
                Instances::notify(problem, true);
                return false;
            }
            shortcut.icon = *icon;
        }
        std::string problem = Platform::createShortcut(shortcut);
        if (!problem.empty()) {
            Instances::notify(problem, true);
            return false;
        }
        Instances::notify(std::string(shortcut.relative ? "已创建快捷方式（相对路径） " : "已创建快捷方式 ") +
                                  Platform::display(*target),
                          false);
        return true;
    }

    std::vector<unsigned char> pixels(const std::filesystem::path& file, int size) {
        if (isIco(file)) {
            return Platform::iconPixels(file, size);
        }
        Image image = load(file);
        return image.rgba.empty() ? std::vector<unsigned char>() : resample(reduced(std::move(image)), size);
    }
}
