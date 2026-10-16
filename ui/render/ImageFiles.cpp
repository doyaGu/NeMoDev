#include "ui/render/UiRenderer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace vdev {
bool WriteBgraPng(const std::string &path, int width, int height, const unsigned char *pixels) {
    if (!pixels || width <= 0 || height <= 0) return false;
    std::vector<unsigned char> rgba(pixels, pixels + static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i+2]);
    std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) return false;
    const int written = stbi_write_png_to_func([](void *context, void *data, int size) {
        static_cast<std::ofstream *>(context)->write(static_cast<const char *>(data), size);
    }, &file, width, height, 4, rgba.data(), width * 4);
    file.close();
    return written && file.good();
}
bool ReadImageRgba(const std::string &path, std::vector<unsigned char> &pixels, int &width, int &height) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) return false;
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(file), {});
    if (bytes.size() > std::numeric_limits<int>::max()) return false;
    int channels = 0;
    stbi_uc *image = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!image) return false;
    pixels.assign(image, image + static_cast<size_t>(width) * height * 4);
    stbi_image_free(image);
    return true;
}
}
