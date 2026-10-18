#include "render/Screenshot.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace zl::render {

bool ConvertBgraToRgba(const void* data, std::uint32_t width,
                       std::uint32_t height, std::uint32_t pitch,
                       std::uint32_t size, bool yflip,
                       ScreenshotImage* out) {
    if (out == nullptr || data == nullptr || width == 0 || height == 0) {
        return false;
    }
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * 4u;
    const std::uint64_t required = static_cast<std::uint64_t>(pitch) * height;
    if (pitch < rowBytes || size < required) {
        return false;
    }
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;
    if (pixels > static_cast<std::uint64_t>(
                      std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    out->rgba.resize(static_cast<std::size_t>(pixels));
    const auto* source = static_cast<const std::uint8_t*>(data);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = yflip ? (height - 1u - y) : y;
        const std::uint8_t* row = source + static_cast<std::size_t>(sourceY) * pitch;
        std::uint8_t* target =
            out->rgba.data() + static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4u;
            target[0] = pixel[2];
            target[1] = pixel[1];
            target[2] = pixel[0];
            target[3] = pixel[3];
            target += 4;
        }
    }
    out->width = width;
    out->height = height;
    return true;
}

}  // namespace zl::render
