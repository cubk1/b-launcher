#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ThemePaintSpec {

    enum Region { WINDOW, TITLEBAR, SIDEBAR, CONTENT, CARD, DIALOG, MENU, PRIMARY_BUTTON, NAV_ACTIVE, REGION_COUNT };

    enum class Kind { Solid, Linear, Radial, Image, Shader };
    enum class Fit { Cover, Contain, Stretch, Tile };
    enum class Blend { Normal, Multiply, Screen, Add };
    enum class Motion { None, Shift, Rotate, Hue };

    struct Stop {
        uint32_t color = 0;
        float at = 0.0f;

        bool operator==(const Stop& other) const = default;
    };

    struct Layer {
        std::string id;
        Kind kind = Kind::Solid;
        Blend blend = Blend::Normal;
        float opacity = 1.0f;
        float inset = 0.0f;
        bool dither = false;
        std::vector<Stop> stops;
        float angle = 180.0f;
        float center[2] = {0.5f, 0.5f};
        float radius = 0.5f;
        Motion motion = Motion::None;
        float period = 10.0f;
        std::string source;
        Fit fit = Fit::Cover;
        float scale = 1.0f;
        float offset[2] = {0.0f, 0.0f};
        float align[2] = {0.5f, 0.5f};
        uint32_t tint = 0xFFFFFFFFu;
        std::vector<std::string> textures;
        bool halfRes = false;
        bool backdrop = false;

        bool operator==(const Layer& other) const = default;
    };

    struct RegionSpec {
        std::vector<Layer> layers;
        float blur = 0.0f;

        bool operator==(const RegionSpec& other) const = default;
    };

    enum class Extent { Units, Percent, Auto };

    struct Decoration {
        std::string id;
        std::string source;
        Region region = CONTENT;
        float anchor[2] = {1.0f, 1.0f};
        float offset[2] = {0.0f, 0.0f};
        float size[2] = {0.0f, 0.0f};
        Extent extent[2] = {Extent::Auto, Extent::Auto};
        bool above = false;
        std::vector<std::string> pages;
        float opacity = 1.0f;

        bool operator==(const Decoration& other) const = default;
    };

    enum FontRole { FONT_TEXT, FONT_BOLD, FONT_DISPLAY, FONT_MONO, FONT_CODE, FONT_ROLES };

    struct FontSpec {
        std::string source;
        int face = 0;
        int weight = 0;
        std::vector<std::string> system;

        bool operator==(const FontSpec& other) const = default;
    };

    constexpr int SOUND_KINDS = 5;

    struct SoundSpec {
        std::string source;
        bool silent = false;

        bool operator==(const SoundSpec& other) const = default;
    };

    struct BackgroundSpec {
        std::string image;
        bool stretch = false;
        float sidebarBlur = 40.0f;
        float panelBlur = 12.0f;
        int enabled = -1;

        bool operator==(const BackgroundSpec& other) const = default;
    };

    struct Spec {
        RegionSpec regions[REGION_COUNT];
        std::vector<Decoration> decorations;
        FontSpec fonts[FONT_ROLES];
        SoundSpec sounds[SOUND_KINDS];
        float soundVolume = 1.0f;
        BackgroundSpec background;
        bool animated = false;

        bool operator==(const Spec& other) const = default;
    };

    void apply(const Spec& spec);
    void override(const Spec* spec);
    const Spec& current();
    unsigned generation();
    bool custom(Region region);
    bool regionOf(const std::string& name, Region& out);
    bool splitSource(const std::string& source, std::string& owner, std::string& path);
}
