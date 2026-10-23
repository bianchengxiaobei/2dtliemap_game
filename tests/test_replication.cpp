#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/EcsWorld.h"
#include "game/EntityReplication.h"
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

    actors.zombie = world.CreateWithStableId(200);
    registry.emplace<ecs::Position>(actors.zombie, ecs::Position{3.0f, 4.0f, 0});
    registry.emplace<ecs::Velocity>(actors.zombie, ecs::Velocity{1.0f, 0.0f});
    registry.emplace<ecs::SpriteRef>(actors.zombie,
                                     ecs::SpriteRef{ecs::HashName("character.placeholder"), 0,
                                                    0xFFFFFFFFu});
    registry.emplace<ecs::Facing>(actors.zombie, ecs::Facing{});
    registry.emplace<ecs::AnimState>(actors.zombie, ecs::AnimState{});
    registry.emplace<ecs::Health>(actors.zombie, ecs::Health{50.0f, 100.0f});
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
    CHECK(registry.Size() == 10);
    CHECK(registry.Find(game::kReplicationPosition) != nullptr);
    CHECK(registry.Find(game::kReplicationHealth) != nullptr);
    CHECK(registry.Find(game::kReplicationEquipment) != nullptr);
    CHECK(registry.Find(game::kReplicationAction) != nullptr);
    CHECK(registry.Find(game::kReplicationVariant) != nullptr);
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

    const game::ReplicatedActor* zombie = client.Find(200);
    REQUIRE(zombie != nullptr);
    CHECK_FALSE(zombie->player);
    CHECK(zombie->ownerStableId == 0);
    CHECK(zombie->hasVelocity);
    CHECK(zombie->velocity.x == doctest::Approx(1.0f));
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