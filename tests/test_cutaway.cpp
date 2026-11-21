#include <doctest/doctest.h>

#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "game/AssetManifest.h"
#include "game/RenderCollectSystem.h"
#include "render/BuildingOcclusion.h"
#include "render/CutawayLogic.h"
#include "render/DrawList.h"
#include "render/FoliageFlatten.h"
#include "render/RoofCull.h"
#include "render/WindowPeek.h"

using zl::render::CutawayProbe;
using zl::render::CutawayRules;
using zl::render::CutawaySquareKey;
using zl::render::CutawaySquareMap;
using zl::render::CutawaySquareState;
using zl::render::CutawayStyle;
using zl::render::EvaluateWallCut;
using zl::render::IsCutawaySquareTarget;
using zl::render::PruneCutawaySquares;
using zl::render::SquareEnabled;
using zl::render::UpdateCutawaySquareState;
using zl::render::WallCutFlags;
using zl::render::WallSquareCut;

namespace {

CutawayProbe MakeProbe(int x, int y, int roomId, bool hasWall, float dist, bool los) {
    CutawayProbe probe;
    probe.key = {x, y, 0};
    probe.roomId = roomId;
    probe.hasWall = hasWall;
    probe.distToPlayer = dist;
    probe.lineOfSight = los;
    return probe;
}

}  // namespace

TEST_CASE("M36-T1 每格锁：目标变化才翻转，锁内保持旧值") {
    CutawaySquareState state;
    constexpr double kLock = 0.75;

    CHECK(state.enabled == false);
    CHECK(UpdateCutawaySquareState(state, true, 0.0, kLock));
    CHECK(state.enabled == true);
    CHECK(state.target == true);
    CHECK(state.hasTarget);

    // 锁内目标翻转不生效，也不延长锁。
    CHECK_FALSE(UpdateCutawaySquareState(state, false, 0.2, kLock));
    CHECK(state.enabled == true);
    CHECK(state.target == false);
    CHECK(state.lockUntil == doctest::Approx(kLock));

    // 同目标重复调用不重置锁。
    CHECK_FALSE(UpdateCutawaySquareState(state, true, 0.5, kLock));
    CHECK(state.lockUntil == doctest::Approx(kLock));

    // 锁过期后按最新目标翻转。
    CHECK(UpdateCutawaySquareState(state, false, 0.8, kLock));
    CHECK(state.enabled == false);
    CHECK(state.lockUntil == doctest::Approx(kLock + 0.8));

    // 新一轮锁内再翻回不生效。
    CHECK_FALSE(UpdateCutawaySquareState(state, true, 0.9, kLock));
    CHECK(state.enabled == false);

    // 锁过期后恢复。
    CHECK(UpdateCutawaySquareState(state, true, kLock + 0.8, kLock));
    CHECK(state.enabled == true);
}

TEST_CASE("M36-T1 锁时长 0 时目标变化立即生效") {
    CutawaySquareState state;
    CHECK(UpdateCutawaySquareState(state, true, 1.0, 0.0));
    CHECK(state.enabled == true);
    CHECK(UpdateCutawaySquareState(state, false, 1.0, 0.0));
    CHECK(state.enabled == false);
}

TEST_CASE("M36-T1 资格：房间内直接剖切；边界墙需邻接房间/距离/视线") {
    const std::vector<int> rooms{3};
    CutawayRules rules;

    CHECK(IsCutawaySquareTarget(MakeProbe(10, 10, 3, false, 2.0f, false), rooms, rules));

    CutawayProbe wall = MakeProbe(9, 10, -1, true, 2.0f, true);
    CHECK_FALSE(IsCutawaySquareTarget(wall, rooms, rules));  // 未接触房间
    wall.neighborRooms[4] = 3;  // S 邻格属于剖切房间
    CHECK(IsCutawaySquareTarget(wall, rooms, rules));

    CutawayProbe far = MakeProbe(9, 10, -1, true, 7.0f, true);
    far.neighborRooms[4] = 3;
    CHECK_FALSE(IsCutawaySquareTarget(far, rooms, rules));  // 超出 6 格

    CutawayProbe blocked = MakeProbe(9, 10, -1, true, 2.0f, false);
    blocked.neighborRooms[0] = 3;
    CHECK_FALSE(IsCutawaySquareTarget(blocked, rooms, rules));  // 无视线

    CutawayProbe floor = MakeProbe(9, 10, -1, false, 2.0f, true);
    floor.neighborRooms[4] = 3;
    CHECK_FALSE(IsCutawaySquareTarget(floor, rooms, rules));  // 非墙不因邻接参与

    CHECK_FALSE(IsCutawaySquareTarget(MakeProbe(10, 10, 3, false, 0.0f, true), {}, rules));

    CutawayRules unlimited;
    unlimited.maxWallDistance = 0.0f;
    CutawayProbe farUnlimited = MakeProbe(9, 10, -1, true, 99.0f, true);
    farUnlimited.neighborRooms[4] = 3;
    CHECK(IsCutawaySquareTarget(farUnlimited, rooms, unlimited));
}

TEST_CASE("M36-T1 墙裁切由自身与 S/E 邻格剖切标志决定") {
    CutawaySquareMap squares;
    squares[{10, 10, 0}].enabled = true;
    CHECK(SquareEnabled(squares, 10, 10, 0));
    CHECK(WallSquareCut(squares, 10, 10, 0));
    CHECK(WallSquareCut(squares, 10, 9, 0));   // S 邻格（10,10）
    CHECK(WallSquareCut(squares, 9, 10, 0));   // E 邻格（10,10）
    CHECK_FALSE(WallSquareCut(squares, 12, 12, 0));
    CHECK_FALSE(WallSquareCut(squares, 10, 10, 1));  // 不同 z

    squares.clear();
    squares[{11, 10, 0}].enabled = true;
    CHECK(WallSquareCut(squares, 10, 10, 0));  // E 邻格
    CHECK_FALSE(WallSquareCut(squares, 9, 10, 0));

    squares.clear();
    squares[{10, 11, 0}].enabled = true;
    CHECK(WallSquareCut(squares, 10, 10, 0));  // S 邻格
    CHECK_FALSE(WallSquareCut(squares, 10, 9, 0));
}

TEST_CASE("M36-T1 清理只移除已关闭且锁过期的格") {
    CutawaySquareMap squares;
    squares[{0, 0, 0}] = CutawaySquareState{true, true, true, 0.0};
    squares[{1, 0, 0}] = CutawaySquareState{false, false, true, 1.0};
    squares[{2, 0, 0}] = CutawaySquareState{false, false, true, 5.0};

    CHECK(PruneCutawaySquares(squares, 0.5) == 0u);
    CHECK(PruneCutawaySquares(squares, 2.0) == 1u);
    CHECK(squares.count({1, 0, 0}) == 0u);
    CHECK(squares.count({2, 0, 0}) == 1u);

    CHECK(PruneCutawaySquares(squares, 6.0) == 1u);
    CHECK(squares.size() == 1u);  // 仍启用的格保留
    CHECK(squares.count({0, 0, 0}) == 1u);
}


namespace {

zl::render::DrawItem MakeWallItem() {
    zl::render::DrawItem item;
    item.rect = {0.0f, 0.0f, 64.0f, 112.0f};
    item.u0 = 0.0f;
    item.v0 = 0.0f;
    item.u1 = 1.0f;
    item.v1 = 1.0f;
    item.color = 0xFFFFFFFFu;
    item.stage = 1;
    item.z = 0;
    item.layer = static_cast<int>(zl::world::SquareLayer::Wall);
    item.cutawayEdges = 3u;
    return item;
}

constexpr float kInf = std::numeric_limits<float>::max();

}  // namespace

TEST_CASE("M36-T2 方向：N 面看自身+E 邻格，W 面看自身+S 邻格") {
    CutawaySquareMap squares;
    squares[{10, 10, 0}].enabled = true;

    WallCutFlags cut = EvaluateWallCut(squares, 10, 10, 0, 1u);  // 仅 N
    CHECK(cut.north);
    CHECK_FALSE(cut.west);

    cut = EvaluateWallCut(squares, 10, 10, 0, 2u);  // 仅 W
    CHECK_FALSE(cut.north);
    CHECK(cut.west);

    cut = EvaluateWallCut(squares, 10, 10, 0, 3u);
    CHECK(cut.north);
    CHECK(cut.west);

    // 自身未剖切，只有 E 邻格：N 面仍剖（对齐 PZ cutE）。
    squares.clear();
    squares[{11, 10, 0}].enabled = true;
    cut = EvaluateWallCut(squares, 10, 10, 0, 3u);
    CHECK(cut.north);
    CHECK_FALSE(cut.west);

    // 自身未剖切，只有 S 邻格：W 面仍剖（对齐 PZ cutS）。
    squares.clear();
    squares[{10, 11, 0}].enabled = true;
    cut = EvaluateWallCut(squares, 10, 10, 0, 3u);
    CHECK_FALSE(cut.north);
    CHECK(cut.west);

    // 未声明边 = 不可剖，邻格剖切也不参与。
    CHECK_FALSE(EvaluateWallCut(squares, 10, 10, 0, 0u).Any());
    CHECK_FALSE(EvaluateWallCut(squares, 10, 10, 0, 1u).west);
    CHECK_FALSE(EvaluateWallCut(squares, 10, 10, 0, 2u).north);
}

TEST_CASE("M36-T2 裁切：N 压底、W 按列裁、NW 叠加、墙顶对齐") {
    WallCutFlags n;
    n.north = true;
    WallCutFlags w;
    w.west = true;
    WallCutFlags nw = n;
    nw.west = true;

    // N：只压底，顶部不动。
    zl::render::DrawItem item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, n, 0.5f, kInf, static_cast<std::uint8_t>(CutawayStyle::Full)));
    CHECK(item.rect.y == doctest::Approx(0.0f));
    CHECK(item.rect.h == doctest::Approx(56.0f));
    CHECK(item.v1 == doctest::Approx(0.5f));
    CHECK(item.cutawayTrimmed);

    // W + left：保留左侧，宽减半。
    item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, w, 0.5f, kInf, static_cast<std::uint8_t>(CutawayStyle::Left)));
    CHECK(item.rect.x == doctest::Approx(0.0f));
    CHECK(item.rect.w == doctest::Approx(32.0f));
    CHECK(item.u1 == doctest::Approx(0.5f));
    CHECK(item.rect.h == doctest::Approx(112.0f));

    // W + right：保留右侧，矩形右移并同步 UV。
    item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, w, 0.5f, kInf, static_cast<std::uint8_t>(CutawayStyle::Right)));
    CHECK(item.rect.x == doctest::Approx(32.0f));
    CHECK(item.rect.w == doctest::Approx(32.0f));
    CHECK(item.u0 == doctest::Approx(0.5f));
    CHECK(item.u1 == doctest::Approx(1.0f));

    // W + full：整块墙仍按高度压底。
    item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, w, 0.5f, kInf, static_cast<std::uint8_t>(CutawayStyle::Full)));
    CHECK(item.rect.h == doctest::Approx(56.0f));
    CHECK(item.rect.w == doctest::Approx(64.0f));

    // NW + left：两轴叠加。
    item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, nw, 0.5f, kInf, static_cast<std::uint8_t>(CutawayStyle::Left)));
    CHECK(item.rect.h == doctest::Approx(56.0f));
    CHECK(item.rect.w == doctest::Approx(32.0f));

    // 玩家头顶上限优先于 keepRatio。
    item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, n, 0.5f, 40.0f, static_cast<std::uint8_t>(CutawayStyle::Full)));
    CHECK(item.rect.h == doctest::Approx(40.0f));
    CHECK(item.v1 == doctest::Approx(40.0f / 112.0f));

    // 比例合法但压到 1px 内：整块隐藏。
    item = MakeWallItem();
    CHECK(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, n, 0.5f, 0.5f, static_cast<std::uint8_t>(CutawayStyle::Full)));
    CHECK((item.color >> 24) == 0u);
    CHECK(item.cutawayTrimmed);
}

TEST_CASE("M36-T2 无标记墙/无方向不裁切") {
    zl::render::DrawItem item = MakeWallItem();
    item.cutawayEdges = 0u;
    const CutawaySquareMap empty;
    CHECK_FALSE(EvaluateWallCut(empty, 0, 0, 0, item.cutawayEdges).Any());
    CHECK_FALSE(zl::game::RenderCollectSystem::TrimWallCutaway(
        item, WallCutFlags{}, 0.45f, kInf, 0));
    CHECK(item.rect.h == doctest::Approx(112.0f));
    CHECK_FALSE(item.cutawayTrimmed);
}

TEST_CASE("M36-T2 manifest：cutaway 元数据校验与内置资源标记") {
    zl::game::AssetCutawayMeta meta;
    std::string error;

    const nlohmann::json valid = {{"edges", {"n", "w"}}, {"style", "full"}};
    REQUIRE_MESSAGE(zl::game::ParseCutawayMeta(valid, &meta, &error), error);
    CHECK(meta.edges == 3u);
    CHECK(meta.style == zl::game::AssetCutawayMeta::Style::Full);

    const nlohmann::json left = {{"edges", {"w"}}, {"style", "left"}};
    REQUIRE_MESSAGE(zl::game::ParseCutawayMeta(left, &meta, &error), error);
    CHECK(meta.edges == 2u);
    CHECK(meta.style == zl::game::AssetCutawayMeta::Style::Left);

    for (const nlohmann::json bad : {nlohmann::json{{"edges", {"e"}}},
                                     nlohmann::json{{"edges", {}}},
                                     nlohmann::json{{"edges", {"n"}}, {"style", "diagonal"}},
                                     nlohmann::json{{"edges", {"n"}}, {"unknown", 1}}}) {
        meta.edges = 9u;
        CHECK_FALSE(zl::game::ParseCutawayMeta(bad, &meta, &error));
        CHECK(meta.edges == 0u);
    }

    // 内置清单：砖墙可剖，门/木结构不可剖。
    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/asset_manifest.json";
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), path);
    nlohmann::json doc;
    file >> doc;
    const auto& logical = doc.at("logical");
    REQUIRE(logical.at("wall.brick").contains("cutaway"));
    REQUIRE_MESSAGE(zl::game::ParseCutawayMeta(logical.at("wall.brick").at("cutaway"), &meta,
                                               &error),
                    error);
    CHECK(meta.edges == 3u);
    CHECK_FALSE(logical.at("wall.door").contains("cutaway"));
    CHECK_FALSE(logical.at("object.wall_h").contains("cutaway"));
}

namespace {

zl::render::BuildingInfo MakeBuilding(int id, int minX, int minY, int maxX, int maxY,
                                       std::vector<int> rooms = {}) {
    zl::render::BuildingInfo b;
    b.id = id;
    b.minX = minX;
    b.minY = minY;
    b.maxX = maxX;
    b.maxY = maxY;
    b.z = 0;
    b.roomIds = rooms.empty() ? std::vector<int>{id} : std::move(rooms);
    return b;
}

const zl::render::SpriteRect kBigPlayerRect{-2000.0f, -2000.0f, 4000.0f, 4000.0f};

}  // namespace

TEST_CASE("M36-T3 房间聚合成建筑：相交/相邻合并，远处拆分") {
    std::vector<zl::render::RoomBounds> rooms;
    rooms.push_back({1, 0, 0, 2, 2, 0});
    rooms.push_back({2, 3, 0, 4, 2, 0});  // 与 1 相邻 1 格
    rooms.push_back({3, 20, 20, 21, 21, 0});
    rooms.push_back({4, 22, 21, 23, 23, 0});  // 与 3 相邻

    const auto buildings = zl::render::GroupRoomsIntoBuildings(rooms);
    REQUIRE(buildings.size() == 2u);
    CHECK(buildings[0].id == 1);
    CHECK(buildings[0].minX == 0);
    CHECK(buildings[0].maxX == 4);
    CHECK(buildings[0].roomIds.size() == 2u);
    CHECK(buildings[1].id == 3);
    CHECK(buildings[1].minX == 20);
    CHECK(buildings[1].maxX == 23);

    const auto rect = zl::render::BuildingScreenRect(buildings[0]);
    CHECK(rect.w > 0.0f);
    CHECK(rect.h > 96.0f);  // 含墙/屋顶高度余量
}

TEST_CASE("M36-T3 建筑筛选：排除自身/脚下，按间隙排序并截断") {
    std::vector<zl::render::BuildingInfo> buildings;
    buildings.push_back(MakeBuilding(1, 6, 6, 7, 8));    // 间隙 2
    buildings.push_back(MakeBuilding(2, 9, 9, 10, 10));  // 间隙 8
    buildings.push_back(MakeBuilding(3, 12, 6, 13, 7));  // 间隙 8，id 更大
    buildings.push_back(MakeBuilding(4, 4, 4, 6, 6));    // 包含玩家 (5,5)
    buildings.push_back(MakeBuilding(5, 6, 4, 8, 6, {7}));  // 玩家房间 7

    std::vector<int> chosen;
    const std::size_t count = zl::render::SelectOccluderBuildings(
        kBigPlayerRect, 5, 5, 7, buildings, 2, &chosen);
    REQUIRE(count == 2u);
    REQUIRE(chosen.size() == 2u);
    CHECK(chosen[0] == 1);
    CHECK(chosen[1] == 2);
    CHECK(std::find(chosen.begin(), chosen.end(), 4) == chosen.end());
    CHECK(std::find(chosen.begin(), chosen.end(), 5) == chosen.end());

    // 玩家西北侧（建筑不延伸到东/南）不作为遮挡。
    std::vector<zl::render::BuildingInfo> behind;
    behind.push_back(MakeBuilding(9, 0, 0, 2, 2));
    CHECK(zl::render::SelectOccluderBuildings(kBigPlayerRect, 5, 5, -1, behind, 8,
                                              &chosen) == 0u);

    // maxCount=0 关闭选择。
    CHECK(zl::render::SelectOccluderBuildings(kBigPlayerRect, 5, 5, -1, buildings, 0,
                                              &chosen) == 0u);
}

TEST_CASE("M36-T3 方格裁切：建筑外扩 1 格、距离 6、玩家不在建筑内") {
    const auto b = MakeBuilding(1, 10, 10, 14, 14);

    CHECK(zl::render::ShouldCutBuildingSquare(b, 10, 10, 8.0f, 8.0f, 6.0f));
    CHECK(zl::render::ShouldCutBuildingSquare(b, 9, 9, 8.0f, 8.0f, 6.0f));  // 外扩 1
    CHECK_FALSE(zl::render::ShouldCutBuildingSquare(b, 8, 8, 8.0f, 8.0f, 6.0f));
    CHECK_FALSE(zl::render::ShouldCutBuildingSquare(b, 14, 14, 8.0f, 8.0f, 6.0f));

    // 玩家在建筑内：不剖切。
    CHECK_FALSE(zl::render::ShouldCutBuildingSquare(b, 10, 10, 12.0f, 12.0f, 6.0f));

    // 距离可关闭（0 = 不限制），但建筑范围仍生效。
    CHECK(zl::render::ShouldCutBuildingSquare(b, 14, 14, 8.0f, 8.0f, 0.0f));
}

TEST_CASE("M36-T4 窗口扒看：距离/朝向/窗后房间/回退") {
    auto roomAt = [](int x, int y) {
        if (x == 10 && y == 9) {
            return 3;
        }
        if (x == 11 && y == 10) {
            return 4;
        }
        return -1;
    };

    // 玩家贴窗格南侧（碰撞外 0.35 格）、朝北。
    CHECK(zl::render::WindowPeekRoomId(10.5f, 11.35f, 0.0f, -1.0f, 10, 10, roomAt,
                                       0.9f) == 3);
    // 背对窗户。
    CHECK(zl::render::WindowPeekRoomId(10.5f, 11.35f, 0.0f, 1.0f, 10, 10, roomAt,
                                       0.9f) == -1);
    // 超出距离（贴窗 1.2 格）。
    CHECK(zl::render::WindowPeekRoomId(10.5f, 12.2f, 0.0f, -1.0f, 10, 10, roomAt,
                                       0.9f) == -1);
    // 朝东贴窗。
    CHECK(zl::render::WindowPeekRoomId(9.6f, 10.5f, 1.0f, 0.0f, 10, 10, roomAt,
                                       0.9f) == 4);
    // 窗后无房间时回退查窗格自身。
    auto roomAtWindow = [](int x, int y) {
        return (x == 10 && y == 10) ? 5 : -1;
    };
    CHECK(zl::render::WindowPeekRoomId(10.5f, 11.35f, 0.0f, -1.0f, 10, 10,
                                       roomAtWindow, 0.9f) == 5);
    // 距离 0 = 不限制。
    CHECK(zl::render::WindowPeekRoomId(10.5f, 20.0f, 0.0f, -1.0f, 10, 10, roomAt,
                                       0.0f) == 3);
    // 无朝向。
    CHECK(zl::render::WindowPeekRoomId(10.5f, 10.85f, 0.0f, 0.0f, 10, 10, roomAt,
                                       0.9f) == -1);
}

TEST_CASE("M36-T4 manifest：窗格元数据与窗户帧") {
    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/asset_manifest.json";
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), path);
    nlohmann::json doc;
    file >> doc;
    const auto& logical = doc.at("logical");
    REQUIRE(logical.contains("wall.window_h"));
    REQUIRE(logical.contains("wall.window_v"));
    CHECK(logical.at("wall.window_h").at("window").get<bool>());
    CHECK(logical.at("wall.window_v").at("window").get<bool>());
    CHECK_FALSE(logical.at("wall.brick").contains("window"));
    CHECK(logical.at("wall.window_h").at("own").at("frames").front() ==
          "wall_window_h_1");
    CHECK(logical.at("wall.window_v").at("own").at("frames").front() ==
          "wall_window_v_1");
}

namespace {

zl::render::DrawItem UpperItem(int gx, int gy, int roomId) {
    zl::render::DrawItem item;
    item.z = 1;
    item.gx = gx;
    item.diag = gx + gy;
    item.isRoof = true;
    item.roofOwnerRoom = roomId;
    item.roomId = roomId;
    return item;
}

zl::render::BuildingInfo TestBuilding(int id, int minX, int maxX, std::vector<int> rooms) {
    zl::render::BuildingInfo building;
    building.id = id;
    building.minX = minX;
    building.maxX = maxX;
    building.minY = 0;
    building.maxY = 4;
    building.roomIds = std::move(rooms);
    return building;
}

}  // namespace

TEST_CASE("M36-T5 屋顶归属：只隐藏自身/被遮挡建筑，相邻建筑保留") {
    const std::vector<zl::render::BuildingInfo> buildings = {
        TestBuilding(10, 0, 4, {1}), TestBuilding(20, 10, 14, {2})};
    zl::render::CutawaySquareMap squares;
    zl::render::CutawaySquareState state;
    state.enabled = true;
    squares[{0, 0, 0}] = state;

    const zl::render::DrawItem roofA = UpperItem(1, 1, 1);
    const zl::render::DrawItem roofB = UpperItem(11, 1, 2);

    // 本房间在剖切集合。
    CHECK(zl::render::ShouldHideUpper(roofA, {1}, buildings, {}, -1, squares, true));
    // 玩家所在建筑：即使房间不在集合也隐藏。
    CHECK(zl::render::ShouldHideUpper(roofA, {}, buildings, {}, 10, squares, true));
    // 相邻建筑屋顶保留。
    CHECK_FALSE(zl::render::ShouldHideUpper(roofB, {1}, buildings, {}, -1, squares, true));
    // 被 T3 选中的建筑隐藏屋顶。
    CHECK(zl::render::ShouldHideUpper(roofB, {1}, buildings, {20}, -1, squares, true));
    // z=0 / 非屋顶对象不处理。
    zl::render::DrawItem ground = roofA;
    ground.z = 0;
    CHECK_FALSE(zl::render::ShouldHideUpper(ground, {1}, buildings, {}, -1, squares, true));
}

TEST_CASE("M36-T5 空悬上层：仅正下方 1 格内有剖切格时隐藏") {
    const std::vector<zl::render::BuildingInfo> buildings = {
        TestBuilding(10, 0, 4, {1})};
    zl::render::CutawaySquareMap squares;
    zl::render::CutawaySquareState state;
    state.enabled = true;
    squares[{4, 4, 0}] = state;

    const zl::render::DrawItem floating = UpperItem(3, 3, -1);
    CHECK(zl::render::ShouldHideUpper(floating, {}, buildings, {}, -1, squares, true));
    CHECK(zl::render::ShouldHideUpper(UpperItem(5, 4, -1), {}, buildings, {}, -1, squares,
                                      true));
    // 远处空悬上层（露台/独栋）保留。
    CHECK_FALSE(zl::render::ShouldHideUpper(UpperItem(20, 20, -1), {}, buildings, {}, -1,
                                            squares, true));
    // 有归属但不在任何集合/建筑内：保留。
    CHECK_FALSE(zl::render::ShouldHideUpper(UpperItem(4, 4, 2), {}, buildings, {}, -1,
                                            squares, true));
}

TEST_CASE("M36-T5 开关回退：roofCull=false 回到按格剖切") {
    const std::vector<zl::render::BuildingInfo> buildings = {
        TestBuilding(10, 0, 4, {1})};
    zl::render::CutawaySquareMap squares;
    zl::render::CutawaySquareState state;
    state.enabled = true;
    squares[{1, 1, 0}] = state;

    const zl::render::DrawItem roof = UpperItem(1, 1, 1);
    CHECK(zl::render::ShouldHideUpper(roof, {}, buildings, {}, -1, squares, false));
    CHECK_FALSE(zl::render::ShouldHideUpper(roof, {1}, buildings, {}, -1,
                                            zl::render::CutawaySquareMap{}, false));
}

TEST_CASE("M36-T6 只影响玩家之后且相交的 foliage") {
    std::vector<zl::render::DrawItem> items(4);
    items[0].foliage = true;
    items[0].rect = {100.0f, 100.0f, 64.0f, 100.0f};
    items[0].color = 0xFF00FF00u;
    items[0].stage = 1;
    items[1] = items[0];  // 玩家
    items[1].foliage = false;
    items[2] = items[0];  // 玩家之后的树
    items[3] = items[0];  // 不相交的树
    items[3].rect = {400.0f, 400.0f, 64.0f, 100.0f};

    const zl::render::SpriteRect playerRect = {110.0f, 120.0f, 32.0f, 64.0f};
    const std::size_t changed =
        zl::render::FlattenOccludingFoliage(playerRect, 1, 0.4f, 0.5f, items);
    CHECK(changed == 1);
    CHECK(items[0].rect.h == doctest::Approx(100.0f));  // 玩家之前不处理
    CHECK(items[0].color == 0xFF00FF00u);
    CHECK(items[1].color == 0xFF00FF00u);  // 玩家自身不处理
    CHECK(items[2].rect.h == doctest::Approx(50.0f));
    CHECK(items[2].rect.y == doctest::Approx(150.0f));  // 底边不动（200）
    CHECK((items[2].color >> 24) == 102u);
    CHECK(items[3].rect.h == doctest::Approx(100.0f));  // 不相交
}

TEST_CASE("M36-T6 alpha 不抬高已有透明度，开关回退为 no-op") {
    std::vector<zl::render::DrawItem> items(3);
    items[0].rect = {100.0f, 100.0f, 32.0f, 64.0f};
    items[1].foliage = true;
    items[1].rect = {110.0f, 120.0f, 64.0f, 100.0f};
    items[1].stage = 1;
    items[1].color = 0x3300FF00u;  // 已比目标更透明
    items[2] = items[1];
    items[2].stage = 0;  // 地面批不处理

    CHECK(zl::render::FlattenOccludingFoliage(items[0].rect, 0, 0.4f, 0.5f, items) == 1);
    CHECK((items[1].color >> 24) == 0x33u);
    CHECK(items[1].rect.h == doctest::Approx(50.0f));
    CHECK(items[2].rect.h == doctest::Approx(100.0f));

    // alpha=1、keepRatio=1 相当于关闭：不改颜色/高度。
    std::vector<zl::render::DrawItem> untouched(2);
    untouched[0].rect = {100.0f, 100.0f, 32.0f, 64.0f};
    untouched[1] = items[1];
    untouched[1].rect = {110.0f, 120.0f, 64.0f, 100.0f};
    untouched[1].color = 0xFF00FF00u;
    CHECK(zl::render::FlattenOccludingFoliage(untouched[0].rect, 0, 1.0f, 1.0f,
                                              untouched) == 0);
    CHECK(untouched[1].rect.h == doctest::Approx(100.0f));
    CHECK(untouched[1].color == 0xFF00FF00u);
}

TEST_CASE("M36-T6 manifest：树/高草 foliage 标记") {
    const std::string path = std::string(ZL_SOURCE_DIR) + "/assets/asset_manifest.json";
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.is_open(), path);
    nlohmann::json doc;
    file >> doc;
    const auto& logical = doc.at("logical");
    REQUIRE(logical.contains("object.tree"));
    REQUIRE(logical.contains("decor.grass_tuft"));
    CHECK(logical.at("object.tree").at("foliage").get<bool>());
    CHECK(logical.at("decor.grass_tuft").at("foliage").get<bool>());
    CHECK_FALSE(logical.at("wall.brick").contains("foliage"));
}

