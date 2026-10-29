#include "ui/imgui/screens/shell/Icons.h"
#include "ui/imgui/screens/shell/EmbeddedIcons.h"

#include "ui/imgui/panel/UiImages.h"

#include <core/installer/loader.hpp>

namespace Icons {

    namespace loaders = b_launcher_base::install;

    static uint32_t load(uint32_t& texture, const unsigned char* data, unsigned int size) {
        if (texture == 0) {
            texture = UiImages::uploadEncoded(data, (int) size, nullptr, nullptr);
        }
        return texture;
    }

    Encoded encoded(loaders::loader_type type) {
        switch (type) {
            case loaders::loader_type::forge: return {EmbeddedIcons::forge, EmbeddedIcons::forgeSize};
            case loaders::loader_type::neoforge: return {EmbeddedIcons::neoforge, EmbeddedIcons::neoforgeSize};
            case loaders::loader_type::cleanroom: return {EmbeddedIcons::cleanroom, EmbeddedIcons::cleanroomSize};
            case loaders::loader_type::fabric:
            case loaders::loader_type::fabric_api: return {EmbeddedIcons::fabric, EmbeddedIcons::fabricSize};
            case loaders::loader_type::legacy_fabric:
            case loaders::loader_type::legacy_fabric_api:
                return {EmbeddedIcons::legacyFabric, EmbeddedIcons::legacyFabricSize};
            case loaders::loader_type::quilt:
            case loaders::loader_type::quilt_api: return {EmbeddedIcons::quilt, EmbeddedIcons::quiltSize};
            case loaders::loader_type::optifine: return {EmbeddedIcons::optifine, EmbeddedIcons::optifineSize};
            case loaders::loader_type::liteloader: return {EmbeddedIcons::liteloader, EmbeddedIcons::liteloaderSize};
        }
        return {};
    }

    Encoded encodedVanilla() {
        return {EmbeddedIcons::vanilla, EmbeddedIcons::vanillaSize};
    }

    uint32_t forge() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::forge, EmbeddedIcons::forgeSize);
    }

    uint32_t vanilla() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::vanilla, EmbeddedIcons::vanillaSize);
    }

    uint32_t loader(loaders::loader_type type) {
        static uint32_t neoforge = 0;
        static uint32_t cleanroom = 0;
        static uint32_t fabric = 0;
        static uint32_t quilt = 0;
        static uint32_t optifine = 0;
        static uint32_t legacyFabric = 0;
        static uint32_t liteloader = 0;
        switch (type) {
            case loaders::loader_type::forge: return forge();
            case loaders::loader_type::neoforge:
                return load(neoforge, EmbeddedIcons::neoforge, EmbeddedIcons::neoforgeSize);
            case loaders::loader_type::cleanroom:
                return load(cleanroom, EmbeddedIcons::cleanroom, EmbeddedIcons::cleanroomSize);
            case loaders::loader_type::fabric:
            case loaders::loader_type::fabric_api:
                return load(fabric, EmbeddedIcons::fabric, EmbeddedIcons::fabricSize);
            case loaders::loader_type::legacy_fabric:
            case loaders::loader_type::legacy_fabric_api:
                return load(legacyFabric, EmbeddedIcons::legacyFabric, EmbeddedIcons::legacyFabricSize);
            case loaders::loader_type::quilt:
            case loaders::loader_type::quilt_api: return load(quilt, EmbeddedIcons::quilt, EmbeddedIcons::quiltSize);
            case loaders::loader_type::optifine:
                return load(optifine, EmbeddedIcons::optifine, EmbeddedIcons::optifineSize);
            case loaders::loader_type::liteloader:
                return load(liteloader, EmbeddedIcons::liteloader, EmbeddedIcons::liteloaderSize);
        }
        return 0;
    }

    uint32_t iris() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::iris, EmbeddedIcons::irisSize);
    }

    uint32_t canvas() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::canvas, EmbeddedIcons::canvasSize);
    }

    uint32_t hmcl() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::hmcl, EmbeddedIcons::hmclSize);
    }

    uint32_t pcl() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::pcl, EmbeddedIcons::pclSize);
    }

    uint32_t backgroundSidebar() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::backgroundSidebar, EmbeddedIcons::backgroundSidebarSize);
    }

    uint32_t backgroundPanel() {
        static uint32_t texture = 0;
        return load(texture, EmbeddedIcons::backgroundPanel, EmbeddedIcons::backgroundPanelSize);
    }
}
