#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "characters/systems/DeathSystem.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/ScriptEvent.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/ZombieFactory.h"
#include "script/EventBridge.h"
#include "script/ScriptManager.h"

using namespace zl;

namespace {

// 翻译断言小工具：字段存在且类型/值符合
template <typename T>
bool FieldIs(const core::ScriptEvent& event, const char* name, const T& expected) {
    const core::ScriptValue* value = event.Find(name);
    if (value == nullptr) {
        return false;
    }
    const auto* actual = std::get_if<T>(value);
    return actual != nullptr && *actual == expected;
}

// doctest 表达式分解与 EnTT 的 entity/null 比较符冲突（C2593）→ 先归约成 bool
bool IsValidEntity(ecs::Entity entity) {
    return entity != entt::null;
}

}  // namespace

TEST_CASE("M7-T4：事件表 v1 翻译（EventBus 载荷 → 脚本字段）") {
    // OnGameStart：无字段
    {
        const auto event = script::EventBridge::Translate(core::Event{std::string(core::kOnGameStart), {}});
        REQUIRE(event.has_value());
        CHECK(event->name == "OnGameStart");
        CHECK(event->fields.empty());
    }
    // OnTick：tick int64
    {
        const auto event = script::EventBridge::Translate(
            core::Event{std::string(core::kOnTick), static_cast<std::int64_t>(42)});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "tick", static_cast<std::int64_t>(42)));
    }
    // OnPlayerMove：playerId + x/y/z
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnPlayerMove), core::PlayerEvent{7, 1.5f, 2.5f, 0}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "x", 1.5));
        CHECK(FieldIs(*event, "y", 2.5));
        CHECK(FieldIs(*event, "z", static_cast<std::int64_t>(0)));
    }
    // OnPlayerDeath：仅 playerId
    {
        const auto event = script::EventBridge::Translate(
            core::Event{std::string(core::kOnPlayerDeath), core::PlayerEvent{9, 3.0f, 4.0f, 0}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(9)));
        CHECK(event->fields.size() == 1);
    }
    // OnZombieSpawn：zombieId + x/y/z
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnZombieSpawn), core::ZombieEvent{5, 0, 12.0f, 13.0f, 0}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "zombieId", static_cast<std::int64_t>(5)));
        CHECK(FieldIs(*event, "x", 12.0));
        CHECK(FieldIs(*event, "y", 13.0));
    }
    // OnZombieDead：zombieId + killer
    {
        const auto event = script::EventBridge::Translate(
            core::Event{std::string(core::kOnZombieDead), core::ZombieEvent{5, 3, 0.0f, 0.0f, 0}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "zombieId", static_cast<std::int64_t>(5)));
        CHECK(FieldIs(*event, "killer", static_cast<std::int64_t>(3)));
    }
    // OnItemPickup / OnItemDrop：itemId + defId + count
    for (const std::string_view key : {core::kOnItemPickup, core::kOnItemDrop}) {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(key), core::ItemEvent{11, "bandage", 2}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "itemId", static_cast<std::int64_t>(11)));
        CHECK(FieldIs(*event, "defId", std::string("bandage")));
        CHECK(FieldIs(*event, "count", static_cast<std::int64_t>(2)));
    }
    // OnSound：x/y/z/radius/soundType
    {
        core::SoundEvent sound;
        sound.x = 3.0f;
        sound.y = 4.0f;
        sound.radius = 6.5f;
        sound.type = core::SoundType::Gunshot;
        const auto event =
            script::EventBridge::Translate(core::Event{std::string(core::kOnSound), sound});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "x", 3.0));
        CHECK(FieldIs(*event, "y", 4.0));
        CHECK(FieldIs(*event, "radius", 6.5));
        CHECK(FieldIs(*event, "soundType", std::string("Gunshot")));
    }
    // OnChunkLoaded / OnChunkUnloaded：cx/cy
    for (const std::string_view key : {core::kOnChunkLoaded, core::kOnChunkUnloaded}) {
        const auto event =
            script::EventBridge::Translate(core::Event{std::string(key), core::ChunkEvent{2, 3}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "cx", static_cast<std::int64_t>(2)));
        CHECK(FieldIs(*event, "cy", static_cast<std::int64_t>(3)));
    }
    // OnSaveBegin/End：slotName；OnModReload：modId
    {
        const auto save = script::EventBridge::Translate(
            core::Event{std::string(core::kOnSaveBegin), std::string("slot1")});
        REQUIRE(save.has_value());
        CHECK(FieldIs(*save, "slotName", std::string("slot1")));
        const auto reload = script::EventBridge::Translate(
            core::Event{std::string(core::kOnModReload), std::string("m1")});
        REQUIRE(reload.has_value());
        CHECK(FieldIs(*reload, "modId", std::string("m1")));
    }
    // 非 v1 键 / 载荷类型不符 → nullopt
    CHECK_FALSE(script::EventBridge::Translate(core::Event{"InputAction", std::string("Quit")})
                    .has_value());
    CHECK_FALSE(script::EventBridge::Translate(core::Event{std::string(core::kOnTick), true})
                    .has_value());
    CHECK_FALSE(script::EventBridge::Translate(core::Event{std::string(core::kOnZombieDead), 1})
                    .has_value());
}

TEST_CASE("M7-T4：僵尸工厂发 OnZombieSpawn（排队；Flush 后到达）") {
    core::EventBus bus;
    std::vector<core::Event> captured;
    bus.Subscribe(core::kOnZombieSpawn,
                  [&captured](const core::Event& event) { captured.push_back(event); });

    ecs::EcsWorld ecs;
    const ecs::Entity zombie = game::SpawnZombie(ecs, nullptr, 5.5f, 6.5f, &bus);
    REQUIRE(IsValidEntity(zombie));
    CHECK(captured.empty());  // 排队：tick 末统一派发

    bus.FlushQueued();
    REQUIRE(captured.size() == 1);
    const auto* payload = std::get_if<core::ZombieEvent>(&captured[0].payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->zombieId == ecs.IdOf(zombie).id);
    CHECK(payload->x == doctest::Approx(5.5f));
    CHECK(payload->y == doctest::Approx(6.5f));
    CHECK(payload->z == 0);

    // 无总线（events=nullptr）：不发事件（默认参数路径）
    const ecs::Entity quiet = game::SpawnZombie(ecs, nullptr, 1.0f, 1.0f);
    CHECK(IsValidEntity(quiet));
    bus.FlushQueued();
    CHECK(captured.size() == 1);
}

TEST_CASE("M7-T4：DeathSystem 发 OnZombieDead / OnPlayerDeath") {
    core::EventBus bus;
    std::vector<core::Event> zombieEvents;
    std::vector<core::Event> playerEvents;
    bus.Subscribe(core::kOnZombieDead,
                  [&zombieEvents](const core::Event& event) { zombieEvents.push_back(event); });
    bus.Subscribe(core::kOnPlayerDeath,
                  [&playerEvents](const core::Event& event) { playerEvents.push_back(event); });

    ecs::EcsWorld ecs;
    characters::DeathSystem death(nullptr, 0.5f, &bus);
    entt::registry& registry = ecs.Registry();

    // 玩家（有 PlayerTag）→ OnPlayerDeath
    const ecs::Entity player = ecs.Create();
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Health>(player, ecs::Health{0.0f, 100.0f});
    registry.emplace<ecs::Dying>(player, ecs::Dying{0.05f});  // 倒地窗口即将结束
    registry.emplace<ecs::Position>(player, ecs::Position{1.0f, 2.0f, 0});

    // 僵尸（无 PlayerTag）→ OnZombieDead（killer 暂未知 = 0）
    const ecs::Entity zombie = ecs.Create();
    registry.emplace<ecs::Health>(zombie, ecs::Health{0.0f, 100.0f});
    registry.emplace<ecs::Dying>(zombie, ecs::Dying{0.05f});
    registry.emplace<ecs::Position>(zombie, ecs::Position{8.0f, 9.0f, 0});

    death.Update(ecs, 0.1f);
    CHECK(zombieEvents.empty());  // 排队
    bus.FlushQueued();

    REQUIRE(zombieEvents.size() == 1);
    const auto* zombiePayload = std::get_if<core::ZombieEvent>(&zombieEvents[0].payload);
    REQUIRE(zombiePayload != nullptr);
    CHECK(zombiePayload->zombieId == ecs.IdOf(zombie).id);
    CHECK(zombiePayload->killerId == 0);
    CHECK(zombiePayload->x == doctest::Approx(8.0f));

    REQUIRE(playerEvents.size() == 1);
    const auto* playerPayload = std::get_if<core::PlayerEvent>(&playerEvents[0].payload);
    REQUIRE(playerPayload != nullptr);
    CHECK(playerPayload->playerId == ecs.IdOf(player).id);

    // 无总线（events=nullptr）：不发事件
    ecs::EcsWorld quietEcs;
    characters::DeathSystem quietDeath(nullptr, 0.5f);
    const ecs::Entity quietZombie = quietEcs.Create();
    quietEcs.Registry().emplace<ecs::Health>(quietZombie, ecs::Health{0.0f, 100.0f});
    quietEcs.Registry().emplace<ecs::Dying>(quietZombie, ecs::Dying{0.0f});
    quietDeath.Update(quietEcs, 0.1f);
    CHECK(zombieEvents.size() == 1);  // 仍是之前那 1 条
}

namespace {

// 最小假后端（不执行脚本；用于 manager 层的 OnModReload 行为）
class FakeRuntime final : public script::IScriptRuntime {
public:
    const char* Name() const override { return "fake"; }
    bool Initialize(std::string&) override { return true; }
    void Shutdown() override {}
    bool LoadMod(const script::ModInfo& mod, std::string&) override {
        m_mods.push_back(mod.id);
        return true;
    }
    bool ReloadMod(const std::string&, std::string&) override { return true; }
    void UnloadMod(const std::string& modId) override {
        m_mods.erase(std::remove(m_mods.begin(), m_mods.end(), modId), m_mods.end());
    }
    std::vector<script::ModError> DispatchEventToMod(const std::string&,
                                                     const core::ScriptEvent&) override {
        return {};
    }
    const std::vector<std::string>& LoadedMods() const override { return m_mods; }

private:
    std::vector<std::string> m_mods;
};

}  // namespace

TEST_CASE("M7-T4：热重载成功后发 OnModReload（modId）") {
    core::EventBus bus;
    script::ScriptManager manager;
    manager.RegisterRuntime(std::make_unique<FakeRuntime>());
    REQUIRE(manager.InitializeAll());
    manager.SetEventBus(&bus);

    std::vector<core::Event> captured;
    bus.Subscribe(core::kOnModReload,
                  [&captured](const core::Event& event) { captured.push_back(event); });

    script::ModInfo mod;
    mod.id = "m1";
    mod.backend = "fake";
    std::string error;
    REQUIRE(manager.LoadMod(mod, error));

    REQUIRE(manager.ReloadMod("m1", error));
    CHECK(captured.empty());  // 排队派发
    bus.FlushQueued();
    REQUIRE(captured.size() == 1);
    const auto* payload = std::get_if<std::string>(&captured[0].payload);
    REQUIRE(payload != nullptr);
    CHECK(*payload == "m1");
}
