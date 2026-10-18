#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zl::render {

struct ScreenshotImage {
    std::string filePath;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    bool Valid() const {
        if (width == 0 || height == 0) {
            return false;
        }
        const std::uint64_t expected =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;
        return rgba.size() == expected;
    }
};

bool ConvertBgraToRgba(const void* data, std::uint32_t width, std::uint32_t height,
                       std::uint32_t pitch, std::uint32_t size, bool yflip,
                       ScreenshotImage* out);

}  // namespace zl::render
