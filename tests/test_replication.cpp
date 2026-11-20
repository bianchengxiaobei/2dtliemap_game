#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "game/Companion.h"
#include "game/EntityReplication.h"
#include "game/StructureComponents.h"
#include "game/VehicleComponents.h"
#include "items/ItemComponents.h"
#include "net/Replication.h"

using namespace zl;

namespace {

struct TestActors {
    entt::entity player = entt::null;
    entt::entity zombie = entt::null;
    std::uint64_t playerId = 100;
    std::uint64_t zombieId = 200;
};

TestActors MakeActors(ecs::EcsWorld& world) {
    entt::registry& registry = world.Registry();
    TestActors actors;
    actors.player = world.CreateWithStableId(100);
    registry.emplace<ecs::Position>(actors.player, ecs::Position{1.0f, 2.0f, 0});
    registry.emplace<ecs::Velocity>(actors.player, ecs::Velocity{});
    registry.emplace<ecs::SpriteRef>(actors.player,
                                     ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                                                    0xFFFFFFFFu});
    registry.emplace<ecs::Facing>(actors.player, ecs::Facing{});
    registry.emplace<ecs::AnimState>(actors.player, ecs::AnimState{});
    registry.emplace<ecs::Health>(actors.player, ecs::Health{80.0f, 100.0f});
    registry.emplace<ecs::PlayerTag>(actors.player);
    registry.emplace<ecs::NetworkOwner>(actors.player, ecs::NetworkOwner{100});
    registry.emplace<ecs::ActionState>(actors.player, ecs::ActionState{});
    ecs::Equipment equipment;
    equipment.slots[0] = ecs::ItemInstance{"axe", 1, 0.8f};
    registry.emplace<ecs::Equipment>(actors.player, std::move(equipment));
    ecs::Infection infection;
    infection.stage = ecs::InfectionStage::Incubating;
    infection.progress = 0.2f;
    infection.slowStrength = 0.5f;
    infection.slowRemaining = 20.0f;
    infection.slowDuration = 60.0f;
    infection.timeToNextStage = 30.0f;
    registry.emplace<ecs::Infection>(actors.player, infection);

    actors.zombie = world.CreateWithStableId(200);
    registry.emplace<ecs::Position>(actors.zombie, ecs::Position{3.0f, 4.0f, 0});
    registry.emplace<ecs::Velocity>(actors.zombie, ecs::Velocity{1.0f, 0.0f});
    registry.emplace<ecs::SpriteRef>(actors.zombie,
                                     ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                                                    0xFFFFFFFFu});
    registry.emplace<ecs::Facing>(actors.zombie, ecs::Facing{});
    registry.emplace<ecs::AnimState>(actors.zombie, ecs::AnimState{});
    registry.emplace<ecs::Health>(actors.zombie, ecs::Health{50.0f, 100.0f});
    ecs::ZombieSpecial special;
    special.kind = ecs::ZombieSpecialKind::Breaker;
    special.cooldown = 0.5f;
    special.windup = 0.2f;
    special.windupDuration = 0.9f;
    special.serial = 7;
    registry.emplace<ecs::ZombieSpecial>(actors.zombie, special);
    return actors;
}

net::ReplicationRegistry MakeRegistry() {
    net::ReplicationRegistry registry;
    REQUIRE(game::RegisterEntityReplicationComponents(registry));
    return registry;
}

}  // namespace

TEST_CASE("M13 Replication：全量快照、组件注册表与客户端副本") {
    net::ReplicationRegistry registry;
    REQUIRE(game::RegisterEntityReplicationComponents(registry));
    CHECK(registry.Size() == 33);
    CHECK(registry.Find(game::kReplicationVehicleMods) != nullptr);
    CHECK(registry.Find(game::kReplicationStealth) != nullptr);
    CHECK(registry.Find(game::kReplicationPosition) != nullptr);
    CHECK(registry.Find(game::kReplicationHealth) != nullptr);
    CHECK(registry.Find(game::kReplicationEquipment) != nullptr);
    CHECK(registry.Find(game::kReplicationAction) != nullptr);
    CHECK(registry.Find(game::kReplicationCombatStance) != nullptr);
    CHECK(registry.Find(game::kReplicationVariant) != nullptr);
    CHECK(registry.Find(game::kReplicationMerchant) != nullptr);
    CHECK(registry.Find(game::kReplicationAnimal) != nullptr);
    CHECK(registry.Find(game::kReplicationAnimalCorpse) != nullptr);
    CHECK_FALSE(registry.Register(game::kReplicationPosition, "duplicate", 4));

    ecs::EcsWorld world;
    const TestActors actors = MakeActors(world);
    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(10, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    CHECK(full.full);
    CHECK(full.serverTick == 10);
    CHECK(full.upserts.size() == 2);
    CHECK(producer.BaselineCount() == 2);

    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    CHECK(decoded.full);
    CHECK(decoded.upserts.size() == 2);

    game::EntityReplicationClient client;
    REQUIRE(client.Apply(decoded, &error));
    CHECK(client.LastAppliedTick() == 10);
    CHECK(client.Actors().size() == 2);
    const game::ReplicatedActor* player = client.Find(100);
    REQUIRE(player != nullptr);
    CHECK(player->player);
    CHECK(player->ownerStableId == 100);
    REQUIRE(player->hasPosition);
    CHECK(player->position.x == doctest::Approx(1.0f));
    CHECK(player->position.y == doctest::Approx(2.0f));
    REQUIRE(player->hasHealth);
    CHECK(player->health.current == doctest::Approx(80.0f));
    REQUIRE(player->hasEquipment);
    REQUIRE(player->equipment[0].has_value());
    CHECK(player->equipment[0]->defId == "axe");
    REQUIRE(player->hasAction);
    CHECK(player->action.action == ecs::ActorAction::Idle);
    REQUIRE(player->hasInfection);
    CHECK(player->infection.stage == ecs::InfectionStage::Incubating);
    CHECK(player->infection.progress == doctest::Approx(0.2f));
    CHECK(player->infection.slowRemaining == doctest::Approx(20.0f));

    const game::ReplicatedActor* zombie = client.Find(200);
    REQUIRE(zombie != nullptr);
    CHECK_FALSE(zombie->player);
    CHECK(zombie->ownerStableId == 0);
    CHECK(zombie->hasVelocity);
    CHECK(zombie->velocity.x == doctest::Approx(1.0f));
    REQUIRE(zombie->hasSpecial);
    CHECK(zombie->special.kind == ecs::ZombieSpecialKind::Breaker);
    CHECK(zombie->special.windup == doctest::Approx(0.2f));
    CHECK(zombie->special.windupDuration == doctest::Approx(0.9f));
    CHECK(zombie->special.serial == 7u);
}

TEST_CASE("M34-F1-T5 ZLR1 STLH：1 字节姿态、moving 位与缺失回退") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const TestActors actors = MakeActors(world);
    ecsRegistry.emplace<ecs::StealthState>(
        actors.player,
        ecs::StealthState{true, 0.55f, 0.45f, 0.65f});
    ecsRegistry.get<ecs::Velocity>(actors.player) = ecs::Velocity{1.0f, 0.0f};

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;
    net::ReplicationSnapshot full = producer.BuildFull(10, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const net::ReplicatedEntity* playerEntity = nullptr;
    for (const net::ReplicatedEntity& entity : full.upserts) {
        if (entity.stableId == actors.playerId) {
            playerEntity = &entity;
        }
    }
    REQUIRE(playerEntity != nullptr);
    const net::ReplicatedComponent* stealthComponent =
        playerEntity->Find(game::kReplicationStealth);
    REQUIRE(stealthComponent != nullptr);
    REQUIRE(stealthComponent->data.size() == 1u);
    CHECK(stealthComponent->data[0] == 3u);

    std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    const game::ReplicatedActor* remote = client.Find(actors.playerId);
    REQUIRE(remote != nullptr);
    CHECK(remote->hasStealth);
    CHECK(remote->stealthCrouched);
    CHECK(remote->stealthMoving);
    const game::ReplicatedActor* zombie = client.Find(actors.zombieId);
    REQUIRE(zombie != nullptr);
    CHECK_FALSE(zombie->hasStealth);

    ecsRegistry.get<ecs::Velocity>(actors.player).x = 0.0f;
    ecsRegistry.get<ecs::StealthState>(actors.player).crouched = false;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(11, 0.0f, 0.0f, &delta, &error));
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    remote = client.Find(actors.playerId);
    REQUIRE(remote != nullptr);
    CHECK(remote->hasStealth);
    CHECK_FALSE(remote->stealthCrouched);
    CHECK_FALSE(remote->stealthMoving);
}

TEST_CASE("M13 Replication：增量只发变化组件，AOI 离开发移除，回来重新生成") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    const TestActors actors = MakeActors(world);
    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;

    const net::ReplicationSnapshot full = producer.BuildFull(10, 0.0f, 3.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    auto bytes = net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    entt::registry& ecsRegistry = world.Registry();
    ecsRegistry.get<ecs::Health>(actors.zombie).current = 25.0f;
    ecsRegistry.get<ecs::Position>(actors.player).x = 2.0f;

    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(11, 0.0f, 3.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    CHECK_FALSE(delta.full);
    CHECK(delta.baseTick == 10);
    CHECK(delta.removals.empty());
    REQUIRE(delta.upserts.size() == 2);

    const net::ReplicatedEntity* zombieUpdate = nullptr;
    const net::ReplicatedEntity* playerUpdate = nullptr;
    for (const net::ReplicatedEntity& entity : delta.upserts) {
        if (entity.stableId == actors.zombieId) {
            zombieUpdate = &entity;
        } else if (entity.stableId == actors.playerId) {
            playerUpdate = &entity;
        }
    }
    REQUIRE(zombieUpdate != nullptr);
    REQUIRE(playerUpdate != nullptr);
    REQUIRE(zombieUpdate->components.size() == 1);
    CHECK(zombieUpdate->components[0].type == game::kReplicationHealth);
    REQUIRE(playerUpdate->components.size() == 1);
    CHECK(playerUpdate->components[0].type == game::kReplicationPosition);

    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    CHECK(client.LastAppliedTick() == 11);
    const game::ReplicatedActor* zombie = client.Find(actors.zombieId);
    REQUIRE(zombie != nullptr);
    CHECK(zombie->health.current == doctest::Approx(25.0f));
    const game::ReplicatedActor* player = client.Find(actors.playerId);
    REQUIRE(player != nullptr);
    CHECK(player->position.x == doctest::Approx(2.0f));
    ecs::Position sampled;
    REQUIRE(client.SamplePosition(actors.playerId, 0.5f, &sampled));
    CHECK(sampled.x == doctest::Approx(1.5f));

    ecsRegistry.get<ecs::Position>(actors.zombie).x = 100.0f;
    REQUIRE(producer.BuildDelta(12, 0.0f, 3.0f, &delta, &error));
    CHECK(delta.upserts.empty());
    REQUIRE(delta.removals.size() == 1);
    CHECK(delta.removals[0] == actors.zombieId);
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    CHECK(client.Find(actors.zombieId) == nullptr);

    ecsRegistry.get<ecs::Position>(actors.zombie).x = 3.0f;
    REQUIRE(producer.BuildDelta(13, 0.0f, 3.0f, &delta, &error));
    REQUIRE(delta.upserts.size() == 1);
    CHECK(delta.upserts[0].stableId == actors.zombieId);
    CHECK(delta.upserts[0].components.size() >= 4);
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    CHECK(client.Find(actors.zombieId) != nullptr);

    ecsRegistry.emplace_or_replace<ecs::NetworkOwner>(
        actors.zombie, ecs::NetworkOwner{actors.playerId});
    ecsRegistry.get<ecs::Position>(actors.zombie).x = 3.5f;
    REQUIRE(producer.BuildDelta(14, 0.0f, 3.0f, &delta, &error));
    REQUIRE(delta.upserts.size() == 1);
    CHECK(delta.upserts[0].components.size() >= 4);
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    const game::ReplicatedActor* owned = client.Find(actors.zombieId);
    REQUIRE(owned != nullptr);
    CHECK(owned->ownerStableId == actors.playerId);
    CHECK(owned->position.x == doctest::Approx(3.5f));

    world.Destroy(actors.zombie);
    world.Flush();
    REQUIRE(producer.BuildDelta(15, 0.0f, 3.0f, &delta, &error));
    REQUIRE(delta.removals.size() == 1);
    CHECK(delta.removals[0] == actors.zombieId);
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    CHECK(client.Find(actors.zombieId) == nullptr);
}

TEST_CASE("M13 Replication：错误基线、损坏负载与非法协议被拒") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    const TestActors actors = MakeActors(world);
    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;

    const net::ReplicationSnapshot full = producer.BuildFull(1, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(client.Apply(full, &error));

    net::ReplicationSnapshot badDelta;
    badDelta.full = false;
    badDelta.serverTick = 2;
    badDelta.baseTick = 0;
    CHECK_FALSE(client.Apply(badDelta, &error));
    CHECK(client.NeedsFullSnapshot());

    client.Reset();
    REQUIRE(client.Apply(full, &error));
    net::ReplicationSnapshot badBase = full;
    badBase.full = false;
    badBase.serverTick = 2;
    badBase.baseTick = 0;
    badBase.upserts.clear();
    CHECK_FALSE(client.Apply(badBase, &error));

    net::ReplicationSnapshot badPosition = full;
    badPosition.serverTick = 2;
    REQUIRE_FALSE(badPosition.upserts.empty());
    badPosition.upserts[0].stableId = 100;
    badPosition.upserts[0].components.clear();
    net::ReplicatedComponent position;
    position.type = game::kReplicationPosition;
    position.version = 1;
    position.data.resize(12);
    position.data[0] = 0x00;
    position.data[1] = 0x00;
    position.data[2] = 0xC0;
    position.data[3] = 0x7F;  // NaN
    badPosition.upserts[0].components.push_back(position);
    CHECK_FALSE(client.Apply(badPosition, &error));
    CHECK(client.NeedsFullSnapshot());
    CHECK(client.Find(100) != nullptr);

    net::ReplicationSnapshot duplicate = full;
    duplicate.serverTick = 2;
    duplicate.upserts.push_back(duplicate.upserts[0]);
    CHECK(net::EncodeReplicationSnapshot(duplicate, registry, &error).empty());

    net::ReplicationSnapshot unknown = full;
    unknown.serverTick = 2;
    unknown.upserts[0].components[0].type = 0xDEADBEEFu;
    CHECK(net::EncodeReplicationSnapshot(unknown, registry, &error).empty());

    std::vector<std::uint8_t> encoded =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!encoded.empty(), error);
    encoded[0] ^= 0xFFu;
    net::ReplicationSnapshot decoded;
    CHECK_FALSE(net::DecodeReplicationSnapshot(encoded, registry, &decoded, &error));
    (void)actors;
}

TEST_CASE("M13 Replication：远程位置插值与本地玩家轻量预测/校正") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    const TestActors actors = MakeActors(world);
    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient::Options options;
    options.localSnapDistance = 0.2f;
    options.localCorrectionBlend = 0.25f;
    game::EntityReplicationClient client(options);
    std::string error;

    const net::ReplicationSnapshot full = producer.BuildFull(1, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(client.Apply(full, &error));
    REQUIRE(client.SetLocalPlayer(actors.playerId, 4.0f));

    ecs::Position predicted;
    REQUIRE(client.AdvanceLocalPrediction(0.25f, 1.0f, 0.0f));
    REQUIRE(client.LocalPlayerPosition(&predicted));
    CHECK(predicted.x == doctest::Approx(2.0f));

    entt::registry& ecsRegistry = world.Registry();
    ecsRegistry.get<ecs::Position>(actors.player).x = 0.2f;
    ecsRegistry.get<ecs::Position>(actors.zombie).x = 4.0f;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(2, 0.0f, 0.0f, &delta, &error));
    REQUIRE(client.Apply(delta, &error));
    REQUIRE(client.LocalPlayerPosition(&predicted));
    CHECK(predicted.x == doctest::Approx(0.2f));
    CHECK(client.GetStats().snaps == 1);

    REQUIRE(client.AdvanceLocalPrediction(0.25f, 1.0f, 0.0f));
    REQUIRE(client.LocalPlayerPosition(&predicted));
    CHECK(predicted.x == doctest::Approx(1.2f));

    ecs::Position remote;
    REQUIRE(client.SamplePosition(actors.zombieId, 0.0f, &remote));
    CHECK(remote.x == doctest::Approx(3.0f));
    REQUIRE(client.SamplePosition(actors.zombieId, 0.5f, &remote));
    CHECK(remote.x == doctest::Approx(3.5f));
    REQUIRE(client.SamplePosition(actors.zombieId, 1.0f, &remote));
    CHECK(remote.x == doctest::Approx(4.0f));
}

TEST_CASE("M13 Replication：空 AOI 与无变化增量保持可用") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    MakeActors(world);
    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{0.5f, 16});
    std::string error;
    const net::ReplicationSnapshot full = producer.BuildFull(1, 50.0f, 50.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    CHECK(full.upserts.empty());
    CHECK(producer.BaselineCount() == 0);

    game::EntityReplicationClient client;
    REQUIRE(client.Apply(full, &error));
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(2, 50.0f, 50.0f, &delta, &error));
    CHECK(delta.upserts.empty());
    CHECK(delta.removals.empty());
    REQUIRE(client.Apply(delta, &error));
    CHECK(client.LastAppliedTick() == 2);
}

TEST_CASE("M13-T7 Replication：地面物ItemInstance 与拾取移") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity item = world.CreateWithStableId(300);
    ecsRegistry.emplace<ecs::Position>(item, ecs::Position{1.0f, 2.0f, 0});
    ecsRegistry.emplace<ecs::ItemInstance>(
        item, ecs::ItemInstance{"bandage", 3, 0.75f});
    ecsRegistry.emplace<ecs::SpriteRef>(
        item, ecs::SpriteRef{ecs::HashName("item.bandage"), -1, 0xFFFFFFFFu});

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full = producer.BuildFull(10, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(full.upserts.size() == 1);
    CHECK(full.upserts[0].archetype == game::kReplicationArchetypeItem);
    CHECK(full.upserts[0].ownerStableId == 0);

    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    const game::ReplicatedActor* actor = client.Find(300);
    REQUIRE(actor != nullptr);
    CHECK_FALSE(actor->player);
    CHECK(actor->hasItem);
    CHECK(actor->item.defId == "bandage");
    CHECK(actor->item.count == 3);
    CHECK(actor->item.condition == doctest::Approx(0.75f));

    world.Destroy(item);
    world.Flush();
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(11, 0.0f, 0.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.removals.size() == 1);
    CHECK(delta.removals[0] == 300);
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    CHECK(client.Find(300) == nullptr);
}
TEST_CASE("M13-T10 Replication：装备、动作、投射物与瞬时特") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    const TestActors actors = MakeActors(world);
    entt::registry& ecsRegistry = world.Registry();

    ecsRegistry.get<ecs::Equipment>(actors.player).slots[0]->condition = 0.25f;
    ecs::ActionState& action = ecsRegistry.get<ecs::ActionState>(actors.player);
    action.action = ecs::ActorAction::Attacking;
    action.elapsed = 0.1f;
    action.duration = 0.28f;

    const ecs::Entity effect = world.CreateWithStableId(300);
    ecsRegistry.emplace<ecs::Position>(effect, ecs::Position{2.0f, 2.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        effect, ecs::SpriteRef{ecs::HashName("effect.impact"), 2, 0xFF7040FFu});
    ecsRegistry.emplace<ecs::TransientEffect>(effect, ecs::TransientEffect{0.35f});

    const ecs::Entity projectile = world.CreateWithStableId(400);
    ecsRegistry.emplace<ecs::Position>(projectile, ecs::Position{2.5f, 2.5f, 0});
    ecsRegistry.emplace<ecs::Velocity>(projectile, ecs::Velocity{18.0f, 0.0f});
    ecsRegistry.emplace<ecs::SpriteRef>(
        projectile,
        ecs::SpriteRef{ecs::HashName("projectile.bullet"), 1, 0xFFFFC040u});
    ecsRegistry.emplace<ecs::Projectile>(projectile,
                                        ecs::Projectile{actors.playerId});

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(20, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    auto bytes = net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* player = client.Find(actors.playerId);
    REQUIRE(player != nullptr);
    REQUIRE(player->hasEquipment);
    REQUIRE(player->equipment[0].has_value());
    CHECK(player->equipment[0]->condition == doctest::Approx(0.25f));
    REQUIRE(player->hasAction);
    CHECK(player->action.action == ecs::ActorAction::Attacking);
    CHECK(player->anim.clip == ecs::AnimClip::Attack);  // clip 不进协议，客户端按动作推
    const game::ReplicatedActor* effectActor = client.Find(300);
    REQUIRE(effectActor != nullptr);
    CHECK(effectActor->archetype == game::kReplicationArchetypeEffect);
    const game::ReplicatedActor* projectileActor = client.Find(400);
    REQUIRE(projectileActor != nullptr);
    CHECK(projectileActor->archetype == game::kReplicationArchetypeProjectile);
    CHECK(projectileActor->ownerStableId == actors.playerId);

    ecsRegistry.get<ecs::Equipment>(actors.player).slots[1] =
        ecs::ItemInstance{"jacket", 1, 1.0f};
    action.action = ecs::ActorAction::Hit;
    action.elapsed = 0.0f;
    action.duration = 0.35f;
    world.Destroy(effect);
    world.Flush();

    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(21, 0.0f, 0.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.removals.size() == 1);
    CHECK(delta.removals[0] == 300);
    const net::ReplicatedEntity* playerUpdate = nullptr;
    for (const net::ReplicatedEntity& entity : delta.upserts) {
        if (entity.stableId == actors.playerId) {
            playerUpdate = &entity;
        }
    }
    REQUIRE(playerUpdate != nullptr);
    bool hasEquipment = false;
    bool hasAction = false;
    for (const net::ReplicatedComponent& component : playerUpdate->components) {
        hasEquipment = hasEquipment || component.type == game::kReplicationEquipment;
        hasAction = hasAction || component.type == game::kReplicationAction;
    }
    CHECK(hasEquipment);
    CHECK(hasAction);

    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    player = client.Find(actors.playerId);
    REQUIRE(player != nullptr);
    REQUIRE(player->equipment[1].has_value());
    CHECK(player->equipment[1]->defId == "jacket");
    CHECK(player->action.action == ecs::ActorAction::Hit);
    CHECK(player->anim.clip == ecs::AnimClip::Hit);
    CHECK(client.Find(300) == nullptr);
}

TEST_CASE("M15-T2 Wounds 经 ZLR1 全量/增量复制到客户端") {
    const net::ReplicationRegistry registry = MakeRegistry();
    CHECK(registry.Find(game::kReplicationWounds) != nullptr);

    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity player = world.CreateWithStableId(700);
    ecsRegistry.emplace<ecs::Position>(player, ecs::Position{1.0f, 1.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        player, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                               0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::Health>(player, ecs::Health{80.0f, 100.0f});
    ecsRegistry.emplace<ecs::PlayerTag>(player);
    ecs::Wounds wounds;
    wounds.count = 1;
    wounds.entries[0] =
        ecs::WoundEntry{ecs::DamageType::Bite, 0.4f, 1.2f, 12.5f, 30.0f, false};
    ecsRegistry.emplace<ecs::Wounds>(player, wounds);

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(30, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(700);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasWounds);
    REQUIRE(actor->wounds.count == 1);
    CHECK(actor->wounds.entries[0].source == ecs::DamageType::Bite);
    CHECK(actor->wounds.entries[0].severity == doctest::Approx(0.4f));
    CHECK(actor->wounds.entries[0].bleedRate == doctest::Approx(1.2f));
    CHECK(actor->wounds.entries[0].bleedRemaining == doctest::Approx(12.5f));

    ecsRegistry.get<ecs::Wounds>(player).entries[0].severity = 0.25f;
    ecsRegistry.get<ecs::Wounds>(player).entries[0].bleedRemaining = 4.0f;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(31, 0.0f, 0.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1);
    REQUIRE(delta.upserts[0].components.size() == 1);
    CHECK(delta.upserts[0].components[0].type == game::kReplicationWounds);
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    actor = client.Find(700);
    REQUIRE(actor != nullptr);
    CHECK(actor->wounds.entries[0].severity == doctest::Approx(0.25f));
    CHECK(actor->wounds.entries[0].bleedRemaining == doctest::Approx(4.0f));
}

TEST_CASE("M15-T4 Pain 经 ZLR1 全量/增量复制到客户端") {
    const net::ReplicationRegistry registry = MakeRegistry();
    CHECK(registry.Find(game::kReplicationPain) != nullptr);

    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity player = world.CreateWithStableId(701);
    ecsRegistry.emplace<ecs::Position>(player, ecs::Position{1.0f, 1.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        player, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                               0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::Health>(player, ecs::Health{80.0f, 100.0f});
    ecsRegistry.emplace<ecs::PlayerTag>(player);
    ecs::Pain pain;
    pain.baseLevel = 0.4f;
    pain.level = 0.2f;
    pain.moveScale = 0.93f;
    pain.attackCooldownScale = 1.1f;
    pain.reliefStrength = 0.65f;
    pain.reliefRemaining = 5.0f;
    pain.reliefDuration = 10.0f;
    ecsRegistry.emplace<ecs::Pain>(player, pain);

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 16});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(40, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(701);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasPain);
    CHECK(actor->pain.level == doctest::Approx(0.2f));
    CHECK(actor->pain.moveScale == doctest::Approx(0.93f));
    CHECK(actor->pain.attackCooldownScale == doctest::Approx(1.1f));
    CHECK(actor->pain.reliefRemaining == doctest::Approx(5.0f));

    auto& changed = ecsRegistry.get<ecs::Pain>(player);
    changed.level = 0.0f;
    changed.moveScale = 1.0f;
    changed.attackCooldownScale = 1.0f;
    changed.reliefRemaining = 4.0f;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(41, 0.0f, 0.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1);
    REQUIRE(delta.upserts[0].components.size() == 1);
    CHECK(delta.upserts[0].components[0].type == game::kReplicationPain);
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));
    actor = client.Find(701);
    REQUIRE(actor != nullptr);
    CHECK(actor->pain.level == doctest::Approx(0.0f));
    CHECK(actor->pain.moveScale == doctest::Approx(1.0f));
    CHECK(actor->pain.reliefRemaining == doctest::Approx(4.0f));
}

TEST_CASE("X-F1 2b-2 仓库内容通过 ZLR1 全量/增量复制") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity storage = world.CreateWithStableId(730);
    ecsRegistry.emplace<ecs::Position>(storage, ecs::Position{4.0f, 5.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        storage,
        ecs::SpriteRef{ecs::HashName("object.crate"), 0, 0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::PlayerStorage>(storage, ecs::PlayerStorage{2});
    ecsRegistry.emplace<ecs::Container>(
        storage,
        ecs::Container{{ecs::ItemInstance{"bandage", 2, 0.75f}}, 50.0f});
    ecsRegistry.emplace<ecs::StructureOwner>(
        storage, ecs::StructureOwner{100});

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 8});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(1, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(730);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasStorage);
    CHECK(actor->storage.maxStacks == 2);
    CHECK(actor->storageContainer.capacityKg == doctest::Approx(50.0f));
    REQUIRE(actor->storageContainer.items.size() == 1);
    CHECK(actor->storageContainer.items[0].defId == "bandage");
    CHECK(actor->storageContainer.items[0].count == 2);
    CHECK(actor->storageContainer.items[0].condition ==
          doctest::Approx(0.75f));

    ecsRegistry.get<ecs::Container>(storage).items.push_back(
        ecs::ItemInstance{"axe", 1, 1.0f});
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(2, 0.0f, 0.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1);
    REQUIRE(delta.upserts[0].components.size() == 1);
    CHECK(delta.upserts[0].components[0].type == game::kReplicationStorage);
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &delta, &error));
    REQUIRE(client.Apply(delta, &error));
    actor = client.Find(730);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->storageContainer.items.size() == 2);
    CHECK(actor->storageContainer.items[1].defId == "axe");

    game::EntityReplicationProducer::Options hiddenOptions;
    hiddenOptions.aoiRadius = 10.0f;
    hiddenOptions.maxEntities = 8;
    hiddenOptions.viewerStableId = 200;
    game::EntityReplicationProducer hiddenProducer(world, registry,
                                                   hiddenOptions);
    const net::ReplicationSnapshot hiddenFull =
        hiddenProducer.BuildFull(3, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> hiddenBytes =
        net::EncodeReplicationSnapshot(hiddenFull, registry, &error);
    REQUIRE_MESSAGE(!hiddenBytes.empty(), error);
    net::ReplicationSnapshot hiddenDecoded;
    REQUIRE(net::DecodeReplicationSnapshot(hiddenBytes, registry,
                                           &hiddenDecoded, &error));
    game::EntityReplicationClient hiddenClient;
    REQUIRE(hiddenClient.Apply(hiddenDecoded, &error));
    const game::ReplicatedActor* hiddenActor = hiddenClient.Find(730);
    REQUIRE(hiddenActor != nullptr);
    CHECK_FALSE(hiddenActor->hasStorage);
}

TEST_CASE("M21-F2b ZLR1：发电机负载、冷藏柜和泛光灯供电状态复制") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity generator = world.CreateWithStableId(810);
    ecsRegistry.emplace<ecs::Position>(generator,
                                       ecs::Position{2.0f, 3.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        generator,
        ecs::SpriteRef{ecs::HashName("object.generator"), 0, 0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::Generator>(
        generator, ecs::Generator{320.0f, 8.0f, true, 2.0f});

    const ecs::Entity floodlight = world.CreateWithStableId(811);
    ecsRegistry.emplace<ecs::Position>(floodlight,
                                       ecs::Position{3.0f, 3.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        floodlight,
        ecs::SpriteRef{ecs::HashName("object.floodlight"), 0, 0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::Floodlight>(
        floodlight, ecs::Floodlight{true, true});
    ecsRegistry.emplace<ecs::Health>(floodlight, ecs::Health{42.0f, 80.0f});

    const ecs::Entity fridge = world.CreateWithStableId(812);
    ecsRegistry.emplace<ecs::Position>(fridge, ecs::Position{4.0f, 3.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        fridge, ecs::SpriteRef{ecs::HashName("object.fridge"), 0, 0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::ColdStorage>(fridge, ecs::ColdStorage{true});

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 8});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(1, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(810);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasGenerator);
    CHECK(actor->generator.fuelMinutes == doctest::Approx(320.0f));
    CHECK(actor->generator.running);
    CHECK(actor->generator.load == doctest::Approx(2.0f));
    const game::ReplicatedActor* light = client.Find(811);
    REQUIRE(light != nullptr);
    REQUIRE(light->hasFloodlight);
    CHECK(light->floodlight.on);
    CHECK(light->floodlight.powered);
    REQUIRE(light->hasHealth);
    CHECK(light->health.current == doctest::Approx(42.0f));
    CHECK(light->health.max == doctest::Approx(80.0f));
    const game::ReplicatedActor* cold = client.Find(812);
    REQUIRE(cold != nullptr);
    REQUIRE(cold->hasColdStorage);
    CHECK(cold->coldStorage.powered);

    ecsRegistry.get<ecs::Generator>(generator).load = 3.5f;
    ecsRegistry.get<ecs::Floodlight>(floodlight).on = false;
    ecsRegistry.get<ecs::Floodlight>(floodlight).powered = false;
    ecsRegistry.get<ecs::Health>(floodlight).current = 17.0f;
    ecsRegistry.get<ecs::ColdStorage>(fridge).powered = false;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(2, 0.0f, 0.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    net::ReplicationSnapshot decodedDelta;
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &decodedDelta,
                                           &error));
    REQUIRE(client.Apply(decodedDelta, &error));

    actor = client.Find(810);
    REQUIRE(actor != nullptr);
    CHECK(actor->generator.load == doctest::Approx(3.5f));
    light = client.Find(811);
    REQUIRE(light != nullptr);
    CHECK_FALSE(light->floodlight.on);
    CHECK_FALSE(light->floodlight.powered);
    REQUIRE(light->hasHealth);
    CHECK(light->health.current == doctest::Approx(17.0f));
    cold = client.Find(812);
    REQUIRE(cold != nullptr);
    CHECK_FALSE(cold->coldStorage.powered);
}

TEST_CASE("X-F3 复制审计：农场/陷阱/商人状态完整往返") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();

    const ecs::Entity farm = world.CreateWithStableId(820);
    ecsRegistry.emplace<ecs::Position>(farm, ecs::Position{3.0f, 4.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        farm, ecs::SpriteRef{ecs::HashName("object.farm_plot"), 0, 0xFFFFFFFFu});
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
    ecsRegistry.emplace<ecs::FarmPlot>(farm, plot);

    const ecs::Entity trap = world.CreateWithStableId(821);
    ecsRegistry.emplace<ecs::Position>(trap, ecs::Position{4.0f, 4.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        trap, ecs::SpriteRef{ecs::HashName("object.trap"), 0, 0xFFFFFFFFu});
    ecs::SpikeTrap spike;
    spike.armed = true;
    spike.cooldown = 0.2f;
    spike.cooldownSeconds = 1.5f;
    spike.damage = 45.0f;
    spike.radius = 1.0f;
    spike.staggerSeconds = 0.35f;
    spike.uses = 4;
    ecsRegistry.emplace<ecs::SpikeTrap>(trap, spike);
    ecsRegistry.emplace<ecs::PlacedTrap>(
        trap, ecs::PlacedTrap{"bear_trap", true, 1.25f, 3, 0x123456789ull});

    const ecs::Entity merchant = world.CreateWithStableId(822);
    ecsRegistry.emplace<ecs::Position>(merchant, ecs::Position{5.0f, 4.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        merchant,
        ecs::SpriteRef{ecs::HashName("object.merchant"), 0, 0xFFFFFFFFu});
    ecs::MerchantStock stock;
    stock.stock = {3, 1, 0, 2, 1};
    stock.restockDay = 6;
    ecsRegistry.emplace<ecs::MerchantStock>(merchant, stock);
    ecsRegistry.emplace<game::SettlementNpc>(
        merchant, game::SettlementNpc{"village_haven",
                                      game::SettlementNpcKind::Merchant, 5.0f,
                                      4.0f, 1.5f,
                                      game::NpcActivity::Standing});

    const ecs::Entity requester = world.CreateWithStableId(824);
    ecsRegistry.emplace<ecs::Position>(requester,
                                       ecs::Position{7.0f, 4.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        requester,
        ecs::SpriteRef{ecs::HashName("character.placeholder"), 0, 0xFFFFFFFFu});
    ecsRegistry.emplace<game::SettlementNpc>(
        requester, game::SettlementNpc{"village_haven",
                                       game::SettlementNpcKind::Requester, 7.0f,
                                       4.0f, 1.5f,
                                       game::NpcActivity::Standing});

    const ecs::Entity variant = world.CreateWithStableId(823);
    ecsRegistry.emplace<ecs::Position>(variant, ecs::Position{6.0f, 4.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        variant,
        ecs::SpriteRef{ecs::HashName("character.placeholder"), 0, 0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::Health>(variant, ecs::Health{90.0f, 120.0f});
    ecsRegistry.emplace<ecs::ZombieVariant>(
        variant,
        ecs::ZombieVariant{ecs::ZombieVariantKind::Armored, 1.4f, 1.6f, 1.2f});

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 8});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(1, 0.0f, 0.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(full.upserts.size() == 5u);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* farmActor = client.Find(820);
    REQUIRE(farmActor != nullptr);
    REQUIRE(farmActor->hasFarmPlot);
    CHECK(farmActor->farmPlot.cropId == "cabbage");
    CHECK(farmActor->farmPlot.growthMinutes == doctest::Approx(120.0f));
    CHECK(farmActor->farmPlot.waterMinutes == doctest::Approx(45.0f));
    CHECK(farmActor->farmPlot.dryMinutes == doctest::Approx(3.0f));
    CHECK(farmActor->farmPlot.growMinutes == doctest::Approx(240.0f));
    CHECK(farmActor->farmPlot.stage == ecs::CropStage::Growing);
    CHECK(farmActor->farmPlot.diseased);
    CHECK(farmActor->farmPlot.fertilized);
    CHECK(farmActor->farmPlot.harvestYield == 2);

    const game::ReplicatedActor* trapActor = client.Find(821);
    REQUIRE(trapActor != nullptr);
    REQUIRE(trapActor->hasTrap);
    CHECK(trapActor->trap.armed);
    CHECK(trapActor->trap.cooldown == doctest::Approx(0.2f));
    CHECK(trapActor->trap.cooldownSeconds == doctest::Approx(1.5f));
    CHECK(trapActor->trap.damage == doctest::Approx(45.0f));
    CHECK(trapActor->trap.radius == doctest::Approx(1.0f));
    CHECK(trapActor->trap.staggerSeconds == doctest::Approx(0.35f));
    CHECK(trapActor->trap.uses == 4);
    REQUIRE(trapActor->hasPlacedTrap);
    CHECK(trapActor->placedTrap.defId == "bear_trap");
    CHECK(trapActor->placedTrap.armed);
    CHECK(trapActor->placedTrap.cooldown == doctest::Approx(1.25f));
    CHECK(trapActor->placedTrap.uses == 3);
    CHECK(trapActor->placedTrap.owner == 0x123456789ull);
    CHECK(trapActor->ownerStableId == 0x123456789ull);

    const game::ReplicatedActor* merchantActor = client.Find(822);
    REQUIRE(merchantActor != nullptr);
    REQUIRE(merchantActor->hasMerchantStock);
    CHECK((merchantActor->merchantStock.stock == std::array<int, 5>{3, 1, 0, 2, 1}));
    CHECK(merchantActor->merchantStock.restockDay == 6);
    REQUIRE(merchantActor->hasSettlementNpc);
    CHECK(merchantActor->settlementNpc.settlementId == "village_haven");
    CHECK(merchantActor->settlementNpc.kind ==
          game::SettlementNpcKind::Merchant);

    const game::ReplicatedActor* requesterActor = client.Find(824);
    REQUIRE(requesterActor != nullptr);
    REQUIRE(requesterActor->hasSettlementNpc);
    CHECK(requesterActor->settlementNpc.settlementId == "village_haven");
    CHECK(requesterActor->settlementNpc.kind ==
          game::SettlementNpcKind::Requester);
    CHECK(requesterActor->settlementNpc.activity ==
          game::NpcActivity::Standing);

    const game::ReplicatedActor* variantActor = client.Find(823);
    REQUIRE(variantActor != nullptr);
    REQUIRE(variantActor->hasVariant);
    CHECK(variantActor->variant.kind == ecs::ZombieVariantKind::Armored);
    CHECK(variantActor->variant.speedScale == doctest::Approx(1.4f));
    CHECK(variantActor->variant.damageScale == doctest::Approx(1.6f));
    CHECK(variantActor->variant.visualScale == doctest::Approx(1.2f));
}
TEST_CASE("M19 LootContainer 复制：表、搜索状态与内容往返") {
    net::ReplicationRegistry registry;
    REQUIRE(game::RegisterEntityReplicationComponents(registry));
    ecs::EcsWorld world;
    entt::registry& entities = world.Registry();
    const entt::entity container = world.CreateWithStableId(900);
    entities.emplace<ecs::Position>(container, ecs::Position{4.0f, 2.0f, 0});
    entities.emplace<ecs::SpriteRef>(
        container,
        ecs::SpriteRef{ecs::HashName("object.crate"), 0, 0xFFFFFFFFu});
    entities.emplace<ecs::LootContainer>(
        container, ecs::LootContainer{"poi_gas_station", 0, true});
    ecs::Container contents;
    contents.capacityKg = 22.0f;
    contents.items.push_back(ecs::ItemInstance{"bandage", 2, 0.75f});
    for (int i = 0; i < 7; ++i) {
        contents.items.push_back(ecs::ItemInstance{"rag", 1, 1.0f});
    }
    entities.emplace<ecs::Container>(container, std::move(contents));

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{32.0f, 32});
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(10, 4.0f, 2.0f, &error);
    REQUIRE(error.empty());
    std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE(!bytes.empty());
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    game::EntityReplicationClient client;
    REQUIRE(client.Apply(decoded, &error));
    const game::ReplicatedActor* actor = client.Find(900);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasLoot);
    CHECK(actor->lootContainer.tableId == "poi_gas_station");
    CHECK(actor->lootContainer.searched);
    REQUIRE(actor->lootContainerItems.items.size() == 8u);
    CHECK(actor->lootContainerItems.items[0].defId == "bandage");
    CHECK(actor->lootContainerItems.items[0].count == 2);
    CHECK(actor->lootContainerItems.items[7].defId == "rag");
    CHECK(actor->lootContainerItems.capacityKg == doctest::Approx(22.0f));
}


TEST_CASE("M23-F3-T6 Replication：无 Sprite 火源与燃烧状态") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& entities = world.Registry();

    const ecs::Entity fire = world.CreateWithStableId(300);
    entities.emplace<ecs::Position>(fire, ecs::Position{10.5f, 11.5f, 0});
    entities.emplace<ecs::FireAnchor>(fire, ecs::FireAnchor{10, 11, 0});
    ecs::FireSource source;
    source.materialId = "wood_floor";
    source.phase = ecs::FirePhase::Burning;
    source.ignitionProgress = 1.5f;
    source.ignitionDuration = 1.5f;
    source.burnRemaining = 30.0f;
    source.burnDuration = 45.0f;
    source.smokePerSecond = 0.6f;
    source.heatRadius = 2.0f;
    source.spreadAccumulator = 1.0f;
    entities.emplace<ecs::FireSource>(fire, source);

    const ecs::Entity zombie = world.CreateWithStableId(301);
    entities.emplace<ecs::Position>(zombie, ecs::Position{11.5f, 11.5f, 0});
    entities.emplace<ecs::SpriteRef>(
        zombie, ecs::SpriteRef{ecs::HashName("character.zombie"), 0,
                               0xFFFFFFFFu});
    entities.emplace<ecs::Health>(zombie, ecs::Health{60.0f, 100.0f});
    ecs::BurningZombie burning;
    burning.remaining = 5.5f;
    burning.damageAccumulator = 0.25f;
    burning.spreadAccumulator = 0.75f;
    burning.panicRemaining = 3.5f;
    burning.panicSpeedScale = 1.25f;
    burning.panicChaseRadius = 12.0f;
    entities.emplace<ecs::BurningZombie>(zombie, burning);

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{32.0f, 32});
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(10, 10.0f, 10.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(full.upserts.size() == 2u);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    game::EntityReplicationClient client;
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* fireActor = client.Find(300);
    REQUIRE(fireActor != nullptr);
    CHECK(fireActor->archetype == game::kReplicationArchetypeProp);
    REQUIRE(fireActor->hasFire);
    CHECK(fireActor->fireAnchor.x == 10);
    CHECK(fireActor->fireAnchor.y == 11);
    CHECK(fireActor->fireSource.materialId == "wood_floor");
    CHECK(fireActor->fireSource.phase == ecs::FirePhase::Burning);
    CHECK(fireActor->fireSource.burnRemaining == doctest::Approx(30.0f));

    const game::ReplicatedActor* zombieActor = client.Find(301);
    REQUIRE(zombieActor != nullptr);
    REQUIRE(zombieActor->hasBurning);
    CHECK(zombieActor->burning.remaining == doctest::Approx(5.5f));
    CHECK(zombieActor->burning.damageAccumulator == doctest::Approx(0.25f));
    CHECK(zombieActor->burning.panicRemaining == doctest::Approx(3.5f));
    CHECK(zombieActor->burning.panicChaseRadius == doctest::Approx(12.0f));

    entities.get<ecs::FireSource>(fire).phase = ecs::FirePhase::Extinguished;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(11, 10.0f, 10.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1u);
    CHECK(delta.upserts[0].stableId == 300u);
}

TEST_CASE("M25-T3-V4 ZLR1：载具燃料/车况/驾驶员与后备箱复制") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity vehicle = world.CreateWithStableId(950);
    ecsRegistry.emplace<ecs::Position>(vehicle, ecs::Position{12.5f, 8.5f, 0});
    ecsRegistry.emplace<ecs::Velocity>(vehicle, ecs::Velocity{1.5f, -0.5f});
    ecsRegistry.emplace<ecs::SpriteRef>(
        vehicle,
        ecs::SpriteRef{ecs::HashName("object.vehicle"), 0, 0xFFFFFFFFu});
    ecs::Vehicle initialVehicleState;
    initialVehicleState.fuel = 0.42f;
    initialVehicleState.condition = 0.77f;
    initialVehicleState.headlights = true;
    initialVehicleState.type = ecs::VehicleType::HeavyPickup;
    initialVehicleState.heading = 1.0f;
    ecsRegistry.emplace<ecs::Vehicle>(vehicle, initialVehicleState);
    ecsRegistry.emplace<ecs::VehicleDriver>(vehicle,
                                            ecs::VehicleDriver{123});
    ecsRegistry.emplace<ecs::VehiclePassenger>(
        vehicle, ecs::VehiclePassenger{456, 789});
    ecsRegistry.emplace<ecs::PlayerStorage>(vehicle, ecs::PlayerStorage{6});
    ecsRegistry.emplace<ecs::Container>(
        vehicle, ecs::Container{{ecs::ItemInstance{"fuel_can", 2, 0.9f}},
                                40.0f});

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{32.0f, 8});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(20, 10.0f, 10.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(950);
    REQUIRE(actor != nullptr);
    CHECK(actor->archetype == game::kReplicationArchetypeProp);
    REQUIRE(actor->hasVehicle);
    CHECK(actor->vehicle.fuel == doctest::Approx(0.42f));
    CHECK(actor->vehicle.condition == doctest::Approx(0.77f));
    CHECK(actor->vehicle.headlights);
    CHECK(actor->vehicle.type == ecs::VehicleType::HeavyPickup);
    CHECK(actor->vehicle.heading == doctest::Approx(1.0f));
    CHECK(actor->vehicleDriverStableId == 123u);
    CHECK(actor->vehiclePassengerStableId == 456u);
    CHECK(actor->vehicleSecondPassengerStableId == 789u);
    REQUIRE(actor->hasStorage);
    REQUIRE(actor->storageContainer.items.size() == 1u);
    CHECK(actor->storageContainer.items[0].defId == "fuel_can");

    auto& vehicleState = ecsRegistry.get<ecs::Vehicle>(vehicle);
    vehicleState.fuel = 0.15f;
    vehicleState.condition = 0.33f;
    vehicleState.headlights = false;
    vehicleState.type = ecs::VehicleType::LightPickup;
    vehicleState.heading = -0.5f;
    ecsRegistry.get<ecs::VehicleDriver>(vehicle).stableId = 0;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(21, 10.0f, 10.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1u);
    REQUIRE(delta.upserts[0].components.size() == 1u);
    CHECK(delta.upserts[0].components[0].type == game::kReplicationVehicle);
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &delta, &error));
    REQUIRE(client.Apply(delta, &error));

    actor = client.Find(950);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasVehicle);
    CHECK(actor->vehicle.fuel == doctest::Approx(0.15f));
    CHECK(actor->vehicle.condition == doctest::Approx(0.33f));
    CHECK_FALSE(actor->vehicle.headlights);
    CHECK(actor->vehicle.type == ecs::VehicleType::LightPickup);
    CHECK(actor->vehicle.heading == doctest::Approx(-0.5f));
    CHECK(actor->vehicleDriverStableId == 0u);
    CHECK(actor->vehiclePassengerStableId == 456u);
    CHECK(actor->vehicleSecondPassengerStableId == 789u);
    REQUIRE(actor->hasStorage);
    CHECK(actor->storageContainer.items.size() == 1u);
}

TEST_CASE("M30-T9 CombatStance 经 ZLR1 全量/增量复制到客户端") {
    const net::ReplicationRegistry registry = MakeRegistry();
    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity player = world.CreateWithStableId(980);
    ecsRegistry.emplace<ecs::Position>(player, ecs::Position{5.0f, 6.0f, 0});
    ecsRegistry.emplace<ecs::SpriteRef>(
        player, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                               0xFFFFFFFFu});
    ecsRegistry.emplace<ecs::Health>(player, ecs::Health{100.0f, 100.0f});
    ecsRegistry.emplace<ecs::PlayerTag>(player);
    ecs::CombatStance stance;
    stance.blocking = true;
    stance.blockElapsed = 0.10f;
    stance.guardBreakLeft = 0.30f;
    ecsRegistry.emplace<ecs::CombatStance>(player, stance);

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{10.0f, 8});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(50, 5.0f, 6.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    const std::vector<std::uint8_t> bytes =
        net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(980);
    REQUIRE(actor != nullptr);
    REQUIRE(actor->hasCombatStance);
    CHECK(actor->combatStance.blocking);
    CHECK(actor->combatStance.blockElapsed == doctest::Approx(0.10f));
    CHECK(actor->combatStance.guardBreakLeft == doctest::Approx(0.30f));

    auto& changed = ecsRegistry.get<ecs::CombatStance>(player);
    changed.blocking = false;
    changed.blockElapsed = 0.0f;
    changed.guardBreakLeft = 0.0f;
    changed.dodgeLeft = 0.28f;
    changed.dodgeInvulnLeft = 0.18f;
    changed.dodgeDirX = 0.6f;
    changed.dodgeDirY = 0.8f;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(51, 5.0f, 6.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1u);
    REQUIRE(delta.upserts[0].components.size() == 1u);
    CHECK(delta.upserts[0].components[0].type ==
          game::kReplicationCombatStance);
    const std::vector<std::uint8_t> deltaBytes =
        net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!deltaBytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(deltaBytes, registry, &delta,
                                           &error));
    REQUIRE(client.Apply(delta, &error));

    actor = client.Find(980);
    REQUIRE(actor != nullptr);
    CHECK_FALSE(actor->combatStance.blocking);
    CHECK(actor->combatStance.dodgeLeft == doctest::Approx(0.28f));
    CHECK(actor->combatStance.dodgeInvulnLeft == doctest::Approx(0.18f));
    CHECK(actor->combatStance.dodgeDirX == doctest::Approx(0.6f));
    CHECK(actor->combatStance.dodgeDirY == doctest::Approx(0.8f));

    changed.dodgeDirX = 0.0f;
    changed.dodgeDirY = 0.0f;
    const net::ReplicationSnapshot invalid =
        producer.BuildFull(52, 5.0f, 6.0f, &error);
    CHECK(invalid.upserts.empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("M33-F3-T6 同伴复制 defId/owner/信任/委托/装备摘要与旧负载") {
    const net::ReplicationRegistry registry = MakeRegistry();
    CHECK(registry.Find(game::kReplicationCompanion) != nullptr);

    ecs::EcsWorld world;
    entt::registry& ecsRegistry = world.Registry();
    const ecs::Entity companion = world.CreateWithStableId(600);
    ecsRegistry.emplace<ecs::Position>(companion,
                                       ecs::Position{10.0f, 12.0f, 0});
    ecsRegistry.emplace<ecs::Velocity>(companion, ecs::Velocity{});
    ecsRegistry.emplace<ecs::SpriteRef>(
        companion, ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                                  0xFF9AD8B0u});
    ecsRegistry.emplace<ecs::Facing>(companion, ecs::Facing{});
    ecsRegistry.emplace<ecs::AnimState>(companion, ecs::AnimState{});
    ecsRegistry.emplace<ecs::Health>(companion, ecs::Health{72.0f, 95.0f});
    ecsRegistry.emplace<ecs::FriendlyNpc>(companion);
    ecsRegistry.emplace<ecs::NoCombatTarget>(companion);
    ecsRegistry.emplace<game::Companion>(
        companion, game::Companion{"companion_scout_amo", 500,
                                   "south_farm"});
    game::CompanionState state;
    state.loyalty = 73;
    state.command = game::CompanionCommand::Stay;
    state.standby = true;
    state.personalStatus = game::CompanionQuestStatus::Active;
    state.personalStage = 1;
    state.personalProgress = 2;
    state.personalRewardMask = 1;
    state.equipment.slots[0] =
        ecs::ItemInstance{"kitchen_knife", 1, 0.5f, 0};
    state.equipment.slots[1] =
        ecs::ItemInstance{"jacket", 1, 0.75f, 0};
    ecsRegistry.emplace<game::CompanionState>(companion, state);

    game::EntityReplicationProducer producer(
        world, registry, game::EntityReplicationProducer::Options{16.0f, 8});
    game::EntityReplicationClient client;
    std::string error;
    const net::ReplicationSnapshot full =
        producer.BuildFull(60, 10.0f, 12.0f, &error);
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(full.upserts.size() == 1u);

    auto bytes = net::EncodeReplicationSnapshot(full, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot decoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &decoded, &error));
    REQUIRE(client.Apply(decoded, &error));

    const game::ReplicatedActor* actor = client.Find(600);
    REQUIRE(actor != nullptr);
    CHECK(actor->ownerStableId == 500u);
    CHECK_FALSE(actor->player);
    REQUIRE(actor->hasCompanion);
    CHECK(actor->companion.defId == "companion_scout_amo");
    CHECK(actor->companion.command == game::CompanionCommand::Stay);
    CHECK(actor->companion.standby);
    CHECK_FALSE(actor->companion.downed);
    CHECK_FALSE(actor->companion.dead);
    CHECK(actor->companion.loyalty == 73);
    CHECK(actor->companion.questStatus ==
          game::CompanionQuestStatus::Active);
    CHECK(actor->companion.questStage == 1);
    CHECK(actor->companion.questProgress == 2);
    CHECK(actor->companion.rewardMask == 1u);
    REQUIRE(actor->hasHealth);
    CHECK(actor->health.current == doctest::Approx(72.0f));
    REQUIRE(actor->hasEquipment);
    REQUIRE(actor->equipment[0].has_value());
    CHECK(actor->equipment[0]->defId == "kitchen_knife");
    REQUIRE(actor->equipment[1].has_value());
    CHECK(actor->equipment[1]->defId == "jacket");

    state.loyalty = 74;
    state.personalProgress = 3;
    ecsRegistry.get<game::CompanionState>(companion) = state;
    net::ReplicationSnapshot delta;
    REQUIRE(producer.BuildDelta(61, 10.0f, 12.0f, &delta, &error));
    REQUIRE_MESSAGE(error.empty(), error);
    REQUIRE(delta.upserts.size() == 1u);
    REQUIRE(delta.upserts[0].components.size() == 1u);
    CHECK(delta.upserts[0].components[0].type ==
          game::kReplicationCompanion);
    bytes = net::EncodeReplicationSnapshot(delta, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &delta, &error));
    REQUIRE(client.Apply(delta, &error));
    actor = client.Find(600);
    REQUIRE(actor != nullptr);
    CHECK(actor->companion.loyalty == 74);
    CHECK(actor->companion.questProgress == 3);

    // 旧 ZLR1 负载没有 CMPN 组件：客户端仍按普通角色显示，不猜造同伴状态。
    net::ReplicationSnapshot legacy = full;
    for (net::ReplicatedEntity& entity : legacy.upserts) {
        auto& components = entity.components;
        components.erase(
            std::remove_if(
                components.begin(), components.end(),
                [](const net::ReplicatedComponent& component) {
                    return component.type == game::kReplicationCompanion;
                }),
            components.end());
    }
    game::EntityReplicationClient legacyClient;
    bytes = net::EncodeReplicationSnapshot(legacy, registry, &error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    net::ReplicationSnapshot legacyDecoded;
    REQUIRE(net::DecodeReplicationSnapshot(bytes, registry, &legacyDecoded,
                                           &error));
    REQUIRE(legacyClient.Apply(legacyDecoded, &error));
    const game::ReplicatedActor* legacyActor = legacyClient.Find(600);
    REQUIRE(legacyActor != nullptr);
    CHECK_FALSE(legacyActor->hasCompanion);
    CHECK(legacyActor->ownerStableId == 500u);
    REQUIRE(legacyActor->hasHealth);
    CHECK(legacyActor->health.current == doctest::Approx(72.0f));
    REQUIRE(legacyActor->hasEquipment);
    CHECK(legacyActor->equipment[0]->defId == "kitchen_knife");
}
