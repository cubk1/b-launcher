#include "UiFont.h"
#include "UiRender.h"
#include "UiCommon.h"
#include "../fonts/EmbeddedFonts.h"
#include "../gl/UiGL.h"

#include "imgui.h"
#include "imgui_internal.h"
#ifdef IMGUI_ENABLE_FREETYPE
#include "misc/freetype/imgui_freetype.h"
#endif

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace UiFont {

    ImFont* baseFonts[FAMILY_COUNT] = {};
    std::unordered_map<int, int> fallbackCache;
    bool loaded = false;
    bool raw = false;
    bool shadow = false;
    bool shadowPass = false;
    std::vector<std::string> fallbackFonts = {"msyh.ttc#1", "msyh.ttf", "seguisym.ttf"};
#ifdef IMGUI_ENABLE_FREETYPE
    unsigned int loaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
#else
    unsigned int loaderFlags = 0;
#endif
    unsigned int fallbackLoaderFlags = 0;
    int subpixel = SUBPIXEL_AUTO;
    float gamma = 0.0f;
    float textScale = 1.0f;
    static int fallbackCount = 0;
    static int activeSubpixel = SUBPIXEL_OFF;

    static int resolveSubpixel() {
        if (subpixel != SUBPIXEL_AUTO) {
            return subpixel;
        }
#if defined(_WIN32)
        BOOL smoothing = FALSE;
        UINT type = 0;
        UINT orientation = FE_FONTSMOOTHINGORIENTATIONRGB;
        SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0);
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0);
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGORIENTATION, 0, &orientation, 0);
        if (smoothing && type == FE_FONTSMOOTHINGCLEARTYPE) {
            return orientation == FE_FONTSMOOTHINGORIENTATIONBGR ? SUBPIXEL_BGR : SUBPIXEL_RGB;
        }
#endif
        return SUBPIXEL_OFF;
    }

    static float resolveGamma() {
        if (gamma > 0.0f) {
            return gamma;
        }
#if defined(_WIN32)
        UINT contrast = 0;
        if (SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST, 0, &contrast, 0) && contrast >= 1000 &&
            contrast <= 2200) {
            return contrast / 1000.0f;
        }
#endif
        return 1.2f;
    }

    static std::string windowsFontsDir() {
#if defined(_WIN32)
        char buffer[MAX_PATH];
        UINT length = GetWindowsDirectoryA(buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            return std::string(buffer, length) + "\\Fonts\\";
        }
#endif
        return "C:\\Windows\\Fonts\\";
    }

    static void crisp(ImFontConfig& config, unsigned int flags) {
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        config.FontLoaderFlags = flags;
#ifdef IMGUI_ENABLE_FREETYPE
        if (activeSubpixel == SUBPIXEL_RGB) {
            config.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_SubpixelRGB;
        } else if (activeSubpixel == SUBPIXEL_BGR) {
            config.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_SubpixelBGR;
        }
#endif
    }

    static ImFont* fromMemory(const unsigned char* data, unsigned int size, const char* name) {
        ImFontConfig config;
        crisp(config, loaderFlags);
        config.FontDataOwnedByAtlas = false;
        snprintf(config.Name, sizeof(config.Name), "%s", name);
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*) data, (int) size, 16.0f, &config);
    }

    static ImFont* fromSystem(const std::string& spec) {
        std::string file = spec;
        int face = 0;
        size_t hash = spec.find('#');
        if (hash != std::string::npos) {
            file = spec.substr(0, hash);
            face = std::atoi(spec.c_str() + hash + 1);
        }
        bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\' || file[0] == '/');
        std::string path = absolute ? file : windowsFontsDir() + file;
        std::ifstream probe(path, std::ios::binary);
        if (!probe) {
            return nullptr;
        }
        probe.close();
        ImFontConfig config;
        crisp(config, fallbackLoaderFlags);
        config.Flags |= ImFontFlags_NoLoadError;
        config.FontNo = face;
        snprintf(config.Name, sizeof(config.Name), "%s", spec.c_str());
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config);
    }

    void init() {
        ensureBaseFonts();
    }

    void ensureBaseFonts() {
        if (loaded) {
            return;
        }
        loaded = true;
#ifdef IMGUI_ENABLE_FREETYPE
        activeSubpixel = resolveSubpixel();
        ImGuiFreeType::SetCoverageGamma(resolveGamma());
#endif
        ImFont* text = fromMemory(EmbeddedFonts::textFont, EmbeddedFonts::textFontSize, "SSTRg.TTF");
        baseFonts[MEDIUM] = text;
        baseFonts[BOLD] = text;
        baseFonts[DISPLAY] = text;
        baseFonts[MONO] = text;
        ImFont* code = fromSystem("consola.ttf");
        baseFonts[CODE] = code != nullptr ? code : text;
        baseFonts[ICON] = fromMemory(EmbeddedFonts::iconFont, EmbeddedFonts::iconFontSize, "fa-solid-900.ttf");
        fallbackCount = 0;
        for (const std::string& spec : fallbackFonts) {
            if (fallbackCount >= MAX_FALLBACKS) {
                break;
            }
            ImFont* font = fromSystem(spec);
            if (font != nullptr) {
                baseFonts[FALLBACK + fallbackCount++] = font;
            }
        }
    }

    static ImFont* baseOf(int family) {
        ImFont* font = family >= 0 && family < FAMILY_COUNT ? baseFonts[family] : nullptr;
        if (font == nullptr) {
            font = baseFonts[MEDIUM];
        }
        if (font == nullptr) {
            font = ImGui::GetIO().Fonts->Fonts.Size > 0 ? ImGui::GetIO().Fonts->Fonts[0] : nullptr;
        }
        return font;
    }

    static int pixelScale() {
        return std::max(1, (int) UiRender::pixelScale());
    }

    static Slot fontSlot(ImFont* base, float size) {
        float physical = std::max(1.0f, Ui::round(size * pixelScale() * 2.0f) / 2.0f);
        return Slot{base, physical};
    }

    Slot slot(int family, float size) {
        ensureBaseFonts();
        return fontSlot(baseOf(family), size * textScale);
    }

    Slot drawSlot(int family, float size) {
        ensureBaseFonts();
        return fontSlot(baseOf(family), size * textScale * UiRender::stableScale() * UiRender::rasterScale);
    }

    static std::unordered_map<ImFont*, float> emRatios;

    static unsigned int readU16(const unsigned char* data, size_t at) {
        return ((unsigned int) data[at] << 8) | data[at + 1];
    }

    static unsigned int readU32(const unsigned char* data, size_t at) {
        return ((unsigned int) data[at] << 24) | ((unsigned int) data[at + 1] << 16) |
                ((unsigned int) data[at + 2] << 8) | data[at + 3];
    }

    static float emRatio(ImFont* font) {
        auto cached = emRatios.find(font);
        if (cached != emRatios.end()) {
            return cached->second;
        }
        float ratio = 1.0f;
        if (font != nullptr && font->Sources.Size > 0 && font->Sources[0]->FontData != nullptr) {
            const unsigned char* data = (const unsigned char*) font->Sources[0]->FontData;
            size_t size = (size_t) font->Sources[0]->FontDataSize;
            size_t base = 0;
            if (size > 12 && data[0] == 't' && data[1] == 't' && data[2] == 'c' && data[3] == 'f') {
                unsigned int index = (unsigned int) std::max(0, (int) font->Sources[0]->FontNo);
                if (16 + index * 4 + 4 <= size) {
                    base = readU32(data, 12 + index * 4);
                }
            }
            if (base + 12 <= size) {
                unsigned int tables = readU16(data, base + 4);
                int unitsPerEm = 0;
                int ascender = 0;
                int descender = 0;
                for (unsigned int i = 0; i < tables && base + 12 + i * 16 + 16 <= size; i++) {
                    size_t record = base + 12 + i * 16;
                    unsigned int offset = readU32(data, record + 8);
                    if (std::memcmp(data + record, "head", 4) == 0 && offset + 20 <= size) {
                        unitsPerEm = (int) readU16(data, offset + 18);
                    } else if (std::memcmp(data + record, "hhea", 4) == 0 && offset + 8 <= size) {
                        ascender = (short) readU16(data, offset + 4);
                        descender = (short) readU16(data, offset + 6);
                    }
                }
                if (unitsPerEm > 0 && ascender - descender > 0) {
                    ratio = (float) (ascender - descender) / (float) unitsPerEm;
                }
            }
        }
        emRatios[font] = ratio;
        return ratio;
    }

    static bool isFallback(ImFont* font) {
        for (int i = 0; i < fallbackCount; i++) {
            if (baseFonts[FALLBACK + i] == font) {
                return true;
            }
        }
        return false;
    }

    static const ImFontGlyph* referenceGlyph(ImFont* font, ImFontBaked* baked) {
        const char32_t* refs = font == baseFonts[ICON] ? U"\uF111" : isFallback(font) ? U"\u4E2DH" : U"H";
        for (; *refs != 0; refs++) {
            const ImFontGlyph* glyph = baked->FindGlyphNoFallback((ImWchar) *refs);
            if (glyph != nullptr && glyph->Visible) {
                return glyph;
            }
        }
        return nullptr;
    }

    struct Ink {
        float x0, y0, x1, y1;
    };

    static Ink inkOf(const ImFontGlyph* glyph) {
        Ink ink{glyph->X0, glyph->Y0, glyph->X1, glyph->Y1};
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        ImTextureData* tex = atlas->TexData;
        if (tex == nullptr || tex->Pixels == nullptr || tex->Format != ImTextureFormat_RGBA32 ||
            glyph->PackId == ImFontAtlasRectId_Invalid) {
            return ink;
        }
        ImTextureRect* r = ImFontAtlasPackGetRect(atlas, glyph->PackId);
        if (r == nullptr || r->w == 0 || r->h == 0) {
            return ink;
        }
        int top = -1, bottom = -1, left = r->w, right = -1;
        for (int row = 0; row < r->h; row++) {
            const unsigned char* pixels = (const unsigned char*) tex->GetPixelsAt(r->x, r->y + row);
            for (int col = 0; col < r->w; col++) {
                if (pixels[col * 4 + 3] >= 128) {
                    top = top < 0 ? row : top;
                    bottom = row;
                    left = std::min(left, col);
                    right = std::max(right, col);
                }
            }
        }
        if (top < 0) {
            return ink;
        }
        float unitX = (glyph->X1 - glyph->X0) / r->w;
        float unitY = (glyph->Y1 - glyph->Y0) / r->h;
        return Ink{glyph->X0 + left * unitX, glyph->Y0 + top * unitY, glyph->X0 + (right + 1) * unitX,
                   glyph->Y0 + (bottom + 1) * unitY};
    }

    static bool odd(float pixels) {
        return ((int) Ui::round(pixels)) % 2 != 0;
    }

    static std::unordered_map<unsigned long long, float> bakedSizes;

    static float bakedSize(ImFont* font, float size) {
        float requested = std::max(1.0f, size * emRatio(font));
        float nearest = std::max(1.0f, (float) Ui::round(requested));
        if (font == baseFonts[ICON]) {
            return nearest;
        }
        unsigned long long key =
                ((unsigned long long) (uintptr_t) font << 16) ^ (unsigned long long) (requested * 64.0f);
        auto cached = bakedSizes.find(key);
        if (cached != bakedSizes.end()) {
            return cached->second;
        }
        float candidates[3] = {nearest, nearest + 1.0f, nearest - 1.0f};
        if (requested < nearest) {
            std::swap(candidates[1], candidates[2]);
        }
        float chosen = nearest;
        for (float candidate : candidates) {
            if (candidate < 1.0f) {
                continue;
            }
            ImFontBaked* baked = font->GetFontBaked(candidate, 1.0f);
            const ImFontGlyph* glyph = baked != nullptr ? referenceGlyph(font, baked) : nullptr;
            if (glyph != nullptr && !odd(inkOf(glyph).y1 - inkOf(glyph).y0)) {
                chosen = candidate;
                break;
            }
        }
        bakedSizes[key] = chosen;
        return chosen;
    }

    static ImFontBaked* bakedOf(const Slot& slot, float& factor) {
        factor = 1.0f;
        if (slot.font == nullptr) {
            return nullptr;
        }
        return slot.font->GetFontBaked(bakedSize(slot.font, slot.size), 1.0f);
    }

    float slotAscent(const Slot& slot) {
        float factor = 1.0f;
        ImFontBaked* baked = bakedOf(slot, factor);
        if (baked == nullptr) {
            return 0.0f;
        }
        return baked->Ascent * factor / (float) pixelScale();
    }

    static float slotCenter(const Slot& slot) {
        float factor = 1.0f;
        ImFontBaked* baked = bakedOf(slot, factor);
        if (baked == nullptr) {
            return 0.0f;
        }
        const ImFontGlyph* glyph = referenceGlyph(slot.font, baked);
        if (glyph != nullptr) {
            Ink ink = inkOf(glyph);
            return (ink.y0 + ink.y1) * 0.5f * factor / (float) pixelScale();
        }
        return (baked->Ascent - baked->Descent) * 0.5f * factor / (float) pixelScale();
    }

    float slotHeight(const Slot& slot) {
        float factor = 1.0f;
        ImFontBaked* baked = bakedOf(slot, factor);
        if (baked == nullptr) {
            return 0.0f;
        }
        return (baked->Ascent - baked->Descent) * factor / (float) pixelScale();
    }

    float slotWidth(const std::u32string& text, const Slot& slot) {
        if (text.empty()) {
            return 0.0f;
        }
        float factor = 1.0f;
        ImFontBaked* baked = bakedOf(slot, factor);
        if (baked == nullptr) {
            return 0.0f;
        }
        float sum = 0;
        for (char32_t c : text) {
            if (c > 0xFFFF) {
                sum += baked->FallbackAdvanceX;
                continue;
            }
            sum += baked->GetCharAdvance((ImWchar) c);
        }
        return sum * factor / (float) pixelScale();
    }

    static ImU32 toIm(uint32_t argb) {
        return ((argb >> 24) & 0xFF) << IM_COL32_A_SHIFT | ((argb >> 16) & 0xFF) << IM_COL32_R_SHIFT |
                ((argb >> 8) & 0xFF) << IM_COL32_G_SHIFT | (argb & 0xFF) << IM_COL32_B_SHIFT;
    }

    static float snapPixel(float value) {
        return std::ceil(value - 0.5f - 0.001f);
    }

    static void beginSubpixel(const ImDrawList*, const ImDrawCmd* cmd) {
        const float* rgb = (const float*) cmd->UserCallbackData;
        UiGL::BlendColor(rgb[0], rgb[1], rgb[2], 1.0f);
        UiGL::BlendFuncSeparate(UGL_CONSTANT_COLOR, UGL_ONE_MINUS_SRC_COLOR, UGL_ONE, UGL_ONE_MINUS_SRC_ALPHA);
    }

    static void endSubpixel(const ImDrawList*, const ImDrawCmd*) {
        UiGL::BlendFuncSeparate(UGL_SRC_ALPHA, UGL_ONE_MINUS_SRC_ALPHA, UGL_ONE, UGL_ONE_MINUS_SRC_ALPHA);
    }

    void drawSlotStringScaled(const std::u32string& text, float x, float y, uint32_t color, const Slot& slot,
                              float extraScale) {
        if (text.empty() || slot.font == nullptr) {
            return;
        }
        float factor = 1.0f;
        ImFontBaked* baked = bakedOf(slot, factor);
        if (baked == nullptr) {
            return;
        }
        for (char32_t c : text) {
            baked->FindGlyph((ImWchar) (c <= 0xFFFF ? c : 0xFFFD));
        }
        baked = bakedOf(slot, factor);
        UiRender::flush();
        ImDrawList* dl = UiRender::drawList();
        dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
        float scale = (float) pixelScale();
        float unit = UiRender::unit();
        float glyphScale = factor / scale * extraScale;
        int visible = 0;
        for (char32_t c : text) {
            const ImFontGlyph* glyph = baked->FindGlyph((ImWchar) (c <= 0xFFFF ? c : 0xFFFD));
            if (glyph != nullptr && glyph->Visible) {
                visible++;
            }
        }
        bool lcd = visible > 0 && activeSubpixel != SUBPIXEL_OFF && UiGL::BlendColor != nullptr;
        if (lcd) {
            float rgb[3] = {((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f};
            dl->AddCallback(beginSubpixel, rgb, sizeof(rgb));
        }
        if (visible > 0) {
            dl->PrimReserve(visible * 6, visible * 4);
            unsigned int alpha = (color >> 24) & 0xFF;
            ImU32 col = lcd ? IM_COL32(alpha, alpha, alpha, alpha) : toIm(color);
            bool snapped = std::fabs(extraScale - 1.0f) < 0.001f;
            float pen = x;
            float top = snapped ? snapPixel(y * scale) / scale : y;
            for (char32_t c : text) {
                const ImFontGlyph* glyph = baked->FindGlyph((ImWchar) (c <= 0xFFFF ? c : 0xFFFD));
                if (glyph == nullptr) {
                    continue;
                }
                if (glyph->Visible) {
                    float originX = snapped ? snapPixel(pen * scale) / scale : pen;
                    float x1 = originX + glyph->X0 * glyphScale;
                    float x2 = originX + glyph->X1 * glyphScale;
                    float y1 = top + glyph->Y0 * glyphScale;
                    float y2 = top + glyph->Y1 * glyphScale;
                    ImU32 tint = glyph->Colored ? (col | ~IM_COL32_A_MASK) : col;
                    dl->PrimRectUV(ImVec2(x1 * unit, y1 * unit), ImVec2(x2 * unit, y2 * unit),
                                   ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1), tint);
                }
                pen += glyph->AdvanceX * glyphScale;
            }
        }
        if (lcd) {
            dl->AddCallback(endSubpixel, nullptr);
        }
        dl->PopTexture();
    }

    float width(const std::string& text, int family, float size) {
        std::string stripped = raw ? text : strip(text);
        if (stripped.empty()) {
            return 0.0f;
        }
        float drawScale = UiRender::stableScale() * UiRender::rasterScale;
        bool asDrawn = drawScale > 0.0f;
        auto measure = [&](const std::u32string& chars, int run) {
            return asDrawn ? slotWidth(chars, drawSlot(run, size)) / drawScale : slotWidth(chars, slot(run, size));
        };
        std::u32string chars = Ui::decode(stripped);
        if (!needsFallback(chars, family)) {
            return measure(chars, family);
        }
        float total = 0.0f;
        size_t start = 0;
        int run = familyFor(chars[0], family);
        for (size_t i = 1; i <= chars.size(); i++) {
            int current = i < chars.size() ? familyFor(chars[i], family) : -1;
            if (i == chars.size() || current != run) {
                total += measure(chars.substr(start, i - start), run);
                start = i;
                run = current;
            }
        }
        return total;
    }

    static std::string fitCut(const std::string& text, int family, float size, float available, bool keepEnd) {
        static std::unordered_map<std::string, std::string> fitted;
        std::string key = text;
        key += '\x1f';
        key += std::to_string(family) + ":" + std::to_string((int) (size * 4.0f)) + ":" +
                std::to_string((int) (available * 4.0f)) + ":" +
                std::to_string((int) (UiRender::stableScale() * UiRender::rasterScale * 1000.0f)) +
                (keepEnd ? "<" : ">");
        auto found = fitted.find(key);
        if (found != fitted.end()) {
            return found->second;
        }
        std::string result = text;
        if (width(text, family, size) > available) {
            const std::string ellipsis = "\xE2\x80\xA6";
            int length = (int) Ui::length(text);
            auto piece = [&](int count) {
                return keepEnd ? ellipsis + Ui::substring(text, length - count, count)
                               : Ui::substring(text, 0, count) + ellipsis;
            };
            int low = 1;
            int high = std::max(1, length - 1);
            while (low < high) {
                int middle = (low + high + 1) / 2;
                if (width(piece(middle), family, size) <= available) {
                    low = middle;
                } else {
                    high = middle - 1;
                }
            }
            result = piece(low);
        }
        if (fitted.size() > 8192) {
            fitted.clear();
        }
        fitted.emplace(std::move(key), result);
        return result;
    }

    std::string fitEnd(const std::string& text, int family, float size, float available) {
        return fitCut(text, family, size, available, false);
    }

    std::string fitStart(const std::string& text, int family, float size, float available) {
        return fitCut(text, family, size, available, true);
    }

    static bool breaksAnywhere(char32_t c) {
        return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
                (c >= 0xFF00 && c <= 0xFFEF);
    }

    std::vector<std::string> wrap(const std::string& text, int family, float size, float maxWidth) {
        std::vector<std::string> lines;
        auto fits = [&](const std::u32string& value) { return width(Ui::encode(value), family, size) <= maxWidth; };
        std::u32string all = Ui::decode(text);
        size_t start = 0;
        while (true) {
            size_t end = all.find(U'\n', start);
            std::u32string paragraph =
                    all.substr(start, end == std::u32string::npos ? std::u32string::npos : end - start);
            std::vector<std::u32string> tokens;
            for (size_t i = 0; i < paragraph.size();) {
                char32_t c = paragraph[i];
                size_t j = i + 1;
                if (c == U' ') {
                    while (j < paragraph.size() && paragraph[j] == U' ') {
                        j++;
                    }
                } else if (!breaksAnywhere(c)) {
                    while (j < paragraph.size() && paragraph[j] != U' ' && !breaksAnywhere(paragraph[j])) {
                        j++;
                    }
                }
                tokens.push_back(paragraph.substr(i, j - i));
                i = j;
            }
            std::u32string line;
            auto splitLong = [&]() {
                if (fits(line)) {
                    return;
                }
                std::u32string piece;
                for (char32_t c : line) {
                    if (!piece.empty() && !fits(piece + c)) {
                        lines.push_back(Ui::encode(piece));
                        piece.clear();
                    }
                    piece.push_back(c);
                }
                line = piece;
            };
            for (const std::u32string& token : tokens) {
                bool space = token[0] == U' ';
                if (line.empty()) {
                    if (!space) {
                        line = token;
                        splitLong();
                    }
                    continue;
                }
                if (fits(line + token)) {
                    line += token;
                    continue;
                }
                while (!line.empty() && line.back() == U' ') {
                    line.pop_back();
                }
                lines.push_back(Ui::encode(line));
                line = space ? std::u32string() : token;
                splitLong();
            }
            while (!line.empty() && line.back() == U' ') {
                line.pop_back();
            }
            lines.push_back(Ui::encode(line));
            if (end == std::u32string::npos) {
                break;
            }
            start = end + 1;
        }
        return lines;
    }

    static bool canDisplay(ImFont* font, char32_t c) {
        if (font == nullptr) {
            return false;
        }
        if (c > 0xFFFF) {
            return false;
        }
        return font->IsGlyphInFont((ImWchar) c);
    }

    int familyFor(char32_t c, int family) {
        if (c < 128 || family == ICON || family >= FALLBACK || c > 0xFFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            return family;
        }
        int key = (family << 16) | (int) c;
        auto it = fallbackCache.find(key);
        if (it != fallbackCache.end()) {
            return it->second;
        }
        ensureBaseFonts();
        int result = family;
        ImFont* font = baseOf(family);
        if (font != nullptr && !canDisplay(font, c)) {
            for (int i = 0; i < fallbackCount; i++) {
                if (canDisplay(baseFonts[FALLBACK + i], c)) {
                    result = FALLBACK + i;
                    break;
                }
            }
        }
        fallbackCache[key] = result;
        return result;
    }

    bool supported(char32_t c, int family) {
        return familyFor(c, family) == family;
    }

    bool needsFallback(const std::u32string& text, int family) {
        for (char32_t c : text) {
            if (!supported(c, family)) {
                return true;
            }
        }
        return false;
    }

    float drawRuns(const std::u32string& text, float cursor, float top, uint32_t color, int family, float size,
                   float extra) {
        Slot base = drawSlot(family, size);
        if (!needsFallback(text, family)) {
            drawSlotStringScaled(text, cursor, top, color, base, extra);
            return cursor + slotWidth(text, base) * extra;
        }
        size_t start = 0;
        int run = familyFor(text[0], family);
        for (size_t i = 1; i <= text.size(); i++) {
            int current = i < text.size() ? familyFor(text[i], family) : -1;
            if (i == text.size() || current != run) {
                std::u32string segment = text.substr(start, i - start);
                Slot runSlot = run == family ? base : drawSlot(run, size);
                float shift = run == family ? 0.0f : (slotAscent(base) - slotAscent(runSlot)) * extra;
                drawSlotStringScaled(segment, cursor, top + shift, color, runSlot, extra);
                cursor += slotWidth(segment, runSlot) * extra;
                start = i;
                run = current;
            }
        }
        return cursor;
    }

    std::string strip(const std::string& text) {
        if (text.find("\xC2\xA7") == std::string::npos) {
            return text;
        }
        std::u32string chars = Ui::decode(text);
        std::u32string out;
        out.reserve(chars.size());
        for (size_t i = 0; i < chars.size(); i++) {
            char32_t c = chars[i];
            if (c == 167 && i + 1 < chars.size()) {
                i++;
                continue;
            }
            out.push_back(c);
        }
        return Ui::encode(out);
    }

    float ascent(int family, float size) {
        return slotAscent(slot(family, size));
    }

    float height(int family, float size) {
        return slotHeight(slot(family, size));
    }

    uint32_t colorOf(char32_t code) {
        char32_t c = code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
        switch (c) {
            case '0': return 0xFF000000u;
            case '1': return 0xFF0000AAu;
            case '2': return 0xFF00AA00u;
            case '3': return 0xFF00AAAAu;
            case '4': return 0xFFAA0000u;
            case '5': return 0xFFAA00AAu;
            case '6': return 0xFFFFAA00u;
            case '7': return 0xFFAAAAAAu;
            case '8': return 0xFF555555u;
            case '9': return 0xFF5555FFu;
            case 'a': return 0xFF55FF55u;
            case 'b': return 0xFF55FFFFu;
            case 'c': return 0xFFFF5555u;
            case 'd': return 0xFFFF55FFu;
            case 'e': return 0xFFFFFF55u;
            case 'f': return 0xFFFFFFFFu;
            default: return 0;
        }
    }

    static bool drawIconGlyph(char32_t c, float x, float y, uint32_t color, float size, float extra) {
        if (c > 0xFFFF) {
            return false;
        }
        Slot nominal = drawSlot(ICON, size);
        float factor = 1.0f;
        ImFontBaked* baked = bakedOf(nominal, factor);
        const ImFontGlyph* glyph = baked != nullptr ? baked->FindGlyphNoFallback((ImWchar) c) : nullptr;
        if (glyph == nullptr || !glyph->Visible) {
            return false;
        }
        float scale = (float) pixelScale();
        float boxHeight = size * UiRender::stableScale() * UiRender::rasterScale;
        float nominalWidth = glyph->AdvanceX / scale;
        float centreX = (UiRender::transformX(x) + nominalWidth * 0.5f * extra) * scale;
        float centreY = (UiRender::transformY(y) + boxHeight * 0.5f * extra) * scale;
        Slot chosen = nominal;
        const ImFontGlyph* ink = glyph;
        if (std::fabs(extra - 1.0f) < 0.001f) {
            centreX = snapPixel(centreX);
            bool halfY = std::fabs(centreY - std::floor(centreY) - 0.5f) < 0.25f;
            centreY = halfY ? std::floor(centreY) + 0.5f : snapPixel(centreY);
            int best = 1 << 30;
            for (float delta : {0.0f, 1.0f, -1.0f}) {
                Slot candidate{nominal.font, nominal.size + delta};
                if (candidate.size < 1.0f) {
                    continue;
                }
                ImFontBaked* candidateBaked = bakedOf(candidate, factor);
                const ImFontGlyph* candidateGlyph =
                        candidateBaked != nullptr ? candidateBaked->FindGlyphNoFallback((ImWchar) c) : nullptr;
                if (candidateGlyph == nullptr || !candidateGlyph->Visible) {
                    continue;
                }
                Ink candidateInk = inkOf(candidateGlyph);
                int score = (odd(candidateInk.y1 - candidateInk.y0) != halfY ? 4 : 0) +
                        (odd(candidateInk.x1 - candidateInk.x0) ? 2 : 0) + (delta != 0.0f ? 1 : 0);
                if (score < best) {
                    best = score;
                    chosen = candidate;
                    ink = candidateGlyph;
                }
            }
            baked = bakedOf(chosen, factor);
            ink = baked->FindGlyphNoFallback((ImWchar) c);
        }
        Ink box = inkOf(ink);
        bool snapped = std::fabs(extra - 1.0f) < 0.001f;
        float left = centreX - (box.x1 - box.x0) * 0.5f * extra;
        float top = centreY - (box.y1 - box.y0) * 0.5f * extra;
        if (snapped) {
            left = snapPixel(left);
            top = snapPixel(top);
        }
        float pen = (left - box.x0 * extra) / scale;
        float lineTop = (top - box.y0 * extra) / scale;
        drawSlotStringScaled(std::u32string(1, c), pen, lineTop, color, chosen, extra);
        return true;
    }

    float centreY(const std::string& text, int family, float size, float y) {
        float drawSize = size * UiRender::stableScale() * UiRender::rasterScale;
        float extra = UiRender::scaleFactor / UiRender::rasterScale;
        std::u32string chars = Ui::decode(strip(text));
        Slot slot = drawSlot(chars.empty() ? family : familyFor(chars[0], family), size);
        float scale = (float) pixelScale();
        float centre = slotCenter(slot) * extra;
        float top = (UiRender::transformY(y) + drawSize * 0.5f * extra - centre) * scale;
        top = (std::fabs(extra - 1.0f) < 0.001f ? snapPixel(top) : top) / scale;
        float gui = top + centre;
        return ((gui - UiRender::pivotY) / UiRender::scaleFactor + UiRender::pivotY - UiRender::offsetY) /
                UiRender::uiScale;
    }

    void draw(const std::string& text, float x, float y, uint32_t rgba, int family, float size) {
        if (text.empty()) {
            return;
        }
        uint32_t color = UiRender::applyAlpha(rgba);
        if (((color >> 24) & 0xFF) == 0) {
            return;
        }
        float drawSize = size * UiRender::stableScale() * UiRender::rasterScale;
        Slot index = drawSlot(family, size);
        float extra = UiRender::scaleFactor / UiRender::rasterScale;
        std::u32string leading = Ui::decode(strip(text));
        int leadFamily = leading.empty() ? family : familyFor(leading[0], family);
        Slot lead = leadFamily == family ? index : drawSlot(leadFamily, size);
        float top = UiRender::transformY(y) +
                (drawSize * 0.5f - slotCenter(lead) + slotAscent(lead) - slotAscent(index)) * extra;
        if (shadow && !shadowPass) {
            shadowPass = true;
            uint32_t shadowColor = (uint32_t) Ui::round(((color >> 24) & 0xFF) * 0.7f) << 24;
            float offset = std::max(0.5f, size * 0.08f);
            draw(strip(text), x + offset, y + offset, shadowColor, family, size);
            shadowPass = false;
        }
        std::u32string chars = Ui::decode(text);
        if (family == ICON && chars.size() == 1 && drawIconGlyph(chars[0], x, y, color, size, extra)) {
            return;
        }
        if (raw || chars.find((char32_t) 167) == std::u32string::npos) {
            drawRuns(chars, UiRender::transformX(x), top, color, family, size, extra);
            return;
        }
        float cursor = UiRender::transformX(x);
        uint32_t alphaBits = color & 0xFF000000u;
        std::u32string buffer;
        uint32_t segColor = color;
        for (size_t i = 0; i <= chars.size(); i++) {
            bool end = i == chars.size();
            char32_t c = end ? U' ' : chars[i];
            if (!end && c != (char32_t) 167) {
                buffer.push_back(c);
                continue;
            }
            if (!buffer.empty()) {
                cursor = drawRuns(buffer, cursor, top, segColor, family, size, extra);
                buffer.clear();
            }
            if (end || i + 1 >= chars.size()) {
                break;
            }
            uint32_t rgb = colorOf(chars[++i]);
            segColor = rgb == 0 ? color : (alphaBits | (rgb & 0xFFFFFFu));
        }
    }
}
