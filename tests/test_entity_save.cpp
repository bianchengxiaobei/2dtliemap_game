#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "items/ItemComponents.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using namespace zl;

namespace {

save::ComponentRegistry MakeRegistry() {
    save::ComponentRegistry registry;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(registry, &error));
    CHECK(error.empty());
    CHECK(registry.Size() == 11u);
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
    registry.emplace<ecs::PlayerTag>(entity);

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

TEST_CASE("M10-T3 玩家与物品栏按 StableId 往返") {
    const save::ComponentRegistry components = MakeRegistry();

    ecs::EcsWorld source;
    const ecs::Entity player = source.Create();
    AddPlayerComponents(source, player);

    std::string error;
    save::SavedEntity saved;
    REQUIRE(save::CapturePlayer(source, components, &saved, &error));
    CHECK(error.empty());
    CHECK(saved.stableId == source.IdOf(player).id);
    CHECK(saved.components.size() == 9u);
    CHECK(FindComponent(saved, save::kSaveComponentItem) == nullptr);
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
    const auto* facing = registry.try_get<ecs::Facing>(loaded);
    REQUIRE(facing != nullptr);
    CHECK(facing->dir == ecs::Direction::Right);

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
    CHECK(savedPlayer->components.size() == 9u);
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

TEST_CASE("M10-T3 未知组件按段跳过，已知组件继续应用") {
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

TEST_CASE("M10-T3 CapturePlayer 要求唯一的 PlayerTag") {
    const save::ComponentRegistry components = MakeRegistry();
    std::string error;
    save::SavedEntity saved;

    ecs::EcsWorld empty;
    CHECK_FALSE(save::CapturePlayer(empty, components, &saved, &error));
    CHECK(error.find("未找到") != std::string::npos);

    ecs::EcsWorld multiple;
    multiple.Registry().emplace<ecs::PlayerTag>(multiple.Create());
    multiple.Registry().emplace<ecs::PlayerTag>(multiple.Create());
    CHECK_FALSE(save::CapturePlayer(multiple, components, &saved, &error));
    CHECK(error.find("数量") != std::string::npos);
}

TEST_CASE("M10-T3 实体快照拒绝重复 ID、版本不兼容与非法组件") {
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

    SUBCASE("组件版本不兼容") {
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

TEST_CASE("M10-T3 LoadEntities 冲突时回滚本次创建") {
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
