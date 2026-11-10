#include "app/system/IconFile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace IconFile {

    namespace {
        constexpr int BASE_PIXELS = 256;
        const int ICON_SIZES[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};

        struct Image {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> rgba;
        };

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

        Image reduced(const std::vector<unsigned char>& rgba, int width, int height) {
            Image image{width, height, rgba};
            if (width <= BASE_PIXELS && height <= BASE_PIXELS) {
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
    }

    std::vector<unsigned char> square(const std::vector<unsigned char>& rgba, int width, int height, int size) {
        if (rgba.size() < (size_t) width * height * 4 || width <= 0 || height <= 0) {
            return {};
        }
        return resample(reduced(rgba, width, height), size);
    }

    std::string ico(const std::vector<unsigned char>& rgba, int width, int height) {
        if (rgba.size() < (size_t) width * height * 4 || width <= 0 || height <= 0) {
            return {};
        }
        Image image = reduced(rgba, width, height);
        std::vector<std::string> images;
        for (int size : ICON_SIZES) {
            std::vector<unsigned char> pixels = resample(image, size);
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
                    const unsigned char* p = &pixels[((size_t) y * size + x) * 4];
                    dib.push_back((char) p[2]);
                    dib.push_back((char) p[1]);
                    dib.push_back((char) p[0]);
                    dib.push_back((char) p[3]);
                }
            }
            for (int y = size - 1; y >= 0; y--) {
                std::string row(stride, '\0');
                for (int x = 0; x < size; x++) {
                    if (pixels[((size_t) y * size + x) * 4 + 3] == 0) {
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
}
