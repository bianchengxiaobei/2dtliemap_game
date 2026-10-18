#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "render/Screenshot.h"

using namespace zl;

TEST_CASE("M10-T4 BGRA 屏幕截图转换为 RGBA") {
    constexpr std::uint32_t kWidth = 2;
    constexpr std::uint32_t kHeight = 2;
    constexpr std::uint32_t kPitch = kWidth * 4u + 4u;
    std::vector<std::uint8_t> bgra(kPitch * kHeight, 0xEEu);
    bgra[0] = 1;
    bgra[1] = 2;
    bgra[2] = 3;
    bgra[3] = 4;
    bgra[4] = 5;
    bgra[5] = 6;
    bgra[6] = 7;
    bgra[7] = 8;
    bgra[kPitch + 0] = 9;
    bgra[kPitch + 1] = 10;
    bgra[kPitch + 2] = 11;
    bgra[kPitch + 3] = 12;
    bgra[kPitch + 4] = 13;
    bgra[kPitch + 5] = 14;
    bgra[kPitch + 6] = 15;
    bgra[kPitch + 7] = 16;

    render::ScreenshotImage image;
    REQUIRE(render::ConvertBgraToRgba(bgra.data(), kWidth, kHeight, kPitch,
                                      static_cast<std::uint32_t>(bgra.size()),
                                      false, &image));
    CHECK(image.Valid());
    CHECK(image.width == 2u);
    CHECK(image.height == 2u);
    REQUIRE(image.rgba.size() == 16u);
    CHECK(image.rgba[0] == 3);
    CHECK(image.rgba[1] == 2);
    CHECK(image.rgba[2] == 1);
    CHECK(image.rgba[3] == 4);
    CHECK(image.rgba[4] == 7);
    CHECK(image.rgba[5] == 6);
    CHECK(image.rgba[6] == 5);
    CHECK(image.rgba[7] == 8);
    CHECK(image.rgba[8] == 11);
    CHECK(image.rgba[9] == 10);
    CHECK(image.rgba[10] == 9);
    CHECK(image.rgba[11] == 12);
    CHECK(image.rgba[12] == 15);
    CHECK(image.rgba[13] == 14);
    CHECK(image.rgba[14] == 13);
    CHECK(image.rgba[15] == 16);
}

TEST_CASE("M10-T4 BGRA 转换 yflip 与非法数据") {
    constexpr std::uint32_t kPitch = 8;
    std::vector<std::uint8_t> bgra(kPitch * 2, 0);
    bgra[0] = 10;
    bgra[1] = 20;
    bgra[2] = 30;
    bgra[3] = 40;
    bgra[kPitch + 0] = 50;
    bgra[kPitch + 1] = 60;
    bgra[kPitch + 2] = 70;
    bgra[kPitch + 3] = 80;

    render::ScreenshotImage image;
    REQUIRE(render::ConvertBgraToRgba(bgra.data(), 2, 2, kPitch,
                                      static_cast<std::uint32_t>(bgra.size()), true,
                                      &image));
    CHECK(image.rgba[0] == 70);
    CHECK(image.rgba[1] == 60);
    CHECK(image.rgba[2] == 50);
    CHECK(image.rgba[3] == 80);
    CHECK(image.rgba[8] == 30);
    CHECK(image.rgba[9] == 20);
    CHECK(image.rgba[10] == 10);
    CHECK(image.rgba[11] == 40);

    CHECK_FALSE(render::ConvertBgraToRgba(nullptr, 2, 2, kPitch, 32, false, &image));
    CHECK_FALSE(render::ConvertBgraToRgba(bgra.data(), 0, 2, kPitch, 32, false, &image));
    CHECK_FALSE(render::ConvertBgraToRgba(bgra.data(), 2, 2, 4, 32, false, &image));
    CHECK_FALSE(render::ConvertBgraToRgba(bgra.data(), 2, 2, kPitch, 4, false, &image));
    CHECK_FALSE(render::ConvertBgraToRgba(bgra.data(), 2, 2, kPitch, 32, false, nullptr));
}
