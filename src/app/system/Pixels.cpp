#include "app/system/Pixels.h"

#include "stb_image.h"

#if !defined(_WIN32)
#include <zlib.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
namespace {
    void bigEndian(std::string& out, uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back((char) ((value >> shift) & 0xFF));
        }
    }

    void chunk(std::string& out, const char* type, const std::string& body) {
        bigEndian(out, (uint32_t) body.size());
        std::string tagged = std::string(type, 4) + body;
        out += tagged;
        bigEndian(out, (uint32_t) crc32(0, (const Bytef*) tagged.data(), (uInt) tagged.size()));
    }
}
#endif

namespace {
    uint32_t little(const unsigned char* p, int bytes) {
        uint32_t value = 0;
        for (int i = bytes - 1; i >= 0; i--) {
            value = (value << 8) | p[i];
        }
        return value;
    }

    std::shared_ptr<const Pixels> decodeIco(const unsigned char* bytes, size_t size) {
        if (size < 6 || little(bytes, 2) != 0 || little(bytes + 2, 2) != 1) {
            return nullptr;
        }
        size_t count = little(bytes + 4, 2);
        size_t best = 0;
        uint64_t bestArea = 0;
        for (size_t i = 0; i < count && 6 + i * 16 + 16 <= size; i++) {
            const unsigned char* entry = bytes + 6 + i * 16;
            uint64_t w = entry[0] == 0 ? 256 : entry[0];
            uint64_t h = entry[1] == 0 ? 256 : entry[1];
            if (w * h > bestArea) {
                bestArea = w * h;
                best = i + 1;
            }
        }
        if (best == 0) {
            return nullptr;
        }
        const unsigned char* entry = bytes + 6 + (best - 1) * 16;
        size_t length = little(entry + 8, 4);
        size_t offset = little(entry + 12, 4);
        if (offset > size || length > size - offset || length < 40) {
            return nullptr;
        }
        const unsigned char* image = bytes + offset;
        if (std::memcmp(image, "\x89PNG", 4) == 0) {
            return Pixels::decode(image, length);
        }
        int width = (int) little(image + 4, 4);
        int height = (int) little(image + 8, 4) / 2;
        size_t header = little(image, 4);
        if (little(image + 14, 2) != 32 || width <= 0 || height <= 0 || width > 1024 || height > 1024 ||
            header > length || (size_t) width * height * 4 > length - header) {
            return nullptr;
        }
        auto pixels = std::make_shared<Pixels>();
        pixels->width = width;
        pixels->height = height;
        pixels->rgba.resize((size_t) width * height * 4);
        for (int y = 0; y < height; y++) {
            const unsigned char* row = image + header + (size_t) (height - 1 - y) * width * 4;
            unsigned char* to = pixels->rgba.data() + (size_t) y * width * 4;
            for (int x = 0; x < width; x++) {
                to[x * 4] = row[x * 4 + 2];
                to[x * 4 + 1] = row[x * 4 + 1];
                to[x * 4 + 2] = row[x * 4];
                to[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        return pixels;
    }
}

#if !defined(_WIN32)
std::string Pixels::encodePng(const Pixels& pixels) {
    if (pixels.width <= 0 || pixels.height <= 0 || pixels.rgba.size() < (size_t) pixels.width * pixels.height * 4) {
        return {};
    }
    size_t stride = (size_t) pixels.width * 4;
    std::string raw;
    raw.reserve((stride + 1) * pixels.height);
    for (int y = 0; y < pixels.height; y++) {
        raw.push_back(0);
        raw.append((const char*) pixels.rgba.data() + (size_t) y * stride, stride);
    }
    uLongf packed = compressBound((uLong) raw.size());
    std::string compressed((size_t) packed, '\0');
    if (compress2((Bytef*) compressed.data(), &packed, (const Bytef*) raw.data(), (uLong) raw.size(), 6) != Z_OK) {
        return {};
    }
    compressed.resize((size_t) packed);
    std::string header;
    bigEndian(header, (uint32_t) pixels.width);
    bigEndian(header, (uint32_t) pixels.height);
    header += std::string("\x08\x06\x00\x00\x00", 5);
    std::string out = "\x89PNG\r\n\x1a\n";
    chunk(out, "IHDR", header);
    chunk(out, "IDAT", compressed);
    chunk(out, "IEND", std::string());
    return out;
}
#endif

std::shared_ptr<const Pixels> Pixels::decode(const void* data, size_t size) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded =
            stbi_load_from_memory((const unsigned char*) data, (int) size, &width, &height, &channels, 4);
    if (decoded == nullptr) {
        return decodeIco((const unsigned char*) data, size);
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

namespace {
    struct Tap {
        int first = 0;
        std::vector<float> weights;
    };

    std::vector<Tap> taps(int from, int to) {
        std::vector<Tap> out((size_t) to);
        float scale = (float) from / (float) to;
        float radius = std::max(1.0f, scale);
        for (int i = 0; i < to; i++) {
            float center = (i + 0.5f) * scale;
            int first = std::max(0, (int) std::floor(center - radius));
            int last = std::min(from - 1, (int) std::ceil(center + radius));
            Tap& tap = out[(size_t) i];
            tap.first = first;
            float total = 0.0f;
            for (int k = first; k <= last; k++) {
                float weight = std::max(0.0f, 1.0f - std::fabs((k + 0.5f - center) / radius));
                tap.weights.push_back(weight);
                total += weight;
            }
            if (total <= 0.0f) {
                tap.first = std::clamp((int) center, 0, from - 1);
                tap.weights.assign(1, 1.0f);
                total = 1.0f;
            }
            for (float& weight : tap.weights) {
                weight /= total;
            }
        }
        return out;
    }
}

Pixels Pixels::resized(const Pixels& source, int x, int y, int w, int h, int width, int height) {
    Pixels out;
    x = std::clamp(x, 0, source.width);
    y = std::clamp(y, 0, source.height);
    w = std::clamp(w, 0, source.width - x);
    h = std::clamp(h, 0, source.height - y);
    if (w <= 0 || h <= 0 || width <= 0 || height <= 0 ||
        source.rgba.size() < (size_t) source.width * source.height * 4) {
        return out;
    }
    std::vector<float> premultiplied((size_t) w * h * 4);
    for (int row = 0; row < h; row++) {
        const unsigned char* in = source.rgba.data() + ((size_t) (y + row) * source.width + x) * 4;
        float* to = premultiplied.data() + (size_t) row * w * 4;
        for (int col = 0; col < w; col++, in += 4, to += 4) {
            float alpha = in[3] / 255.0f;
            to[0] = in[0] * alpha;
            to[1] = in[1] * alpha;
            to[2] = in[2] * alpha;
            to[3] = in[3];
        }
    }
    std::vector<Tap> across = taps(w, width);
    std::vector<Tap> down = taps(h, height);
    std::vector<float> rows((size_t) width * h * 4);
    for (int row = 0; row < h; row++) {
        const float* in = premultiplied.data() + (size_t) row * w * 4;
        float* to = rows.data() + (size_t) row * width * 4;
        for (int col = 0; col < width; col++) {
            const Tap& tap = across[(size_t) col];
            float sum[4] = {};
            for (size_t k = 0; k < tap.weights.size(); k++) {
                const float* pixel = in + (size_t) (tap.first + (int) k) * 4;
                for (int c = 0; c < 4; c++) {
                    sum[c] += pixel[c] * tap.weights[k];
                }
            }
            std::copy(sum, sum + 4, to + (size_t) col * 4);
        }
    }
    out.width = width;
    out.height = height;
    out.rgba.resize((size_t) width * height * 4);
    for (int row = 0; row < height; row++) {
        const Tap& tap = down[(size_t) row];
        unsigned char* to = out.rgba.data() + (size_t) row * width * 4;
        for (int col = 0; col < width; col++, to += 4) {
            float sum[4] = {};
            for (size_t k = 0; k < tap.weights.size(); k++) {
                const float* pixel = rows.data() + ((size_t) (tap.first + (int) k) * width + col) * 4;
                for (int c = 0; c < 4; c++) {
                    sum[c] += pixel[c] * tap.weights[k];
                }
            }
            float alpha = std::clamp(sum[3], 0.0f, 255.0f);
            float undo = alpha > 0.0f ? 255.0f / alpha : 0.0f;
            for (int c = 0; c < 3; c++) {
                to[c] = (unsigned char) std::lround(std::clamp(sum[c] * undo, 0.0f, 255.0f));
            }
            to[3] = (unsigned char) std::lround(alpha);
        }
    }
    return out;
}
