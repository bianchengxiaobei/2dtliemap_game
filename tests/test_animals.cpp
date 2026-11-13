#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Animals.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using namespace zl;

namespace {

const std::filesystem::path kRoot{ZL_SOURCE_DIR};

struct Fixtures {
    items::ItemRegistry items;
    items::LootTableRegistry loot;

    bool Load() {
        if (!items.LoadFile(kRoot / "assets" / "items.json")) {
            return false;
        }
        return loot.LoadFile(kRoot / "assets" / "loot.json", items);
    }
};

std::string OneAnimalJson() {
    return R"({
  "format": "animals-v1",
  "animals": [
    {
      "id": "deer",
      "name": "鹿",
      "kind": "passive",
      "sprite": "animal.deer",
      "maxHealth": 45.0,
      "wanderSpeed": 2.0,
      "fleeSpeed": 4.8,
      "alertRadius": 9.0,
      "fleeRadius": 8.0,
      "hearingRadius": 13.0,
      "fleeSeconds": 5.0,
      "damage": 0.0,
      "attackRange": 0.0,
      "attackCooldown": 0.0,
      "corpseSprite": "corpse.placeholder",
      "corpseSearchSeconds": 1.5,
      "corpseTool": "kitchen_knife",
      "corpseToolWear": 0.02,
      "lootTableId": "animal_deer"
    }
  ]
})";
}

void EmplaceAnimal(ecs::EcsWorld& world, ecs::Entity entity, bool fleeing) {
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{12.5f, 8.25f, 0});
    registry.emplace<ecs::Health>(entity, ecs::Health{45.0f, 45.0f});
    registry.emplace<ecs::Animal>(entity,
                                  ecs::Animal{"deer", ecs::AnimalKind::Passive});
    ecs::Flee flee;
    flee.active = fleeing;
    flee.remaining = 2.5f;
    flee.directionX = 0.6f;
    flee.directionY = -0.8f;
    flee.threatX = 10.0f;
    flee.threatY = 9.0f;
    flee.alertness = 0.75f;
    registry.emplace<ecs::Flee>(entity, flee);
}

}

TEST_CASE("M23-F2b-T1 动物数据契约覆盖被动/危险与掉落") {
    Fixtures fixtures;
    REQUIRE(fixtures.Load());
    game::AnimalRegistry animals;
    std::string error;
    REQUIRE_MESSAGE(
        animals.LoadFile(kRoot / "assets" / "animals.json", fixtures.items,
                         fixtures.loot, &error),
        error);

    REQUIRE(animals.Size() == 5u);
    const game::AnimalDef* deer = animals.Find("deer");
    REQUIRE(deer != nullptr);
    CHECK(deer->kind == ecs::AnimalKind::Passive);
    CHECK(deer->maxHealth == doctest::Approx(45.0f));
    CHECK(deer->wanderSpeed == doctest::Approx(2.0f));
    CHECK(deer->fleeSpeed == doctest::Approx(4.8f));
    CHECK(deer->alertRadius == doctest::Approx(9.0f));
    CHECK(deer->damage == doctest::Approx(0.0f));
    CHECK(deer->lootTableId == "animal_deer");
    CHECK(fixtures.loot.Find(deer->lootTableId) != nullptr);
    CHECK(fixtures.items.Find("raw_venison") != nullptr);
    CHECK(deer->spawn.forest == doctest::Approx(0.65f));
    CHECK(deer->spawn.meadow == doctest::Approx(0.12f));
    CHECK(deer->spawn.maxPerChunk == 1);

    const game::AnimalDef* boar = animals.Find("boar");
    REQUIRE(boar != nullptr);
    CHECK(boar->kind == ecs::AnimalKind::Dangerous);
    CHECK(boar->damage == doctest::Approx(10.0f));
    CHECK(boar->attackRange == doctest::Approx(1.5f));
    CHECK(boar->attackCooldown == doctest::Approx(1.2f));
    CHECK(fixtures.loot.Find(boar->lootTableId) != nullptr);
    CHECK(boar->spawn.poi == doctest::Approx(0.8f));
    REQUIRE(boar->spawn.poiIds.size() == 1u);
    CHECK(boar->spawn.poiIds[0] == "farm_barn");
    CHECK(std::string(game::AnimalKindName(ecs::AnimalKind::Dangerous)) ==
          "dangerous");

    const game::AnimalDef* chicken = animals.Find("chicken");
    const game::AnimalDef* goat = animals.Find("goat");
    REQUIRE(chicken != nullptr);
    REQUIRE(goat != nullptr);
    CHECK(chicken->kind == ecs::AnimalKind::Passive);
    CHECK(chicken->maxHealth == doctest::Approx(12.0f));
    CHECK(chicken->lootTableId == "animal_chicken");
    CHECK(chicken->spawn.maxPerChunk == 2);
    CHECK(goat->kind == ecs::AnimalKind::Dangerous);
    CHECK(goat->damage == doctest::Approx(6.0f));
    CHECK(goat->lootTableId == "animal_goat");
    CHECK(fixtures.loot.Find(goat->lootTableId) != nullptr);
}

TEST_CASE("M23-F2b-T8 动物占位视觉按物种缩放") {
    CHECK(game::AnimalVisualScale("rabbit") == doctest::Approx(0.45f));
    CHECK(game::AnimalVisualScale("deer") == doctest::Approx(0.85f));
    CHECK(game::AnimalVisualScale("boar") == doctest::Approx(1.10f));
    CHECK(game::AnimalVisualScale("chicken") == doctest::Approx(0.55f));
    CHECK(game::AnimalVisualScale("goat") == doctest::Approx(1.0f));
    CHECK(game::AnimalVisualScale("unknown") == doctest::Approx(1.0f));
    CHECK(game::AnimalVisualScale("rabbit") < game::AnimalVisualScale("deer"));
    CHECK(game::AnimalVisualScale("deer") < game::AnimalVisualScale("boar"));
}

TEST_CASE("M23-F2b-T1 动物数据严格校验且失败不替换旧表") {
    Fixtures fixtures;
    REQUIRE(fixtures.Load());
    game::AnimalRegistry animals;
    std::string error;
    REQUIRE_MESSAGE(animals.LoadFromString(OneAnimalJson(), "animals-test",
                                           fixtures.items, fixtures.loot,
                                           &error),
                    error);
    REQUIRE(animals.Size() == 1u);

    const auto reject = [&](const std::string& text) {
        std::string rejectError;
        CHECK_FALSE(animals.LoadFromString(text, "animals-invalid",
                                           fixtures.items, fixtures.loot,
                                           &rejectError));
        CHECK_FALSE(rejectError.empty());
        CHECK(animals.Size() == 1u);
    };

    std::string text = OneAnimalJson();
    text.replace(text.find("\"kind\": \"passive\""),
                 std::string("\"kind\": \"passive\"").size(),
                 "\"kind\": \"hostile\"");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"lootTableId\": \"animal_deer\""),
                 std::string("\"lootTableId\": \"animal_deer\"").size(),
                 "\"lootTableId\": \"missing_table\"");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"damage\": 0.0"), std::string("\"damage\": 0.0").size(),
                 "\"damage\": 1.0");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"fleeSpeed\": 4.8"),
                 std::string("\"fleeSpeed\": 4.8").size(),
                 "\"fleeSpeed\": 1.0");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"corpseTool\": \"kitchen_knife\""),
                 std::string("\"corpseTool\": \"kitchen_knife\"").size(),
                 "\"corpseTool\": \"missing_tool\"");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"kind\": \"passive\""),
                 std::string("\"kind\": \"passive\"").size(),
                 "\"kind\": \"dangerous\"");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"id\": \"deer\""), std::string("\"id\": \"deer\"").size(),
                 "\"id\": \"deer\", \"unknown\": 1");
    reject(text);

    text = OneAnimalJson();
    text.replace(text.find("\"lootTableId\": \"animal_deer\""),
                 std::string("\"lootTableId\": \"animal_deer\"").size(),
                 "\"lootTableId\": \"animal_deer\", \"spawn\": { \"forest\": 1.0, \"maxPerChunk\": 0 }");
    reject(text);
}

TEST_CASE("M23-F2b-T1 Animal/Flee/AnimalCorpse 存档往返") {
    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    CHECK(error.empty());
    CHECK(components.Size() == 46u);

    ecs::EcsWorld source;
    const ecs::Entity animal = source.Create();
    EmplaceAnimal(source, animal, true);
    source.Registry().emplace<ecs::SpriteRef>(
        animal, ecs::SpriteRef{ecs::HashName("animal.deer"), 0, 0xFFFFFFFFu});

    const ecs::Entity corpse = source.Create();
    source.Registry().emplace<ecs::Position>(corpse,
                                             ecs::Position{12.5f, 8.25f, 0});
    source.Registry().emplace<ecs::Corpse>(corpse,
                                           ecs::Corpse{source.IdOf(animal).id});
    source.Registry().emplace<ecs::AnimalCorpse>(
        corpse, ecs::AnimalCorpse{"deer", false});
    source.Registry().emplace<ecs::SpriteRef>(
        corpse,
        ecs::SpriteRef{ecs::HashName("corpse.placeholder"), 0, 0xFFFFFFFFu});

    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, target, components, &loaded, &error));
    REQUIRE(loaded.size() == 2u);

    const ecs::Entity loadedAnimal = target.FindById(source.IdOf(animal).id);
    const ecs::Entity loadedCorpse = target.FindById(source.IdOf(corpse).id);
    REQUIRE(loadedAnimal != ecs::EcsWorld::kNull);
    REQUIRE(loadedCorpse != ecs::EcsWorld::kNull);

    const auto* animalData = target.Registry().try_get<ecs::Animal>(loadedAnimal);
    REQUIRE(animalData != nullptr);
    CHECK(animalData->speciesId == "deer");
    CHECK(animalData->kind == ecs::AnimalKind::Passive);
    const auto* flee = target.Registry().try_get<ecs::Flee>(loadedAnimal);
    REQUIRE(flee != nullptr);
    CHECK(flee->active);
    CHECK(flee->remaining == doctest::Approx(2.5f));
    CHECK(flee->directionX == doctest::Approx(0.6f));
    CHECK(flee->alertness == doctest::Approx(0.75f));

    const auto* corpseData =
        target.Registry().try_get<ecs::AnimalCorpse>(loadedCorpse);
    REQUIRE(corpseData != nullptr);
    CHECK(corpseData->speciesId == "deer");
    CHECK_FALSE(corpseData->harvested);
    const auto* genericCorpse =
        target.Registry().try_get<ecs::Corpse>(loadedCorpse);
    REQUIRE(genericCorpse != nullptr);
    CHECK(genericCorpse->sourceId == source.IdOf(animal).id);
}
