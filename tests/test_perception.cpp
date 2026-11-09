#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <unordered_set>

#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "characters/components/CharacterComponents.h"
#include "core/EventBus.h"
#include "core/SoundEvent.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/RangedSound.h"
#include "items/ItemDef.h"

using namespace zl;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// 感知测试场：60×60 可走平地 + 可变墙体集合（无 chunk，直接按格集合判可走）
struct PercRig {
    std::unordered_set<std::uint64_t> blocked;  // 墙体格
    ai::PerceptionParams params;
    ecs::EcsWorld world;
    ai::PerceptionSystem perception;
    ai::AiSystem aiSystem;

    explicit PercRig(ai::PerceptionParams p = {})
        : params(p),
          perception([this](int wx, int wy) { return !Blocked(wx, wy); }, p),
          aiSystem([this](int wx, int wy) { return !Blocked(wx, wy); }) {}

    static std::uint64_t Key(int x, int y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
               static_cast<std::uint32_t>(y);
    }
    bool Blocked(int x, int y) const {
        if (x < 0 || y < 0 || x >= 60 || y >= 60) {
            return true;  // 地图边界
        }
        return blocked.count(Key(x, y)) != 0;
    }

    ecs::Entity AddZombie(float x, float y, ecs::Direction dir = ecs::Direction::Down) {
        entt::registry& r = world.Registry();
        const ecs::Entity e = world.Create();
        r.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        r.emplace<ecs::Velocity>(e, ecs::Velocity{});
        r.emplace<ecs::Health>(e, ecs::Health{100.0f, 100.0f});
        r.emplace<ecs::Facing>(e, ecs::Facing{dir});
        r.emplace<ecs::AiState>(e, ecs::AiState{});
        ecs::PerceptionState percep;
        percep.lastKnownX = x;
        percep.lastKnownY = y;
        r.emplace<ecs::PerceptionState>(e, percep);
        r.emplace<ecs::MoveIntent>(e, ecs::MoveIntent{});
        r.emplace<ecs::PathRequest>(e, ecs::PathRequest{});
        r.emplace<ecs::PathFollow>(e, ecs::PathFollow{});
        return e;
    }

    ecs::Entity AddPlayer(float x, float y) {
        const ecs::Entity e = world.Create();
        entt::registry& r = world.Registry();
        r.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        r.emplace<ecs::PlayerTag>(e);
        return e;
    }

    void Tick() {
        perception.Update(world, kDt);
        aiSystem.Update(world, kDt);
        world.Flush();
    }
    void Run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            Tick();
        }
    }

    ecs::Position& Pos(ecs::Entity e) { return world.Registry().get<ecs::Position>(e); }
    ecs::AiState& Ai(ecs::Entity e) { return world.Registry().get<ecs::AiState>(e); }
    ecs::PathRequest& Request(ecs::Entity e) {
        return world.Registry().get<ecs::PathRequest>(e);
    }
    ecs::PerceptionState& Percep(ecs::Entity e) {
        return world.Registry().get<ecs::PerceptionState>(e);
    }
};

}  // namespace

TEST_CASE("Perception：视觉 = 距离 × 视锥（近距全向）——背后看不见、超出视距看不见") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f, ecs::Direction::Down);  // 朝南
    const ecs::Entity p = rig.AddPlayer(5.5f, 8.5f);                       // 正前方 3 格
    const std::uint64_t playerId = rig.world.IdOf(p).id;

    rig.Tick();
    CHECK(rig.Ai(z).targetId == playerId);
    CHECK(rig.Percep(z).alertness == 1.0f);
    CHECK(rig.Percep(z).sinceSeen == 0.0f);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(5.5f));
    CHECK(rig.perception.GetStats().seeing == 1);
    CHECK(rig.perception.GetStats().alerted == 1);

    // 玩家绕到正后方 3 格：视锥外（>110°/2=55°）→ 看不见，目标清除
    rig.Pos(p).y = 2.5f;
    rig.Tick();
    CHECK(rig.Ai(z).targetId == 0u);
    CHECK(rig.Percep(z).sinceSeen > 0.0f);
    CHECK(rig.Percep(z).lastKnownY == doctest::Approx(8.5f));  // 可疑点保留在最后看见处

    // 正前方但超出视距（14 > 12）→ 看不见
    rig.Pos(p).y = 19.5f;
    rig.Tick();
    CHECK(rig.Ai(z).targetId == 0u);

    // 回到正前方 3 格 → 重新看见
    rig.Pos(p).y = 8.5f;
    rig.Tick();
    CHECK(rig.Ai(z).targetId == playerId);
}

TEST_CASE("Perception：近距全向（closeRadius 内无视朝向）") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f, ecs::Direction::Down);
    rig.AddPlayer(5.5f, 4.2f);  // 正后方 1.3 格 < closeRadius 2.5
    rig.Tick();
    CHECK(rig.Ai(z).targetId != 0u);
}

TEST_CASE("Perception：视线被墙遮挡（移除墙恢复可见）") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f, ecs::Direction::Down);
    const ecs::Entity p = rig.AddPlayer(5.5f, 8.5f);  // 正前方 3 格，视线上穿过 (5,7)

    rig.blocked.insert(PercRig::Key(5, 7));
    rig.Tick();
    CHECK(rig.Ai(z).targetId == 0u);  // 墙挡住

    rig.blocked.clear();
    rig.Tick();
    CHECK(rig.Ai(z).targetId == rig.world.IdOf(p).id);  // 无遮挡即看见
}

TEST_CASE("Perception：FOV 参数（360° → 背后也可见）") {
    ai::PerceptionParams params;
    params.fovDegrees = 360.0f;
    PercRig rig(params);
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f, ecs::Direction::Down);
    rig.AddPlayer(5.5f, 2.5f);  // 正后方 3 格
    rig.Tick();
    CHECK(rig.Ai(z).targetId != 0u);
}

TEST_CASE("Perception：听觉（半径内警觉 + 可疑点；半径外无感）+ 警觉衰减归零") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(z).idleSeconds = 100.0f;  // 排除 Wander 干扰

    core::SoundEvent gunshot;
    gunshot.x = 10.5f;
    gunshot.y = 5.5f;
    gunshot.radius = 6.0f;  // 距离 5 → 可闻
    gunshot.type = core::SoundType::Gunshot;
    rig.perception.PushSound(gunshot);
    rig.perception.Update(rig.world, kDt);  // 只跑感知

    CHECK(rig.Percep(z).alertness == doctest::Approx(0.6f));
    CHECK(rig.Percep(z).sinceHeard == 0.0f);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(10.5f));  // 可疑点 = 声源
    CHECK(rig.Percep(z).lastKnownY == doctest::Approx(5.5f));

    // 半径外：无感（警觉继续衰减、sinceHeard 增长、可疑点不被覆盖）
    core::SoundEvent far;
    far.x = 30.0f;
    far.y = 30.0f;
    far.radius = 5.0f;
    rig.perception.PushSound(far);
    rig.perception.Update(rig.world, kDt);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(10.5f));
    CHECK(rig.Percep(z).sinceHeard > 0.0f);

    // 衰减：8s 从 1 → 0（此时约 0.6，4s 后 ≈ 0.1）
    rig.Run(4 * 60);
    CHECK(rig.Percep(z).alertness == doctest::Approx(0.1f).epsilon(0.05));
    rig.Run(2 * 60);
    CHECK(rig.Percep(z).alertness == 0.0f);
}

TEST_CASE("Perception→AiSystem：听见声音去声源查看（无视觉），超时放弃回 Idle") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(z).idleSeconds = 100.0f;

    core::SoundEvent sound;
    sound.x = 20.0f;
    sound.y = 20.0f;
    sound.radius = 30.0f;
    rig.perception.PushSound(sound);
    rig.Tick();  // 感知 → AI 同 tick 消费

    CHECK(rig.Ai(z).status == ecs::AiStatus::SearchLastKnown);  // 去查看
    CHECK(rig.Request(z).goalX == 20);
    CHECK(rig.Request(z).goalY == 20);

    rig.Run(6 * 60 + 10);  // 搜索超时
    CHECK(rig.Ai(z).status == ecs::AiStatus::Idle);
}

TEST_CASE("Perception：听觉隔墙衰减（有效半径 = 半径 × 系数^墙数）") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(z).idleSeconds = 100.0f;
    rig.Percep(z).sinceHeard = 100.0f;  // 排除初始 sinceHeard=0 的"刚听见"假象

    core::SoundEvent shot;
    shot.x = 10.5f;  // 距离 5
    shot.y = 5.5f;
    shot.radius = 10.0f;
    shot.type = core::SoundType::Gunshot;

    // 无墙：距离 5 ≤ 10 → 可闻
    rig.perception.PushSound(shot);
    rig.perception.Update(rig.world, kDt);
    CHECK(rig.Percep(z).sinceHeard == 0.0f);
    CHECK(rig.Percep(z).alertness == doctest::Approx(0.6f));
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(10.5f));

    // 1 墙（7,5）：有效半径 10×0.6=6 ≥ 5 → 仍可闻
    rig.blocked.insert(PercRig::Key(7, 5));
    rig.Percep(z).sinceHeard = 100.0f;
    rig.perception.PushSound(shot);
    rig.perception.Update(rig.world, kDt);
    CHECK(rig.Percep(z).sinceHeard == 0.0f);

    // 2 墙（7,5)+(8,5)：有效半径 10×0.6²=3.6 < 5 → 不可闻（可疑点不被覆盖）
    rig.blocked.insert(PercRig::Key(8, 5));
    rig.Percep(z).sinceHeard = 100.0f;
    core::SoundEvent farShot;
    farShot.x = 15.5f;  // 距离 10
    farShot.y = 5.5f;
    farShot.radius = 10.0f;
    farShot.type = core::SoundType::Gunshot;
    rig.perception.PushSound(farShot);
    rig.perception.Update(rig.world, kDt);
    CHECK(rig.Percep(z).sinceHeard > 100.0f);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(10.5f));

    // 拆墙恢复：距离 10 ≤ 10 → 可闻，可疑点更新到新声源
    rig.blocked.clear();
    rig.perception.PushSound(farShot);
    rig.perception.Update(rig.world, kDt);
    CHECK(rig.Percep(z).sinceHeard == 0.0f);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(15.5f));
}

TEST_CASE("M5-T4：事件总线 OnSound（SoundEvent 载荷）→ 感知系统消费") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(z).idleSeconds = 100.0f;
    rig.Percep(z).sinceHeard = 100.0f;

    // 复刻 GameApp 接线：发射方只发事件，订阅端转交感知系统
    core::EventBus bus;
    const core::EventBus::SubscriptionId sub = bus.Subscribe(
        core::kOnSound, [&rig](const core::Event& event) {
            if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
                rig.perception.PushSound(*sound);
            }
        });

    core::SoundEvent shot;
    shot.x = 20.5f;  // 距离 15（> sightRadius：纯听觉）
    shot.y = 5.5f;
    shot.radius = 30.0f;
    shot.type = core::SoundType::Gunshot;
    bus.EmitQueued(core::kOnSound, shot);
    CHECK(bus.QueuedCount() == 1);
    bus.FlushQueued();  // 主循环 tick 末投递语义

    rig.perception.Update(rig.world, kDt);
    CHECK(rig.Percep(z).sinceHeard == 0.0f);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(20.5f));
    CHECK(rig.Percep(z).alertness == doctest::Approx(0.6f));
    CHECK(rig.perception.GetStats().heard == 1);

    bus.Unsubscribe(sub);
}

TEST_CASE("M23-F1-T5：远程枪声 noiseRadius 经 OnSound 吸引僵尸搜索") {
    PercRig rig;
    const ecs::Entity zombie = rig.AddZombie(5.5f, 5.5f);
    rig.Ai(zombie).idleSeconds = 100.0f;
    rig.Percep(zombie).sinceHeard = 100.0f;

    items::RangedCombat ranged;
    ranged.noiseRadius = 28.0f;
    const core::SoundEvent shot = game::MakeRangedGunshot(20.5f, 5.5f, ranged);

    core::EventBus bus;
    const core::EventBus::SubscriptionId subscription = bus.Subscribe(
        core::kOnSound, [&rig](const core::Event& event) {
            if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
                rig.perception.PushSound(*sound);
            }
        });
    bus.EmitQueued(core::kOnSound, shot);
    bus.FlushQueued();

    rig.Tick();
    CHECK(rig.Ai(zombie).status == ecs::AiStatus::SearchLastKnown);
    CHECK(rig.Request(zombie).goalX == 20);
    CHECK(rig.Request(zombie).goalY == 5);
    CHECK(rig.Percep(zombie).lastKnownX == doctest::Approx(20.5f));

    bus.Unsubscribe(subscription);
}

TEST_CASE("M5-T4 验收：枪声吸引半径外僵尸（事件总线全链）；隔墙衰减下不被吸引") {
    PercRig rig;
    const ecs::Entity open = rig.AddZombie(5.5f, 5.5f);    // 距声源 15，无遮挡
    const ecs::Entity walled = rig.AddZombie(35.5f, 5.5f);  // 距声源 15，中间 2 墙
    rig.Ai(open).idleSeconds = 100.0f;
    rig.Ai(walled).idleSeconds = 100.0f;
    rig.Percep(open).sinceHeard = 100.0f;
    rig.Percep(walled).sinceHeard = 100.0f;
    rig.blocked.insert(PercRig::Key(25, 5));
    rig.blocked.insert(PercRig::Key(26, 5));

    core::EventBus bus;
    const core::EventBus::SubscriptionId sub = bus.Subscribe(
        core::kOnSound, [&rig](const core::Event& event) {
            if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
                rig.perception.PushSound(*sound);
            }
        });
    core::SoundEvent shot;
    shot.x = 20.5f;
    shot.y = 5.5f;
    shot.radius = 30.0f;  // 大半径枪声
    shot.type = core::SoundType::Gunshot;
    bus.EmitQueued(core::kOnSound, shot);
    bus.FlushQueued();

    rig.Tick();  // 感知 → AI 同 tick 消费
    CHECK(rig.Ai(open).status == ecs::AiStatus::SearchLastKnown);  // 听到 → 去声源查看
    CHECK(rig.Request(open).goalX == 20);
    CHECK(rig.Request(open).goalY == 5);
    // 2 墙衰减：30×0.6²=10.8 < 15 → 听不见，保持原有节奏
    CHECK(rig.Ai(walled).status == ecs::AiStatus::Idle);
    CHECK(rig.Percep(walled).sinceHeard > 1.0f);

    bus.Unsubscribe(sub);
}

TEST_CASE("Perception→AiSystem：看见→追击；目标消失→去最后已知位置搜索再放弃（M5 验收链）") {
    PercRig rig;
    const ecs::Entity z = rig.AddZombie(5.5f, 5.5f, ecs::Direction::Down);
    const ecs::Entity p = rig.AddPlayer(5.5f, 8.5f);
    rig.Ai(z).idleSeconds = 100.0f;

    rig.Tick();
    CHECK(rig.Ai(z).status == ecs::AiStatus::Chase);
    CHECK(rig.Request(z).goalX == 5);  // 走向玩家（最后已知位置）
    CHECK(rig.Request(z).goalY == 8);

    // 玩家瞬移消失（超出视距）→ Chase 断目标 → SearchLastKnown（目标格 = 最后看见处）
    rig.Pos(p).x = 40.0f;
    rig.Pos(p).y = 40.0f;
    rig.Tick();
    CHECK(rig.Ai(z).targetId == 0u);
    CHECK(rig.Ai(z).status == ecs::AiStatus::SearchLastKnown);
    CHECK(rig.Percep(z).lastKnownX == doctest::Approx(5.5f));
    CHECK(rig.Percep(z).lastKnownY == doctest::Approx(8.5f));

    rig.Run(6 * 60 + 10);  // 搜索无果 → 放弃
    CHECK(rig.Ai(z).status == ecs::AiStatus::Idle);
}
