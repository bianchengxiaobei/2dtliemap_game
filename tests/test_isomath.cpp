#include <doctest/doctest.h>

#include <cmath>

#include "render/IsoMath.h"

using namespace zl;

namespace {

constexpr float kEps = 1e-3f;

}  // namespace

TEST_CASE("ToScreen 基本公式（M2-T4）") {
    const glm::vec2 o = render::ToScreen(0, 0);
    CHECK(o.x == doctest::Approx(0.0f));
    CHECK(o.y == doctest::Approx(0.0f));

    CHECK(render::ToScreen(1, 0).x == doctest::Approx(32.0f));
    CHECK(render::ToScreen(1, 0).y == doctest::Approx(16.0f));

    CHECK(render::ToScreen(0, 1).x == doctest::Approx(-32.0f));
    CHECK(render::ToScreen(0, 1).y == doctest::Approx(16.0f));

    CHECK(render::ToScreen(2, 1).x == doctest::Approx(32.0f));
    CHECK(render::ToScreen(2, 1).y == doctest::Approx(48.0f));

    // z 层垂直偏移：96 像素/层
    CHECK(render::ToScreen(0, 0, 1).y == doctest::Approx(-96.0f));
    CHECK(render::ToScreen(0, 0, 3).y == doctest::Approx(-288.0f));
}

TEST_CASE("ToScreen/FromScreen 往返一致性") {
    for (int z = 0; z <= 3; ++z) {
        for (float gx = -10.0f; gx <= 10.0f; gx += 0.37f) {
            for (float gy = -10.0f; gy <= 10.0f; gy += 0.53f) {
                const glm::vec2 screen = render::ToScreen(gx, gy, z);
                const glm::vec2 back = render::FromScreen(screen, z);
                CHECK(back.x == doctest::Approx(gx).epsilon(kEps));
                CHECK(back.y == doctest::Approx(gy).epsilon(kEps));
            }
        }
    }
}

TEST_CASE("PickCell：格中心命中自身") {
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            const glm::vec2 c = render::ToScreen(static_cast<float>(x), static_cast<float>(y));
            const render::GridPick p = render::PickCell(c, 0, 32, 32);
            CHECK(p.hit);
            CHECK(p.x == x);
            CHECK(p.y == y);
        }
    }
}

TEST_CASE("PickCell：缩放/平移等仿射组合不影响反解（以像素为单位输入）") {
    // 模拟：世界像素 -> 屏幕像素 = (world - camera) * zoom + viewport/2
    const glm::vec2 camera{123.0f, -45.0f};
    const float zoom = 1.7f;
    const glm::vec2 viewport{1280.0f, 720.0f};
    // 取格内偏一点而不是正中心（正中心是 floor() 边界，浮点误差会翻转归属）
    const glm::vec2 insideOffset{0.13f, 0.27f};
    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 16; ++y) {
            const glm::vec2 world =
                render::ToScreen(static_cast<float>(x) + insideOffset.x,
                                 static_cast<float>(y) + insideOffset.y);
            const glm::vec2 screen = (world - camera) * zoom + viewport * 0.5f;
            const glm::vec2 worldBack = (screen - viewport * 0.5f) / zoom + camera;
            const render::GridPick p = render::PickCell(worldBack, 0, 32, 32);
            CHECK(p.hit);
            CHECK(p.x == x);
            CHECK(p.y == y);
        }
    }
}

TEST_CASE("PickCell：边界确定性归属") {
    // 格 (2,3) 的菱形上顶点是分区边界（相邻菱形交点），确定性归属 floor 结果 (2,3)。
    const glm::vec2 top = render::ToScreen(2.0f, 3.0f) + glm::vec2(0.0f, -16.0f);
    const render::GridPick p = render::PickCell(top, 0, 32, 32);
    CHECK(p.hit);
    CHECK(p.x == 2);
    CHECK(p.y == 3);

    // 菱形内部采样（|dx|/32 + |dy|/16 < 1）仍命中自身
    const float dxs[] = {-20.0f, -10.0f, 0.0f, 10.0f, 20.0f};
    const float dys[] = {-8.0f, 0.0f, 8.0f};
    for (float dx : dxs) {
        for (float dy : dys) {
            if (std::abs(dx) / 32.0f + std::abs(dy) / 16.0f >= 1.0f) {
                continue;  // 菱形外，不属于本格
            }
            const glm::vec2 pt = render::ToScreen(4.0f, 4.0f) + glm::vec2(dx, dy);
            const render::GridPick q = render::PickCell(pt, 0, 32, 32);
            CHECK(q.x == 4);
            CHECK(q.y == 4);
        }
    }
}

TEST_CASE("PickCell：地图外未命中") {
    const render::GridPick neg = render::PickCell(render::ToScreen(-1.0f, 0.0f), 0, 32, 32);
    CHECK_FALSE(neg.hit);
    const render::GridPick over = render::PickCell(render::ToScreen(32.0f, 5.0f), 0, 32, 32);
    CHECK_FALSE(over.hit);
    // 恰好压线到右边界（x == mapWidth）视为未命中（[0, w) 半开区间）
    const render::GridPick edge = render::PickCell(render::ToScreen(32.0f, 0.0f), 0, 32, 32);
    CHECK_FALSE(edge.hit);
}

TEST_CASE("SpriteDrawRect：64×128 画布地面块中心重合格子中心") {
    // 地面帧典型参数：fx=64, fy=128, ox=0, oy=96, w=63, h=32
    for (int gx = 0; gx < 5; ++gx) {
        for (int gy = 0; gy < 5; ++gy) {
            const glm::vec2 center = render::ToScreen(static_cast<float>(gx), static_cast<float>(gy));
            const render::SpriteRect r =
                render::SpriteDrawRect(static_cast<float>(gx), static_cast<float>(gy), 0,
                                       64, 128, 0, 96, 63, 32);
            // 绘制矩形中心 ≈ 格子中心（x 差 0.5 来自 63 宽裁剪，容差放宽）
            CHECK(std::abs(r.x + r.w * 0.5f - center.x) <= 0.51f);
            CHECK((r.y + r.h * 0.5f) == doctest::Approx(center.y).epsilon(0.01));
            // 相邻格子的绘制偏移严格是 (32, 16)
            const render::SpriteRect r2 =
                render::SpriteDrawRect(static_cast<float>(gx + 1), static_cast<float>(gy), 0,
                                       64, 128, 0, 96, 63, 32);
            CHECK((r2.x - r.x) == doctest::Approx(32.0f));
            CHECK((r2.y - r.y) == doctest::Approx(16.0f));
        }
    }
}

TEST_CASE("SpriteDrawRect：192×256 画布高物体锚在格中心附近") {
    // 树帧典型参数：fx=192, fy=256, ox=37, oy=3, w=118, h=251
    const glm::vec2 center = render::ToScreen(1.0f, 1.0f);
    const render::SpriteRect r =
        render::SpriteDrawRect(1.0f, 1.0f, 0, 192, 256, 37, 3, 118, 251);
    // 树底 ≈ 格中心 + 14 像素（画布底部纹理区 = 脚部接触带）
    CHECK((r.y + r.h - center.y) == doctest::Approx(14.0f));
    // 水平中心 ≈ 格中心（o=37 + w=118 在 192 画布内水平对称）
    CHECK(std::abs(r.x + r.w * 0.5f - center.x) <= 1.01f);
}

TEST_CASE("SpriteDrawRect：64×224 画布墙体块立于格上（M3 占位墙约定）") {
    // 墙帧参数（assets/own：wall_brick_1/wall_door_1）：fx=64, fy=224, ox=0, oy=112, w=64, h=112
    const glm::vec2 center = render::ToScreen(1.0f, 1.0f);
    const render::SpriteRect r = render::SpriteDrawRect(1.0f, 1.0f, 0, 64, 224, 0, 112, 64, 112);
    CHECK(r.x == doctest::Approx(center.x - 32.0f));         // 水平居中
    CHECK((r.y + r.h) == doctest::Approx(center.y + 16.0f)); // 底部落在格中心 +16 接触带
    CHECK(r.y == doctest::Approx(center.y - 96.0f));         // 顶 = 一格楼高（kFloorPx）

    // 相邻格（东邻）墙的水平错位 = (32, 16)，与地砖剖分一致
    const render::SpriteRect r2 = render::SpriteDrawRect(2.0f, 1.0f, 0, 64, 224, 0, 112, 64, 112);
    CHECK((r2.x - r.x) == doctest::Approx(32.0f));
    CHECK((r2.y - r.y) == doctest::Approx(16.0f));
}
