#include "app/system/Pixels.h"

#include "stb_image.h"

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
