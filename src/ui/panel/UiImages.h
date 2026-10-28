#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UiImages {

    constexpr int KIND_MISSING = 0;
    constexpr int KIND_ITEM = 1;
    constexpr int KIND_TEXTURE = 2;
    constexpr int KIND_LOADING = 3;
    constexpr int KIND_FAILED = 4;

    struct Resolved {
        uint32_t texture;
        int width;
        int height;
    };

    extern bool promoted;
    extern std::function<bool(const std::string& source, Resolved& out)> itemResolver;
    extern std::function<bool(const std::string& source, Resolved& out)> textureResolver;
    extern std::function<bool(const std::string& source, Resolved& out)> silhouetteResolver;

    int resolve(const std::string& source);
    int resolveItem(const std::string& source, const std::string& name);
    int resolveTexture(const std::string& source, const std::string& path);
    void startFetch(const std::string& source);
    void drainQueue();
    void fetchOne(const std::string& source);
    void promote(const std::string& source);
    bool isGif(const std::vector<unsigned char>& data);
    bool promoteGif(const std::string& source, const std::vector<unsigned char>& data);
    uint32_t upload(const unsigned char* rgba, int width, int height);
    uint32_t uploadEncoded(const unsigned char* data, int size, int* width, int* height);
    int widthOf(const std::string& source);
    int heightOf(const std::string& source);
    uint32_t textureOf(const std::string& source);
}
