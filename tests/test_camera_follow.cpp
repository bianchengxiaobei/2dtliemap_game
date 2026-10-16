#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "game/CameraFollow.h"
#include "render/Camera.h"
#include "render/IsoMath.h"

using namespace zl;

TEST_CASE("CameraFollowStep：指数趋近（收敛/单调/帧率无关/退化输入）") {
    const glm::vec2 target(100.0f, 50.0f);
    glm::vec2 center(0.0f, 0.0f);
    float prevDist = glm::length(target - center);
    for (int i = 0; i < 120; ++i) {  // 2s @60Hz，rate=8
        center = game::CameraFollowStep(center, target, 8.0f, 1.0f / 60.0f);
        const glm::vec2 delta = target - center;
        const float dist = glm::length(delta);
        CHECK(dist <= prevDist + 1e-4f);  // 单调收敛（不回头）
        prevDist = dist;
    }
    CHECK(glm::length(target - center) < 1.0f);  // 2s 后 ≈ 原距离 × e^-16

    // 帧率无关：连续两步 dt == 一步 2dt（指数可乘性，严格相等）
    const glm::vec2 twoSteps =
        game::CameraFollowStep(game::CameraFollowStep({0.0f, 0.0f}, target, 8.0f, 1.0f / 60.0f),
                               target, 8.0f, 1.0f / 60.0f);
    const glm::vec2 oneStep = game::CameraFollowStep({0.0f, 0.0f}, target, 8.0f, 2.0f / 60.0f);
    CHECK(twoSteps.x == doctest::Approx(oneStep.x).epsilon(1e-5));
    CHECK(twoSteps.y == doctest::Approx(oneStep.y).epsilon(1e-5));

    // 退化：rate<=0 瞬间到位；dt<=0 不动
    CHECK(game::CameraFollowStep({1.0f, 2.0f}, target, 0.0f, 1.0f) == target);
    const glm::vec2 still = game::CameraFollowStep({3.0f, 4.0f}, target, 8.0f, 0.0f);
    CHECK(still.x == 3.0f);
    CHECK(still.y == 4.0f);
}

TEST_CASE("MapPixelBounds：菱形投影 AABB == 四角 ToScreen 的包围盒") {
    const float gx0 = 0.0f;
    const float gy0 = 0.0f;
    const float gx1 = 120.0f;
    const float gy1 = 90.0f;
    glm::vec2 minPx;
    glm::vec2 maxPx;
    game::MapPixelBounds(gx0, gy0, gx1, gy1, &minPx, &maxPx);

    const glm::vec2 corners[4] = {render::ToScreen(gx0, gy0), render::ToScreen(gx1, gy0),
                                  render::ToScreen(gx0, gy1), render::ToScreen(gx1, gy1)};
    float minX = corners[0].x, maxX = corners[0].x, minY = corners[0].y, maxY = corners[0].y;
    for (const glm::vec2& c : corners) {
        CHECK(c.x >= minPx.x - 0.001f);
        CHECK(c.x <= maxPx.x + 0.001f);
        CHECK(c.y >= minPx.y - 0.001f);
        CHECK(c.y <= maxPx.y + 0.001f);
        minX = std::min(minX, c.x);
        maxX = std::max(maxX, c.x);
        minY = std::min(minY, c.y);
        maxY = std::max(maxY, c.y);
    }
    CHECK(minPx.x == doctest::Approx(minX));  // 且是紧包围盒
    CHECK(maxPx.x == doctest::Approx(maxX));
    CHECK(minPx.y == doctest::Approx(minY));
    CHECK(maxPx.y == doctest::Approx(maxY));

    // 具体值（village 120×120：minX=(0-120)*32=-3840，maxX=120*32，minY=0，maxY=240*16）
    glm::vec2 m0;
    glm::vec2 m1;
    game::MapPixelBounds(0, 0, 120, 120, &m0, &m1);
    CHECK(m0.x == doctest::Approx(-3840.0f));
    CHECK(m1.x == doctest::Approx(3840.0f));
    CHECK(m0.y == doctest::Approx(0.0f));
    CHECK(m1.y == doctest::Approx(3840.0f));
}

TEST_CASE("Camera::SetBounds：中心夹取（含缩放换算与解除边界）") {
    render::Camera camera;
    camera.SetViewport(1280, 720);
    camera.SetBounds({-3840.0f, 0.0f}, {3840.0f, 3840.0f});

    // 1.0x：half = (640, 360)
    camera.SetCenter({-10000.0f, -10000.0f});
    CHECK(camera.Center().x == doctest::Approx(-3840.0f + 640.0f));
    CHECK(camera.Center().y == doctest::Approx(0.0f + 360.0f));
    camera.SetCenter({10000.0f, 10000.0f});
    CHECK(camera.Center().x == doctest::Approx(3840.0f - 640.0f));
    CHECK(camera.Center().y == doctest::Approx(3840.0f - 360.0f));

    // 放大到 1.5x：可视半宽 1280/2/1.5 ≈ 426.7 → 夹取范围变宽
    camera.ZoomStep(1);
    CHECK(camera.Zoom() == doctest::Approx(1.5f));
    camera.SetCenter({-10000.0f, 10000.0f});
    CHECK(camera.Center().x == doctest::Approx(-3840.0f + 1280.0f / 2.0f / 1.5f));
    CHECK(camera.Center().y == doctest::Approx(3840.0f - 720.0f / 2.0f / 1.5f));

    // 解除边界后可自由离开
    camera.ClearBounds();
    camera.SetCenter({-10000.0f, -10000.0f});
    CHECK(camera.Center().x == doctest::Approx(-10000.0f));
    CHECK(camera.Center().y == doctest::Approx(-10000.0f));
}

TEST_CASE("相机跟随集成：目标越界 → 相机停在地图边缘（玩家出图）") {
    render::Camera camera;
    camera.SetViewport(1280, 720);
    glm::vec2 minPx;
    glm::vec2 maxPx;
    game::MapPixelBounds(0, 0, 120, 120, &minPx, &maxPx);  // village 边界
    camera.SetBounds(minPx, maxPx);

    // 目标远在地图外（西北）：屏幕 (-9600, -1600)，x/y 都越界
    const glm::vec2 target = render::ToScreen(-200.0f, 100.0f, 0);
    for (int i = 0; i < 120; ++i) {  // 2s：指数收敛 + 每步夹取
        camera.SetCenter(game::CameraFollowStep(camera.Center(), target, 8.0f, 1.0f / 60.0f));
    }
    // 收敛到夹取点：(-3840+640, 0+360)
    CHECK(camera.Center().x == doctest::Approx(-3200.0f).epsilon(0.01));
    CHECK(camera.Center().y == doctest::Approx(360.0f).epsilon(0.01));
}
