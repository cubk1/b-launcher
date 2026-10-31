#include "app/system/Pixels.h"

#include "stb_image.h"

#include <cstring>
#include <string>
#include <vector>

std::shared_ptr<const Pixels> Pixels::decode(const void* data, size_t size) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded =
            stbi_load_from_memory((const unsigned char*) data, (int) size, &width, &height, &channels, 4);
    if (decoded == nullptr) {
        return nullptr;
    }
    auto pixels = std::make_shared<Pixels>();
    pixels->width = width;
    pixels->height = height;
    pixels->rgba.assign(decoded, decoded + (size_t) width * height * 4);
    stbi_image_free(decoded);
    return pixels;
}

std::shared_ptr<const Pixels> Pixels::decodePng(const void* data, size_t size, uint32_t maxSide) {
    static const unsigned char SIGNATURE[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const unsigned char* bytes = (const unsigned char*) data;
    if (size < sizeof(SIGNATURE) || size > (size_t) INT32_MAX ||
        std::memcmp(bytes, SIGNATURE, sizeof(SIGNATURE)) != 0) {
        return nullptr;
    }
    auto number = [bytes](size_t at) {
        return ((uint32_t) bytes[at] << 24) | ((uint32_t) bytes[at + 1] << 16) | ((uint32_t) bytes[at + 2] << 8) |
                (uint32_t) bytes[at + 3];
    };
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bits = 0;
    std::string compressed;
    size_t at = sizeof(SIGNATURE);
    while (at + 12 <= size) {
        size_t length = number(at);
        if (length > size - at - 12) {
            return nullptr;
        }
        const unsigned char* type = bytes + at + 4;
        const unsigned char* body = bytes + at + 8;
        bool first = at == sizeof(SIGNATURE);
        if (std::memcmp(type, "CgBI", 4) == 0 || (first && std::memcmp(type, "IHDR", 4) != 0)) {
            return nullptr;
        }
        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13) {
            width = number(at + 8);
            height = number(at + 12);
            uint32_t channels = body[9] == 2 ? 3 : body[9] == 4 ? 2 : body[9] == 6 ? 4 : 1;
            bits = channels * body[8];
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            compressed.append((const char*) body, length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + length;
    }
    if (width == 0 || height == 0 || width > maxSide || height > maxSide || bits == 0 || bits > 64 ||
        compressed.empty()) {
        return nullptr;
    }
    size_t row = ((size_t) width * bits + 7) / 8 + 1;
    size_t limit = row * height * 2 + 64;
    std::vector<char> scratch(limit);
    if (stbi_zlib_decode_buffer(scratch.data(), (int) limit, compressed.data(), (int) compressed.size()) < 0) {
        return nullptr;
    }
    return decode(data, size);
}
