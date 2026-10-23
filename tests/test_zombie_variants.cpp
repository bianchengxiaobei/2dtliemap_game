#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ecs/EcsWorld.h"
#include "game/ZombieFactory.h"
#include "game/ZombieVariants.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

TEST_CASE("M14-T7 variant weights by POI and night") {
    const zl::game::ZombieVariantWeights base =
        zl::game::ZombieVariantWeightsFor(10.0f, 10.0f, 12);
    const zl::game::ZombieVariantWeights house =
        zl::game::ZombieVariantWeightsFor(72.0f, 21.0f, 12);
    const zl::game::ZombieVariantWeights night =
        zl::game::ZombieVariantWeightsFor(10.0f, 10.0f, 23);
    const zl::game::ZombieVariantWeights houseNight =
        zl::game::ZombieVariantWeightsFor(72.0f, 21.0f, 23);
    CHECK(base.Total() == 100);
    CHECK(house.strong > base.strong);
    CHECK(night.fast > base.fast);
    CHECK(houseNight.strong > house.strong);
    CHECK(houseNight.strong > night.strong);
    CHECK(zl::game::ZombieVariantWeightsFor(10.0f, 10.0f, 6).Total() == 100);
}

TEST_CASE("M14-T7 deterministic variant pick and profiles") {
    CHECK(zl::game::PickZombieVariant(72.0f, 21.0f, 23, 42u) ==
          zl::game::PickZombieVariant(72.0f, 21.0f, 23, 42u));
    std::set<zl::ecs::ZombieVariantKind> seen;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        seen.insert(zl::game::PickZombieVariant(72.0f, 21.0f, 23, seed));
    }
    CHECK(seen.size() >= 2u);

    const auto& normal =
        zl::game::ZombieVariantProfileFor(zl::ecs::ZombieVariantKind::Normal);
    const auto& fast =
        zl::game::ZombieVariantProfileFor(zl::ecs::ZombieVariantKind::Fast);
    const auto& strong =
        zl::game::ZombieVariantProfileFor(zl::ecs::ZombieVariantKind::Strong);
    CHECK(fast.speedScale > normal.speedScale);
    CHECK(fast.healthScale < normal.healthScale);
    CHECK(strong.damageScale > normal.damageScale);
    CHECK(strong.healthScale > normal.healthScale);
    CHECK(strong.visualScale > normal.visualScale);
    CHECK(normal.tint != fast.tint);
    CHECK(fast.tint != strong.tint);
}

TEST_CASE("M14-T7 spawn applies variant attributes") {
    zl::ecs::EcsWorld world;
    const zl::ecs::Entity normal =
        zl::game::SpawnZombie(world, nullptr, 10.0f, 10.0f, nullptr);
    const zl::ecs::Entity fast =
        zl::game::SpawnZombie(world, nullptr, 11.0f, 10.0f, nullptr,
                             zl::ecs::ZombieVariantKind::Fast);
    const zl::ecs::Entity strong =
        zl::game::SpawnZombie(world, nullptr, 12.0f, 10.0f, nullptr,
                             zl::ecs::ZombieVariantKind::Strong);
    CHECK(world.Alive(normal));
    CHECK(world.Alive(fast));
    CHECK(world.Alive(strong));

    const auto* normalHealth = world.Registry().try_get<zl::ecs::Health>(normal);
    const auto* fastHealth = world.Registry().try_get<zl::ecs::Health>(fast);
    const auto* strongHealth = world.Registry().try_get<zl::ecs::Health>(strong);
    REQUIRE(normalHealth != nullptr);
    REQUIRE(fastHealth != nullptr);
    REQUIRE(strongHealth != nullptr);
    CHECK(strongHealth->max > normalHealth->max);
    CHECK(fastHealth->max < normalHealth->max);

    const auto* normalVariant =
        world.Registry().try_get<zl::ecs::ZombieVariant>(normal);
    const auto* fastVariant = world.Registry().try_get<zl::ecs::ZombieVariant>(fast);
    const auto* strongVariant =
        world.Registry().try_get<zl::ecs::ZombieVariant>(strong);
    REQUIRE(normalVariant != nullptr);
    REQUIRE(fastVariant != nullptr);
    REQUIRE(strongVariant != nullptr);
    CHECK(fastVariant->speedScale > normalVariant->speedScale);
    CHECK(strongVariant->visualScale > normalVariant->visualScale);
    CHECK(world.Registry().get<zl::ecs::SpriteRef>(fast).tint !=
          world.Registry().get<zl::ecs::SpriteRef>(normal).tint);
}

TEST_CASE("M14-T7 ZombieVariant save roundtrip") {
    zl::save::ComponentRegistry components;
    std::string error;
    REQUIRE(zl::save::RegisterBuiltinComponents(components, &error));
    CHECK(components.Size() == 16u);

    zl::ecs::EcsWorld source;
    const zl::ecs::Entity zombie = source.Create();
    source.Registry().emplace<zl::ecs::Position>(
        zombie, zl::ecs::Position{10.5f, 11.5f, 0});
    source.Registry().emplace<zl::ecs::ZombieVariant>(
        zombie, zl::game::MakeZombieVariant(zl::ecs::ZombieVariantKind::Strong));

    zl::save::SavedEntity saved;
    REQUIRE(zl::save::CaptureEntity(source, zombie, components, &saved, &error));
    const std::vector<std::uint8_t> bytes =
        zl::save::EncodeEntitySnapshot(zl::save::EntitySnapshot{{saved}}, &error);
    REQUIRE_FALSE(bytes.empty());
    zl::save::EntitySnapshot decoded;
    REQUIRE(zl::save::DecodeEntitySnapshot(bytes, &decoded, &error));

    zl::ecs::EcsWorld target;
    const zl::ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != zl::ecs::EcsWorld::kNull);
    REQUIRE(zl::save::ApplyEntity(decoded.entities[0], target, loaded, components,
                                  &error));
    const auto* variant = target.Registry().try_get<zl::ecs::ZombieVariant>(loaded);
    REQUIRE(variant != nullptr);
    CHECK(variant->kind == zl::ecs::ZombieVariantKind::Strong);
    CHECK(variant->visualScale > 1.0f);
}
