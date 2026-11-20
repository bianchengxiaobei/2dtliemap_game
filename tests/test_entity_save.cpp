#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "core/Config.h"
#include "ai/components/AiComponents.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/FireComponents.h"
#include "game/ForageComponents.h"
#include "game/HomesteadComponents.h"
#include "game/MissionComponents.h"
#include "game/Missions.h"
#include "game/PoiComponents.h"
#include "game/SettlementReputation.h"
#include "game/StructureComponents.h"
#include "game/VehicleComponents.h"
#include "items/ItemComponents.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "script/ModDataComponents.h"

using namespace zl;

namespace {

save::ComponentRegistry MakeRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    CHECK(registry.Size() == 53u);
    return registry;
}

void AddPlayerComponents(ecs::EcsWorld& world, ecs::Entity entity) {
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{70.5f, 24.25f, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{1.25f, -0.5f});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                               0xFFFFFFFFu});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{ecs::Direction::Right});
    registry.emplace<ecs::Health>(entity, ecs::Health{82.5f, 100.0f});
    registry.emplace<ecs::Needs>(entity, ecs::Needs{0.1f, 0.2f, 0.3f});
    registry.emplace<ecs::FoodState>(
        entity, ecs::FoodState{0.55f, 20.0f, 0.1f, 45.0f});
    registry.emplace<ecs::PlayerTag>(entity);
    registry.emplace<ecs::KnownRecipes>(
        entity, ecs::KnownRecipes{{"campfire_beans", "field_bandage"}});
    ecs::CraftingJob job;
    job.active = true;
    job.recipeId = "campfire_beans";
    job.elapsed = 0.4f;
    job.duration = 1.0f;
    job.toolEfficiency = 0.75f;
    job.batchTotal = 3;
    job.batchCompleted = 1;
    job.queue = {ecs::CraftQueueItem{"field_bandage", 2},
                 ecs::CraftQueueItem{"campfire_beans", 1}};
    registry.emplace<ecs::CraftingJob>(entity, std::move(job));

    ecs::Progression progression;
    progression.level = 3;
    progression.xp = 12.5f;
    progression.perkPoints = 1;
    progression.skills[0] = ecs::SkillProgress{2, 8.0f};
    progression.skills[3] = ecs::SkillProgress{1, 4.0f};
    progression.perks = {"bruiser", "fleet"};
    registry.emplace<ecs::Progression>(entity, std::move(progression));
    registry.emplace<ecs::PoiTasks>(entity,
                                    ecs::PoiTasks{{"gas_station", "clinic"}});
    ecs::MissionLog missions;
    missions.lastDay = 4;
    missions.randomDay = 4;
    missions.missions.push_back(ecs::MissionRunState{
        "radio_signal", {1}, ecs::MissionRunStatus::Completed, 3, true});
    missions.missions.push_back(ecs::MissionRunState{
        "daily_patrol", {2, 3}, ecs::MissionRunStatus::Active, 4, false,
        "village_haven"});
    registry.emplace<ecs::MissionLog>(entity, std::move(missions));

    ecs::Container bag;
    bag.capacityKg = 15.0f;
    bag.items.push_back(ecs::ItemInstance{"bandage", 3, 0.75f});
    bag.items.push_back(ecs::ItemInstance{"axe", 1, 0.4f});
    registry.emplace<ecs::Container>(entity, std::move(bag));

    ecs::Equipment equipment;
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        ecs::ItemInstance{"axe", 1, 0.8f};
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::Body)] =
        ecs::ItemInstance{"jacket", 1, 1.0f};
    registry.emplace<ecs::Equipment>(entity, std::move(equipment));
    ecs::Climate climate;
    climate.bodyC = 36.4f;
    climate.wetness = 0.3f;
    registry.emplace<ecs::Climate>(entity, climate);
}

const save::SavedComponent* FindComponent(const save::SavedEntity& entity,
                                          std::uint32_t type) {
    for (const save::SavedComponent& component : entity.components) {
        if (component.type == type) {
            return &component;
        }
    }
    return nullptr;
}

const save::SavedEntity* FindEntity(const save::EntitySnapshot& snapshot,
                                    std::uint64_t stableId) {
    for (const save::SavedEntity& entity : snapshot.entities) {
        if (entity.stableId == stableId) {
            return &entity;
        }
    }
    return nullptr;
}

template <typename WriteFn>
save::SavedComponent MakeComponent(std::uint32_t type, std::uint16_t version,
                                   WriteFn writeFn) {
    save::BinaryWriter writer;
    REQUIRE(writer.BeginSegment(type, version));
    REQUIRE(writeFn(writer));
    REQUIRE(writer.EndSegment());
    const auto bytes = writer.Data();
    save::SavedComponent component;
    component.type = type;
    component.version = version;
    component.bytes.assign(bytes.begin(), bytes.end());
    return component;
}

save::EntitySnapshot SnapshotOf(const save::SavedEntity& entity) {
    save::EntitySnapshot snapshot;
    snapshot.entities.push_back(entity);
    return snapshot;
}

}  // namespace

TEST_CASE("M10-T3 玩家与物品栏StableId 往") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    CHECK(error.empty());
    CHECK(saved.stableId == source.IdOf(player).id);
    CHECK(saved.components.size() == 16u);
    CHECK(FindComponent(saved, save::kSaveComponentItem) == nullptr);
    const save::SavedComponent* knownComponent =
        FindComponent(saved, save::kSaveComponentKnownRecipes);
    REQUIRE(knownComponent != nullptr);
    const save::SavedComponent* craftingComponent =
        FindComponent(saved, save::kSaveComponentCraftingJob);
    REQUIRE(craftingComponent != nullptr);
    const save::SavedComponent* containerComponent =
        FindComponent(saved, save::kSaveComponentContainer);
    REQUIRE(containerComponent != nullptr);
    const save::SavedComponent* equipmentComponent =
        FindComponent(saved, save::kSaveComponentEquipment);
    REQUIRE(equipmentComponent != nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    CHECK(error.empty());

    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    CHECK(error.empty());
    REQUIRE(decoded.entities.size() == 1u);
    CHECK(decoded.entities[0].stableId == saved.stableId);
    CHECK(decoded.entities[0].components.size() == saved.components.size());

    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    CHECK(error.empty());
    CHECK(target.IdOf(loaded).id == saved.stableId);
    CHECK(target.FindById(saved.stableId) == loaded);
    CHECK(target.AliveCount() == 1u);

    entt::registry& registry = target.Registry();
    REQUIRE(registry.all_of<ecs::PlayerTag>(loaded));
    const auto* position = registry.try_get<ecs::Position>(loaded);
    REQUIRE(position != nullptr);
    CHECK(position->x == doctest::Approx(70.5f));
    CHECK(position->y == doctest::Approx(24.25f));
    CHECK(position->z == 0);
    const auto* velocity = registry.try_get<ecs::Velocity>(loaded);
    REQUIRE(velocity != nullptr);
    CHECK(velocity->x == doctest::Approx(1.25f));
    CHECK(velocity->y == doctest::Approx(-0.5f));
    const auto* health = registry.try_get<ecs::Health>(loaded);
    REQUIRE(health != nullptr);
    CHECK(health->current == doctest::Approx(82.5f));
    CHECK(health->max == doctest::Approx(100.0f));
    const auto* needs = registry.try_get<ecs::Needs>(loaded);
    REQUIRE(needs != nullptr);
    CHECK(needs->hunger == doctest::Approx(0.1f));
    CHECK(needs->thirst == doctest::Approx(0.2f));
    CHECK(needs->fatigue == doctest::Approx(0.3f));
    const auto* food = registry.try_get<ecs::FoodState>(loaded);
    REQUIRE(food != nullptr);
    CHECK(food->nutrition == doctest::Approx(0.55f));
    CHECK(food->poisonRemaining == doctest::Approx(20.0f));
    CHECK(food->poisonDamagePerSecond == doctest::Approx(0.1f));
    CHECK(food->poisonTotal == doctest::Approx(45.0f));
    const auto* facing = registry.try_get<ecs::Facing>(loaded);
    REQUIRE(facing != nullptr);
    CHECK(facing->dir == ecs::Direction::Right);
    const auto* climate = registry.try_get<ecs::Climate>(loaded);
    REQUIRE(climate != nullptr);
    CHECK(climate->bodyC == doctest::Approx(36.4f));
    CHECK(climate->wetness == doctest::Approx(0.3f));
    const auto* known = registry.try_get<ecs::KnownRecipes>(loaded);
    REQUIRE(known != nullptr);
    REQUIRE(known->recipeIds.size() == 2u);
    CHECK(known->recipeIds[0] == "campfire_beans");
    CHECK(known->recipeIds[1] == "field_bandage");

    const auto* job = registry.try_get<ecs::CraftingJob>(loaded);
    REQUIRE(job != nullptr);
    CHECK(job->active);
    CHECK(job->recipeId == "campfire_beans");
    CHECK(job->elapsed == doctest::Approx(0.4f));
    CHECK(job->duration == doctest::Approx(1.0f));
    CHECK(job->toolEfficiency == doctest::Approx(0.75f));
    CHECK(job->batchTotal == 3);
    CHECK(job->batchCompleted == 1);
    REQUIRE(job->queue.size() == 2u);
    CHECK(job->queue[0].recipeId == "field_bandage");
    CHECK(job->queue[0].count == 2);
    CHECK(job->queue[1].recipeId == "campfire_beans");
    CHECK(job->queue[1].count == 1);

    const auto* progression = registry.try_get<ecs::Progression>(loaded);
    REQUIRE(progression != nullptr);
    CHECK(progression->level == 3);
    CHECK(progression->xp == doctest::Approx(12.5f));
    CHECK(progression->perkPoints == 1);
    CHECK(progression->skills[0].level == 2);
    CHECK(progression->skills[0].xp == doctest::Approx(8.0f));
    CHECK(progression->skills[3].level == 1);
    REQUIRE(progression->perks.size() == 2u);
    CHECK(progression->perks[0] == "bruiser");
    CHECK(progression->perks[1] == "fleet");

    const auto* poiTasks = registry.try_get<ecs::PoiTasks>(loaded);
    REQUIRE(poiTasks != nullptr);
    REQUIRE(poiTasks->completed.size() == 2u);
    CHECK(poiTasks->completed[0] == "clinic");
    CHECK(poiTasks->completed[1] == "gas_station");

    const auto* missionLog = registry.try_get<ecs::MissionLog>(loaded);
    REQUIRE(missionLog != nullptr);
    CHECK(missionLog->lastDay == 4);
    CHECK(missionLog->randomDay == 4);
    REQUIRE(missionLog->missions.size() == 2u);
    const auto* daily = game::FindMissionState(*missionLog, "daily_patrol");
    REQUIRE(daily != nullptr);
    CHECK(daily->status == ecs::MissionRunStatus::Active);
    CHECK(daily->day == 4);
    CHECK_FALSE(daily->rewardClaimed);
    REQUIRE(daily->progress.size() == 2u);
    CHECK(daily->progress[0] == 2);
    CHECK(daily->progress[1] == 3);
    CHECK(daily->settlementId == "village_haven");
    const auto* radio = game::FindMissionState(*missionLog, "radio_signal");
    REQUIRE(radio != nullptr);
    CHECK(radio->status == ecs::MissionRunStatus::Completed);
    CHECK(radio->rewardClaimed);

    const auto* bag = registry.try_get<ecs::Container>(loaded);
    REQUIRE(bag != nullptr);
    CHECK(bag->capacityKg == doctest::Approx(15.0f));
    REQUIRE(bag->items.size() == 2u);
    CHECK(bag->items[0].defId == "bandage");
    CHECK(bag->items[0].count == 3);
    CHECK(bag->items[0].condition == doctest::Approx(0.75f));
    CHECK(bag->items[1].defId == "axe");
    CHECK(bag->items[1].count == 1);
    CHECK(bag->items[1].condition == doctest::Approx(0.4f));

    const auto* equipment = registry.try_get<ecs::Equipment>(loaded);
    REQUIRE(equipment != nullptr);
    const auto& primary =
        equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
    const auto& body =
        equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
    REQUIRE(primary.has_value());
    REQUIRE(body.has_value());
    CHECK(primary->defId == "axe");
    CHECK(primary->condition == doctest::Approx(0.8f));
    CHECK(body->defId == "jacket");
}

TEST_CASE("M23-F1-T2：弹匣状态在玩家背包/主手/地面存档往返") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto& bag = source.Registry().get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"pistol", 1, 0.8f, 6});
    auto& equipment = source.Registry().get<ecs::Equipment>(player);
    equipment.slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)] =
        ecs::ItemInstance{"pistol", 1, 0.9f, 4};

    const ecs::Entity ground = source.Create();
    source.Registry().emplace<ecs::Position>(
        ground, ecs::Position{73.0f, 27.0f, 0});
    source.Registry().emplace<ecs::ItemInstance>(
        ground, ecs::ItemInstance{"pistol", 1, 0.7f, 2});
    const std::uint64_t playerId = source.IdOf(player).id;
    const std::uint64_t groundId = source.IdOf(ground).id;

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    const auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, target, components, &loaded, &error));
    const ecs::Entity loadedPlayer = target.FindById(playerId);
    const ecs::Entity loadedGround = target.FindById(groundId);
    REQUIRE(loadedPlayer != ecs::EcsWorld::kNull);
    REQUIRE(loadedGround != ecs::EcsWorld::kNull);

    const auto* loadedBag = target.Registry().try_get<ecs::Container>(loadedPlayer);
    REQUIRE(loadedBag != nullptr);
    REQUIRE(loadedBag->items.size() == 3u);
    CHECK(loadedBag->items.back().defId == "pistol");
    CHECK(loadedBag->items.back().loadedAmmo == 6);

    const auto* loadedEquipment =
        target.Registry().try_get<ecs::Equipment>(loadedPlayer);
    REQUIRE(loadedEquipment != nullptr);
    const auto& primary = loadedEquipment->slots[static_cast<std::size_t>(
        ecs::EquipSlot::PrimaryHand)];
    REQUIRE(primary.has_value());
    CHECK(primary->defId == "pistol");
    CHECK(primary->loadedAmmo == 4);

    const auto* loadedGroundItem =
        target.Registry().try_get<ecs::ItemInstance>(loadedGround);
    REQUIRE(loadedGroundItem != nullptr);
    CHECK(loadedGroundItem->defId == "pistol");
    CHECK(loadedGroundItem->loadedAmmo == 2);
}

TEST_CASE("M23-F1-T2：旧 v1 物品/容器/装备默认空弹匣") {
    const save::ComponentRegistry components = MakeRegistry();

    const auto legacyItem = [](save::BinaryWriter& writer) {
        return writer.WriteString("pistol") &&
               writer.Write(std::int32_t{1}) && writer.Write(0.5f);
    };

    save::SavedEntity saved;
    saved.stableId = 77;
    saved.components.push_back(MakeComponent(
        save::kSaveComponentItem, save::kSaveComponentVersion,
        legacyItem));
    saved.components.push_back(MakeComponent(
        save::kSaveComponentContainer, save::kSaveComponentVersion,
        [legacyItem](save::BinaryWriter& writer) {
            return writer.Write(10.0f) && writer.Write(std::uint32_t{1}) &&
                   legacyItem(writer);
        }));
    saved.components.push_back(MakeComponent(
        save::kSaveComponentEquipment, save::kSaveComponentVersion,
        [legacyItem](save::BinaryWriter& writer) {
            return writer.Write(true) && legacyItem(writer) &&
                   writer.Write(false);
        }));

    ecs::EcsWorld world;
    const ecs::Entity entity = world.CreateWithStableId(saved.stableId);
    REQUIRE(entity != ecs::EcsWorld::kNull);
    std::string error;
    REQUIRE(save::ApplyEntity(saved, world, entity, components, &error));
    CHECK(error.empty());

    const auto* item = world.Registry().try_get<ecs::ItemInstance>(entity);
    REQUIRE(item != nullptr);
    CHECK(item->loadedAmmo == 0);
    const auto* bag = world.Registry().try_get<ecs::Container>(entity);
    REQUIRE(bag != nullptr);
    REQUIRE(bag->items.size() == 1u);
    CHECK(bag->items[0].loadedAmmo == 0);
    const auto* equipment = world.Registry().try_get<ecs::Equipment>(entity);
    REQUIRE(equipment != nullptr);
    REQUIRE(equipment->slots[0].has_value());
    CHECK(equipment->slots[0]->loadedAmmo == 0);
}

TEST_CASE("M10-T3 CraftingJob 拒绝非法批次与队") {
    const save::ComponentRegistry components = MakeRegistry();
    std::string error;

    ecs::EcsWorld invalidProgressWorld;
    const ecs::Entity invalidProgress = invalidProgressWorld.Create();
    AddPlayerComponents(invalidProgressWorld, invalidProgress);
    auto& progressJob = invalidProgressWorld.Registry()
                            .get<ecs::CraftingJob>(invalidProgress);
    progressJob.batchCompleted = progressJob.batchTotal;
    save::SavedEntity rejected;
    CHECK_FALSE(
        save::CapturePlayer(invalidProgressWorld, components, &rejected, &error));
    CHECK_FALSE(error.empty());

    ecs::EcsWorld invalidQueueWorld;
    const ecs::Entity invalidQueue = invalidQueueWorld.Create();
    AddPlayerComponents(invalidQueueWorld, invalidQueue);
    auto& queueJob =
        invalidQueueWorld.Registry().get<ecs::CraftingJob>(invalidQueue);
    queueJob.queue.assign(static_cast<std::size_t>(ecs::kMaxCraftQueueEntries) + 1,
                          ecs::CraftQueueItem{"field_bandage", 1});
    error.clear();
    CHECK_FALSE(
        save::CapturePlayer(invalidQueueWorld, components, &rejected, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("M10-T3 全量快照保留 StableId、排序与 Corpse 引用") {
    const save::ComponentRegistry components = MakeRegistry();
    struct RuntimeOnly {
        int value = 0;
    };

    ecs::EcsWorld source;
    entt::registry& registry = source.Registry();
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    registry.emplace<RuntimeOnly>(player, RuntimeOnly{7});
    const std::uint64_t playerId = source.IdOf(player).id;

    const ecs::Entity corpse = source.Create();
    registry.emplace<ecs::Position>(corpse, ecs::Position{71.0f, 25.0f, 0});
    registry.emplace<ecs::SpriteRef>(
        corpse, ecs::SpriteRef{ecs::HashName("corpse.placeholder"), 0,
                               0xFFFFFFFFu});
    registry.emplace<ecs::Corpse>(corpse, ecs::Corpse{playerId});

    const ecs::Entity ground = source.Create();
    registry.emplace<ecs::Position>(ground, ecs::Position{72.5f, 26.5f, 0});
    registry.emplace<ecs::ItemInstance>(ground,
                                        ecs::ItemInstance{"rag", 2, 0.5f});
    registry.emplace<ecs::SpriteRef>(
        ground, ecs::SpriteRef{ecs::HashName("item.rag"), -1, 0xFFFFFFFFu});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    CHECK(error.empty());
    REQUIRE(snapshot.entities.size() == 3u);
    CHECK(snapshot.entities[0].stableId < snapshot.entities[1].stableId);
    CHECK(snapshot.entities[1].stableId < snapshot.entities[2].stableId);
    CHECK(FindEntity(snapshot, playerId) != nullptr);
    const save::SavedEntity* savedPlayer = FindEntity(snapshot, playerId);
    REQUIRE(savedPlayer != nullptr);
    CHECK(savedPlayer->components.size() == 16u);
    CHECK(FindComponent(*savedPlayer, save::kSaveComponentContainer) != nullptr);

    save::EntitySnapshot filtered;
    REQUIRE(save::CaptureEntities(
        source, components, &filtered,
        [player](ecs::Entity entity) { return entity == player; }, &error));
    REQUIRE(filtered.entities.size() == 1u);
    CHECK(filtered.entities[0].stableId == playerId);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    REQUIRE(decoded.entities.size() == 3u);

    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, target, components, &loaded, &error));
    CHECK(error.empty());
    REQUIRE(loaded.size() == 3u);
    CHECK(target.AliveCount() == 3u);

    const ecs::Entity loadedPlayer = target.FindById(playerId);
    const ecs::Entity loadedCorpse = target.FindById(source.IdOf(corpse).id);
    const ecs::Entity loadedGround = target.FindById(source.IdOf(ground).id);
    REQUIRE(loadedPlayer != ecs::EcsWorld::kNull);
    REQUIRE(loadedCorpse != ecs::EcsWorld::kNull);
    REQUIRE(loadedGround != ecs::EcsWorld::kNull);
    CHECK_FALSE(target.Registry().all_of<RuntimeOnly>(loadedPlayer));
    REQUIRE(target.Registry().all_of<ecs::PlayerTag>(loadedPlayer));

    const auto* corpseData = target.Registry().try_get<ecs::Corpse>(loadedCorpse);
    REQUIRE(corpseData != nullptr);
    CHECK(corpseData->sourceId == playerId);
    const auto* item = target.Registry().try_get<ecs::ItemInstance>(loadedGround);
    REQUIRE(item != nullptr);
    CHECK(item->defId == "rag");
    CHECK(item->count == 2);
    CHECK(item->condition == doctest::Approx(0.5f));
}

TEST_CASE("M10-T3 未知组件按段跳过，已知组件继续应") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    saved.components.push_back(MakeComponent(
        save::MakeMagic('U', 'N', 'K', 'N'), 1,
        [](save::BinaryWriter& writer) {
            return writer.Write(std::uint32_t{123});
        }));

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    CHECK(error.empty());
    const auto* bag = target.Registry().try_get<ecs::Container>(loaded);
    REQUIRE(bag != nullptr);
    CHECK(bag->items.size() == 2u);
    const auto* equipment = target.Registry().try_get<ecs::Equipment>(loaded);
    REQUIRE(equipment != nullptr);
    CHECK(equipment->slots[static_cast<std::size_t>(
              ecs::EquipSlot::PrimaryHand)]
              .has_value());
}

TEST_CASE("M10-T3 CapturePlayer 要求唯一PlayerTag") {
    const save::ComponentRegistry components = MakeRegistry();
    std::string error;
    save::SavedEntity saved;

    ecs::EcsWorld empty;
    CHECK_FALSE(save::CapturePlayer(empty, components, &saved, &error));
    CHECK(error.find("未找") != std::string::npos);

    ecs::EcsWorld multiple;
    multiple.Registry().emplace<ecs::PlayerTag>(multiple.Create());
    multiple.Registry().emplace<ecs::PlayerTag>(multiple.Create());
    CHECK_FALSE(save::CapturePlayer(multiple, components, &saved, &error));
    CHECK(error.find("数量") != std::string::npos);
}

TEST_CASE("M10-T3 实体快照拒绝重复 ID、版本不兼容与非法组") {
    const save::ComponentRegistry components = MakeRegistry();
    std::string error;

    SUBCASE("重复 StableId") {
        save::EntitySnapshot snapshot;
        snapshot.entities.push_back(save::SavedEntity{7, {}});
        snapshot.entities.push_back(save::SavedEntity{7, {}});
        const auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
        CHECK(bytes.empty());
        CHECK(error.find("重复") != std::string::npos);
    }

    SUBCASE("实体段版本不兼容") {
        save::BinaryWriter writer;
        REQUIRE(writer.BeginSegment(save::kMagicEntities, 2));
        REQUIRE(writer.Write(std::uint32_t{0}));
        REQUIRE(writer.EndSegment());
        save::EntitySnapshot snapshot;
        CHECK_FALSE(save::DecodeEntitySnapshot(writer.Data(), &snapshot, &error));
        CHECK(error.find("版本") != std::string::npos);
    }

    SUBCASE("组件版本不兼") {
        save::SavedEntity saved;
        saved.stableId = 42;
        saved.components.push_back(MakeComponent(
            save::kSaveComponentPosition, 2,
            [](save::BinaryWriter& writer) {
                return writer.Write(1.0f) && writer.Write(2.0f) &&
                       writer.Write(0);
            }));
        ecs::EcsWorld world;
        const ecs::Entity entity = world.CreateWithStableId(42);
        REQUIRE(entity != ecs::EcsWorld::kNull);
        CHECK_FALSE(save::ApplyEntity(saved, world, entity, components, &error));
        CHECK(error.find("版本") != std::string::npos);
    }

    SUBCASE("非法物品数据") {
        save::SavedEntity saved;
        saved.stableId = 43;
        saved.components.push_back(MakeComponent(
            save::kSaveComponentItem, 1,
            [](save::BinaryWriter& writer) {
                return writer.WriteString("bandage") &&
                       writer.Write(std::int32_t{0}) &&
                       writer.Write(1.0f);
            }));
        ecs::EcsWorld world;
        const ecs::Entity entity = world.CreateWithStableId(43);
        REQUIRE(entity != ecs::EcsWorld::kNull);
        CHECK_FALSE(save::ApplyEntity(saved, world, entity, components, &error));
        CHECK(error.find("ItemInstance") != std::string::npos);
        CHECK_FALSE(world.Registry().all_of<ecs::ItemInstance>(entity));
    }

    SUBCASE("截断的实体段") {
        save::EntitySnapshot snapshot;
        snapshot.entities.push_back(save::SavedEntity{44, {}});
        auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
        REQUIRE_FALSE(bytes.empty());
        bytes.pop_back();
        save::EntitySnapshot decoded;
        CHECK_FALSE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
        CHECK_FALSE(error.empty());
    }

    SUBCASE("重复组件类型") {
        save::SavedEntity saved;
        saved.stableId = 45;
        saved.components.push_back(MakeComponent(
            save::kSaveComponentPlayerTag, 1,
            [](save::BinaryWriter&) { return true; }));
        saved.components.push_back(MakeComponent(
            save::kSaveComponentPlayerTag, 1,
            [](save::BinaryWriter&) { return true; }));
        save::EntitySnapshot snapshot;
        snapshot.entities.push_back(saved);
        const auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
        CHECK(bytes.empty());
        CHECK(error.find("重复") != std::string::npos);
    }
}

TEST_CASE("M10-T3 LoadEntities 冲突时回滚本次创") {
    const save::ComponentRegistry components = MakeRegistry();
    save::EntitySnapshot snapshot;
    snapshot.entities.push_back(save::SavedEntity{10, {}});
    snapshot.entities.push_back(save::SavedEntity{11, {}});

    ecs::EcsWorld world;
    const ecs::Entity existing = world.CreateWithStableId(11);
    REQUIRE(existing != ecs::EcsWorld::kNull);

    std::vector<ecs::Entity> loaded{existing};
    std::string error;
    CHECK_FALSE(save::LoadEntities(snapshot, world, components, &loaded, &error));
    CHECK_FALSE(error.empty());
    CHECK(loaded.empty());
    CHECK(world.Alive(existing));
    CHECK(world.FindById(10) == ecs::EcsWorld::kNull);
    CHECK(world.AliveCount() == 1u);
}

TEST_CASE("M10-T3 组件注册表拒绝非法或重复注册") {
    save::ComponentRegistry registry;
    save::ComponentCodec codec;
    codec.type = save::kSaveComponentPosition;
    codec.name = "Position";
    codec.has = [](const entt::registry&, entt::entity) { return false; };
    codec.write = [](const entt::registry&, entt::entity,
                     save::BinaryWriter&) { return true; };
    codec.read = [](save::BinaryReader&, entt::registry&, entt::entity) {
        return true;
    };

    std::string error;
    REQUIRE(registry.Register(codec, &error));
    CHECK(error.empty());
    CHECK(registry.Size() == 1u);
    CHECK(registry.Find(save::kSaveComponentPosition) != nullptr);
    CHECK(registry.Find(save::kSaveComponentHealth) == nullptr);

    CHECK_FALSE(registry.Register(codec, &error));
    CHECK(error.find("重复") != std::string::npos);

    save::ComponentCodec invalid;
    invalid.type = 0;
    invalid.name = "Invalid";
    invalid.has = codec.has;
    invalid.write = codec.write;
    invalid.read = codec.read;
    CHECK_FALSE(registry.Register(invalid, &error));
    CHECK(error.find("类型") != std::string::npos);
}

TEST_CASE("M15-T2 Wounds 存档往返；旧档无伤口组件保持兼容") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto& wounds = source.Registry().emplace<ecs::Wounds>(player);
    wounds.count = 2;
    wounds.entries[0] = ecs::WoundEntry{ecs::DamageType::Bite, 0.4f, 1.2f, 12.5f,
                                        30.0f, false};
    wounds.entries[1] = ecs::WoundEntry{ecs::DamageType::Claw, 0.2f, 0.0f, 0.0f,
                                        18.0f, true};

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    CHECK(error.empty());
    const save::SavedComponent* woundComponent =
        FindComponent(saved, save::kSaveComponentWounds);
    REQUIRE(woundComponent != nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    CHECK(error.empty());

    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    CHECK(error.empty());
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    CHECK(error.empty());
    const auto* loadedWounds = target.Registry().try_get<ecs::Wounds>(loaded);
    REQUIRE(loadedWounds != nullptr);
    REQUIRE(loadedWounds->count == 2);
    CHECK(loadedWounds->entries[0].source == ecs::DamageType::Bite);
    CHECK(loadedWounds->entries[0].severity == doctest::Approx(0.4f));
    CHECK(loadedWounds->entries[0].bleedRate == doctest::Approx(1.2f));
    CHECK(loadedWounds->entries[0].bleedRemaining == doctest::Approx(12.5f));
    CHECK(loadedWounds->entries[0].bleedDuration == doctest::Approx(30.0f));
    CHECK_FALSE(loadedWounds->entries[0].bandaged);
    CHECK(loadedWounds->entries[1].source == ecs::DamageType::Claw);
    CHECK(loadedWounds->entries[1].bandaged);
    CHECK(loadedWounds->entries[1].bleedRemaining == doctest::Approx(0.0f));

    source.Registry().remove<ecs::Wounds>(player);
    save::SavedEntity legacy;
    REQUIRE(save::CapturePlayer(source, components, &legacy, &error));
    CHECK(FindComponent(legacy, save::kSaveComponentWounds) == nullptr);
    ecs::EcsWorld legacyTarget;
    const ecs::Entity legacyLoaded =
        legacyTarget.CreateWithStableId(legacy.stableId);
    REQUIRE(legacyLoaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(legacy, legacyTarget, legacyLoaded, components,
                              &error));
    CHECK_FALSE(legacyTarget.Registry().all_of<ecs::Wounds>(legacyLoaded));
}

TEST_CASE("M33-F1-T4 SettlementReputation 存档往返与旧档缺省") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto& reputation =
        source.Registry().emplace<game::SettlementReputation>(player);
    reputation.settlements.push_back(
        game::SettlementReputationEntry{"village_haven", 25});
    reputation.settlements.push_back(
        game::SettlementReputationEntry{"north_fields", -10});
    reputation.appliedKeys.push_back(
        "mission:village_haven:settlement_help:4:complete");
    reputation.appliedKeys.push_back("trade:village_haven:4:0");

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    CHECK(FindComponent(saved, save::kSaveComponentSettlementReputation) !=
          nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));

    const auto* loadedReputation =
        target.Registry().try_get<game::SettlementReputation>(loaded);
    REQUIRE(loadedReputation != nullptr);
    REQUIRE(loadedReputation->settlements.size() == 2u);
    CHECK(loadedReputation->settlements[0].settlementId == "village_haven");
    CHECK(loadedReputation->settlements[0].score == 25);
    CHECK(loadedReputation->settlements[1].settlementId == "north_fields");
    CHECK(loadedReputation->settlements[1].score == -10);
    REQUIRE(loadedReputation->appliedKeys.size() == 2u);
    CHECK(loadedReputation->appliedKeys[0] ==
          "mission:village_haven:settlement_help:4:complete");
    CHECK(loadedReputation->appliedKeys[1] == "trade:village_haven:4:0");

    source.Registry().remove<game::SettlementReputation>(player);
    save::SavedEntity legacy;
    REQUIRE(save::CapturePlayer(source, components, &legacy, &error));
    CHECK(FindComponent(legacy, save::kSaveComponentSettlementReputation) ==
          nullptr);
    ecs::EcsWorld legacyTarget;
    const ecs::Entity legacyLoaded =
        legacyTarget.CreateWithStableId(legacy.stableId);
    REQUIRE(legacyLoaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(legacy, legacyTarget, legacyLoaded, components,
                              &error));
    CHECK(legacyTarget.Registry()
              .try_get<game::SettlementReputation>(legacyLoaded) == nullptr);
}

TEST_CASE("M33-F1-T4 旧 v1 MissionLog 无聚落来源仍可读取") {
    const save::ComponentRegistry components = MakeRegistry();
    const save::SavedComponent missionComponent = MakeComponent(
        save::kSaveComponentMissionLog, 1, [](save::BinaryWriter& writer) {
            return writer.Write(static_cast<std::int32_t>(3)) &&
                   writer.Write(static_cast<std::int32_t>(0)) &&
                   writer.Write(static_cast<std::uint32_t>(1)) &&
                   writer.WriteString("settlement_help", 64) &&
                   writer.Write(static_cast<std::uint8_t>(0)) &&
                   writer.Write(static_cast<std::int32_t>(3)) &&
                   writer.Write(static_cast<std::uint8_t>(0)) &&
                   writer.Write(static_cast<std::uint32_t>(1)) &&
                   writer.Write(static_cast<std::int32_t>(1));
        });
    save::SavedEntity saved;
    saved.stableId = 41;
    saved.components.push_back(missionComponent);

    ecs::EcsWorld target;
    const ecs::Entity loaded = target.CreateWithStableId(saved.stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    std::string error;
    REQUIRE(save::ApplyEntity(saved, target, loaded, components, &error));
    const auto* log = target.Registry().try_get<ecs::MissionLog>(loaded);
    REQUIRE(log != nullptr);
    REQUIRE(log->missions.size() == 1u);
    CHECK(log->missions[0].id == "settlement_help");
    CHECK(log->missions[0].progress.size() == 1u);
    CHECK(log->missions[0].progress[0] == 1);
    CHECK(log->missions[0].settlementId.empty());
}

TEST_CASE("M15-T4 Pain 存档往返：保存基础疼痛与剩余止痛时间") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    ecs::Pain pain;
    pain.baseLevel = 0.4f;
    pain.reliefStrength = 0.65f;
    pain.reliefRemaining = 5.0f;
    pain.reliefDuration = 10.0f;
    source.Registry().emplace<ecs::Pain>(player, pain);

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    CHECK(FindComponent(saved, save::kSaveComponentPain) != nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));

    const auto* loadedPain = target.Registry().try_get<ecs::Pain>(loaded);
    REQUIRE(loadedPain != nullptr);
    CHECK(loadedPain->baseLevel == doctest::Approx(0.4f));
    CHECK(loadedPain->reliefStrength == doctest::Approx(0.65f));
    CHECK(loadedPain->reliefRemaining == doctest::Approx(5.0f));
    CHECK(loadedPain->reliefDuration == doctest::Approx(10.0f));

    source.Registry().remove<ecs::Pain>(player);
    save::SavedEntity legacy;
    REQUIRE(save::CapturePlayer(source, components, &legacy, &error));
    CHECK(FindComponent(legacy, save::kSaveComponentPain) == nullptr);
}

TEST_CASE("M19-F2 POI 守军标记存档往返") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity zombie = source.Create();
    entt::registry& registry = source.Registry();
    registry.emplace<ecs::Position>(zombie, ecs::Position{86.5f, 62.5f, 0});
    registry.emplace<ecs::PoiGuard>(zombie, ecs::PoiGuard{"farm_barn"});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 1u);
    const save::SavedEntity& saved = snapshot.entities[0];
    REQUIRE(FindComponent(saved, save::kSaveComponentPoiGuard) != nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded = target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components, &error));
    const auto* guard = target.Registry().try_get<ecs::PoiGuard>(loaded);
    REQUIRE(guard != nullptr);
    CHECK(guard->poiId == "farm_barn");
}

TEST_CASE("M20-F2 Climate 存档往返与旧档缺省") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto& climate = source.Registry().get<ecs::Climate>(player);
    climate.bodyC = 35.7f;
    climate.wetness = 0.64f;

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    REQUIRE(FindComponent(saved, save::kSaveComponentClimate) != nullptr);
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loadedClimate = target.Registry().try_get<ecs::Climate>(loaded);
    REQUIRE(loadedClimate != nullptr);
    CHECK(loadedClimate->bodyC == doctest::Approx(35.7f));
    CHECK(loadedClimate->wetness == doctest::Approx(0.64f));

    source.Registry().remove<ecs::Climate>(player);
    save::SavedEntity legacy;
    REQUIRE(save::CapturePlayer(source, components, &legacy, &error));
    CHECK(FindComponent(legacy, save::kSaveComponentClimate) == nullptr);
}

TEST_CASE("M20-F1 Infection 存档往返与旧档缺省") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    ecs::Infection infection;
    infection.stage = ecs::InfectionStage::Fever;
    infection.progress = 0.52f;
    infection.slowStrength = 0.5f;
    infection.slowRemaining = 45.0f;
    infection.slowDuration = 180.0f;
    infection.timeToNextStage = 120.0f;
    source.Registry().emplace_or_replace<ecs::Infection>(player, infection);

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    CHECK(error.empty());
    CHECK(saved.components.size() == 17u);
    REQUIRE(FindComponent(saved, save::kSaveComponentInfection) != nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loadedInfection =
        target.Registry().try_get<ecs::Infection>(loaded);
    REQUIRE(loadedInfection != nullptr);
    CHECK(loadedInfection->stage == ecs::InfectionStage::Fever);
    CHECK(loadedInfection->progress == doctest::Approx(0.52f));
    CHECK(loadedInfection->slowStrength == doctest::Approx(0.5f));
    CHECK(loadedInfection->slowRemaining == doctest::Approx(45.0f));
    CHECK(loadedInfection->slowDuration == doctest::Approx(180.0f));
    CHECK(loadedInfection->timeToNextStage == doctest::Approx(120.0f));

    source.Registry().remove<ecs::Infection>(player);
    save::SavedEntity legacy;
    REQUIRE(save::CapturePlayer(source, components, &legacy, &error));
    CHECK(FindComponent(legacy, save::kSaveComponentInfection) == nullptr);
}

TEST_CASE("M20-F3 ZombieSpecial 存档往返与旧档缺省") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity zombie = source.Create();
    entt::registry& registry = source.Registry();
    registry.emplace<ecs::Position>(zombie, ecs::Position{70.5f, 24.5f, 0});
    ecs::ZombieSpecial special;
    special.kind = ecs::ZombieSpecialKind::Howler;
    special.cooldown = 7.5f;
    special.windup = 0.4f;
    special.windupDuration = 1.2f;
    special.serial = 3;
    registry.emplace<ecs::ZombieSpecial>(zombie, special);

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 1u);
    REQUIRE(FindComponent(snapshot.entities[0],
                          save::kSaveComponentZombieSpecial) != nullptr);
    const auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loadedSpecial =
        target.Registry().try_get<ecs::ZombieSpecial>(loaded);
    REQUIRE(loadedSpecial != nullptr);
    CHECK(loadedSpecial->kind == ecs::ZombieSpecialKind::Howler);
    CHECK(loadedSpecial->cooldown == doctest::Approx(7.5f));
    CHECK(loadedSpecial->windup == doctest::Approx(0.4f));
    CHECK(loadedSpecial->windupDuration == doctest::Approx(1.2f));
    CHECK(loadedSpecial->serial == 3u);
}
TEST_CASE("M20-F4 Homestead facility state save roundtrip") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity facility = source.Create();
    entt::registry& registry = source.Registry();
    registry.emplace<ecs::Position>(facility, ecs::Position{64.5f, 24.5f, 0});
    registry.emplace<ecs::SpriteRef>(
        facility, ecs::SpriteRef{ecs::HashName("object.farm_plot"), 0,
                                 0xFFFFFFFFu});
    registry.emplace<ecs::StructureOwner>(facility,
                                           ecs::StructureOwner{0x123456789ull});

    ecs::FarmPlot plot;
    plot.cropId = "cabbage";
    plot.growthMinutes = 120.0f;
    plot.waterMinutes = 45.0f;
    plot.dryMinutes = 3.0f;
    plot.growMinutes = 240.0f;
    plot.stage = ecs::CropStage::Growing;
    plot.harvestYield = 2;
    plot.diseased = true;
    plot.fertilized = true;
    registry.emplace<ecs::FarmPlot>(facility, plot);

    ecs::Generator generator;
    generator.fuelMinutes = 300.0f;
    generator.radius = 8.0f;
    generator.running = true;
    registry.emplace<ecs::Generator>(facility, generator);
    registry.emplace<ecs::ColdStorage>(facility, ecs::ColdStorage{true});
    ecs::Floodlight floodlight;
    floodlight.on = false;
    floodlight.powered = false;
    registry.emplace<ecs::Floodlight>(facility, floodlight);
    registry.emplace<ecs::Health>(facility, ecs::Health{37.0f, 80.0f});

    ecs::SpikeTrap trap;
    trap.armed = true;
    trap.cooldown = 0.2f;
    trap.cooldownSeconds = 1.5f;
    trap.damage = 45.0f;
    trap.radius = 1.0f;
    trap.staggerSeconds = 0.35f;
    trap.uses = 4;
    registry.emplace<ecs::SpikeTrap>(facility, trap);

    ecs::MerchantStock stock;
    stock.stock = {3, 1, 0, 2, 1};
    stock.restockDay = 6;
    registry.emplace<ecs::MerchantStock>(facility, stock);

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 1u);
    for (const std::uint32_t type :
          {save::kSaveComponentFarmPlot, save::kSaveComponentGenerator,
           save::kSaveComponentColdStorage, save::kSaveComponentFloodlight,
           save::kSaveComponentHealth, save::kSaveComponentSpikeTrap,
           save::kSaveComponentMerchantStock,
           save::kSaveComponentStructureOwner}) {
        CHECK(FindComponent(snapshot.entities[0], type) != nullptr);
    }
    const auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));

    const auto* loadedPlot = target.Registry().try_get<ecs::FarmPlot>(loaded);
    REQUIRE(loadedPlot != nullptr);
    CHECK(loadedPlot->cropId == "cabbage");
    CHECK(loadedPlot->growthMinutes == doctest::Approx(120.0f));
    CHECK(loadedPlot->waterMinutes == doctest::Approx(45.0f));
    CHECK(loadedPlot->stage == ecs::CropStage::Growing);
    CHECK(loadedPlot->diseased);
    CHECK(loadedPlot->fertilized);
    const auto* loadedGenerator =
        target.Registry().try_get<ecs::Generator>(loaded);
    REQUIRE(loadedGenerator != nullptr);
    CHECK(loadedGenerator->fuelMinutes == doctest::Approx(300.0f));
    CHECK(loadedGenerator->running);
    const auto* loadedCold =
        target.Registry().try_get<ecs::ColdStorage>(loaded);
    REQUIRE(loadedCold != nullptr);
    CHECK(loadedCold->powered);
    const auto* loadedFloodlight =
        target.Registry().try_get<ecs::Floodlight>(loaded);
    REQUIRE(loadedFloodlight != nullptr);
    CHECK_FALSE(loadedFloodlight->on);
    CHECK_FALSE(loadedFloodlight->powered);
    const auto* loadedHealth =
        target.Registry().try_get<ecs::Health>(loaded);
    REQUIRE(loadedHealth != nullptr);
    CHECK(loadedHealth->current == doctest::Approx(37.0f));
    CHECK(loadedHealth->max == doctest::Approx(80.0f));
    const auto* loadedTrap =
        target.Registry().try_get<ecs::SpikeTrap>(loaded);
    REQUIRE(loadedTrap != nullptr);
    CHECK(loadedTrap->uses == 4);
    CHECK(loadedTrap->cooldown == doctest::Approx(0.2f));
    const auto* loadedStock =
        target.Registry().try_get<ecs::MerchantStock>(loaded);
    REQUIRE(loadedStock != nullptr);
    CHECK((loadedStock->stock == std::array<int, 5>{3, 1, 0, 2, 1}));
    CHECK(loadedStock->restockDay == 6);
    const auto* loadedOwner =
        target.Registry().try_get<ecs::StructureOwner>(loaded);
    REQUIRE(loadedOwner != nullptr);
    CHECK(loadedOwner->ownerStableId == 0x123456789ull);
}

TEST_CASE("M23-F2a-T2 ForageState save roundtrip") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    ecs::ForageState forage;
    forage.harvested.push_back(
        ecs::ForageHarvest{"forest", 12, 34, 0, 340.5});
    forage.harvested.push_back(
        ecs::ForageHarvest{"meadow", 56, 78, 0, 180.0});
    source.Registry().emplace<ecs::ForageState>(player, forage);

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 1u);
    REQUIRE(FindComponent(snapshot.entities[0],
                          save::kSaveComponentForage) != nullptr);
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());

    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loadedForage =
        target.Registry().try_get<ecs::ForageState>(loaded);
    REQUIRE(loadedForage != nullptr);
    REQUIRE(loadedForage->harvested.size() == 2u);
    CHECK(loadedForage->harvested[0].zoneId == "forest");
    CHECK(loadedForage->harvested[0].x == 12);
    CHECK(loadedForage->harvested[0].y == 34);
    CHECK(loadedForage->harvested[0].availableAtMinutes ==
          doctest::Approx(340.5));
    CHECK(loadedForage->harvested[1].zoneId == "meadow");
}


TEST_CASE("M23-F3-T6 FireAnchor/FireSource/BurningZombie save roundtrip") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity fire = source.Create();
    entt::registry& sourceRegistry = source.Registry();
    sourceRegistry.emplace<ecs::Position>(fire, ecs::Position{10.5f, 11.5f, 0});
    sourceRegistry.emplace<ecs::FireAnchor>(fire, ecs::FireAnchor{10, 11, 0});
    ecs::FireSource fireSource;
    fireSource.materialId = "wood_floor";
    fireSource.phase = ecs::FirePhase::Burning;
    fireSource.ignitionProgress = 1.25f;
    fireSource.ignitionDuration = 1.5f;
    fireSource.burnRemaining = 32.5f;
    fireSource.burnDuration = 45.0f;
    fireSource.smokePerSecond = 0.6f;
    fireSource.heatRadius = 2.0f;
    fireSource.smolderElapsed = 0.0f;
    fireSource.spreadAccumulator = 2.5f;
    sourceRegistry.emplace<ecs::FireSource>(fire, fireSource);

    const ecs::Entity zombie = source.Create();
    sourceRegistry.emplace<ecs::Position>(zombie, ecs::Position{11.5f, 11.5f, 0});
    ecs::BurningZombie burning;
    burning.remaining = 6.5f;
    burning.damageAccumulator = 0.4f;
    burning.spreadAccumulator = 0.2f;
    burning.panicRemaining = 3.0f;
    burning.panicSpeedScale = 1.25f;
    burning.panicChaseRadius = 12.0f;
    sourceRegistry.emplace<ecs::BurningZombie>(zombie, burning);

    const ecs::Entity burned = source.Create();
    sourceRegistry.emplace<ecs::Position>(burned,
                                          ecs::Position{12.5f, 11.5f, 0});
    sourceRegistry.emplace<ecs::FireAnchor>(burned, ecs::FireAnchor{12, 11, 0});
    ecs::FireSource burnedSource;
    burnedSource.materialId = "wood_wall";
    burnedSource.phase = ecs::FirePhase::Extinguished;
    burnedSource.ignitionDuration = 1.5f;
    burnedSource.burnDuration = 90.0f;
    burnedSource.heatRadius = 2.5f;
    sourceRegistry.emplace<ecs::FireSource>(burned, burnedSource);

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 3u);
    const save::SavedEntity* savedFire = nullptr;
    const save::SavedEntity* savedZombie = nullptr;
    const save::SavedEntity* savedBurned = nullptr;
    for (const save::SavedEntity& entity : snapshot.entities) {
        if (entity.stableId == source.IdOf(fire).id) {
            savedFire = &entity;
        } else if (entity.stableId == source.IdOf(zombie).id) {
            savedZombie = &entity;
        } else if (entity.stableId == source.IdOf(burned).id) {
            savedBurned = &entity;
        }
    }
    REQUIRE(savedFire != nullptr);
    REQUIRE(savedZombie != nullptr);
    REQUIRE(savedBurned != nullptr);
    CHECK(FindComponent(*savedFire, save::kSaveComponentFireAnchor) != nullptr);
    CHECK(FindComponent(*savedFire, save::kSaveComponentFireSource) != nullptr);
    CHECK(FindComponent(*savedZombie, save::kSaveComponentBurningZombie) !=
          nullptr);
    CHECK(FindComponent(*savedBurned, save::kSaveComponentFireSource) !=
          nullptr);

    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, target, components, &loaded, &error));
    REQUIRE(loaded.size() == 3u);
    const ecs::Entity loadedFire = target.FindById(source.IdOf(fire).id);
    const ecs::Entity loadedZombie = target.FindById(source.IdOf(zombie).id);
    const ecs::Entity loadedBurned = target.FindById(source.IdOf(burned).id);
    REQUIRE(loadedFire != ecs::EcsWorld::kNull);
    REQUIRE(loadedZombie != ecs::EcsWorld::kNull);
    REQUIRE(loadedBurned != ecs::EcsWorld::kNull);
    const auto* anchor = target.Registry().try_get<ecs::FireAnchor>(loadedFire);
    REQUIRE(anchor != nullptr);
    CHECK(anchor->x == 10);
    CHECK(anchor->y == 11);
    CHECK(anchor->z == 0);
    const auto* loadedSource =
        target.Registry().try_get<ecs::FireSource>(loadedFire);
    REQUIRE(loadedSource != nullptr);
    CHECK(loadedSource->materialId == "wood_floor");
    CHECK(loadedSource->phase == ecs::FirePhase::Burning);
    CHECK(loadedSource->burnRemaining == doctest::Approx(32.5f));
    CHECK(loadedSource->spreadAccumulator == doctest::Approx(2.5f));
    const auto* loadedBurning =
        target.Registry().try_get<ecs::BurningZombie>(loadedZombie);
    REQUIRE(loadedBurning != nullptr);
    CHECK(loadedBurning->remaining == doctest::Approx(6.5f));
    CHECK(loadedBurning->panicRemaining == doctest::Approx(3.0f));
    CHECK(loadedBurning->panicChaseRadius == doctest::Approx(12.0f));
    const auto* loadedBurnedSource =
        target.Registry().try_get<ecs::FireSource>(loadedBurned);
    REQUIRE(loadedBurnedSource != nullptr);
    CHECK(loadedBurnedSource->materialId == "wood_wall");
    CHECK(loadedBurnedSource->phase == ecs::FirePhase::Extinguished);
    CHECK(loadedBurnedSource->burnRemaining == doctest::Approx(0.0f));
}

TEST_CASE("M23-F6-T5 旧档 MissionLog 短进度与未知任务可读取") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto& log = source.Registry().get<ecs::MissionLog>(player);
    log.lastDay = 3;
    log.randomDay = 2;
    log.missions.clear();
    log.missions.push_back(ecs::MissionRunState{
        "clinic_supply", {}, ecs::MissionRunStatus::Active, 3, false});
    log.missions.push_back(ecs::MissionRunState{
        "hospital_quarantine", {1}, ecs::MissionRunStatus::Active, 3, false});
    log.missions.push_back(ecs::MissionRunState{
        "legacy_removed", {5}, ecs::MissionRunStatus::Completed, 2, true});

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));

    const auto* missionLog = target.Registry().try_get<ecs::MissionLog>(loaded);
    REQUIRE(missionLog != nullptr);
    REQUIRE(missionLog->missions.size() == 3u);
    const auto* clinic = game::FindMissionState(*missionLog, "clinic_supply");
    REQUIRE(clinic != nullptr);
    CHECK(clinic->progress.empty());
    const auto* hospital =
        game::FindMissionState(*missionLog, "hospital_quarantine");
    REQUIRE(hospital != nullptr);
    REQUIRE(hospital->progress.size() == 1u);
    CHECK(hospital->progress[0] == 1);
    const auto* legacy = game::FindMissionState(*missionLog, "legacy_removed");
    REQUIRE(legacy != nullptr);
    CHECK(legacy->status == ecs::MissionRunStatus::Completed);
    CHECK(legacy->rewardClaimed);
}

TEST_CASE("M24-T3 旧档 radio_repair 状态决定中继链可用性") {
    core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    game::MissionConfig missions;
    std::string error;
    REQUIRE(game::LoadMissionConfig(config, &missions, &error));
    const game::MissionDef* recover = missions.Find("relay_recover");
    REQUIRE(recover != nullptr);

    const save::ComponentRegistry components = MakeRegistry();
    auto reloadedCanActivate = [&](ecs::MissionRunStatus status) {
        ecs::EcsWorld source;
        const ecs::Entity player = source.Create();
        AddPlayerComponents(source, player);
        auto& log = source.Registry().get<ecs::MissionLog>(player);
        log.missions.push_back(ecs::MissionRunState{
            "radio_repair", {1}, status, 2, false});
        save::SavedEntity saved;
        REQUIRE(save::CapturePlayer(source, components, &saved, &error));
        const std::vector<std::uint8_t> bytes =
            save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
        REQUIRE_FALSE(bytes.empty());
        save::EntitySnapshot decoded;
        REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
        ecs::EcsWorld target;
        const ecs::Entity loaded =
            target.CreateWithStableId(decoded.entities[0].stableId);
        REQUIRE(loaded != ecs::EcsWorld::kNull);
        REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded,
                                  components, &error));
        const auto* loadedLog =
            target.Registry().try_get<ecs::MissionLog>(loaded);
        REQUIRE(loadedLog != nullptr);
        return game::MissionCanActivate(*recover, *loadedLog, 4);
    };
    CHECK_FALSE(reloadedCanActivate(ecs::MissionRunStatus::Active));
    CHECK(reloadedCanActivate(ecs::MissionRunStatus::Completed));
}

TEST_CASE("M23-F2b 动物尸体容器与已剥取状态存档往返") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto& bag = source.Registry().get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"raw_boar_meat", 3, 0.8f});

    const ecs::Entity corpse = source.Create();
    const std::uint64_t corpseId = source.IdOf(corpse).id;
    entt::registry& sourceRegistry = source.Registry();
    sourceRegistry.emplace<ecs::Position>(corpse, ecs::Position{12.5f, 8.25f, 0});
    sourceRegistry.emplace<ecs::Corpse>(corpse, ecs::Corpse{999});
    sourceRegistry.emplace<ecs::AnimalCorpse>(
        corpse, ecs::AnimalCorpse{"boar", true});
    sourceRegistry.emplace<ecs::SpriteRef>(
        corpse, ecs::SpriteRef{ecs::HashName("corpse.placeholder"), 0,
                               0xFFFFFFFFu});
    ecs::Container loot;
    loot.capacityKg = 18.0f;
    loot.items.push_back(ecs::ItemInstance{"animal_hide", 2, 1.0f});
    loot.items.push_back(ecs::ItemInstance{"bone", 1, 1.0f});
    sourceRegistry.emplace<ecs::Container>(corpse, loot);
    sourceRegistry.emplace<ecs::LootContainer>(
        corpse, ecs::LootContainer{"animal_boar", 42, true});

    std::string error;
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

    const ecs::Entity loadedCorpse = target.FindById(corpseId);
    REQUIRE(loadedCorpse != ecs::EcsWorld::kNull);
    const auto* corpseState =
        target.Registry().try_get<ecs::AnimalCorpse>(loadedCorpse);
    REQUIRE(corpseState != nullptr);
    CHECK(corpseState->speciesId == "boar");
    CHECK(corpseState->harvested);
    const auto* corpseContainer =
        target.Registry().try_get<ecs::Container>(loadedCorpse);
    REQUIRE(corpseContainer != nullptr);
    REQUIRE(corpseContainer->items.size() == 2u);
    CHECK(corpseContainer->items[0].defId == "animal_hide");
    CHECK(corpseContainer->items[0].count == 2);
    CHECK(corpseContainer->items[1].defId == "bone");

    const auto* loadedBag =
        target.Registry().try_get<ecs::Container>(target.FindById(
            source.IdOf(player).id));
    REQUIRE(loadedBag != nullptr);
    bool foundMeat = false;
    for (const ecs::ItemInstance& item : loadedBag->items) {
        if (item.defId == "raw_boar_meat" && item.count == 3) {
            foundMeat = true;
        }
    }
    CHECK(foundMeat);
}
TEST_CASE("M24-T4 随机事件敌人标记随存档恢复") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity actor = source.CreateWithStableId(9001);
    REQUIRE(actor != ecs::EcsWorld::kNull);
    source.Registry().emplace<ecs::Position>(
        actor, ecs::Position{70.5f, 33.5f, 0});
    source.Registry().emplace<ecs::Health>(
        actor, ecs::Health{100.0f, 100.0f});
    source.Registry().emplace<ecs::RandomEventActor>(
        actor, ecs::RandomEventActor{"event_wandering_horde", 3});

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    const auto bytes = save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));

    ecs::EcsWorld target;
    std::vector<ecs::Entity> loaded;
    REQUIRE(save::LoadEntities(decoded, target, components, &loaded, &error));
    const ecs::Entity restored = target.FindById(9001);
    REQUIRE(restored != ecs::EcsWorld::kNull);
    const auto* tag = target.Registry().try_get<ecs::RandomEventActor>(restored);
    REQUIRE(tag != nullptr);
    CHECK(tag->eventId == "event_wandering_horde");
    CHECK(tag->index == 3);
}


TEST_CASE("M25-T2 Vehicle state and trunk save roundtrip") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    const ecs::Entity vehicle = source.Create();
    const std::uint64_t vehicleId = source.IdOf(vehicle).id;
    entt::registry& registry = source.Registry();
    registry.emplace<ecs::Position>(vehicle, ecs::Position{80.5f, 30.5f, 0});
    registry.emplace<ecs::SpriteRef>(
        vehicle, ecs::SpriteRef{ecs::HashName("object.vehicle"), 0,
                                0xFFFFFFFFu});
    ecs::Vehicle savedVehicle{0.35f, 0.72f, true};
    savedVehicle.type = ecs::VehicleType::HeavyPickup;
    savedVehicle.heading = 1.0f;
    registry.emplace<ecs::Vehicle>(vehicle, savedVehicle);
    ecs::Container trunk;
    trunk.capacityKg = 40.0f;
    trunk.items.push_back(ecs::ItemInstance{"repair_kit", 2, 1.0f});
    trunk.items.push_back(ecs::ItemInstance{"fuel_can", 1, 1.0f});
    registry.emplace<ecs::Container>(vehicle, trunk);
    registry.emplace<ecs::PlayerStorage>(vehicle, ecs::PlayerStorage{6});

    std::string error;
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
    const ecs::Entity restored = target.FindById(vehicleId);
    REQUIRE(restored != ecs::EcsWorld::kNull);

    const auto* state = target.Registry().try_get<ecs::Vehicle>(restored);
    REQUIRE(state != nullptr);
    CHECK(state->condition == doctest::Approx(0.35f));
    CHECK(state->fuel == doctest::Approx(0.72f));
    CHECK(state->headlights);
    CHECK(state->type == ecs::VehicleType::HeavyPickup);
    CHECK(state->heading == doctest::Approx(1.0f));

    const auto* position = target.Registry().try_get<ecs::Position>(restored);
    REQUIRE(position != nullptr);
    CHECK(position->x == doctest::Approx(80.5f));
    CHECK(position->y == doctest::Approx(30.5f));

    const auto* storage = target.Registry().try_get<ecs::PlayerStorage>(restored);
    REQUIRE(storage != nullptr);
    CHECK(storage->maxStacks == 6);
    const auto* container = target.Registry().try_get<ecs::Container>(restored);
    REQUIRE(container != nullptr);
    CHECK(container->capacityKg == doctest::Approx(40.0f));
    REQUIRE(container->items.size() == 2u);
    CHECK(container->items[0].defId == "repair_kit");
    CHECK(container->items[0].count == 2);
    CHECK(container->items[1].defId == "fuel_can");
    CHECK(container->items[1].count == 1);
}

TEST_CASE("M26-V5 单机多车载具存档保留全部车辆") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    std::uint64_t ids[3] = {};
    for (int i = 0; i < 3; ++i) {
        const ecs::Entity vehicle = source.Create();
        ids[i] = source.IdOf(vehicle).id;
        entt::registry& registry = source.Registry();
        registry.emplace<ecs::Position>(
            vehicle,
            ecs::Position{70.5f + static_cast<float>(i) * 3.0f, 30.5f, 0});
        registry.emplace<ecs::SpriteRef>(
            vehicle, ecs::SpriteRef{ecs::HashName("object.vehicle_south"), 0,
                                    0xFFFFFFFFu});
        registry.emplace<ecs::Vehicle>(
            vehicle, ecs::Vehicle{0.9f - static_cast<float>(i) * 0.2f,
                                  0.8f - static_cast<float>(i) * 0.1f, i == 2});
    }

    std::string error;
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
    CHECK(loaded.size() == 3u);
    for (int i = 0; i < 3; ++i) {
        const ecs::Entity restored = target.FindById(ids[i]);
        REQUIRE(restored != ecs::EcsWorld::kNull);
        const auto* state = target.Registry().try_get<ecs::Vehicle>(restored);
        REQUIRE(state != nullptr);
        CHECK(state->condition ==
              doctest::Approx(0.9f - static_cast<float>(i) * 0.2f));
        CHECK(state->fuel ==
              doctest::Approx(0.8f - static_cast<float>(i) * 0.1f));
        CHECK(state->headlights == (i == 2));
        const auto* position =
            target.Registry().try_get<ecs::Position>(restored);
        REQUIRE(position != nullptr);
        CHECK(position->x ==
              doctest::Approx(70.5f + static_cast<float>(i) * 3.0f));
        CHECK(position->y == doctest::Approx(30.5f));
    }
}

TEST_CASE("M25-T4-V2 旧 v1 车辆存档默认关闭车灯") {
    const save::ComponentRegistry components = MakeRegistry();
    save::SavedEntity saved;
    saved.stableId = 951;
    saved.components.push_back(MakeComponent(
        save::kSaveComponentVehicle, save::kSaveComponentVersion,
        [](save::BinaryWriter& writer) {
            return writer.Write(0.35f) && writer.Write(0.72f);
        }));

    ecs::EcsWorld world;
    const ecs::Entity entity = world.CreateWithStableId(saved.stableId);
    REQUIRE(entity != ecs::EcsWorld::kNull);
    std::string error;
    REQUIRE(save::ApplyEntity(saved, world, entity, components, &error));
    CHECK(error.empty());
    const auto* state = world.Registry().try_get<ecs::Vehicle>(entity);
    REQUIRE(state != nullptr);
    CHECK(state->condition == doctest::Approx(0.35f));
    CHECK(state->fuel == doctest::Approx(0.72f));
    CHECK_FALSE(state->headlights);
}

TEST_CASE("M33-F4-T2 车辆改装组件存档往返与旧档") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    const ecs::Entity vehicle = source.Create();
    const std::uint64_t vehicleId = source.IdOf(vehicle).id;
    entt::registry& registry = source.Registry();
    registry.emplace<ecs::Position>(vehicle, ecs::Position{80.5f, 30.5f, 0});
    registry.emplace<ecs::Vehicle>(vehicle, ecs::Vehicle{});
    ecs::VehicleMods mods;
    mods.defIds[0] = "vehicle_armor_plate";
    mods.defIds[1] = "vehicle_storage_rack";
    mods.defIds[3] = "vehicle_searchlight";
    registry.emplace<ecs::VehicleMods>(vehicle, mods);

    std::string error;
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
    const ecs::Entity restored = target.FindById(vehicleId);
    REQUIRE(restored != ecs::EcsWorld::kNull);
    const auto* restoredMods =
        target.Registry().try_get<ecs::VehicleMods>(restored);
    REQUIRE(restoredMods != nullptr);
    CHECK(restoredMods->defIds[0] == "vehicle_armor_plate");
    CHECK(restoredMods->defIds[1] == "vehicle_storage_rack");
    CHECK(restoredMods->defIds[2].empty());
    CHECK(restoredMods->defIds[3] == "vehicle_searchlight");
    CHECK(restoredMods->defIds[4].empty());

    save::SavedEntity oldVehicle;
    oldVehicle.stableId = 952;
    oldVehicle.components.push_back(MakeComponent(
        save::kSaveComponentVehicle, 3,
        [](save::BinaryWriter& writer) {
            return writer.Write(0.8f) && writer.Write(0.5f) &&
                   writer.Write(static_cast<std::uint8_t>(0)) &&
                   writer.Write(static_cast<std::uint8_t>(1)) &&
                   writer.Write(1.5f);
        }));
    ecs::EcsWorld oldTarget;
    const ecs::Entity oldEntity = oldTarget.CreateWithStableId(952);
    REQUIRE(oldEntity != ecs::EcsWorld::kNull);
    error.clear();
    REQUIRE(save::ApplyEntity(oldVehicle, oldTarget, oldEntity, components,
                             &error));
    CHECK(oldTarget.Registry().try_get<ecs::Vehicle>(oldEntity) != nullptr);
    CHECK(oldTarget.Registry().try_get<ecs::VehicleMods>(oldEntity) ==
          nullptr);
}

TEST_CASE("M33-F5-T2 世界阶段进度与奖励幂等键存档往返") {
    const save::ComponentRegistry components = MakeRegistry();
    ecs::EcsWorld source;
    const ecs::Entity poiWorld =
        source.CreateWithStableId(ecs::kPoiWorldProgressStableId);
    ecs::PoiWorldProgress progress;
    game::PoiProgress poi;
    poi.poiId = "staged_hospital";
    poi.currentStage = 2;
    poi.completedStages = {"approach", "find_key"};
    poi.chosenBranch = "morgue_route";
    poi.acquiredKeys = {"front"};
    poi.powered = true;
    poi.awaitingChoice = true;
    poi.attempts = 2;
    poi.stageKills = 1;
    poi.lastLiveGuards = 3;
    progress.pois.push_back(poi);
    source.Registry().emplace<ecs::PoiWorldProgress>(poiWorld,
                                                     std::move(progress));

    std::string error;
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    REQUIRE(snapshot.entities.size() == 1u);
    REQUIRE(FindComponent(snapshot.entities[0],
                          save::kSaveComponentPoiWorldProgress) != nullptr);
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(snapshot, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* loadedProgress =
        target.Registry().try_get<ecs::PoiWorldProgress>(loaded);
    REQUIRE(loadedProgress != nullptr);
    REQUIRE(loadedProgress->pois.size() == 1u);
    CHECK(loadedProgress->pois[0].poiId == "staged_hospital");
    CHECK(loadedProgress->pois[0].currentStage == 2);
    CHECK(loadedProgress->pois[0].completedStages ==
          std::vector<std::string>({"approach", "find_key"}));
    CHECK(loadedProgress->pois[0].chosenBranch == "morgue_route");
    CHECK(loadedProgress->pois[0].acquiredKeys ==
          std::vector<std::string>({"front"}));
    CHECK(loadedProgress->pois[0].powered);
    CHECK(loadedProgress->pois[0].awaitingChoice);
    CHECK(loadedProgress->pois[0].attempts == 2);
    CHECK(loadedProgress->pois[0].stageKills == 1);

    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);
    auto* tasks = source.Registry().try_get<ecs::PoiTasks>(player);
    REQUIRE(tasks != nullptr);
    tasks->rewardClaims = {"staged_hospital.find_key"};
    save::SavedEntity savedPlayer;
    REQUIRE(save::CapturePlayer(source, components, &savedPlayer, &error));
    const std::vector<std::uint8_t> playerBytes =
        save::EncodeEntitySnapshot(SnapshotOf(savedPlayer), &error);
    REQUIRE_FALSE(playerBytes.empty());
    save::EntitySnapshot decodedPlayer;
    REQUIRE(save::DecodeEntitySnapshot(playerBytes, &decodedPlayer, &error));
    ecs::EcsWorld playerTarget;
    const ecs::Entity loadedPlayer =
        playerTarget.CreateWithStableId(decodedPlayer.entities[0].stableId);
    REQUIRE(save::ApplyEntity(decodedPlayer.entities[0], playerTarget,
                              loadedPlayer, components, &error));
    const auto* loadedTasks =
        playerTarget.Registry().try_get<ecs::PoiTasks>(loadedPlayer);
    REQUIRE(loadedTasks != nullptr);
    REQUIRE(loadedTasks->rewardClaims.size() == 1u);
    CHECK(loadedTasks->rewardClaims[0] == "staged_hospital.find_key");
}

TEST_CASE("M35-F4-T1 ModData 单例、存档往返与上限") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    CHECK(ecs::FindModDataEntity(source) == ecs::EcsWorld::kNull);
    ecs::ModData& data = ecs::EnsureModData(source);
    const ecs::Entity entity = ecs::FindModDataEntity(source);
    REQUIRE(entity != ecs::EcsWorld::kNull);
    CHECK(source.Registry().all_of<ecs::ModDataTag>(entity));
    CHECK_FALSE(source.Registry().all_of<ecs::Position>(entity));
    CHECK(&ecs::EnsureModData(source) == &data);
    CHECK(source.AliveCount() == 1u);
    CHECK(data.mods.empty());

    std::string error;
    save::SavedEntity emptySaved;
    REQUIRE(save::CaptureEntity(source, entity, components, &emptySaved,
                                &error));
    CHECK(error.empty());
    const save::SavedComponent* emptyComponent =
        FindComponent(emptySaved, save::kSaveComponentModData);
    REQUIRE(emptyComponent != nullptr);
    CHECK(emptyComponent->version == 1u);

    const std::vector<std::uint8_t> emptyBytes =
        save::EncodeEntitySnapshot(SnapshotOf(emptySaved), &error);
    REQUIRE_FALSE(emptyBytes.empty());
    save::EntitySnapshot emptyDecoded;
    REQUIRE(save::DecodeEntitySnapshot(emptyBytes, &emptyDecoded, &error));
    ecs::EcsWorld emptyTarget;
    const ecs::Entity emptyLoaded = emptyTarget.CreateWithStableId(
        emptyDecoded.entities[0].stableId);
    REQUIRE(emptyLoaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(emptyDecoded.entities[0], emptyTarget,
                              emptyLoaded, components, &error));
    const auto* loadedEmpty =
        emptyTarget.Registry().try_get<ecs::ModData>(emptyLoaded);
    REQUIRE(loadedEmpty != nullptr);
    CHECK(loadedEmpty->mods.empty());
    CHECK(emptyTarget.Registry().all_of<ecs::ModDataTag>(emptyLoaded));
    CHECK(ecs::FindModDataEntity(emptyTarget) == emptyLoaded);
    CHECK(emptyTarget.AliveCount() == 1u);

    data.mods.push_back(
        {"alpha", {{"score", "42"}, {"empty", ""}}});
    data.mods.push_back(
        {"测试", {{"标题", "你好"}, {"json", R"({"a":1})"}}});

    save::SavedEntity saved;
    REQUIRE(save::CaptureEntity(source, entity, components, &saved, &error));
    CHECK(error.empty());
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(SnapshotOf(saved), &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    CHECK(error.empty());
    const auto* loadedData = target.Registry().try_get<ecs::ModData>(loaded);
    REQUIRE(loadedData != nullptr);
    REQUIRE(loadedData->mods.size() == 2u);
    CHECK(loadedData->mods[0].first == "alpha");
    REQUIRE(loadedData->mods[0].second.size() == 2u);
    CHECK(loadedData->mods[0].second[0].key == "score");
    CHECK(loadedData->mods[0].second[0].value == "42");
    CHECK(loadedData->mods[0].second[1].key == "empty");
    CHECK(loadedData->mods[0].second[1].value.empty());
    CHECK(loadedData->mods[1].first == "测试");
    REQUIRE(loadedData->mods[1].second.size() == 2u);
    CHECK(loadedData->mods[1].second[0].key == "标题");
    CHECK(loadedData->mods[1].second[0].value == "你好");
    CHECK(loadedData->mods[1].second[1].value == R"({"a":1})");
    CHECK(&ecs::EnsureModData(target) == loadedData);
    CHECK(target.AliveCount() == 1u);

    const auto validMods = data.mods;
    data.mods = {{"alpha", {}}};
    for (int i = 0; i < 65; ++i) {
        data.mods[0].second.push_back(
            ecs::ModDataEntry{"k" + std::to_string(i), "v"});
    }
    save::SavedEntity tooMany;
    CHECK_FALSE(save::CaptureEntity(source, entity, components, &tooMany,
                                    &error));
    CHECK(error.find("ModData") != std::string::npos);

    data.mods = {{"alpha",
                  {{"value", std::string(ecs::kMaxModDataValueBytes + 1,
                                         'x')}}}};
    save::SavedEntity tooLong;
    error.clear();
    CHECK_FALSE(save::CaptureEntity(source, entity, components, &tooLong,
                                    &error));
    CHECK(error.find("ModData") != std::string::npos);

    data.mods = {{"alpha", {}}};
    for (int i = 0; i < 33; ++i) {
        data.mods[0].second.push_back(ecs::ModDataEntry{
            "k" + std::to_string(i),
            std::string(ecs::kMaxModDataValueBytes, 'x')});
    }
    save::SavedEntity tooLarge;
    error.clear();
    CHECK_FALSE(save::CaptureEntity(source, entity, components, &tooLarge,
                                    &error));
    CHECK(error.find("ModData") != std::string::npos);

    data.mods = validMods;
    const save::SavedComponent malformed = MakeComponent(
        save::kSaveComponentModData, 1, [](save::BinaryWriter& writer) {
            return writer.Write(static_cast<std::uint32_t>(1)) &&
                   writer.WriteString("alpha",
                                      ecs::kMaxModDataModIdBytes) &&
                   writer.Write(static_cast<std::uint32_t>(65));
        });
    save::SavedEntity malformedEntity;
    malformedEntity.stableId = 900;
    malformedEntity.components.push_back(malformed);
    ecs::EcsWorld malformedTarget;
    const ecs::Entity malformedLoaded =
        malformedTarget.CreateWithStableId(malformedEntity.stableId);
    REQUIRE(malformedLoaded != ecs::EcsWorld::kNull);
    error.clear();
    CHECK_FALSE(save::ApplyEntity(malformedEntity, malformedTarget,
                                  malformedLoaded, components, &error));
    CHECK(error.find("ModData") != std::string::npos);
    CHECK_FALSE(malformedTarget.Registry().all_of<ecs::ModData>(
        malformedLoaded));

    source.Registry().remove<ecs::ModData>(entity);
    save::SavedEntity legacy;
    error.clear();
    REQUIRE(save::CaptureEntity(source, entity, components, &legacy, &error));
    CHECK(FindComponent(legacy, save::kSaveComponentModData) == nullptr);
}