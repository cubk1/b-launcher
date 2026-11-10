#include "app/instances/Shortcuts.h"
#include "app/instances/Instances.h"
#include "app/system/About.h"
#include "app/system/EmbeddedIcons.h"
#include "app/system/IconFile.h"
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
            std::string out = blb::fs::filter_filename(name, '_');
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
#if !defined(_WIN32)
            std::shared_ptr<const Pixels> picked;
            if (draft.icon == Icon::Custom) {
                auto read = blb::fs::read_file(draft.custom);
                picked = read ? Pixels::decode(read->data(), read->size()) : nullptr;
            } else {
                Encoded encoded = instanceIcon(entry);
                picked = Pixels::decode(encoded.data, encoded.size);
            }
            return picked ? Pixels::encodePng(*picked) : std::string();
#else
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
            return image.rgba.empty() ? std::string() : IconFile::ico(image.rgba, image.width, image.height);
#endif
        }

        std::optional<std::filesystem::path> storeIcon(const Instances::Entry& entry, const std::string& bytes,
                                                       std::string& problem) {
            const Instances::Source* source = Instances::source(entry.source);
            if (source == nullptr) {
                problem = "找不到实例所在的游戏目录";
                return std::nullopt;
            }
            std::filesystem::path folder = blb::meta::game_layout(source->root).cache() / L"shortcuts";
#if defined(_WIN32)
            std::string name = blb::to_hex(blb::sha1::digest(bytes)).substr(0, 16) + ".ico";
#else
            std::string name = blb::to_hex(blb::sha1::digest(bytes)).substr(0, 16) + ".png";
#endif
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
        shortcut.description = "用 " + std::string(About::NAME) + " 启动 " + entry.info.id;
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
        return image.rgba.empty() ? std::vector<unsigned char>()
                                  : IconFile::square(image.rgba, image.width, image.height, size);
    }
}
