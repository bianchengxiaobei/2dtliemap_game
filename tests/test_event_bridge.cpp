#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/DeathSystem.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/ScriptEvent.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/WorldLifecycle.h"
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
    // OnWorldStart：saveId + isNewGame + day + playerId
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnWorldStart),
            core::WorldStartEvent{"slot1", false, 3, 7}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "saveId", std::string("slot1")));
        CHECK(FieldIs(*event, "isNewGame", false));
        CHECK(FieldIs(*event, "day", static_cast<std::int64_t>(3)));
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
    }
    // OnWorldExit：saveId
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnWorldExit), core::WorldExitEvent{"slot1"}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "saveId", std::string("slot1")));
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
    // OnPlayerDeath：playerId + sourceId + cause
    {
        core::PlayerEvent death{9, 3.0f, 4.0f, 0};
        death.sourceId = 42;
        death.cause = "bite";
        const auto event = script::EventBridge::Translate(
            core::Event{std::string(core::kOnPlayerDeath), death});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(9)));
        CHECK(FieldIs(*event, "sourceId", static_cast<std::int64_t>(42)));
        CHECK(FieldIs(*event, "cause", std::string("bite")));
    }
    // OnDamage：实际扣血字段（格挡剩余伤害带 blocked）
    {
        core::DamageEvent damage;
        damage.targetId = 7;
        damage.sourceId = 3;
        damage.amount = 7.5f;
        damage.damageType = ecs::DamageTypeName(ecs::DamageType::Claw);
        damage.cause =
            ecs::DamageCauseName(ecs::DamageCauseFromType(ecs::DamageType::Claw));
        damage.remainingHp = 92.5f;
        damage.lethal = false;
        damage.blocked = true;
        const auto event = script::EventBridge::Translate(
            core::Event{std::string(core::kOnDamage), damage});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "targetId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "sourceId", static_cast<std::int64_t>(3)));
        CHECK(FieldIs(*event, "amount", 7.5));
        CHECK(FieldIs(*event, "damageType", std::string("claw")));
        CHECK(FieldIs(*event, "cause", std::string("claw")));
        CHECK(FieldIs(*event, "remainingHp", 92.5));
        CHECK(FieldIs(*event, "lethal", false));
        CHECK(FieldIs(*event, "blocked", true));
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
    // OnAnimalKilled：animalId + speciesId + killerId + x/y/z
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnAnimalKilled),
            core::AnimalKilledEvent{7, "deer", 3, 11.0f, 12.0f, 1}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "animalId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "speciesId", std::string("deer")));
        CHECK(FieldIs(*event, "killerId", static_cast<std::int64_t>(3)));
        CHECK(FieldIs(*event, "x", 11.0));
        CHECK(FieldIs(*event, "y", 12.0));
        CHECK(FieldIs(*event, "z", static_cast<std::int64_t>(1)));
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
    // OnItemUsed：playerId + defId + spoiled + remaining
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnItemUsed),
            core::ItemUsedEvent{7, "canned_beans", true, 1}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "defId", std::string("canned_beans")));
        CHECK(FieldIs(*event, "spoiled", true));
        CHECK(FieldIs(*event, "remaining", static_cast<std::int64_t>(1)));
    }
    // OnLootSearched：playerId + containerId + tableId + itemCount
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnLootSearched),
            core::LootSearchedEvent{7, 11, "test_crate", 4}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "containerId", static_cast<std::int64_t>(11)));
        CHECK(FieldIs(*event, "tableId", std::string("test_crate")));
        CHECK(FieldIs(*event, "itemCount", static_cast<std::int64_t>(4)));
    }
    // OnCraftStart：playerId + recipeId + outputDefId + outputCount + batchCount
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnCraftStart),
            core::CraftStartEvent{7, "field_bandage", "bandage", 1, 3}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "recipeId", std::string("field_bandage")));
        CHECK(FieldIs(*event, "outputDefId", std::string("bandage")));
        CHECK(FieldIs(*event, "outputCount", static_cast<std::int64_t>(1)));
        CHECK(FieldIs(*event, "batchCount", static_cast<std::int64_t>(3)));
    }
    // OnCraftFinished：playerId + recipeId + outputDefId + outputCount + batchCompleted + failed
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnCraftFinished),
            core::CraftFinishedEvent{7, "field_bandage", "bandage", 1, 2, true}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "recipeId", std::string("field_bandage")));
        CHECK(FieldIs(*event, "outputDefId", std::string("bandage")));
        CHECK(FieldIs(*event, "outputCount", static_cast<std::int64_t>(1)));
        CHECK(FieldIs(*event, "batchCompleted", static_cast<std::int64_t>(2)));
        CHECK(FieldIs(*event, "failed", true));
    }
    // OnBuildPlaced：playerId + defId + x/y/z + vertical
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnBuildPlaced),
            core::BuildPlacedEvent{7, "wall", 10, 11, 0, true}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "defId", std::string("wall")));
        CHECK(FieldIs(*event, "x", static_cast<std::int64_t>(10)));
        CHECK(FieldIs(*event, "y", static_cast<std::int64_t>(11)));
        CHECK(FieldIs(*event, "z", static_cast<std::int64_t>(0)));
        CHECK(FieldIs(*event, "vertical", true));
    }
    // OnBuildDemolished：playerId + defId + x/y/z
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnBuildDemolished),
            core::BuildDemolishedEvent{7, "wall", 10, 11, 0}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "defId", std::string("wall")));
        CHECK(FieldIs(*event, "x", static_cast<std::int64_t>(10)));
        CHECK(FieldIs(*event, "y", static_cast<std::int64_t>(11)));
        CHECK(FieldIs(*event, "z", static_cast<std::int64_t>(0)));
    }
    // OnBuildRepaired：playerId + x/y/z
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnBuildRepaired),
            core::BuildRepairedEvent{7, 10, 11, 0}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "playerId", static_cast<std::int64_t>(7)));
        CHECK(FieldIs(*event, "x", static_cast<std::int64_t>(10)));
        CHECK(FieldIs(*event, "y", static_cast<std::int64_t>(11)));
        CHECK(FieldIs(*event, "z", static_cast<std::int64_t>(0)));
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
    // OnSaveBegin/End：无字段；OnModReload：modId
    {
        const auto save = script::EventBridge::Translate(
            core::Event{std::string(core::kOnSaveBegin), {}});
        REQUIRE(save.has_value());
        CHECK(save->fields.empty());
        const auto reload = script::EventBridge::Translate(
            core::Event{std::string(core::kOnModReload), std::string("m1")});
        REQUIRE(reload.has_value());
        CHECK(FieldIs(*reload, "modId", std::string("m1")));
    }
    // OnUiReady：无字段
    {
        const auto ready = script::EventBridge::Translate(
            core::Event{std::string(core::kOnUiReady), {}});
        REQUIRE(ready.has_value());
        CHECK(ready->name == "OnUiReady");
        CHECK(ready->fields.empty());
    }
    // OnUiFrame：无字段，供 HUD 在暂停时按渲染帧刷新
    {
        const auto frame = script::EventBridge::Translate(
            core::Event{std::string(core::kOnUiFrame), {}});
        REQUIRE(frame.has_value());
        CHECK(frame->name == "OnUiFrame");
        CHECK(frame->fields.empty());
    }
    // OnUiEvent：文档/元素/类型/值/键盘/鼠标
    {
        core::UiEvent ui;
        ui.modId = "ui_mod";
        ui.documentId = "panel";
        ui.elementId = "btn";
        ui.type = "click";
        ui.value = "x";
        ui.key = 65;
        ui.button = 0;
        ui.mouseX = 12.5f;
        ui.mouseY = 34.5f;
        const auto event = script::EventBridge::Translate(
            core::Event{std::string(core::kOnUiEvent), ui});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "modId", std::string("ui_mod")));
        CHECK(FieldIs(*event, "document", std::string("panel")));
        CHECK(FieldIs(*event, "element", std::string("btn")));
        CHECK(FieldIs(*event, "type", std::string("click")));
        CHECK(FieldIs(*event, "value", std::string("x")));
        CHECK(FieldIs(*event, "key", static_cast<std::int64_t>(65)));
        CHECK(FieldIs(*event, "button", static_cast<std::int64_t>(0)));
        CHECK(FieldIs(*event, "mouseX", 12.5));
        CHECK(FieldIs(*event, "mouseY", 34.5));
    }
    // OnMenuChanged：主菜单/暂停菜单同步事件
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnMenuChanged), core::MenuChangedEvent{true, true, true}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "open", true));
        CHECK(FieldIs(*event, "pauseMode", true));
        CHECK(FieldIs(*event, "canContinue", true));
    }
    // OnInventoryChanged：背包开关/刷新同步事件
    {
        const auto event = script::EventBridge::Translate(core::Event{
            std::string(core::kOnInventoryChanged), core::InventoryChangedEvent{true}});
        REQUIRE(event.has_value());
        CHECK(FieldIs(*event, "open", true));
    }
    // 非 v1 键 / 载荷类型不符 → nullopt
    CHECK_FALSE(script::EventBridge::Translate(core::Event{"InputAction", std::string("Quit")})
                    .has_value());
    CHECK_FALSE(script::EventBridge::Translate(core::Event{std::string(core::kOnTick), true})
                    .has_value());
    CHECK_FALSE(script::EventBridge::Translate(core::Event{std::string(core::kOnZombieDead), 1})
                    .has_value());
}

TEST_CASE("M35-F2-T6 世界生命周期事件只发一次") {
    core::EventBus bus;
    std::vector<core::Event> starts;
    std::vector<core::Event> exits;
    bus.Subscribe(core::kOnWorldStart,
                  [&starts](const core::Event& event) { starts.push_back(event); });
    bus.Subscribe(core::kOnWorldExit,
                  [&exits](const core::Event& event) { exits.push_back(event); });

    game::WorldLifecycle lifecycle;
    CHECK_FALSE(lifecycle.Exit(&bus, "before-start"));
    CHECK(exits.empty());
    CHECK_FALSE(lifecycle.Start(
        &bus, game::WorldStartInfo{"", true, 1, 0}));
    CHECK(starts.empty());

    CHECK(lifecycle.Start(
        &bus, game::WorldStartInfo{"slot1", false, 3, 7}));
    REQUIRE(starts.size() == 1u);  // 同步派发
    const auto* startPayload =
        std::get_if<core::WorldStartEvent>(&starts[0].payload);
    REQUIRE(startPayload != nullptr);
    CHECK(startPayload->saveId == "slot1");
    CHECK_FALSE(startPayload->isNewGame);
    CHECK(startPayload->day == 3);
    CHECK(startPayload->playerId == 7u);

    CHECK_FALSE(lifecycle.Start(
        &bus, game::WorldStartInfo{"slot2", true, 4, 8}));
    CHECK(starts.size() == 1u);

    CHECK(lifecycle.Exit(&bus, "slot1"));
    REQUIRE(exits.size() == 1u);  // 同步派发
    const auto* exitPayload =
        std::get_if<core::WorldExitEvent>(&exits[0].payload);
    REQUIRE(exitPayload != nullptr);
    CHECK(exitPayload->saveId == "slot1");
    CHECK_FALSE(lifecycle.Exit(&bus, "slot1"));
    CHECK(exits.size() == 1u);

    lifecycle.Reset();
    CHECK(lifecycle.Start(
        &bus, game::WorldStartInfo{"", true, 1, 9}));
    REQUIRE(starts.size() == 2u);
    const auto* newGamePayload =
        std::get_if<core::WorldStartEvent>(&starts[1].payload);
    REQUIRE(newGamePayload != nullptr);
    CHECK(newGamePayload->saveId.empty());
    CHECK(newGamePayload->isNewGame);
    CHECK(newGamePayload->day == 1);
    CHECK(newGamePayload->playerId == 9u);

    CHECK(lifecycle.Exit(&bus, ""));
    REQUIRE(exits.size() == 2u);
    bus.FlushQueued();
    CHECK(starts.size() == 2u);
    CHECK(exits.size() == 2u);

    game::WorldLifecycle noBus;
    CHECK_FALSE(noBus.Start(
        nullptr, game::WorldStartInfo{"", true, 1, 7}));
    CHECK_FALSE(noBus.Exit(&bus, ""));
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

TEST_CASE("M7-T4：DeathSystem 发 OnZombieDead / OnPlayerDeath / OnAnimalKilled") {
    core::EventBus bus;
    std::vector<core::Event> zombieEvents;
    std::vector<core::Event> playerEvents;
    std::vector<core::Event> animalEvents;
    bus.Subscribe(core::kOnZombieDead,
                  [&zombieEvents](const core::Event& event) { zombieEvents.push_back(event); });
    bus.Subscribe(core::kOnPlayerDeath,
                  [&playerEvents](const core::Event& event) { playerEvents.push_back(event); });
    bus.Subscribe(core::kOnAnimalKilled,
                  [&animalEvents](const core::Event& event) { animalEvents.push_back(event); });

    ecs::EcsWorld ecs;
    characters::DeathSystem death(nullptr, 0.5f, &bus);
    entt::registry& registry = ecs.Registry();

    // 玩家（有 PlayerTag）→ OnPlayerDeath；环境伤害无击杀者。
    const ecs::Entity player = ecs.Create();
    const std::uint64_t playerId = ecs.IdOf(player).id;
    registry.emplace<ecs::PlayerTag>(player);
    registry.emplace<ecs::Health>(player, ecs::Health{0.0f, 100.0f});
    registry.emplace<ecs::Dying>(player, ecs::Dying{0.05f});  // 倒地窗口即将结束
    registry.emplace<ecs::Position>(player, ecs::Position{1.0f, 2.0f, 0});
    registry.emplace<ecs::LastDamage>(
        player, ecs::LastDamage{ecs::DamageCause::Dehydration, 3.0f, 0});

    // 僵尸（无 PlayerTag）→ OnZombieDead；killer 来自 LastDamage.sourceId。
    const ecs::Entity zombie = ecs.Create();
    registry.emplace<ecs::Health>(zombie, ecs::Health{0.0f, 100.0f});
    registry.emplace<ecs::Dying>(zombie, ecs::Dying{0.05f});
    registry.emplace<ecs::Position>(zombie, ecs::Position{8.0f, 9.0f, 0});
    registry.emplace<ecs::LastDamage>(
        zombie, ecs::LastDamage{ecs::DamageCause::Blunt, 15.0f, playerId});

    // 动物 → OnAnimalKilled（不再错发 OnZombieDead）。
    const ecs::Entity animal = ecs.Create();
    registry.emplace<ecs::Health>(animal, ecs::Health{0.0f, 30.0f});
    registry.emplace<ecs::Dying>(animal, ecs::Dying{0.05f});
    registry.emplace<ecs::Position>(animal, ecs::Position{5.0f, 6.0f, 1});
    registry.emplace<ecs::Animal>(
        animal, ecs::Animal{"deer", ecs::AnimalKind::Passive});
    registry.emplace<ecs::LastDamage>(
        animal, ecs::LastDamage{ecs::DamageCause::Blunt, 10.0f, playerId});

    death.Update(ecs, 0.1f);
    CHECK(zombieEvents.empty());  // 排队
    bus.FlushQueued();

    REQUIRE(zombieEvents.size() == 1);
    const auto* zombiePayload = std::get_if<core::ZombieEvent>(&zombieEvents[0].payload);
    REQUIRE(zombiePayload != nullptr);
    CHECK(zombiePayload->zombieId == ecs.IdOf(zombie).id);
    CHECK(zombiePayload->killerId == playerId);
    CHECK(zombiePayload->x == doctest::Approx(8.0f));

    REQUIRE(playerEvents.size() == 1);
    const auto* playerPayload = std::get_if<core::PlayerEvent>(&playerEvents[0].payload);
    REQUIRE(playerPayload != nullptr);
    CHECK(playerPayload->playerId == playerId);
    CHECK(playerPayload->sourceId == 0u);
    CHECK(playerPayload->cause == "dehydration");

    REQUIRE(animalEvents.size() == 1);
    const auto* animalPayload =
        std::get_if<core::AnimalKilledEvent>(&animalEvents[0].payload);
    REQUIRE(animalPayload != nullptr);
    CHECK(animalPayload->animalId == ecs.IdOf(animal).id);
    CHECK(animalPayload->speciesId == "deer");
    CHECK(animalPayload->killerId == playerId);
    CHECK(animalPayload->x == doctest::Approx(5.0f));
    CHECK(animalPayload->y == doctest::Approx(6.0f));
    CHECK(animalPayload->z == 1);

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
