#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <utility>

#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/systems/DeathSystem.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/AnimalHarvest.h"
#include "game/Animals.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"

using namespace zl;

namespace {

const std::filesystem::path kRoot{ZL_SOURCE_DIR};

struct Fixtures {
    items::ItemRegistry items;
    items::LootTableRegistry loot;
    game::AnimalRegistry animals;

    bool Load() {
        std::string error;
        if (!items.LoadFile(kRoot / "assets" / "items.json")) {
            return false;
        }
        if (!loot.LoadFile(kRoot / "assets" / "loot.json", items)) {
            return false;
        }
        return animals.LoadFile(kRoot / "assets" / "animals.json", items, loot,
                                &error);
    }
};

ecs::Entity AddAnimalCorpse(ecs::EcsWorld& world, const std::string& species) {
    const ecs::Entity corpse = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(corpse, ecs::Position{12.5f, 8.25f, 0});
    registry.emplace<ecs::Corpse>(corpse, ecs::Corpse{0});
    registry.emplace<ecs::AnimalCorpse>(
        corpse, ecs::AnimalCorpse{species, false});
    registry.emplace<ecs::SpriteRef>(
        corpse,
        ecs::SpriteRef{ecs::HashName("corpse.placeholder"), 0, 0xFFFFFFFFu});
    return corpse;
}

ecs::Container BagWith(std::string defId, int count, float condition) {
    ecs::Container bag;
    bag.capacityKg = 20.0f;
    bag.items.push_back(
        ecs::ItemInstance{std::move(defId), count, condition});
    return bag;
}

}  // namespace

TEST_CASE("M23-F2b-T4 动物死亡生成动物尸体且不派发僵尸击杀事件") {
    core::EventBus events;
    int zombieDeaths = 0;
    events.Subscribe(core::kOnZombieDead, [&](const core::Event&) {
        ++zombieDeaths;
    });

    ecs::EcsWorld world;
    const ecs::Entity animal = world.Create();
    const std::uint64_t animalId = world.IdOf(animal).id;
    world.Registry().emplace<ecs::Position>(
        animal, ecs::Position{4.5f, 6.5f, 0});
    world.Registry().emplace<ecs::Health>(animal, ecs::Health{0.0f, 45.0f});
    world.Registry().emplace<ecs::Animal>(
        animal, ecs::Animal{"deer", ecs::AnimalKind::Passive});

    characters::DeathSystem death(nullptr, 0.0f, &events);
    death.Update(world, 0.0f);
    death.Update(world, 0.0f);
    world.Flush();
    events.FlushQueued();

    CHECK(zombieDeaths == 0);
    CHECK_FALSE(world.Alive(animal));
    bool found = false;
    world.Registry().view<ecs::AnimalCorpse, ecs::Corpse, ecs::Position>()
        .each([&](const ecs::Entity entity, const ecs::AnimalCorpse& corpse,
                  const ecs::Corpse& generic, const ecs::Position& position) {
            if (found) {
                return;
            }
            found = true;
            CHECK(corpse.speciesId == "deer");
            CHECK_FALSE(corpse.harvested);
            CHECK(generic.sourceId == animalId);
            CHECK(position.x == doctest::Approx(4.5f));
            const auto* sprite = world.Registry().try_get<ecs::SpriteRef>(entity);
            REQUIRE(sprite != nullptr);
            CHECK(sprite->sprite == ecs::HashName("corpse.placeholder"));
        });
    CHECK(found);
}

TEST_CASE("M23-F2b-T4 动物尸体剥取写入战利品并磨损工具") {
    Fixtures fixtures;
    REQUIRE(fixtures.Load());
    const game::AnimalDef* deer = fixtures.animals.Find("deer");
    REQUIRE(deer != nullptr);
    CHECK(deer->corpseSearchSeconds == doctest::Approx(1.5f));

    ecs::EcsWorld world;
    const ecs::Entity corpse = AddAnimalCorpse(world, "deer");
    ecs::Container bag =
        BagWith("kitchen_knife", 1, 1.0f);
    const ecs::Container before = bag;

    const game::AnimalHarvestResult result = game::HarvestAnimalCorpse(
        world, fixtures.items, fixtures.loot, fixtures.animals, corpse, &bag,
        nullptr);
    REQUIRE(result.found);
    CHECK(result.harvested);
    CHECK_FALSE(result.alreadyHarvested);
    CHECK_FALSE(result.missingTool);
    CHECK(result.tableId == "animal_deer");
    CHECK(result.itemStacks > 0);
    CHECK(result.itemsAdded > 0);

    const auto* data = world.Registry().try_get<ecs::AnimalCorpse>(corpse);
    REQUIRE(data != nullptr);
    CHECK(data->harvested);
    const auto* container = world.Registry().try_get<ecs::Container>(corpse);
    REQUIRE(container != nullptr);
    CHECK(container->capacityKg == doctest::Approx(12.0f));
    CHECK_FALSE(container->items.empty());
    const auto* loot = world.Registry().try_get<ecs::LootContainer>(corpse);
    REQUIRE(loot != nullptr);
    CHECK(loot->tableId == "animal_deer");
    CHECK(loot->searched);
    REQUIRE(bag.items.size() == 1u);
    CHECK(bag.items[0].condition ==
          doctest::Approx(before.items[0].condition - deer->corpseToolWear));

    const std::size_t itemCount = container->items.size();
    const float condition = bag.items[0].condition;
    const game::AnimalHarvestResult repeat = game::HarvestAnimalCorpse(
        world, fixtures.items, fixtures.loot, fixtures.animals, corpse, &bag,
        nullptr);
    CHECK(repeat.found);
    CHECK(repeat.alreadyHarvested);
    CHECK_FALSE(repeat.harvested);
    CHECK(container->items.size() == itemCount);
    CHECK(bag.items[0].condition == doctest::Approx(condition));
}

TEST_CASE("M23-F2b-T4 剥取需要可用工具，工具耗尽会移除") {
    Fixtures fixtures;
    REQUIRE(fixtures.Load());

    ecs::EcsWorld world;
    const ecs::Entity noToolCorpse = AddAnimalCorpse(world, "deer");
    ecs::Container emptyBag;
    const game::AnimalHarvestResult missing = game::HarvestAnimalCorpse(
        world, fixtures.items, fixtures.loot, fixtures.animals, noToolCorpse,
        &emptyBag, nullptr);
    CHECK(missing.found);
    CHECK_FALSE(missing.harvested);
    CHECK(missing.missingTool);
    CHECK(missing.error.empty());
    CHECK_FALSE(world.Registry()
                    .get<ecs::AnimalCorpse>(noToolCorpse)
                    .harvested);
    CHECK_FALSE(world.Registry().all_of<ecs::Container>(noToolCorpse));

    const ecs::Entity wornCorpse = AddAnimalCorpse(world, "deer");
    ecs::Container wornBag =
        BagWith("kitchen_knife", 1, 0.01f);
    const game::AnimalHarvestResult worn = game::HarvestAnimalCorpse(
        world, fixtures.items, fixtures.loot, fixtures.animals, wornCorpse,
        &wornBag, nullptr);
    CHECK(worn.found);
    CHECK(worn.harvested);
    CHECK(worn.toolBroke);
    CHECK(wornBag.items.empty());
    CHECK(world.Registry()
              .get<ecs::AnimalCorpse>(wornCorpse)
              .harvested);
}
