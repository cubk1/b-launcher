#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ImFont;

namespace UiFont {

    constexpr int MEDIUM = 0;
    constexpr int BOLD = 1;
    constexpr int ICON = 2;
    constexpr int DISPLAY = 3;
    constexpr int MONO = 4;
    constexpr int CODE = 5;
    constexpr int FALLBACK = 6;
    constexpr int MAX_FALLBACKS = 4;
    constexpr int FAMILY_COUNT = FALLBACK + MAX_FALLBACKS;

    extern ImFont* baseFonts[FAMILY_COUNT];
    extern std::unordered_map<int, int> fallbackCache;
    extern bool loaded;
    extern bool raw;
    extern bool shadow;
    extern bool shadowPass;
    extern std::vector<std::string> fallbackFonts;
    extern unsigned int loaderFlags;
    extern unsigned int fallbackLoaderFlags;
    constexpr int SUBPIXEL_AUTO = -1;
    constexpr int SUBPIXEL_OFF = 0;
    constexpr int SUBPIXEL_RGB = 1;
    constexpr int SUBPIXEL_BGR = 2;
    extern int subpixel;
    extern float gamma;
    extern float textScale;

    struct Slot {
        ImFont* font;
        float size;
    };

    void init();
    void ensureBaseFonts();
    Slot slot(int family, float size);
    Slot drawSlot(int family, float size);
    float width(const std::string& text, int family, float size);
    std::string fitEnd(const std::string& text, int family, float size, float available);
    std::string fitStart(const std::string& text, int family, float size, float available);
    std::vector<std::string> wrap(const std::string& text, int family, float size, float maxWidth);
    int familyFor(char32_t c, int family);
    bool supported(char32_t c, int family);
    bool needsFallback(const std::u32string& text, int family);
    float drawRuns(const std::u32string& text, float cursor, float top, uint32_t color, int family, float size,
                   float extra);
    std::string strip(const std::string& text);
    float ascent(int family, float size);
    float height(int family, float size);
    void draw(const std::string& text, float x, float y, uint32_t rgba, int family, float size);
    float centreY(const std::string& text, int family, float size, float y);

    float slotAscent(const Slot& slot);
    float slotHeight(const Slot& slot);
    float slotWidth(const std::u32string& text, const Slot& slot);
    void drawSlotStringScaled(const std::u32string& text, float x, float y, uint32_t color, const Slot& slot,
                              float extraScale);
    uint32_t colorOf(char32_t code);
}
