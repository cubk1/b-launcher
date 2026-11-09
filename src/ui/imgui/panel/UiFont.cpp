#include "UiFont.h"
#include "UiRender.h"
#include "UiCommon.h"
#include "../fonts/EmbeddedFonts.h"
#include "../gl/UiGL.h"
#include "../gl/RenderUtil.h"

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
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string_view>
#if defined(__APPLE__)
#include <CoreText/CoreText.h>
#elif defined(BLB_FONTCONFIG_LINKED)
#include <fontconfig/fontconfig.h>
#else
#include <dlfcn.h>
#endif
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

namespace UiFont {

#if !defined(_WIN32)
    namespace {
        bool readable(const std::string& file) {
            return access(file.c_str(), R_OK) == 0;
        }

        int faceNamed(const std::string& file, std::string_view family) {
            FT_Library library = nullptr;
            if (FT_Init_FreeType(&library) != 0) {
                return -1;
            }
            int found = -1;
            FT_Face face = nullptr;
            long count = FT_New_Face(library, file.c_str(), -1, &face) == 0 ? face->num_faces : 0;
            if (face != nullptr) {
                FT_Done_Face(face);
            }
            for (long i = 0; i < count && found < 0; i++) {
                if (FT_New_Face(library, file.c_str(), i, &face) != 0) {
                    continue;
                }
                if (face->family_name != nullptr && family == face->family_name && face->style_name != nullptr &&
                    std::string_view(face->style_name) == "Regular") {
                    found = (int) i;
                }
                FT_Done_Face(face);
            }
            FT_Done_FreeType(library);
            return found;
        }

        bool covers(const std::string& spec, unsigned long code) {
            size_t hash = spec.rfind('#');
            std::string file = hash == std::string::npos ? spec : spec.substr(0, hash);
            long face = hash == std::string::npos ? 0 : std::atol(spec.c_str() + hash + 1);
            FT_Library library = nullptr;
            if (FT_Init_FreeType(&library) != 0) {
                return false;
            }
            FT_Face loaded = nullptr;
            bool found = FT_New_Face(library, file.c_str(), face, &loaded) == 0 && FT_Get_Char_Index(loaded, code) != 0;
            if (loaded != nullptr) {
                FT_Done_Face(loaded);
            }
            FT_Done_FreeType(library);
            return found;
        }

#if defined(__APPLE__)
        std::string fontFile(const char* name) {
            CFStringRef family = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
            CTFontRef font = CTFontCreateWithName(family, 16.0, nullptr);
            CFRelease(family);
            std::string file;
            if (font != nullptr) {
                CFURLRef url = (CFURLRef) CTFontCopyAttribute(font, kCTFontURLAttribute);
                char buffer[4096];
                if (url != nullptr && CFURLGetFileSystemRepresentation(url, true, (UInt8*) buffer, sizeof(buffer))) {
                    file = buffer;
                }
                if (url != nullptr) {
                    CFRelease(url);
                }
                CFRelease(font);
            }
            return file;
        }

        std::string systemFont(const char* name) {
            std::string file = fontFile(name);
            if (file.empty() || !readable(file)) {
                return {};
            }
            int face = file.ends_with(".ttc") ? faceNamed(file, name) : 0;
            return face < 0 ? std::string() : file + "#" + std::to_string(face);
        }

        std::vector<std::string> systemFallbacks() {
            std::vector<std::string> out;
            for (const char* name : {"PingFang SC", "Hiragino Sans GB", "STHeiti"}) {
                std::string spec = systemFont(name);
                if (!spec.empty() && covers(spec, 0x4E2D)) {
                    out.push_back(spec);
                    break;
                }
            }
            std::string symbols = systemFont("Apple Symbols");
            if (!symbols.empty()) {
                out.push_back(symbols);
            }
            return out;
        }

        std::string codeFont() {
            return systemFont("Menlo");
        }
#else
        // BLB_SYSTEM_LIBS links fontconfig. Other builds open it at run time, so they start without it.
        struct FontConfig {
            void* config = nullptr;
            void* (*parse)(const unsigned char*) = nullptr;
            int (*substitute)(void*, void*, int) = nullptr;
            void (*defaults)(void*) = nullptr;
            void* (*match)(void*, void*, int*) = nullptr;
            int (*text)(void*, const char*, int, unsigned char**) = nullptr;
            int (*number)(void*, const char*, int, int*) = nullptr;
            void (*destroy)(void*) = nullptr;

#if defined(BLB_FONTCONFIG_LINKED)
            FontConfig() {
                parse = [](const unsigned char* name) -> void* { return FcNameParse(name); };
                substitute = [](void* config, void* pattern, int kind) -> int {
                    return FcConfigSubstitute((FcConfig*) config, (FcPattern*) pattern, (FcMatchKind) kind);
                };
                defaults = [](void* pattern) { FcDefaultSubstitute((FcPattern*) pattern); };
                match = [](void* config, void* pattern, int* result) -> void* {
                    FcResult found = FcResultNoMatch;
                    FcPattern* font = FcFontMatch((FcConfig*) config, (FcPattern*) pattern, &found);
                    *result = found;
                    return font;
                };
                text = [](void* pattern, const char* object, int n, unsigned char** value) -> int {
                    return FcPatternGetString((FcPattern*) pattern, object, n, value);
                };
                number = [](void* pattern, const char* object, int n, int* value) -> int {
                    return FcPatternGetInteger((FcPattern*) pattern, object, n, value);
                };
                destroy = [](void* pattern) { FcPatternDestroy((FcPattern*) pattern); };
                config = FcInitLoadConfigAndFonts();
            }
#else
            void* library = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);

            FontConfig() {
                if (library == nullptr) {
                    return;
                }
                auto init = (void* (*) ()) dlsym(library, "FcInitLoadConfigAndFonts");
                parse = (void* (*) (const unsigned char*) ) dlsym(library, "FcNameParse");
                substitute = (int (*)(void*, void*, int)) dlsym(library, "FcConfigSubstitute");
                defaults = (void (*)(void*)) dlsym(library, "FcDefaultSubstitute");
                match = (void* (*) (void*, void*, int*) ) dlsym(library, "FcFontMatch");
                text = (int (*)(void*, const char*, int, unsigned char**)) dlsym(library, "FcPatternGetString");
                number = (int (*)(void*, const char*, int, int*)) dlsym(library, "FcPatternGetInteger");
                destroy = (void (*)(void*)) dlsym(library, "FcPatternDestroy");
                if (init && parse && substitute && defaults && match && text && number && destroy) {
                    config = init();
                }
            }
#endif

            std::string find(const char* query) {
                if (config == nullptr) {
                    return {};
                }
                void* pattern = parse((const unsigned char*) query);
                if (pattern == nullptr) {
                    return {};
                }
                substitute(config, pattern, 0);
                defaults(pattern);
                int result = 0;
                void* found = match(config, pattern, &result);
                destroy(pattern);
                if (found == nullptr) {
                    return {};
                }
                unsigned char* file = nullptr;
                int index = 0;
                std::string spec;
                if (text(found, "file", 0, &file) == 0 && file != nullptr) {
                    number(found, "index", 0, &index);
                    spec = std::string((const char*) file);
                    if (!readable(spec)) {
                        spec.clear();
                    } else {
                        spec += "#" + std::to_string(index & 0xFFFF);
                    }
                }
                destroy(found);
                return spec;
            }
        };

        FontConfig& fontConfig() {
            static FontConfig instance;
            return instance;
        }

        std::vector<std::string> systemFallbacks() {
            std::vector<std::string> out;
            for (const char* query :
                 {"sans-serif:lang=zh-cn", "Noto Sans CJK SC", "Source Han Sans SC", "Source Han Sans CN",
                  "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Droid Sans Fallback", "AR PL UMing CN", ":lang=zh-cn"}) {
                std::string spec = fontConfig().find(query);
                if (!spec.empty() && covers(spec, 0x4E2D)) {
                    out.push_back(spec);
                    break;
                }
            }
            const char* known[][2] = {
                    {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "Noto Sans CJK SC"},
                    {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "Noto Sans CJK SC"},
                    {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "Noto Sans CJK SC"},
                    {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "WenQuanYi Micro Hei"},
                    {"/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc", "WenQuanYi Micro Hei"},
                    {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", "Droid Sans Fallback"}};
            for (const auto& [file, family] : known) {
                if (!out.empty()) {
                    break;
                }
                if (readable(file)) {
                    int face = faceNamed(file, family);
                    std::string spec = std::string(file) + "#" + std::to_string(face < 0 ? 0 : face);
                    if (covers(spec, 0x4E2D)) {
                        out.push_back(spec);
                    }
                }
            }
            std::string symbols = fontConfig().find("symbol");
            if (!symbols.empty() && std::find(out.begin(), out.end(), symbols) == out.end()) {
                out.push_back(symbols);
            }
            return out;
        }

        std::string codeFont() {
            return fontConfig().find("monospace");
        }
#endif
    }
#endif

    ImFont* baseFonts[FAMILY_COUNT] = {};
    std::unordered_map<int, int> fallbackCache;
    bool loaded = false;
    bool raw = false;
    bool shadow = false;
    bool shadowPass = false;
#if defined(_WIN32)
    std::vector<std::string> fallbackFonts = {"msyh.ttc#1", "msyh.ttf", "seguisym.ttf"};
#else
    std::vector<std::string> fallbackFonts = systemFallbacks();
#endif
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

    namespace {
        const int ROLE_FAMILIES[ROLE_COUNT] = {MEDIUM, BOLD, DISPLAY, MONO, CODE};

        struct Loaded {
            std::string key;
            ImFont* font = nullptr;
            std::shared_ptr<const std::string> data;
            const unsigned char* mapped = nullptr;
            unsigned int mappedSize = 0;
        };

        ImFont* defaultFonts[FAMILY_COUNT] = {};
        std::vector<Loaded> themeFonts;
        FontSet activeSet;
        FontSet pendingSet;
        bool hasPending = false;
        unsigned fontGeneration = 0;
        std::vector<std::string> fontProblems;
        std::unordered_map<ImFont*, float> fontScales;
        std::unordered_map<std::string, std::string> fitted;
    }

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

    static const unsigned char* mapFile(const std::string& path, unsigned int& size) {
#if defined(_WIN32)
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return nullptr;
        }
        LARGE_INTEGER length = {};
        if (!GetFileSizeEx(file, &length) || length.QuadPart < 100 || length.QuadPart > 0x7FFFFFFF) {
            CloseHandle(file);
            return nullptr;
        }
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        CloseHandle(file);
        if (mapping == nullptr) {
            return nullptr;
        }
        void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);
        size = (unsigned int) length.QuadPart;
        return (const unsigned char*) view;
#else
        int file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (file < 0) {
            return nullptr;
        }
        struct stat info {};
        if (fstat(file, &info) != 0 || info.st_size < 100 || info.st_size > 0x7FFFFFFF) {
            close(file);
            return nullptr;
        }
        void* view = mmap(nullptr, (size_t) info.st_size, PROT_READ, MAP_PRIVATE, file, 0);
        close(file);
        if (view == MAP_FAILED) {
            return nullptr;
        }
        size = (unsigned int) info.st_size;
        return (const unsigned char*) view;
#endif
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
        ImFontConfig config;
        crisp(config, fallbackLoaderFlags);
        config.Flags |= ImFontFlags_NoLoadError;
        config.FontNo = face;
        snprintf(config.Name, sizeof(config.Name), "%s", spec.c_str());
        unsigned int size = 0;
        if (const unsigned char* data = mapFile(path, size)) {
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*) data, (int) size, 16.0f, &config);
        }
        std::ifstream probe(path, std::ios::binary);
        if (!probe) {
            return nullptr;
        }
        probe.close();
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config);
    }

    void init() {
        ensureBaseFonts();
    }

    void compact() {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (atlas == nullptr || atlas->Builder == nullptr || atlas->TexData == nullptr) {
            return;
        }
        ImFontAtlasBuildDiscardBakes(atlas, 600);
        ImVec2i size = ImFontAtlasTextureGetSizeEstimate(atlas);
        if (atlas->Builder->RectsDiscardedCount > 0 || size.x != atlas->TexData->Width ||
            size.y != atlas->TexData->Height) {
            ImFontAtlasTextureRepack(atlas, size.x, size.y);
        }
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
#if defined(_WIN32)
        ImFont* code = fromSystem("consola.ttf");
#else
        std::string codeSpec = codeFont();
        ImFont* code = codeSpec.empty() ? nullptr : fromSystem(codeSpec);
#endif
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
        std::copy(std::begin(baseFonts), std::end(baseFonts), std::begin(defaultFonts));
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
                unsigned int index = (unsigned int) font->Sources[0]->FontNo & 0xFFFFu;
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

    static std::unordered_map<unsigned long long, Ink> inks;

    static Ink inkOf(const ImFontBaked* baked, const ImFontGlyph* glyph) {
        Ink ink{glyph->X0, glyph->Y0, glyph->X1, glyph->Y1};
        unsigned long long key = 0;
        if (baked != nullptr) {
            key = (unsigned long long) (uintptr_t) baked->ContainerFont;
            key = key * 1000003ULL ^ (unsigned long long) (baked->Size * 64.0f);
            key = key * 1000003ULL ^ (unsigned long long) glyph->Codepoint;
            auto cached = inks.find(key);
            if (cached != inks.end()) {
                return cached->second;
            }
        }
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
        if (baked != nullptr && inks.size() > 8192) {
            inks.clear();
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
        if (top >= 0) {
            float unitX = (glyph->X1 - glyph->X0) / r->w;
            float unitY = (glyph->Y1 - glyph->Y0) / r->h;
            ink = Ink{glyph->X0 + left * unitX, glyph->Y0 + top * unitY, glyph->X0 + (right + 1) * unitX,
                      glyph->Y0 + (bottom + 1) * unitY};
        }
        if (baked != nullptr) {
            inks[key] = ink;
        }
        return ink;
    }

    static bool odd(float pixels) {
        return ((int) Ui::round(pixels)) % 2 != 0;
    }

    static std::unordered_map<unsigned long long, float> bakedSizes;

    static float scaleOf(ImFont* font);

    static float bakedSize(ImFont* font, float size) {
        float requested = std::max(1.0f, size * emRatio(font) * scaleOf(font));
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
            Ink box = glyph != nullptr ? inkOf(baked, glyph) : Ink{};
            if (glyph != nullptr && !odd(box.y1 - box.y0)) {
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
            Ink ink = inkOf(baked, glyph);
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
        int visible = 0;
        for (char32_t c : text) {
            const ImFontGlyph* glyph = baked->FindGlyph((ImWchar) (c <= 0xFFFF ? c : 0xFFFD));
            if (glyph != nullptr && glyph->Visible) {
                visible++;
            }
        }
        baked = bakedOf(slot, factor);
        UiRender::flush();
        ImDrawList* dl = UiRender::drawList();
        dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
        float scale = (float) pixelScale();
        float unit = UiRender::unit();
        float glyphScale = factor / scale * extraScale;
        bool lcd = visible > 0 && activeSubpixel != SUBPIXEL_OFF && UiGL::BlendColor != nullptr;
        if (lcd) {
            float rgb[3] = {((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f};
            dl->AddCallback(beginSubpixel, rgb, sizeof(rgb));
        }
        if (visible > 0) {
            dl->PrimReserve(visible * 6, visible * 4);
            unsigned int alpha = (color >> 24) & 0xFF;
            ImU32 col = lcd ? IM_COL32(alpha, alpha, alpha, alpha) : RenderUtil::imColor(color);
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
            if (width(ellipsis, family, size) > available) return {};
            int length = (int) Ui::length(text);
            auto piece = [&](int count) {
                return keepEnd ? ellipsis + Ui::substring(text, length - count)
                               : Ui::substring(text, 0, count) + ellipsis;
            };
            int low = 0;
            int high = std::max(0, length - 1);
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

    bool FontSet::same(const FontSet& other) const {
        for (int i = 0; i < ROLE_COUNT; i++) {
            if (roles[i].key != other.roles[i].key) {
                return false;
            }
        }
        return scale == other.scale;
    }

    int roleFamily(int role) {
        return role >= 0 && role < ROLE_COUNT ? ROLE_FAMILIES[role] : MEDIUM;
    }

    unsigned generation() {
        return fontGeneration;
    }

    const std::vector<std::string>& problems() {
        return fontProblems;
    }

    void request(const FontSet& set) {
        pendingSet = set;
        hasPending = true;
    }

    static void unmap(const unsigned char* view, unsigned int size) {
        if (view == nullptr) {
            return;
        }
#if defined(_WIN32)
        UnmapViewOfFile(view);
#else
        munmap((void*) view, size);
#endif
    }

    static int namedInstance(const unsigned char* data, size_t size, int face, int weight) {
        if (weight <= 0) {
            return face;
        }
        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0) {
            return face;
        }
        int result = face;
        FT_Face loaded = nullptr;
        if (FT_New_Memory_Face(library, data, (FT_Long) size, face, &loaded) == 0) {
            FT_MM_Var* axes = nullptr;
            if (FT_HAS_MULTIPLE_MASTERS(loaded) && FT_Get_MM_Var(loaded, &axes) == 0) {
                for (FT_UInt axis = 0; axis < axes->num_axis; axis++) {
                    if (axes->axis[axis].tag != FT_MAKE_TAG('w', 'g', 'h', 't')) {
                        continue;
                    }
                    double best = 1.0e9;
                    for (FT_UInt i = 0; i < axes->num_namedstyles; i++) {
                        double distance = std::fabs(axes->namedstyle[i].coords[axis] / 65536.0 - weight);
                        if (distance < best) {
                            best = distance;
                            result = face | (int) ((i + 1) << 16);
                        }
                    }
                }
                FT_Done_MM_Var(library, axes);
            }
            FT_Done_Face(loaded);
        }
        FT_Done_FreeType(library);
        return result;
    }

    static Loaded loadChoice(const FontChoice& choice) {
        Loaded entry;
        entry.key = choice.key;
        ImFontConfig config;
        crisp(config, loaderFlags);
        config.Flags |= ImFontFlags_NoLoadError;
        config.FontDataOwnedByAtlas = false;
        config.FontNo = (ImU32) std::max(0, choice.face);
        snprintf(config.Name, sizeof(config.Name), "%s", choice.key.c_str());
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (choice.data && choice.data->size() >= 100) {
            entry.data = choice.data;
            config.FontNo = (ImU32) namedInstance((const unsigned char*) entry.data->data(), entry.data->size(),
                                                  (int) config.FontNo, choice.weight);
            entry.font =
                    atlas->AddFontFromMemoryTTF((void*) entry.data->data(), (int) entry.data->size(), 16.0f, &config);
            return entry;
        }
        if (choice.spec.empty()) {
            return entry;
        }
        std::string file = choice.spec;
        size_t hash = file.rfind('#');
        if (hash != std::string::npos) {
            config.FontNo = (ImU32) std::max(0, std::atoi(file.c_str() + hash + 1));
            file = file.substr(0, hash);
        }
        bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\' || file[0] == '/');
        std::string path = absolute ? file : windowsFontsDir() + file;
        entry.mapped = mapFile(path, entry.mappedSize);
        if (entry.mapped == nullptr) {
            return entry;
        }
        config.FontNo = (ImU32) namedInstance(entry.mapped, entry.mappedSize, (int) config.FontNo, choice.weight);
        entry.font = atlas->AddFontFromMemoryTTF((void*) entry.mapped, (int) entry.mappedSize, 16.0f, &config);
        if (entry.font == nullptr) {
            unmap(entry.mapped, entry.mappedSize);
            entry.mapped = nullptr;
        }
        return entry;
    }

    static void release(Loaded& entry) {
        if (entry.font != nullptr) {
            ImGui::GetIO().Fonts->RemoveFont(entry.font);
            entry.font = nullptr;
        }
        unmap(entry.mapped, entry.mappedSize);
        entry.mapped = nullptr;
        entry.data.reset();
    }

    static float advanceOf(ImFontBaked* baked, char32_t c) {
        const ImFontGlyph* glyph = baked->FindGlyphNoFallback((ImWchar) c);
        return glyph != nullptr ? glyph->AdvanceX : 0.0f;
    }

    static bool monospace(ImFont* font) {
        ImFontBaked* baked = font->GetFontBaked(32.0f, 1.0f);
        if (baked == nullptr) {
            return false;
        }
        float low = 1.0e9f;
        float high = 0.0f;
        for (char32_t c : {U'0', U'i', U'W', U'.'}) {
            float advance = advanceOf(baked, c);
            if (advance <= 0.0f) {
                return false;
            }
            low = std::min(low, advance);
            high = std::max(high, advance);
        }
        return high <= low * 1.005f;
    }

    static float capHeight(ImFont* font) {
        ImFontBaked* baked = font->GetFontBaked(std::max(1.0f, (float) Ui::round(64.0f * emRatio(font))), 1.0f);
        const ImFontGlyph* glyph = baked != nullptr ? baked->FindGlyphNoFallback((ImWchar) 'H') : nullptr;
        if (glyph == nullptr || !glyph->Visible) {
            return 0.0f;
        }
        Ink ink = inkOf(nullptr, glyph);
        return ink.y1 - ink.y0;
    }

    static float scaleOf(ImFont* font) {
        auto found = fontScales.find(font);
        return found != fontScales.end() ? found->second : 1.0f;
    }

    bool applyPending() {
        if (!hasPending) {
            return false;
        }
        hasPending = false;
        ensureBaseFonts();
        if (pendingSet.same(activeSet)) {
            return false;
        }
        std::vector<Loaded> next;
        ImFont* chosen[ROLE_COUNT] = {};
        fontProblems.clear();
        for (int role = 0; role < ROLE_COUNT; role++) {
            const FontChoice& choice = pendingSet.roles[role];
            if (choice.key.empty()) {
                continue;
            }
            auto reuse = std::find_if(next.begin(), next.end(),
                                      [&](const Loaded& entry) { return entry.key == choice.key; });
            if (reuse == next.end()) {
                auto kept = std::find_if(themeFonts.begin(), themeFonts.end(),
                                         [&](const Loaded& entry) { return entry.key == choice.key && entry.font; });
                if (kept != themeFonts.end()) {
                    next.push_back(std::move(*kept));
                    kept->font = nullptr;
                    kept->mapped = nullptr;
                } else {
                    Loaded entry = loadChoice(choice);
                    if (entry.font == nullptr) {
                        fontProblems.push_back("无法加载字体 " + choice.key);
                        continue;
                    }
                    next.push_back(std::move(entry));
                }
                reuse = next.end() - 1;
            }
            if (ROLE_FAMILIES[role] == CODE && !monospace(reuse->font)) {
                fontProblems.push_back("代码字体必须是等宽字体：" + choice.key);
                continue;
            }
            chosen[role] = reuse->font;
        }
        for (auto it = next.begin(); it != next.end();) {
            if (std::find(std::begin(chosen), std::end(chosen), it->font) == std::end(chosen)) {
                release(*it);
                it = next.erase(it);
            } else {
                ++it;
            }
        }
        for (Loaded& old : themeFonts) {
            release(old);
        }
        themeFonts = std::move(next);
        fallbackCache.clear();
        emRatios.clear();
        inks.clear();
        bakedSizes.clear();
        fitted.clear();
        fontScales.clear();
        float user = std::clamp(pendingSet.scale, 0.9f, 1.15f);
        float reference = capHeight(defaultFonts[MEDIUM]);
        for (const Loaded& entry : themeFonts) {
            float own = capHeight(entry.font);
            float normal = reference > 0.0f && own > 0.0f ? std::clamp(reference / own, 0.8f, 1.25f) : 1.0f;
            fontScales[entry.font] = normal * user;
        }
        for (int role = 0; role < ROLE_COUNT; role++) {
            int family = ROLE_FAMILIES[role];
            baseFonts[family] = chosen[role] != nullptr ? chosen[role] : defaultFonts[family];
        }
        activeSet = pendingSet;
        fontGeneration++;
        return true;
    }

#if defined(_WIN32)
    static std::string registryFont(HKEY root, const std::string& family) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &key) !=
            ERROR_SUCCESS) {
            return {};
        }
        std::string found;
        wchar_t name[512];
        wchar_t data[1024];
        for (DWORD index = 0; found.empty(); index++) {
            DWORD nameLength = 512;
            DWORD dataLength = sizeof(data);
            DWORD type = 0;
            LONG status = RegEnumValueW(key, index, name, &nameLength, nullptr, &type, (LPBYTE) data, &dataLength);
            if (status == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (status != ERROR_SUCCESS || type != REG_SZ) {
                continue;
            }
            int bytes = WideCharToMultiByte(CP_UTF8, 0, name, (int) nameLength, nullptr, 0, nullptr, nullptr);
            std::string label((size_t) bytes, '\0');
            WideCharToMultiByte(CP_UTF8, 0, name, (int) nameLength, label.data(), bytes, nullptr, nullptr);
            size_t paren = label.rfind(" (");
            if (paren != std::string::npos) {
                label = label.substr(0, paren);
            }
            int face = 0;
            size_t start = 0;
            while (start <= label.size()) {
                size_t split = label.find(" & ", start);
                std::string part = label.substr(start, split == std::string::npos ? std::string::npos : split - start);
                if (part == family || part == family + " Regular") {
                    size_t length = wcsnlen(data, sizeof(data) / sizeof(data[0]));
                    int size = WideCharToMultiByte(CP_UTF8, 0, data, (int) length, nullptr, 0, nullptr, nullptr);
                    std::string file((size_t) size, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, data, (int) length, file.data(), size, nullptr, nullptr);
                    found = file + "#" + std::to_string(face);
                    break;
                }
                if (split == std::string::npos) {
                    break;
                }
                start = split + 3;
                face++;
            }
        }
        RegCloseKey(key);
        return found;
    }
#endif

    std::string systemFontSpec(const std::string& family) {
#if defined(_WIN32)
        if (family == "monospace") {
            return "consola.ttf#0";
        }
        if (family == "sans-serif") {
            return "segoeui.ttf#0";
        }
        if (family == "serif") {
            return "times.ttf#0";
        }
        std::string spec = registryFont(HKEY_LOCAL_MACHINE, family);
        return spec.empty() ? registryFont(HKEY_CURRENT_USER, family) : spec;
#elif defined(__APPLE__)
        return systemFont(family == "monospace" ? "Menlo" : family.c_str());
#else
        return fontConfig().find(family.c_str());
#endif
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
                Ink candidateInk = inkOf(candidateBaked, candidateGlyph);
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
        Ink box = inkOf(baked, ink);
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
        float extra = UiRender::settledScale() / UiRender::rasterScale;
        std::u32string chars = Ui::decode(strip(text));
        Slot slot = drawSlot(chars.empty() ? family : familyFor(chars[0], family), size);
        float scale = (float) pixelScale();
        float centre = slotCenter(slot) * extra;
        float top = (UiRender::transformY(y) + drawSize * 0.5f * extra - centre) * scale;
        top = (std::fabs(extra - 1.0f) < 0.001f ? snapPixel(top) : top) / scale;
        float gui = top + centre;
        return ((gui - UiRender::pivotY) / UiRender::settledScale() + UiRender::pivotY - UiRender::offsetY) /
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
        float extra = UiRender::settledScale() / UiRender::rasterScale;
        bool coded = !raw && text.find("\xC2\xA7") != std::string::npos;
        std::u32string chars = Ui::decode(text);
        std::u32string leading = coded ? Ui::decode(strip(text)) : chars;
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
        if (family == ICON && chars.size() == 1 && drawIconGlyph(chars[0], x, y, color, size, extra)) {
            return;
        }
        if (!coded) {
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
