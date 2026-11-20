#include <cmath>
#include <string>
#include <vector>

#include "doctest/doctest.h"

#include "game/Throwing.h"
#include "items/ItemComponents.h"

using namespace zl;

using zl::game::AdvanceThrownProjectile;
using zl::game::ClipThrowPath;
using zl::game::ConsumeThrowableSource;
using zl::game::ForceSettleThrownProjectile;
using zl::game::ResolveThrowLanding;
using zl::game::SelectThrowTarget;
using zl::game::ThrowCellBlocked;
using zl::game::ThrowFlightSeconds;
using zl::game::ThrowLandingOutcome;
using zl::game::ThrowTargetCandidate;
using zl::game::ThrownProjectile;

TEST_CASE("M34-F2-T3 墙体夹取与飞行时间") {
    const ThrowCellBlocked noWalls = [](int, int) { return false; };
    auto path = ClipThrowPath(0.5f, 0.5f, 4.5f, 0.5f, noWalls);
    CHECK(path.valid);
    CHECK_FALSE(path.hitWall);
    CHECK(path.x == doctest::Approx(4.5f));
    CHECK(path.y == doctest::Approx(0.5f));
    CHECK(path.distance == doctest::Approx(4.0f));

    const ThrowCellBlocked wallAtTwo = [](int x, int y) {
        return x >= 2 && y == 0;
    };
    path = ClipThrowPath(0.5f, 0.5f, 4.5f, 0.5f, wallAtTwo);
    CHECK(path.valid);
    CHECK(path.hitWall);
    CHECK(path.x < 2.0f);
    CHECK(path.x >= 1.75f);
    CHECK(path.y == doctest::Approx(0.5f));
    CHECK(path.distance < 1.5f);

    const ThrowCellBlocked startCellBlocked = [](int x, int) {
        return x == 0;
    };
    path = ClipThrowPath(0.5f, 0.5f, 4.5f, 0.5f, startCellBlocked);
    CHECK(path.valid);
    CHECK_FALSE(path.hitWall);
    CHECK(path.x == doctest::Approx(4.5f));

    path = ClipThrowPath(std::nanf(""), 0.5f, 4.5f, 0.5f, noWalls);
    CHECK_FALSE(path.valid);
    CHECK(ThrowFlightSeconds(0.0f) == doctest::Approx(0.18f));
    CHECK(ThrowFlightSeconds(8.0f) == doctest::Approx(0.74f));
    CHECK(ThrowFlightSeconds(100.0f) == doctest::Approx(1.2f));
    CHECK(ThrowFlightSeconds(std::nanf("")) == doctest::Approx(0.18f));
}

TEST_CASE("M34-F2-T3 落点范围内选择最近有效目标") {
    std::vector<ThrowTargetCandidate> candidates = {
        {11, 3.20f, 3.0f, 0},
        {12, 3.50f, 3.0f, 0},
        {13, 3.0f, 3.5f, 1},
        {14, 5.0f, 3.0f, 0},
    };
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, 0.6f) == 11);
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, 0.6f, 11) == 12);
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, 0.1f) == 0);
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, 0.6f, 14) == 11);

    candidates.push_back({9, 3.1f, 3.0f, 0});
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, 0.6f) == 9);

    candidates.push_back({10, 3.0f, 3.0f, 0});
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, 0.6f) == 10);
    CHECK(SelectThrowTarget(candidates, std::nanf(""), 3.0f, 0, 0.6f) == 0);
    CHECK(SelectThrowTarget(candidates, 3.0f, 3.0f, 0, -1.0f) == 0);
}

TEST_CASE("M34-F2-T3 回收损坏判定与结算幂等") {
    CHECK(ResolveThrowLanding(false, 0.05f, 0.0f) ==
          ThrowLandingOutcome::Consumed);
    CHECK(ResolveThrowLanding(true, 0.0f, 0.99f) ==
          ThrowLandingOutcome::Recovered);
    CHECK(ResolveThrowLanding(true, 0.05f, 0.049f) ==
          ThrowLandingOutcome::Broken);
    CHECK(ResolveThrowLanding(true, 0.05f, 0.05f) ==
          ThrowLandingOutcome::Recovered);
    CHECK(ResolveThrowLanding(true, 1.0f, 0.0f) ==
          ThrowLandingOutcome::Broken);
    CHECK(ResolveThrowLanding(true, std::nanf(""), 0.0f) ==
          ThrowLandingOutcome::Broken);

    ThrownProjectile projectile;
    projectile.duration = 0.5f;
    CHECK_FALSE(AdvanceThrownProjectile(projectile, 0.2f));
    CHECK_FALSE(projectile.landed);
    CHECK_FALSE(AdvanceThrownProjectile(projectile, 0.2f));
    CHECK_FALSE(projectile.landed);
    CHECK(AdvanceThrownProjectile(projectile, 0.1f));
    CHECK(projectile.landed);
    CHECK_FALSE(AdvanceThrownProjectile(projectile, 0.1f));

    ThrownProjectile pending;
    pending.duration = 0.8f;
    pending.elapsed = 0.2f;
    CHECK(ForceSettleThrownProjectile(pending));
    CHECK(pending.landed);
    CHECK(pending.elapsed == doctest::Approx(0.8f));
    CHECK_FALSE(ForceSettleThrownProjectile(pending));
}

TEST_CASE("M34-F2-T3 主手与快捷栏扣除一件") {
    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        ecs::ItemInstance{"makeshift_spear", 1, 0.73f, 0};
    ecs::ItemInstance removed;
    CHECK(ConsumeThrowableSource(&equipment, nullptr, -1, "makeshift_spear",
                                 &removed));
    CHECK(removed.defId == "makeshift_spear");
    CHECK(removed.count == 1);
    CHECK(removed.condition == doctest::Approx(0.73f));
    CHECK_FALSE(equipment
                    .slots[static_cast<std::size_t>(
                        ecs::EquipSlot::PrimaryHand)]
                    .has_value());

    ecs::Equipment stacked;
    stacked.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        ecs::ItemInstance{"forage_stone", 2, 1.0f, 0};
    CHECK(ConsumeThrowableSource(&stacked, nullptr, -1, "forage_stone",
                                 &removed));
    CHECK(removed.count == 1);
    CHECK(stacked
              .slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)]
              ->count == 1);

    ecs::Container bag;
    bag.items.push_back(ecs::ItemInstance{"wood", 3, 1.0f, 0});
    bag.items.push_back(ecs::ItemInstance{"forage_stone", 5, 1.0f, 0});
    CHECK_FALSE(ConsumeThrowableSource(nullptr, &bag, 0, "forage_stone",
                                       &removed));
    CHECK(bag.items[0].count == 3);
    CHECK(bag.items[1].count == 5);

    CHECK(ConsumeThrowableSource(nullptr, &bag, 1, "forage_stone", &removed));
    CHECK(removed.defId == "forage_stone");
    CHECK(removed.count == 1);
    CHECK(bag.items.size() == 2);
    CHECK(bag.items[1].count == 4);

    CHECK_FALSE(ConsumeThrowableSource(nullptr, &bag, 5, "forage_stone",
                                       &removed));
    CHECK_FALSE(ConsumeThrowableSource(nullptr, &bag, -2, "forage_stone",
                                       &removed));
}
