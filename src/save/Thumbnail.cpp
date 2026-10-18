#include "save/Thumbnail.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace zl::save {
namespace {

constexpr int kMaxThumbnailDimension = 16384;

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

void AppendPng(void* context, void* data, int size) {
    if (context == nullptr || data == nullptr || size <= 0) {
        return;
    }
    auto* output = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
}

}  // namespace

bool EncodePng(const ThumbnailImage& image, std::vector<std::uint8_t>* out,
               std::string* error) {
    if (out == nullptr) {
        return Fail(error, "缩略图输出为空");
    }
    if (image.width <= 0 || image.height <= 0 ||
        image.width > kMaxThumbnailDimension ||
        image.height > kMaxThumbnailDimension) {
        return Fail(error, "缩略图尺寸非法");
    }
    if (!image.Valid()) {
        return Fail(error, "缩略图像素数量与尺寸不一致");
    }

    stbi_write_png_compression_level = 6;
    std::vector<std::uint8_t> encoded;
    const int written = stbi_write_png_to_func(
        &AppendPng, &encoded, image.width, image.height, 4, image.rgba.data(),
        image.width * 4);
    if (written == 0 || encoded.empty()) {
        return Fail(error, "PNG 编码失败");
    }
    *out = std::move(encoded);
    return true;
}

bool WritePng(const std::filesystem::path& path, const ThumbnailImage& image,
              std::string* error) {
    std::vector<std::uint8_t> bytes;
    if (!EncodePng(image, &bytes, error)) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return Fail(error, "无法写入缩略图: " + path.string());
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return Fail(error, "写入缩略图失败: " + path.string());
    }
    return true;
}

}  // namespace zl::save
