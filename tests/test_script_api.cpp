#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "ScriptTestHelpers.h"
#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/PerceptionSystem.h"
#include "audio/SoundIds.h"
#include "audio/SoundManager.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/SoundEvent.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Companion.h"
#include "game/ContentReload.h"
#include "game/FireComponents.h"
#include "game/GameContext.h"
#include "game/GroundItems.h"
#include "game/Missions.h"
#include "game/NetworkClient.h"
#include "game/PlayerCommand.h"
#include "game/Poi.h"
#include "game/ProgressionRules.h"
#include "game/SettlementNpc.h"
#include "game/VehicleComponents.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/CommandPipeline.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveTypes.h"
#include "script/ModDataComponents.h"
#include "script/ScriptApi.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"
#include "world/World.h"
#include "world/WorldCoords.h"

using namespace zl;
using zltest::LogCapture;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "bandage", "name": "绷带", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "axe", "name": "斧", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

const char* kRecipesJson = R"({
  "format": "recipes-v3",
  "recipes": [
    {
      "id": "base_bandage",
      "name": "基础绷带",
      "output": { "item": "bandage", "count": 1 },
      "ingredients": [ { "item": "bandage", "count": 1 } ]
    }
  ]
})";

// ScriptApi 测试夹具：最小完整 GameContext（无窗口/渲染/物理）
struct Fixture {
    ecs::EcsWorld ecs;
    items::ItemRegistry items;
    items::RecipeRegistry recipes;
    core::Config config;
    game::PoiConfig poiConfig;
    game::MissionConfig missionConfig;
    world::World world;
    core::GameLoop loop{60.0};
    core::GameTime gameTime;
    game::ProgressionConfig progressionConfig = game::DefaultProgressionConfig();
    ai::PathfindingService pathfinding{nullptr, ai::MapChunkBounds{0, 0, 4, 4}};
    characters::CombatSystem combat;
    game::GameContext ctx;
    script::ScriptApi api;

    Fixture() {
        std::string error;
        CHECK(items.LoadFromString(kItemsJson, "test", &error));
        CHECK(recipes.LoadFromString(kRecipesJson, "test-recipes", items, &error));
        gameTime.Configure(1.0, core::DateTime{});
        ctx.config = &config;
        ctx.ecs = &ecs;
        ctx.items = &items;
        ctx.recipes = &recipes;
        ctx.world = &world;
        ctx.loop = &loop;
        ctx.gameTime = &gameTime;
        ctx.pathfinding = &pathfinding;
        ctx.combat = &combat;
        ctx.progressionConfig = &progressionConfig;
        ctx.missionConfig = &missionConfig;
        api.Attach(&ctx);
    }

    ecs::Entity MakePlayer(float x, float y) {
        const ecs::Entity e = ecs.Create();
        auto& registry = ecs.Registry();
        registry.emplace<ecs::PlayerTag>(e);
        registry.emplace<ecs::Position>(e, ecs::Position{x, y, 0});
        registry.emplace<ecs::Velocity>(e);
        registry.emplace<ecs::Health>(e, ecs::Health{80.0f, 100.0f});
        ecs::Container bag;
        bag.capacityKg = 20.0f;
        registry.emplace<ecs::Container>(e, bag);
        return e;
    }

    void LoadOpenChunk() {
        world::Chunk chunk(0, 0);
        pathfinding.OnChunkLoaded(chunk);
    }
};

}  // namespace

TEST_CASE("M35-F1-T1：API v2 版本与网络模式") {
    script::ScriptApi unattached;
    CHECK(script::ScriptApi::kApiVersion == 2);
    CHECK(unattached.GetApiVersion() == 2);
    CHECK_FALSE(unattached.IsMultiplayer());
    CHECK(unattached.GetNetMode() == "direct");

    Fixture f;
    CHECK(f.api.GetApiVersion() == 2);
    CHECK_FALSE(f.api.IsMultiplayer());
    CHECK(f.api.GetNetMode() == "direct");

    net::CommandPipeline loopback(
        net::CommandPipeline::Options{net::TransportMode::Loopback});
    f.ctx.commands = &loopback;
    CHECK(f.api.IsMultiplayer());
    CHECK(f.api.GetNetMode() == "loopback");

    f.ctx.commands = nullptr;
    CHECK_FALSE(f.api.IsMultiplayer());
    CHECK(f.api.GetNetMode() == "direct");
}

TEST_CASE("M35-F3-T1：动作结果、命令字段填充与 direct/loopback/KCP 路由") {
    script::ScriptActionResult unbound;
    CHECK_FALSE(unbound.ok);
    CHECK_FALSE(unbound.queued);
    CHECK(unbound.error.empty());

    script::ScriptApi unattached;
    net::Command use;
    use.type = net::CommandType::UseBag;
    const script::ScriptActionResult noContext = unattached.SubmitCommand(use);
    CHECK_FALSE(noContext.ok);
    CHECK_FALSE(noContext.error.empty());

    Fixture f;
    const script::ScriptActionResult noPlayer = f.api.SubmitCommand(use);
    CHECK_FALSE(noPlayer.ok);
    CHECK(noPlayer.error.find("玩家") != std::string::npos);

    const ecs::Entity player = f.MakePlayer(2.0f, 3.0f);
    const std::uint64_t playerId = f.ecs.IdOf(player).id;
    REQUIRE(playerId != 0);
    const script::ScriptActionResult noEntry = f.api.SubmitCommand(use);
    CHECK_FALSE(noEntry.ok);
    CHECK(noEntry.error.find("不支持") != std::string::npos);

    f.loop.Frame(1.0 / 30.0, [](double) {}, [](double, double) {});
    const std::uint64_t tick = f.loop.TotalTicks();
    REQUIRE(tick > 0);

    net::CommandPipeline direct(net::CommandPipeline::Options{});
    std::vector<net::Command> directSeen;
    direct.SetHandler([&](const net::Command& command, std::string&) {
        directSeen.push_back(command);
        return true;
    });
    f.ctx.commands = &direct;
    f.ctx.submitPlayerCommand = [&](const net::Command& command, bool& queued,
                                    std::string& error) {
        const game::PlayerCommandResult routed =
            game::SubmitPlayerCommand(f.ctx, false, command);
        queued = routed.queued;
        error = routed.error;
        return routed.ok;
    };

    net::Command directCommand = use;
    directCommand.playerStableId = 999;
    directCommand.tick = 999;
    directCommand.payload.index = 2;
    const script::ScriptActionResult directResult = f.api.SubmitCommand(directCommand);
    CHECK(directResult.ok);
    CHECK_FALSE(directResult.queued);
    CHECK(directResult.error.empty());
    REQUIRE(directSeen.size() == 1);
    CHECK(directSeen[0].playerStableId == playerId);
    CHECK(directSeen[0].tick == tick);
    CHECK(directSeen[0].payload.index == 2);

    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(loopOptions);
    std::vector<net::Command> loopSeen;
    loop.SetHandler([&](const net::Command& command, std::string&) {
        loopSeen.push_back(command);
        return true;
    });
    f.ctx.commands = &loop;
    const script::ScriptActionResult loopResult = f.api.SubmitCommand(use);
    CHECK(loopResult.ok);
    CHECK(loopResult.queued);
    CHECK(loopSeen.empty());
    REQUIRE(loop.Flush());
    CHECK(loopSeen.empty());
    loop.Pump();
    REQUIRE(loopSeen.size() == 1);
    CHECK(loopSeen[0].playerStableId == playerId);
    CHECK(loopSeen[0].tick == tick);

    f.ctx.commands = &direct;
    f.ctx.networkClient = nullptr;
    const game::PlayerCommandResult noClient =
        game::SubmitPlayerCommand(f.ctx, true, use);
    CHECK_FALSE(noClient.ok);
    CHECK_FALSE(noClient.queued);
    CHECK(noClient.error.find("网络客户端") != std::string::npos);

    game::NetworkClient client;
    f.ctx.networkClient = &client;
    directSeen.clear();
    net::Command kcpCommand = use;
    kcpCommand.playerStableId = playerId;
    const game::PlayerCommandResult kcp =
        game::SubmitPlayerCommand(f.ctx, true, kcpCommand);
    CHECK_FALSE(kcp.ok);
    CHECK_FALSE(kcp.queued);
    CHECK_FALSE(kcp.error.empty());
    CHECK(directSeen.empty());
}

TEST_CASE("M35-F3-T2：物品动作映射、索引/槽位/攻击目标校验") {
    Fixture f;
    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    const std::uint64_t playerId = f.ecs.IdOf(player).id;
    REQUIRE(playerId != 0);
    auto& bag = f.ecs.Registry().get<ecs::Container>(player);
    bag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f});
    bag.items.push_back(ecs::ItemInstance{"axe", 1, 1.0f});

    net::CommandPipeline direct(net::CommandPipeline::Options{});
    std::vector<net::Command> seen;
    direct.SetHandler([&](const net::Command& command, std::string&) {
        seen.push_back(command);
        return true;
    });
    f.ctx.commands = &direct;
    f.ctx.submitPlayerCommand = [&](const net::Command& command, bool& queued,
                                    std::string& error) {
        const game::PlayerCommandResult routed =
            game::SubmitPlayerCommand(f.ctx, false, command);
        queued = routed.queued;
        error = routed.error;
        return routed.ok;
    };

    auto use = f.api.UseItem();
    CHECK(use.ok);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.back().type == net::CommandType::UseBag);
    CHECK(seen.back().payload.index == -1);

    use = f.api.UseItem(1);
    CHECK(use.ok);
    CHECK(seen.back().payload.index == 1);
    CHECK_FALSE(f.api.UseItem(2).ok);
    CHECK_FALSE(f.api.UseItem(-2).ok);

    auto equip = f.api.EquipItem(0);
    CHECK(equip.ok);
    CHECK(seen.back().type == net::CommandType::EquipBag);
    CHECK(seen.back().payload.index == 0);
    CHECK(seen.back().payload.targetIndex == 0);
    equip = f.api.EquipItem(1, "body");
    CHECK(equip.ok);
    CHECK(seen.back().payload.targetIndex == 1);
    CHECK_FALSE(f.api.EquipItem(2, "primary").ok);
    CHECK_FALSE(f.api.EquipItem(0, "hand").ok);

    const auto unequip = f.api.Unequip("body");
    CHECK(unequip.ok);
    CHECK(seen.back().type == net::CommandType::Unequip);
    CHECK(seen.back().payload.index == 1);
    CHECK_FALSE(f.api.Unequip("hand").ok);

    const auto dropAll = f.api.DropItem(-1);
    CHECK(dropAll.ok);
    CHECK(seen.back().type == net::CommandType::DropBag);
    CHECK(seen.back().payload.index == -1);
    const auto dropOne = f.api.DropItem(0);
    CHECK(dropOne.ok);
    CHECK(seen.back().payload.index == 0);
    CHECK_FALSE(f.api.DropItem(2).ok);

    const auto nearest = f.api.PickupNearest();
    CHECK(nearest.ok);
    CHECK(seen.back().type == net::CommandType::PickupNearest);
    CHECK_FALSE(f.api.Pickup(0).ok);
    const auto pickup = f.api.Pickup(987654);
    CHECK(pickup.ok);
    CHECK(seen.back().type == net::CommandType::PickupEntity);
    CHECK(seen.back().payload.entityId == 987654);

    const auto attackAir = f.api.Attack();
    CHECK(attackAir.ok);
    CHECK(seen.back().type == net::CommandType::Attack);
    CHECK(seen.back().payload.entityId == 0);
    CHECK((seen.back().payload.flags & net::kAttackHeavyFlag) == 0);
    const auto attackHeavy = f.api.Attack(0, true);
    CHECK(attackHeavy.ok);
    CHECK((seen.back().payload.flags & net::kAttackHeavyFlag) != 0);

    const ecs::Entity target = f.ecs.Create();
    auto& registry = f.ecs.Registry();
    registry.emplace<ecs::Position>(target, ecs::Position{4.0f, 4.0f, 0});
    registry.emplace<ecs::Health>(target, ecs::Health{50.0f, 100.0f});
    registry.emplace<ecs::AiState>(target);
    const std::uint64_t targetId = f.ecs.IdOf(target).id;
    const auto attackTarget = f.api.Attack(targetId);
    CHECK(attackTarget.ok);
    CHECK(seen.back().payload.entityId == targetId);

    registry.get<ecs::Health>(target).current = 0.0f;
    CHECK_FALSE(f.api.Attack(targetId).ok);

    const ecs::Entity corpse = f.ecs.Create();
    registry.emplace<ecs::Corpse>(corpse);
    CHECK_FALSE(f.api.Attack(f.ecs.IdOf(corpse).id).ok);

    registry.get<ecs::Health>(target).current = 50.0f;
    registry.emplace<ecs::CombatStance>(target).dodgeInvulnLeft = 1.0f;
    const auto invulnerable = f.api.Attack(targetId);
    CHECK_FALSE(invulnerable.ok);
    CHECK(invulnerable.error.find("无敌") != std::string::npos);

    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(loopOptions);
    std::vector<net::Command> loopSeen;
    loop.SetHandler([&](const net::Command& command, std::string&) {
        loopSeen.push_back(command);
        return true;
    });
    f.ctx.commands = &loop;
    const auto queuedUse = f.api.UseItem(0);
    CHECK(queuedUse.ok);
    CHECK(queuedUse.queued);
    CHECK(loopSeen.empty());
}

TEST_CASE("M35-F3-T3：直接状态效果（SP only、伤害管线、需求钳制、经验奖励）") {
    Fixture f;
    script::ScriptApi unattached;
    CHECK_FALSE(unattached.ApplyDamage(1.0f).ok);
    CHECK_FALSE(unattached.AdjustNeeds(0.1f).ok);
    CHECK_FALSE(unattached.GiveXp("melee", 1.0f).result.ok);

    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(loopOptions);
    f.ctx.commands = &loop;
    const auto mpDamage = f.api.ApplyDamage(1.0f);
    const auto mpNeeds = f.api.AdjustNeeds(0.1f);
    const auto mpXp = f.api.GiveXp("melee", 1.0f);
    CHECK_FALSE(mpDamage.ok);
    CHECK_FALSE(mpNeeds.ok);
    CHECK_FALSE(mpXp.result.ok);
    CHECK(mpDamage.error.find("联机") != std::string::npos);
    f.ctx.commands = nullptr;

    CHECK_FALSE(f.api.ApplyDamage(1.0f).ok);
    CHECK_FALSE(f.api.AdjustNeeds(0.1f).ok);
    CHECK_FALSE(f.api.GiveXp("melee", 1.0f).result.ok);

    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    entt::registry& registry = f.ecs.Registry();
    auto& health = registry.get<ecs::Health>(player);
    health.current = 80.0f;
    auto& needs = registry.emplace<ecs::Needs>(
        player, ecs::Needs{0.9f, 0.8f, 0.7f, 1.0f, 1.0f, 0.0f, 0});
    auto& progression = registry.emplace<ecs::Progression>(player);
    registry.emplace<ecs::Facing>(player, ecs::Facing{ecs::Direction::Left});
    registry.emplace<ecs::DamageResist>(
        player, ecs::DamageResist{0.0f, 0.0f, 0.5f, 0.0f, 1.0f});

    CHECK_FALSE(f.api.ApplyDamage(0.0f).ok);
    CHECK_FALSE(f.api.ApplyDamage(-1.0f).ok);
    CHECK_FALSE(f.api.ApplyDamage(
        std::numeric_limits<float>::quiet_NaN()).ok);
    CHECK_FALSE(f.api.ApplyDamage(
        std::numeric_limits<float>::infinity()).ok);
    CHECK_FALSE(f.api.ApplyDamage(10.0f, "poison").ok);
    CHECK(health.current == doctest::Approx(80.0f));

    const auto armored = f.api.ApplyDamage(20.0f, "blunt");
    CHECK(armored.ok);
    CHECK(health.current == doctest::Approx(70.0f));

    const auto debug = f.api.ApplyDamage(10.0f, "debug");
    CHECK(debug.ok);
    CHECK(health.current == doctest::Approx(60.0f));

    auto& stance = registry.emplace<ecs::CombatStance>(player);
    stance.dodgeInvulnLeft = 1.0f;
    health.current = 80.0f;
    const auto dodged = f.api.ApplyDamage(20.0f, "blunt");
    CHECK(dodged.ok);
    CHECK(health.current == doctest::Approx(80.0f));
    CHECK(f.combat.GetStats().dodges == 1u);

    stance.dodgeInvulnLeft = 0.0f;
    stance.blocking = true;
    stance.blockElapsed = 10.0f;
    stance.blockReduction = 0.5f;
    registry.emplace<ecs::CombatStamina>(
        player, ecs::CombatStamina{100.0f, 100.0f, 0.0f, 0.0f});
    health.current = 80.0f;
    const auto blocked = f.api.ApplyDamage(20.0f, "blunt");
    CHECK(blocked.ok);
    CHECK(health.current == doctest::Approx(75.0f));
    CHECK(f.combat.GetStats().blocks == 1u);

    health.current = 80.0f;
    const auto debugThroughBlock = f.api.ApplyDamage(10.0f, "debug");
    CHECK(debugThroughBlock.ok);
    CHECK(health.current == doctest::Approx(70.0f));

    health.current = 0.0f;
    CHECK_FALSE(f.api.ApplyDamage(1.0f).ok);
    health.current = 80.0f;

    f.ctx.combat = nullptr;
    CHECK_FALSE(f.api.ApplyDamage(1.0f).ok);
    f.ctx.combat = &f.combat;

    const auto adjusted = f.api.AdjustNeeds(0.2f, 0.3f, 0.4f);
    CHECK(adjusted.ok);
    CHECK(needs.hunger == doctest::Approx(1.0f));
    CHECK(needs.thirst == doctest::Approx(1.0f));
    CHECK(needs.fatigue == doctest::Approx(1.0f));

    needs.hunger = 0.5f;
    needs.thirst = 0.5f;
    needs.fatigue = 0.5f;
    CHECK_FALSE(f.api.AdjustNeeds(0.1f, -0.1f, 0.1f).ok);
    CHECK(needs.hunger == doctest::Approx(0.5f));
    CHECK(needs.thirst == doctest::Approx(0.5f));
    CHECK(needs.fatigue == doctest::Approx(0.5f));
    CHECK_FALSE(f.api.AdjustNeeds(
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f).ok);
    needs.hunger = -0.5f;
    CHECK(f.api.AdjustNeeds(0.0f, 0.0f, 0.0f).ok);
    CHECK(needs.hunger == doctest::Approx(0.0f));

    registry.remove<ecs::Needs>(player);
    CHECK_FALSE(f.api.AdjustNeeds(0.1f).ok);

    const int levelBefore = progression.level;
    const int skillBefore =
        progression.skills[ecs::SkillIndex(ecs::SkillKind::Melee)].level;
    const auto xp = f.api.GiveXp("melee", 1000.0f);
    CHECK(xp.result.ok);
    CHECK(xp.award.characterLevels > 0);
    CHECK(xp.award.skillLevels > 0);
    CHECK(xp.award.perkPoints == xp.award.characterLevels);
    CHECK(xp.award.characterXp > 0.0f);
    CHECK(xp.award.skillXp > 0.0f);
    CHECK(progression.level == levelBefore + xp.award.characterLevels);
    CHECK(progression.skills[ecs::SkillIndex(ecs::SkillKind::Melee)].level ==
          skillBefore + xp.award.skillLevels);
    CHECK(progression.perkPoints >= xp.award.perkPoints);

    const float xpBefore = progression.xp;
    CHECK_FALSE(f.api.GiveXp("Melee", 10.0f).result.ok);
    CHECK_FALSE(f.api.GiveXp("melee", -1.0f).result.ok);
    CHECK_FALSE(f.api.GiveXp(
        "melee", std::numeric_limits<float>::quiet_NaN()).result.ok);
    CHECK(progression.xp == doctest::Approx(xpBefore));

    f.progressionConfig.enabled = false;
    CHECK_FALSE(f.api.GiveXp("melee", 10.0f).result.ok);
    f.progressionConfig.enabled = true;
    f.ctx.progressionConfig = nullptr;
    CHECK_FALSE(f.api.GiveXp("melee", 10.0f).result.ok);
}

TEST_CASE("M35-F3-T4：制作开始/取消/学习（配方索引、数量、材料/站点/锁定）") {
    Fixture f;
    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    const std::uint64_t playerId = f.ecs.IdOf(player).id;
    REQUIRE(playerId != 0);
    auto& bag = f.ecs.Registry().get<ecs::Container>(player);
    f.ctx.recipeKnown = [&](const std::string& recipeId) {
        const auto* known =
            f.ecs.Registry().try_get<ecs::KnownRecipes>(player);
        return known != nullptr &&
               std::find(known->recipeIds.begin(), known->recipeIds.end(),
                         recipeId) != known->recipeIds.end();
    };

    net::CommandPipeline direct(net::CommandPipeline::Options{});
    std::vector<net::Command> seen;
    direct.SetHandler([&](const net::Command& command, std::string&) {
        seen.push_back(command);
        return true;
    });
    f.ctx.commands = &direct;
    f.ctx.submitPlayerCommand = [&](const net::Command& command, bool& queued,
                                    std::string& error) {
        const game::PlayerCommandResult routed =
            game::SubmitPlayerCommand(f.ctx, false, command);
        queued = routed.queued;
        error = routed.error;
        return routed.ok;
    };

    CHECK_FALSE(f.api.StartCraft("missing", 1).ok);
    CHECK_FALSE(f.api.LearnRecipe("missing").ok);
    CHECK_FALSE(f.api.IsRecipeKnown("base_bandage"));
    CHECK_FALSE(f.api.StartCraft("base_bandage", 1).ok);

    const auto learned = f.api.LearnRecipe("base_bandage");
    CHECK(learned.ok);
    CHECK(f.api.IsRecipeKnown("base_bandage"));
    CHECK_FALSE(f.api.LearnRecipe("base_bandage").ok);

    CHECK_FALSE(f.api.StartCraft("base_bandage", 0).ok);
    CHECK_FALSE(f.api.StartCraft("base_bandage", 100).ok);
    CHECK_FALSE(f.api.StartCraft("base_bandage", 1).ok);
    CHECK(seen.empty());

    bag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    const auto started = f.api.StartCraft("base_bandage", 2);
    CHECK(started.ok);
    CHECK_FALSE(started.queued);
    REQUIRE(seen.size() == 1);
    CHECK(seen.back().type == net::CommandType::InventoryAction);
    CHECK(static_cast<ui::InventoryActionType>(seen.back().payload.flags & 0xFFu) ==
          ui::InventoryActionType::Craft);
    CHECK((seen.back().payload.flags >> 8) == 2);
    CHECK(seen.back().payload.index == 0);

    CHECK_FALSE(f.api.CancelCraft().ok);
    f.ecs.Registry().emplace<ecs::CraftingJob>(player);
    f.ecs.Registry().get<ecs::CraftingJob>(player).active = true;
    f.ecs.Registry().get<ecs::CraftingJob>(player).recipeId = "base_bandage";
    const auto cancelled = f.api.CancelCraft();
    CHECK(cancelled.ok);
    REQUIRE(seen.size() == 2);
    CHECK(seen.back().type == net::CommandType::InventoryAction);
    CHECK(static_cast<ui::InventoryActionType>(seen.back().payload.flags & 0xFFu) ==
          ui::InventoryActionType::CancelCraft);

    items::RecipeRegistry stationRecipes;
    std::string recipeError;
    REQUIRE(stationRecipes.LoadFromString(R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "station_brew",
          "name": "站点测试",
          "output": { "item": "bandage", "count": 1 },
          "ingredients": [ { "item": "bandage", "count": 1 } ],
          "station": "workbench",
          "time": 1.0
        }
      ]
    })", "station-test", f.items, &recipeError));
    f.ctx.recipes = &stationRecipes;
    auto& known = f.ecs.Registry().get<ecs::KnownRecipes>(player);
    known.recipeIds.push_back("station_brew");
    CHECK_FALSE(f.api.StartCraft("station_brew", 1).ok);
    f.ctx.craftStationNearby = [](const items::RecipeDef& recipe) {
        return recipe.station.empty() || recipe.station == "workbench";
    };
    const auto stationStarted = f.api.StartCraft("station_brew", 1);
    CHECK(stationStarted.ok);
    REQUIRE(seen.size() == 3);
    CHECK(seen.back().payload.index == 0);

    f.ctx.recipes = &f.recipes;
    f.ctx.craftStationNearby = {};
    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(loopOptions);
    std::vector<net::Command> loopSeen;
    loop.SetHandler([&](const net::Command& command, std::string&) {
        loopSeen.push_back(command);
        return true;
    });
    f.ctx.commands = &loop;
    const auto queuedStart = f.api.StartCraft("base_bandage", 1);
    CHECK(queuedStart.ok);
    CHECK(queuedStart.queued);
    CHECK(loopSeen.empty());
    const auto queuedCancel = f.api.CancelCraft();
    CHECK(queuedCancel.ok);
    CHECK(queuedCancel.queued);
    CHECK(loopSeen.empty());
    CHECK_FALSE(f.api.LearnRecipe("base_bandage").ok);
}

TEST_CASE("M35-F3-T5：建造动作（SP hook、MP 命令、CanPlace 预检）") {
    Fixture f;
    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    (void)player;
    net::CommandPipeline direct(net::CommandPipeline::Options{});
    std::vector<net::Command> seen;
    direct.SetHandler([&](const net::Command& command, std::string&) {
        seen.push_back(command);
        return true;
    });
    f.ctx.commands = &direct;
    f.ctx.submitPlayerCommand = [&](const net::Command& command, bool& queued,
                                    std::string& error) {
        const game::PlayerCommandResult routed =
            game::SubmitPlayerCommand(f.ctx, false, command);
        queued = routed.queued;
        error = routed.error;
        return routed.ok;
    };
    std::string placedId;
    int placedX = 0;
    int placedY = 0;
    bool placedVertical = false;
    int demolishX = -1;
    int repairX = -1;
    int doorX = -1;
    f.ctx.buildCanPlace = [](const game::BuildableDef&, int wx, int wy,
                             bool, std::string& error) {
        if (wx == 9) {
            error = "目标格被占用";
            return false;
        }
        (void)wy;
        return true;
    };
    f.ctx.buildPlace = [&](const game::BuildableDef& def, int wx, int wy,
                           bool vertical, std::string& error) {
        if (wx == 9) {
            error = "目标格被占用";
            return false;
        }
        placedId = def.id;
        placedX = wx;
        placedY = wy;
        placedVertical = vertical;
        return true;
    };
    f.ctx.buildDemolish = [&](int wx, int, std::string&) {
        demolishX = wx;
        return true;
    };
    f.ctx.buildRepair = [&](int wx, int, std::string&) {
        repairX = wx;
        return true;
    };
    f.ctx.buildToggleDoor = [&](int wx, int, std::string&) {
        doorX = wx;
        return true;
    };

    CHECK_FALSE(f.api.CanPlaceBuild("missing", 5, 5).ok);
    CHECK_FALSE(f.api.CanPlaceBuild("barricade", 5, 5, true).ok);
    const auto blocked = f.api.CanPlaceBuild("barricade", 9, 5);
    CHECK_FALSE(blocked.ok);
    CHECK(blocked.error.find("占用") != std::string::npos);
    CHECK(f.api.CanPlaceBuild("barricade", 5, 5).ok);
    CHECK_FALSE(f.api.PlaceBuild("barricade", 9, 6).ok);
    CHECK(f.api.PlaceBuild("barricade", 5, 6).ok);
    CHECK(placedId == "barricade");
    CHECK(placedX == 5);
    CHECK(placedY == 6);
    CHECK_FALSE(placedVertical);
    CHECK(f.api.PlaceBuild("door", 6, 6, true).ok);
    CHECK(placedId == "door");
    CHECK(placedVertical);
    CHECK(f.api.DemolishBuild(7, 6).ok);
    CHECK(demolishX == 7);
    CHECK(f.api.RepairBuild(8, 6).ok);
    CHECK(repairX == 8);
    CHECK(f.api.ToggleBuildDoor(9, 6).ok);
    CHECK(doorX == 9);
    CHECK(seen.empty());

    int doorIndex = -1;
    script::ScriptBuildableList buildables;
    REQUIRE(f.api.ListBuildables(buildables));
    for (const script::ScriptBuildableInfo& info : buildables.buildables) {
        if (info.id == "door") {
            doorIndex = info.index;
            break;
        }
    }
    REQUIRE(doorIndex >= 0);

    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(loopOptions);
    std::vector<net::Command> loopSeen;
    loop.SetHandler([&](const net::Command& command, std::string&) {
        loopSeen.push_back(command);
        return true;
    });
    f.ctx.commands = &loop;
    const auto mpCanPlace = f.api.CanPlaceBuild("door", 7, 8);
    CHECK_FALSE(mpCanPlace.ok);
    CHECK(mpCanPlace.error.find("联机") != std::string::npos);
    const auto placed = f.api.PlaceBuild("door", 7, 8, true);
    CHECK(placed.ok);
    CHECK(placed.queued);
    CHECK(loopSeen.empty());
    REQUIRE(loop.Flush());
    loop.Pump();
    REQUIRE(loopSeen.size() == 1);
    CHECK(loopSeen.back().type == net::CommandType::PlaceStructure);
    CHECK(loopSeen.back().payload.x == doctest::Approx(7.0f));
    CHECK(loopSeen.back().payload.y == doctest::Approx(8.0f));
    CHECK(loopSeen.back().payload.index == doorIndex);
    CHECK((loopSeen.back().payload.flags & 1U) != 0);
    CHECK_FALSE(f.api.PlaceBuild("missing", 7, 8).ok);

    const auto demolished = f.api.DemolishBuild(7, 8);
    CHECK(demolished.ok);
    CHECK(demolished.queued);
    const auto repaired = f.api.RepairBuild(7, 8);
    CHECK(repaired.ok);
    const auto toggled = f.api.ToggleBuildDoor(7, 8);
    CHECK(toggled.ok);
    REQUIRE(loop.Flush());
    loop.Pump();
    REQUIRE(loopSeen.size() == 4);
    CHECK(loopSeen[1].type == net::CommandType::DemolishStructure);
    CHECK(loopSeen[1].payload.x == doctest::Approx(7.0f));
    CHECK(loopSeen[1].payload.flags == 0);
    CHECK(loopSeen[2].type == net::CommandType::RepairStructure);
    CHECK(loopSeen[3].type == net::CommandType::ToggleDoor);

    Fixture noPlayer;
    CHECK_FALSE(noPlayer.api.CanPlaceBuild("barricade", 1, 1).ok);
    CHECK_FALSE(noPlayer.api.PlaceBuild("barricade", 1, 1).ok);
    CHECK_FALSE(noPlayer.api.DemolishBuild(1, 1).ok);
    CHECK_FALSE(noPlayer.api.RepairBuild(1, 1).ok);
    CHECK_FALSE(noPlayer.api.ToggleBuildDoor(1, 1).ok);
}

TEST_CASE("M35-F1-T2：Player 生存状态查询") {
    Fixture f;
    script::ScriptNeeds needs;
    script::ScriptStamina stamina;
    CHECK_FALSE(f.api.GetPlayerNeeds(needs));
    CHECK_FALSE(f.api.GetPlayerStamina(stamina));

    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    CHECK_FALSE(f.api.GetPlayerNeeds(needs));
    CHECK_FALSE(f.api.GetPlayerStamina(stamina));

    entt::registry& registry = f.ecs.Registry();
    registry.emplace<ecs::Needs>(
        player, ecs::Needs{0.2f, 0.3f, 0.4f, 0.9f, 0.8f, 0.0f, 2});
    registry.emplace<ecs::FoodState>(player, ecs::FoodState{0.6f, 0.0f, 0.0f, 0.0f});
    registry.emplace<ecs::CombatStamina>(
        player, ecs::CombatStamina{30.0f, 100.0f, 0.0f, 1.5f});

    REQUIRE(f.api.GetPlayerNeeds(needs));
    CHECK(needs.hunger == doctest::Approx(0.2f));
    CHECK(needs.thirst == doctest::Approx(0.3f));
    CHECK(needs.fatigue == doctest::Approx(0.4f));
    CHECK(needs.nutrition == doctest::Approx(0.6f));
    CHECK(needs.health == doctest::Approx(80.0f));
    CHECK(needs.healthMax == doctest::Approx(100.0f));
    CHECK(needs.moveScale == doctest::Approx(0.9f));
    CHECK(needs.damageScale == doctest::Approx(0.8f));
    CHECK(needs.warning == 2);

    REQUIRE(f.api.GetPlayerStamina(stamina));
    CHECK(stamina.current == doctest::Approx(30.0f));
    CHECK(stamina.max == doctest::Approx(100.0f));
    CHECK(stamina.exhausted);

    auto* ecsNeeds = registry.try_get<ecs::Needs>(player);
    REQUIRE(ecsNeeds != nullptr);
    ecsNeeds->hunger = std::numeric_limits<float>::quiet_NaN();
    ecsNeeds->moveScale = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(f.api.GetPlayerNeeds(needs));
    CHECK(needs.hunger == doctest::Approx(0.0f));
    CHECK(needs.moveScale == doctest::Approx(1.0f));

    auto* ecsStamina = registry.try_get<ecs::CombatStamina>(player);
    REQUIRE(ecsStamina != nullptr);
    ecsStamina->current = 150.0f;
    REQUIRE(f.api.GetPlayerStamina(stamina));
    CHECK(stamina.current == doctest::Approx(100.0f));
}

TEST_CASE("M35-F1-T3：Player 战斗与体温状态查询") {
    Fixture f;
    script::ScriptWounds wounds;
    script::ScriptPain pain;
    script::ScriptInfection infection;
    script::ScriptClimate climate;
    CHECK_FALSE(f.api.GetPlayerWounds(wounds));
    CHECK_FALSE(f.api.GetPlayerPain(pain));
    CHECK_FALSE(f.api.GetPlayerInfection(infection));
    CHECK_FALSE(f.api.GetPlayerClimate(climate));

    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    entt::registry& registry = f.ecs.Registry();

    ecs::Wounds sourceWounds;
    sourceWounds.entries[0].source = ecs::DamageType::Bite;
    sourceWounds.entries[0].severity = 0.5f;
    sourceWounds.entries[0].bleedRate = 0.2f;
    sourceWounds.entries[0].bleedRemaining = 3.0f;
    sourceWounds.entries[1].source = ecs::DamageType::Claw;
    sourceWounds.entries[1].severity = 0.3f;
    sourceWounds.entries[1].bandaged = true;
    sourceWounds.count = 2;
    registry.emplace<ecs::Wounds>(player, sourceWounds);

    ecs::Pain sourcePain;
    sourcePain.baseLevel = 0.3f;
    sourcePain.level = 0.45f;
    sourcePain.reliefRemaining = 8.0f;
    registry.emplace<ecs::Pain>(player, sourcePain);

    ecs::Infection sourceInfection;
    sourceInfection.stage = ecs::InfectionStage::Fever;
    sourceInfection.progress = 0.42f;
    sourceInfection.slowRemaining = 12.0f;
    sourceInfection.timeToNextStage = 30.0f;
    registry.emplace<ecs::Infection>(player, sourceInfection);

    ecs::Climate sourceClimate;
    sourceClimate.bodyC = 38.5f;
    sourceClimate.wetness = 0.25f;
    sourceClimate.ambientC = 5.0f;
    sourceClimate.effectiveC = 16.0f;
    sourceClimate.insulation = 0.7f;
    sourceClimate.waterproof = 0.4f;
    sourceClimate.indoor = true;
    sourceClimate.nearFire = true;
    sourceClimate.warning = 1;
    registry.emplace<ecs::Climate>(player, sourceClimate);

    REQUIRE(f.api.GetPlayerWounds(wounds));
    CHECK(wounds.count == 2);
    REQUIRE(wounds.wounds.size() == 2);
    CHECK(wounds.wounds[0].type == "bite");
    CHECK(wounds.wounds[0].severity == doctest::Approx(0.5f));
    CHECK(wounds.wounds[0].bleeding);
    CHECK_FALSE(wounds.wounds[0].bandaged);
    CHECK(wounds.wounds[1].type == "claw");
    CHECK_FALSE(wounds.wounds[1].bleeding);
    CHECK(wounds.wounds[1].bandaged);

    REQUIRE(f.api.GetPlayerPain(pain));
    CHECK(pain.level == doctest::Approx(0.45f));
    CHECK(pain.baseLevel == doctest::Approx(0.3f));
    CHECK(pain.reliefRemaining == doctest::Approx(8.0f));

    REQUIRE(f.api.GetPlayerInfection(infection));
    CHECK(infection.stage == "fever");
    CHECK(infection.progress == doctest::Approx(0.42f));
    CHECK(infection.slowRemaining == doctest::Approx(12.0f));
    CHECK(infection.timeToNextStage == doctest::Approx(30.0f));

    REQUIRE(f.api.GetPlayerClimate(climate));
    CHECK(climate.bodyC == doctest::Approx(38.5f));
    CHECK(climate.wetness == doctest::Approx(0.25f));
    CHECK(climate.ambientC == doctest::Approx(5.0f));
    CHECK(climate.effectiveC == doctest::Approx(16.0f));
    CHECK(climate.insulation == doctest::Approx(0.7f));
    CHECK(climate.waterproof == doctest::Approx(0.4f));
    CHECK(climate.indoor);
    CHECK(climate.nearFire);
    CHECK(climate.warning == 1);
}

TEST_CASE("M35-F1-T4：Player 成长状态查询") {
    Fixture f;
    script::ScriptProgression progression;
    CHECK_FALSE(f.api.GetPlayerProgression(progression));

    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    CHECK_FALSE(f.api.GetPlayerProgression(progression));

    entt::registry& registry = f.ecs.Registry();
    auto& source = registry.emplace<ecs::Progression>(player);
    source.level = 3;
    source.xp = 50.0f;
    source.perkPoints = 2;
    source.skills[ecs::SkillIndex(ecs::SkillKind::Melee)] = {2, 10.0f};
    source.perks = {"athletic"};

    REQUIRE(f.api.GetPlayerProgression(progression));
    CHECK(progression.level == 3);
    CHECK(progression.xp == doctest::Approx(50.0f));
    CHECK(progression.xpNext ==
          doctest::Approx(game::CharacterXpToNext(source, f.progressionConfig)));
    CHECK(progression.perkPoints == 2);
    REQUIRE(progression.perks.size() == 1);
    CHECK(progression.perks[0] == "athletic");
    REQUIRE(progression.skills.size() == ecs::kSkillCount);
    CHECK(progression.skills[0].id == "melee");
    CHECK(progression.skills[0].level == 2);
    CHECK(progression.skills[0].xp == doctest::Approx(10.0f));
    CHECK(progression.skills[0].xpNext == doctest::Approx(game::SkillXpToNext(
                                                source, ecs::SkillKind::Melee,
                                                f.progressionConfig)));
    CHECK(progression.skills[3].id == "survival");

    source.level = f.progressionConfig.maxLevel;
    for (ecs::SkillProgress& skill : source.skills) {
        skill.level = f.progressionConfig.maxSkillLevel;
    }
    REQUIRE(f.api.GetPlayerProgression(progression));
    CHECK(progression.xpNext == doctest::Approx(0.0f));
    CHECK(progression.skills[0].xpNext == doctest::Approx(0.0f));

    f.ctx.progressionConfig = nullptr;
    CHECK_FALSE(f.api.GetPlayerProgression(progression));
}

TEST_CASE("M35-F1-T5：Player 背包与装备查询") {
    Fixture f;
    script::ScriptInventory inventory;
    script::ScriptEquipment equipment;
    CHECK_FALSE(f.api.GetPlayerInventory(0, 0, inventory));
    CHECK_FALSE(f.api.GetPlayerEquipment(equipment));

    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    entt::registry& registry = f.ecs.Registry();
    auto& bag = registry.get<ecs::Container>(player);

    REQUIRE(f.api.GetPlayerInventory(0, 0, inventory));
    CHECK(inventory.totalStacks == 0);
    CHECK(inventory.stacks.empty());
    CHECK_FALSE(inventory.truncated);
    CHECK(inventory.capacityKg == doctest::Approx(20.0f));
    REQUIRE(f.api.GetPlayerEquipment(equipment));
    CHECK_FALSE(equipment.hasPrimary);
    CHECK_FALSE(equipment.hasBody);

    bag.items.push_back(ecs::ItemInstance{"bandage", 3, 1.0f, 0});
    bag.items.push_back(ecs::ItemInstance{"axe", 1, 0.5f, 0});
    bag.items.push_back(ecs::ItemInstance{"ghost_item", 2, 1.0f, 0});

    REQUIRE(f.api.GetPlayerInventory(0, 2, inventory));
    CHECK(inventory.totalStacks == 3);
    REQUIRE(inventory.stacks.size() == 2);
    CHECK(inventory.truncated);
    CHECK(inventory.weightKg == doctest::Approx(3.3f));
    CHECK(inventory.stacks[0].index == 0);
    CHECK(inventory.stacks[0].defId == "bandage");
    CHECK(inventory.stacks[0].name == "绷带");
    CHECK(inventory.stacks[0].type == "consumable");
    CHECK(inventory.stacks[0].count == 3);
    CHECK(inventory.stacks[0].weightKg == doctest::Approx(0.3f));
    CHECK(inventory.stacks[0].maxStack == 5);
    CHECK_FALSE(inventory.stacks[0].usable);
    CHECK_FALSE(inventory.stacks[0].equippable);
    CHECK(inventory.stacks[1].defId == "axe");
    CHECK(inventory.stacks[1].type == "weapon");
    CHECK(inventory.stacks[1].condition == doctest::Approx(0.5f));
    CHECK(inventory.stacks[1].equippable);
    CHECK(inventory.stacks[1].maxStack == 1);

    REQUIRE(f.api.GetPlayerInventory(2, 2, inventory));
    REQUIRE(inventory.stacks.size() == 1);
    CHECK_FALSE(inventory.truncated);
    CHECK(inventory.stacks[0].index == 2);
    CHECK(inventory.stacks[0].defId == "ghost_item");
    CHECK(inventory.stacks[0].name.empty());
    CHECK(inventory.stacks[0].type.empty());
    CHECK(inventory.stacks[0].weightKg == doctest::Approx(0.0f));
    CHECK(inventory.stacks[0].maxStack == 0);

    REQUIRE(f.api.GetPlayerInventory(99, 2, inventory));
    CHECK(inventory.stacks.empty());
    CHECK_FALSE(inventory.truncated);

    auto& ecsEquipment = registry.emplace<ecs::Equipment>(player);
    ecsEquipment.slots[0] = ecs::ItemInstance{"axe", 1, 0.4f, 0};
    REQUIRE(f.api.GetPlayerEquipment(equipment));
    CHECK(equipment.hasPrimary);
    CHECK_FALSE(equipment.hasBody);
    CHECK(equipment.primary.index == -1);
    CHECK(equipment.primary.defId == "axe");
    CHECK(equipment.primary.condition == doctest::Approx(0.4f));

    CHECK(f.api.CountPlayerItem("bandage") == 3);
    CHECK(f.api.CountPlayerItem("ghost_item") == 2);
    CHECK(f.api.CountPlayerItem("missing") == 0);
    CHECK(f.api.CountPlayerItem("") == 0);
    CHECK(f.api.HasPlayerItem("bandage", 3));
    CHECK_FALSE(f.api.HasPlayerItem("bandage", 4));
    CHECK(f.api.HasPlayerItem("ghost_item", 2));
    CHECK_FALSE(f.api.HasPlayerItem("bandage", 0));
}

TEST_CASE("M35-F1-T6：World 天气与实体查询") {
    Fixture f;
    script::ScriptWeather weather;
    REQUIRE(f.api.GetWeather(weather));
    CHECK(weather.kind == "clear");
    CHECK(weather.ambientC == doctest::Approx(24.0f));
    CHECK(weather.isNight == f.gameTime.IsNight());
    CHECK(f.api.GetTimeOfDay() == doctest::Approx(f.gameTime.DayFraction()));

    script::ScriptEntityInfo info;
    CHECK_FALSE(f.api.GetEntity(0, info));
    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    entt::registry& registry = f.ecs.Registry();
    const std::uint64_t playerId = f.ecs.IdOf(player).id;

    const auto makeAt = [&](float x, float y, int z = 0) {
        const ecs::Entity entity = f.ecs.Create();
        registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
        return entity;
    };

    const ecs::Entity zombie = makeAt(5.0f, 4.0f);
    registry.emplace<ecs::Health>(zombie, ecs::Health{50.0f, 100.0f});
    registry.emplace<ecs::ZombieVariant>(
        zombie, ecs::ZombieVariant{ecs::ZombieVariantKind::Fast, 1.5f, 1.0f, 1.0f});

    const ecs::Entity deadZombie = makeAt(3.5f, 3.5f);
    registry.emplace<ecs::Health>(deadZombie, ecs::Health{0.0f, 100.0f});
    registry.emplace<ecs::ZombieVariant>(deadZombie, ecs::ZombieVariant{});

    const ecs::Entity animal = makeAt(4.0f, 5.0f);
    registry.emplace<ecs::Animal>(
        animal, ecs::Animal{"boar", ecs::AnimalKind::Passive});

    const ecs::Entity item = makeAt(4.0f, 4.0f);
    registry.emplace<ecs::ItemInstance>(
        item, ecs::ItemInstance{"bandage", 2, 1.0f, 0});

    const ecs::Entity loot = makeAt(3.0f, 5.0f);
    registry.emplace<ecs::Container>(
        loot, ecs::Container{{{"bandage", 3, 1.0f, 0}}, 10.0f});
    registry.emplace<ecs::LootContainer>(
        loot, ecs::LootContainer{"loot_a", 0, false});

    const ecs::Entity storage = makeAt(2.0f, 4.0f);
    registry.emplace<ecs::Container>(
        storage, ecs::Container{{{"axe", 1, 1.0f, 0}}, 30.0f});
    registry.emplace<ecs::PlayerStorage>(storage, ecs::PlayerStorage{});

    const ecs::Entity companion = makeAt(3.0f, 3.0f);
    registry.emplace<ecs::Health>(companion, ecs::Health{60.0f, 100.0f});
    registry.emplace<game::Companion>(
        companion, game::Companion{"comp_a", playerId, ""});
    auto& companionState =
        registry.emplace<game::CompanionState>(companion, game::CompanionState{});
    companionState.command = game::CompanionCommand::Stay;
    companionState.downed = true;

    const ecs::Entity npc = makeAt(2.5f, 4.5f);
    auto& settlementNpc =
        registry.emplace<game::SettlementNpc>(npc, game::SettlementNpc{});
    settlementNpc.settlementId = "settlement_a";
    settlementNpc.activity = game::NpcActivity::Patrol;

    const ecs::Entity vehicle = makeAt(7.0f, 4.0f);
    registry.emplace<ecs::Vehicle>(vehicle, ecs::Vehicle{});

    const ecs::Entity fire = makeAt(3.0f, 4.0f);
    ecs::FireSource fireSource;
    fireSource.materialId = "wood";
    fireSource.ownerStableId = 42;
    fireSource.phase = ecs::FirePhase::Burning;
    registry.emplace<ecs::FireSource>(fire, fireSource);

    const ecs::Entity corpse = makeAt(6.0f, 4.0f);
    registry.emplace<ecs::Corpse>(corpse, ecs::Corpse{});

    const ecs::Entity upstairs = makeAt(3.0f, 4.0f, 1);
    registry.emplace<ecs::Animal>(
        upstairs, ecs::Animal{"deer", ecs::AnimalKind::Passive});

    REQUIRE(f.api.GetEntity(playerId, info));
    CHECK(info.kind == "player");
    CHECK(info.hasHealth);
    CHECK(info.health == doctest::Approx(80.0f));

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(zombie).id, info));
    CHECK(info.kind == "zombie");
    CHECK(info.subKind == "fast");
    CHECK(info.health == doctest::Approx(50.0f));

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(animal).id, info));
    CHECK(info.kind == "animal");
    CHECK(info.defId == "boar");
    CHECK(info.subKind == "passive");

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(item).id, info));
    CHECK(info.kind == "item");
    CHECK(info.defId == "bandage");
    CHECK(info.count == 2);

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(loot).id, info));
    CHECK(info.kind == "container");
    CHECK(info.tableId == "loot_a");
    CHECK_FALSE(info.searched);

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(storage).id, info));
    CHECK(info.kind == "storage");

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(companion).id, info));
    CHECK(info.kind == "companion");
    CHECK(info.defId == "comp_a");
    CHECK(info.ownerId == playerId);
    CHECK(info.downed);
    CHECK(info.command == "stay");

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(npc).id, info));
    CHECK(info.kind == "npc");
    CHECK(info.defId == "settlement_a");
    CHECK(info.subKind == "patrol");

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(vehicle).id, info));
    CHECK(info.kind == "vehicle");

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(fire).id, info));
    CHECK(info.kind == "fire");
    CHECK(info.defId == "wood");
    CHECK(info.subKind == "burning");
    CHECK(info.ownerId == 42);

    REQUIRE(f.api.GetEntity(f.ecs.IdOf(corpse).id, info));
    CHECK(info.kind == "corpse");

    script::ScriptEntityQuery query;
    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 0, 2.5f, "any", 0, query));
    CHECK(query.count == 9);
    CHECK_FALSE(query.truncated);
    REQUIRE(query.entities.size() == 9);
    CHECK(query.entities.front().kind == "player");
    for (const script::ScriptEntityInfo& entity : query.entities) {
        CHECK(entity.id != f.ecs.IdOf(deadZombie).id);
        CHECK(entity.id != f.ecs.IdOf(vehicle).id);
        CHECK(entity.id != f.ecs.IdOf(corpse).id);
        CHECK(entity.id != f.ecs.IdOf(upstairs).id);
    }

    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 0, 2.5f, "animal", 0, query));
    REQUIRE(query.count == 1);
    REQUIRE(query.entities.size() == 1);
    CHECK(query.entities[0].defId == "boar");

    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 0, 2.5f, "zombie", 0, query));
    REQUIRE(query.count == 1);
    CHECK(query.entities[0].id == f.ecs.IdOf(zombie).id);

    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 0, 2.5f, "any", 2, query));
    CHECK(query.count == 2);
    CHECK(query.truncated);
    REQUIRE(query.entities.size() == 2);

    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 0, 1.0f, "any", 0, query));
    CHECK(query.count == 7);

    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 1, 1.0f, "any", 0, query));
    REQUIRE(query.count == 1);
    CHECK(query.entities[0].id == f.ecs.IdOf(upstairs).id);

    REQUIRE(f.api.QueryEntities(3.0f, 4.0f, 0, 2.5f, "unknown", 0, query));
    CHECK(query.count == 0);
    CHECK(query.entities.empty());

    script::ScriptContainerInfo container;
    REQUIRE(f.api.GetEntityContainer(f.ecs.IdOf(loot).id, container));
    CHECK(container.tableId == "loot_a");
    CHECK_FALSE(container.searched);
    CHECK_FALSE(container.isStorage);
    CHECK(container.totalStacks == 0);
    CHECK(container.stacks.empty());

    registry.get<ecs::LootContainer>(loot).searched = true;
    REQUIRE(f.api.GetEntityContainer(f.ecs.IdOf(loot).id, container));
    CHECK(container.searched);
    CHECK(container.totalStacks == 1);
    REQUIRE(container.stacks.size() == 1);
    CHECK(container.stacks[0].defId == "bandage");
    CHECK(container.weightKg == doctest::Approx(0.3f));

    REQUIRE(f.api.GetEntityContainer(f.ecs.IdOf(storage).id, container));
    CHECK(container.isStorage);
    CHECK(container.searched);
    CHECK(container.totalStacks == 1);
    CHECK(container.capacityKg == doctest::Approx(30.0f));

    CHECK_FALSE(f.api.GetEntityContainer(f.ecs.IdOf(fire).id, container));
    CHECK_FALSE(f.api.GetEntityContainer(0, container));
}

TEST_CASE("M35-F5-T4：Entities.Query 上限、截断日志与每 mod/tick 统计") {
    Fixture f;
    auto& registry = f.ecs.Registry();
    for (int i = 0; i < 140; ++i) {
        const ecs::Entity entity = f.ecs.Create();
        registry.emplace<ecs::Position>(
            entity, ecs::Position{static_cast<float>(i % 14),
                                  static_cast<float>(i / 14), 0});
    }
    // 半径 100 会被钳制到 32；位于 (100,100) 的动物不应出现在结果里。
    const ecs::Entity farAnimal = f.ecs.Create();
    registry.emplace<ecs::Position>(farAnimal,
                                    ecs::Position{100.0f, 100.0f, 0});
    registry.emplace<ecs::Animal>(
        farAnimal, ecs::Animal{"deer", ecs::AnimalKind::Passive});

    LogCapture logs;
    script::ScriptEntityQuery query;

    // 半径上限：kind=animal 只命中远处动物，钳制后应为空。
    REQUIRE(f.api.QueryEntitiesForMod("guard_mod", 5.0f, 5.0f, 0, 100.0f,
                                      "animal", 128, query));
    CHECK(query.count == 0);
    CHECK_FALSE(query.truncated);

    // 默认 limit=32。
    REQUIRE(f.api.QueryEntitiesForMod("guard_mod", 5.0f, 5.0f, 0, 32.0f,
                                      "any", 0, query));
    CHECK(query.count == 32);
    CHECK(query.truncated);

    // limit 上限 128；140 条匹配只能返回 128 条。
    REQUIRE(f.api.QueryEntitiesForMod("guard_mod", 5.0f, 5.0f, 0, 32.0f,
                                      "any", 999, query));
    CHECK(query.count == 128);
    CHECK(query.truncated);

    // 小半径查询不截断。
    REQUIRE(f.api.QueryEntitiesForMod("other_mod", 5.0f, 5.0f, 0, 1.0f,
                                      "any", 128, query));
    CHECK(query.count == 5);
    CHECK_FALSE(query.truncated);
    REQUIRE(f.api.QueryEntitiesForMod("other_mod", 5.0f, 5.0f, 0, 1.0f,
                                      "any", 128, query));
    CHECK(query.count == 5);
    CHECK_FALSE(query.truncated);

    f.api.FlushQueryStats();
    const std::string text = logs.Str();
    CHECK(text.find("[guard_mod] Entities.Query 截断") != std::string::npos);
    CHECK(text.find("[guard_mod] 查询统计：tick=0 次数=3 截断=2") !=
          std::string::npos);
    CHECK(text.find("[other_mod] 查询统计：tick=0 次数=2 截断=0") !=
          std::string::npos);
    const auto occurrences = [](const std::string& haystack,
                                const std::string& needle) {
        std::size_t count = 0;
        std::size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.size();
        }
        return count;
    };
    CHECK(occurrences(text, "[guard_mod] Entities.Query 截断") == 1);

    // 新 tick 从头计数，不沿用上一 tick 的 3 次。
    f.loop.Frame(1.0 / 60.0, [](double) {}, [](double, double) {});
    LogCapture nextTickLogs;
    REQUIRE(f.api.QueryEntitiesForMod("guard_mod", 5.0f, 5.0f, 0, 1.0f,
                                      "any", 128, query));
    f.api.FlushQueryStats();
    CHECK(nextTickLogs.Str().find(
              "[guard_mod] 查询统计：tick=1 次数=1 截断=0") !=
          std::string::npos);

    // 性能基线：140 个实体、半径 32、默认 limit 32；取 20 次平均，避免单次抖动。
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        REQUIRE(f.api.QueryEntitiesForMod("perf_mod", 5.0f, 5.0f, 0, 32.0f,
                                          "any", 0, query));
    }
    const double avgMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count() /
        20.0;
    MESSAGE("140 实体半径查询平均 " << avgMs << " ms/次");
    CHECK(avgMs < 1.0);
    f.api.FlushQueryStats();
}

TEST_CASE("M35-F1-T7：Items/Craft/Build/Mission 目录查询") {
    Fixture f;

    script::ScriptItemInfo item;
    CHECK_FALSE(f.api.GetItemInfo("missing", item));
    REQUIRE(f.api.GetItemInfo("bandage", item));
    CHECK(item.id == "bandage");
    CHECK(item.name == "绷带");
    CHECK(item.type == "consumable");
    CHECK(item.weight == doctest::Approx(0.1f));
    CHECK(item.maxStack == 5);
    CHECK_FALSE(item.equippable);
    CHECK_FALSE(item.throwable);
    CHECK_FALSE(item.ranged);
    REQUIRE(f.api.GetItemInfo("axe", item));
    CHECK(item.type == "weapon");
    CHECK(item.equippable);

    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    entt::registry& registry = f.ecs.Registry();

    script::ScriptRecipeList recipes;
    REQUIRE(f.api.ListRecipes(recipes));
    CHECK(recipes.count == 1);
    CHECK_FALSE(recipes.truncated);
    REQUIRE(recipes.recipes.size() == 1);
    CHECK(recipes.recipes[0].id == "base_bandage");
    CHECK(recipes.recipes[0].outputDefId == "bandage");
    CHECK(recipes.recipes[0].outputLabel == "绷带");
    CHECK(recipes.recipes[0].known);
    CHECK_FALSE(recipes.recipes[0].canCraft);
    REQUIRE(recipes.recipes[0].ingredients.size() == 1);
    CHECK(recipes.recipes[0].ingredients[0].defId == "bandage");
    CHECK(recipes.recipes[0].ingredients[0].required == 1);
    CHECK(recipes.recipes[0].ingredients[0].have == 0);

    CHECK(f.api.IsRecipeKnown("base_bandage"));
    CHECK_FALSE(f.api.IsRecipeKnown("missing"));

    script::ScriptCraftState craftState;
    REQUIRE(f.api.GetCraftState(craftState));
    CHECK_FALSE(craftState.active);
    CHECK(craftState.queue.empty());

    auto& job = registry.emplace<ecs::CraftingJob>(player);
    job.active = true;
    job.recipeId = "base_bandage";
    job.elapsed = 0.5f;
    job.duration = 1.0f;
    job.batchTotal = 2;
    job.batchCompleted = 1;
    job.queue.push_back(ecs::CraftQueueItem{"base_bandage", 1});
    REQUIRE(f.api.GetCraftState(craftState));
    CHECK(craftState.active);
    CHECK(craftState.recipeId == "base_bandage");
    CHECK(craftState.outputDefId == "bandage");
    CHECK(craftState.outputLabel == "绷带");
    CHECK(craftState.outputCount == 1);
    CHECK(craftState.progress == doctest::Approx(0.5f));
    CHECK(craftState.batchTotal == 2);
    CHECK(craftState.batchCompleted == 1);
    REQUIRE(craftState.queue.size() == 1);
    CHECK(craftState.queue[0].recipeId == "base_bandage");

    ui::InventoryState uiState;
    uiState.crafting.active = true;
    uiState.crafting.recipeId = "base_bandage";
    uiState.crafting.outputLabel = "绷带";
    uiState.crafting.outputCount = 1;
    uiState.crafting.elapsed = 2.0f;
    uiState.crafting.duration = 4.0f;
    uiState.crafting.progress = 0.5f;
    uiState.crafting.batchCount = 1;
    ui::InventoryRecipe uiRecipe;
    uiRecipe.id = "base_bandage";
    uiRecipe.known = false;
    uiRecipe.canCraft = false;
    uiRecipe.station = "workbench";
    uiRecipe.stationLabel = "工作台";
    uiRecipe.stationNearby = false;
    uiRecipe.estimatedSeconds = 3.0f;
    uiState.recipes.push_back(uiRecipe);
    f.ctx.getInventoryState = [&](ui::InventoryState& state) {
        state = uiState;
        return true;
    };
    REQUIRE(f.api.GetCraftState(craftState));
    CHECK(craftState.elapsed == doctest::Approx(2.0f));
    CHECK(craftState.duration == doctest::Approx(4.0f));
    REQUIRE(f.api.ListRecipes(recipes));
    CHECK_FALSE(recipes.recipes[0].known);
    CHECK_FALSE(recipes.recipes[0].stationNearby);
    CHECK(recipes.recipes[0].stationLabel == "工作台");
    CHECK(recipes.recipes[0].seconds == doctest::Approx(3.0f));
    CHECK_FALSE(f.api.IsRecipeKnown("base_bandage"));
    f.ctx.getInventoryState = {};

    script::ScriptBuildableList buildables;
    REQUIRE(f.api.ListBuildables(buildables));
    CHECK(buildables.count > 0);
    CHECK(static_cast<int>(buildables.buildables.size()) == buildables.count);
    CHECK(buildables.buildables[0].index == 0);
    CHECK_FALSE(buildables.buildables[0].id.empty());
    CHECK_FALSE(buildables.buildables[0].layer.empty());
    CHECK_FALSE(buildables.truncated);

    f.missionConfig.enabled = true;
    const auto makeMission = [](std::string id, std::string name,
                                game::MissionKind kind, int minDay) {
        game::MissionDef def;
        def.id = std::move(id);
        def.name = std::move(name);
        def.chain = "main";
        def.order = 1;
        def.kind = kind;
        def.source = game::MissionSource::Radio;
        def.task = game::MissionTaskKind::Generic;
        def.minDay = minDay;
        def.autoActivate = true;
        def.objectives.push_back(game::MissionObjectiveDef{
            game::MissionObjectiveKind::CollectItem, "bandage", 2, 0.0f, 0.0f,
            1.5f, "收集绷带"});
        def.reward.xp = 10.0f;
        def.reward.skill = "survival";
        def.reward.item = "bandage";
        def.reward.itemCount = 1;
        return def;
    };
    f.missionConfig.defs.push_back(
        makeMission("m_active", "进行中", game::MissionKind::Once, 1));
    f.missionConfig.defs.push_back(
        makeMission("m_completed", "已完成", game::MissionKind::Once, 1));
    f.missionConfig.defs.push_back(
        makeMission("m_failed", "已失败", game::MissionKind::Once, 1));
    f.missionConfig.defs.push_back(
        makeMission("m_available", "可接取", game::MissionKind::Once, 1));
    f.missionConfig.defs.push_back(
        makeMission("m_locked", "未解锁", game::MissionKind::Once, 99));

    ecs::MissionLog log;
    log.missions.push_back(ecs::MissionRunState{
        "m_active", {1}, ecs::MissionRunStatus::Active, 2, false, ""});
    log.missions.push_back(ecs::MissionRunState{
        "m_completed", {2}, ecs::MissionRunStatus::Completed, 1, true, ""});
    log.missions.push_back(ecs::MissionRunState{
        "m_failed", {0}, ecs::MissionRunStatus::Failed, 1, false, ""});
    registry.emplace<ecs::MissionLog>(player, std::move(log));

    script::ScriptMissionList missions;
    REQUIRE(f.api.ListMissions("all", missions));
    CHECK(missions.count == 5);
    CHECK_FALSE(missions.truncated);
    REQUIRE(missions.missions.size() == 5);
    CHECK(missions.missions[0].status == "active");
    CHECK(missions.missions[0].day == 2);
    REQUIRE(missions.missions[0].progress.size() == 1);
    CHECK(missions.missions[0].progress[0] == 1);
    CHECK(missions.missions[0].objectives[0].kind == "collect");
    CHECK(missions.missions[0].reward.item == "bandage");
    CHECK(missions.missions[1].status == "completed");
    CHECK(missions.missions[2].status == "failed");
    CHECK(missions.missions[3].status == "available");
    CHECK(missions.missions[4].status == "locked");

    REQUIRE(f.api.ListMissions("active", missions));
    CHECK(missions.count == 1);
    CHECK(missions.missions[0].id == "m_active");
    REQUIRE(f.api.ListMissions("available", missions));
    CHECK(missions.count == 1);
    CHECK(missions.missions[0].id == "m_available");
    REQUIRE(f.api.ListMissions("completed", missions));
    CHECK(missions.count == 1);
    REQUIRE(f.api.ListMissions("failed", missions));
    CHECK(missions.count == 1);
    REQUIRE(f.api.ListMissions("locked", missions));
    CHECK(missions.count == 1);
    CHECK_FALSE(f.api.ListMissions("bogus", missions));

    script::ScriptMissionInfo mission;
    REQUIRE(f.api.GetMission("m_active", mission));
    CHECK(mission.id == "m_active");
    CHECK(mission.chain == "main");
    CHECK(mission.status == "active");
    REQUIRE(mission.objectives.size() == 1);
    CHECK(mission.objectives[0].target == "bandage");
    CHECK(mission.objectives[0].count == 2);
    CHECK(mission.objectives[0].progress == 1);
    CHECK_FALSE(f.api.GetMission("missing", mission));
}

TEST_CASE("M7-T3：玩家查询（稳定 ID/位置/血量；无玩家防护）") {
    Fixture f;
    CHECK(f.api.GetPlayerId() == 0);
    script::ScriptVec3 position;
    CHECK_FALSE(f.api.GetPlayerPosition(position));
    float hp = 0.0f;
    CHECK_FALSE(f.api.GetPlayerHealth(hp));

    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    CHECK(f.api.GetPlayerId() == f.ecs.IdOf(player).id);
    CHECK(f.api.GetPlayerId() != 0);

    REQUIRE(f.api.GetPlayerPosition(position));
    CHECK(position.x == doctest::Approx(3.0f));
    CHECK(position.y == doctest::Approx(4.0f));
    CHECK(position.z == 0);

    REQUIRE(f.api.GetPlayerHealth(hp));
    CHECK(hp == doctest::Approx(80.0f));
}

TEST_CASE("M7-T3：Player.Teleport 与 AddItem（含非法输入）") {
    Fixture f;
    const ecs::Entity player = f.MakePlayer(3.0f, 4.0f);
    f.ecs.Registry().get<ecs::Velocity>(player).x = 1.5f;

    CHECK(f.api.TeleportPlayer(7.5f, 8.25f, 0));
    const ecs::Position& position = f.ecs.Registry().get<ecs::Position>(player);
    CHECK(position.x == doctest::Approx(7.5f));
    CHECK(position.y == doctest::Approx(8.25f));
    CHECK(f.ecs.Registry().get<ecs::Velocity>(player).x == doctest::Approx(0.0f));

    CHECK(f.api.AddPlayerItem("bandage", 3) == 3);
    CHECK(items::CountOf(f.ecs.Registry().get<ecs::Container>(player), "bandage") == 3);
    CHECK(f.api.AddPlayerItem("bandage", 4) == 4);  // maxStack=5：合并到 5 + 新堆 2
    CHECK(items::CountOf(f.ecs.Registry().get<ecs::Container>(player), "bandage") == 7);
    CHECK(f.api.AddPlayerItem("no_such_item", 1) == 0);
    CHECK(f.api.AddPlayerItem("bandage", 0) == 0);
    CHECK(f.api.AddPlayerItem("bandage", -1) == 0);
}

TEST_CASE("M7-T3：World.IsPassable 与 SpawnItem") {
    Fixture f;
    world::Chunk chunk(0, 0);
    chunk.Square(2, 3, 0).AddObject(world::SquareLayer::Wall, {"wall.brick", 0, 0, true});
    f.pathfinding.OnChunkLoaded(chunk);

    CHECK_FALSE(f.api.IsPassable(2, 3));    // 墙：阻挡
    CHECK(f.api.IsPassable(0, 0));          // 空地面
    CHECK_FALSE(f.api.IsPassable(50, 50));  // 未加载 chunk = 不可通行

    CHECK(f.api.SpawnItem("bandage", 5.0f, 6.0f, 0));
    CHECK(game::CountGroundItems(f.ecs) == 1);
    CHECK_FALSE(f.api.SpawnItem("no_such_item", 5.0f, 6.0f, 0));
    CHECK(game::CountGroundItems(f.ecs) == 1);
}

TEST_CASE("X-F2：World.GetSquare 返回只读分层快照") {
    Fixture f;
    world::GridSquare& square = f.world.GetOrCreateSquare(2, 3, 0);
    square.SetFloor(world::SquareObject{"tile.floor.wood", 0, 0, false});
    square.AddObject(
        world::SquareLayer::Wall,
        world::SquareObject{"object.wall_h", 0, 0, true, 80.0f, true});
    square.AddObject(
        world::SquareLayer::Furniture,
        world::SquareObject{"object.crate", 0, 0, false, -1.0f, true});
    square.AddObject(world::SquareLayer::Item,
                     world::SquareObject{"object.locker", 0, 0, false});

    script::ScriptSquare out;
    REQUIRE(f.api.GetSquare(2, 3, 0, out));
    CHECK(out.loaded);
    CHECK(out.hasFloor);
    CHECK(out.floor.sprite == "tile.floor.wood");
    CHECK_FALSE(out.floor.playerBuilt);
    REQUIRE(out.wall.size() == 1);
    CHECK(out.wall[0].sprite == "object.wall_h");
    CHECK(out.wall[0].blocksMovement);
    CHECK(out.wall[0].playerBuilt);
    CHECK(out.wall[0].hp == doctest::Approx(80.0f));
    REQUIRE(out.furniture.size() == 1);
    CHECK(out.furniture[0].sprite == "object.crate");
    CHECK_FALSE(out.furniture[0].blocksMovement);
    REQUIRE(out.item.size() == 1);
    CHECK(out.item[0].sprite == "object.locker");

    CHECK_FALSE(f.api.GetSquare(50, 50, 0, out));
    CHECK_FALSE(out.loaded);
    CHECK_FALSE(f.api.GetSquare(2, 3, 8, out));
}

TEST_CASE("X-F2b-2d：Hud 自定义字段注册、更新与上限") {
    Fixture f;
    f.ctx.hudState.active = true;
    CHECK_FALSE(f.api.RegisterHudField("", "status", "状态"));
    CHECK_FALSE(f.api.RegisterHudField("mod", "", "状态"));
    CHECK_FALSE(f.api.RegisterHudField("mod", "status", ""));
    CHECK(f.api.RegisterHudField("mod", "status", "状态"));
    CHECK(f.api.RegisterHudField("mod", "status", "新状态"));
    CHECK_FALSE(f.api.SetHudField("missing", "x", "x", "info"));
    CHECK_FALSE(f.api.SetHudField("mod", "status", "x", "bad"));
    CHECK(f.api.SetHudField("mod", "status", "运行中", "warn"));

    ui::HudState state;
    REQUIRE(f.api.GetHudState(state));
    CHECK(state.modFieldCount == 1);
    CHECK(state.modFields[0].modId == "mod");
    CHECK(state.modFields[0].id == "status");
    CHECK(state.modFields[0].label == "新状态");
    CHECK(state.modFields[0].text == "运行中");
    CHECK(state.modFields[0].tone == "warn");

    CHECK(f.api.ClearHudField("mod", "status"));
    REQUIRE(f.api.GetHudState(state));
    CHECK(state.modFields[0].text.empty());
    CHECK(state.modFields[0].tone == "info");
    CHECK_FALSE(f.api.ClearHudField("mod", "missing"));

    for (int i = 1; i < 8; ++i) {
        CHECK(f.api.RegisterHudField("mod", "extra_" + std::to_string(i),
                                     "扩展"));
    }
    CHECK_FALSE(f.api.RegisterHudField("mod", "extra_8", "扩展"));
    f.api.RemoveHudFields("mod");
    REQUIRE(f.api.GetHudState(state));
    CHECK(state.modFieldCount == 0);
}

TEST_CASE("M7-T3：Zombies.Spawn/Count/GetPosition（稳定 ID 句柄）") {
    Fixture f;
    CHECK(f.api.ZombieCount() == 0);

    const std::uint64_t handle = f.api.SpawnZombie(10.0f, 11.0f, 0);
    CHECK(handle != 0);
    CHECK(f.api.ZombieCount() == 1);

    script::ScriptVec3 position;
    REQUIRE(f.api.GetZombiePosition(handle, position));
    CHECK(position.x == doctest::Approx(10.0f));
    CHECK(position.y == doctest::Approx(11.0f));
    CHECK_FALSE(f.api.GetZombiePosition(0, position));     // 无效句柄
    CHECK_FALSE(f.api.GetZombiePosition(9999, position));  // 不存在

    f.ecs.Destroy(f.ecs.FindById(handle));
    f.ecs.Flush();
    CHECK(f.api.ZombieCount() == 0);
    CHECK_FALSE(f.api.GetZombiePosition(handle, position));  // 已销毁：句柄失效
}

TEST_CASE("M35-F3-T6：僵尸变体与声音引怪") {
    Fixture f;
    struct VariantRow {
        const char* name;
        ecs::ZombieVariantKind kind;
        float health;
        float speed;
    };
    const VariantRow rows[] = {
        {"normal", ecs::ZombieVariantKind::Normal, 100.0f, 1.0f},
        {"fast", ecs::ZombieVariantKind::Fast, 70.0f, 1.55f},
        {"strong", ecs::ZombieVariantKind::Strong, 170.0f, 0.78f},
        {"armored", ecs::ZombieVariantKind::Armored, 200.0f, 0.62f},
    };
    for (std::size_t i = 0; i < std::size(rows); ++i) {
        const std::uint64_t handle = f.api.SpawnZombie(
            10.0f + static_cast<float>(i), 20.0f, 0, rows[i].name);
        REQUIRE(handle != 0);
        const ecs::Entity zombie = f.ecs.FindById(handle);
        REQUIRE(zombie != ecs::EcsWorld::kNull);
        const auto& registry = f.ecs.Registry();
        const auto* health = registry.try_get<ecs::Health>(zombie);
        REQUIRE(health != nullptr);
        CHECK(health->max == doctest::Approx(rows[i].health));
        CHECK(health->current == doctest::Approx(rows[i].health));
        const auto* variant = registry.try_get<ecs::ZombieVariant>(zombie);
        REQUIRE(variant != nullptr);
        CHECK(variant->kind == rows[i].kind);
        CHECK(variant->speedScale == doctest::Approx(rows[i].speed));
    }

    const std::uint64_t normal = f.api.SpawnZombie(30.0f, 20.0f, 0);
    REQUIRE(normal != 0);
    const auto* normalVariant =
        f.ecs.Registry().try_get<ecs::ZombieVariant>(f.ecs.FindById(normal));
    REQUIRE(normalVariant != nullptr);
    CHECK(normalVariant->kind == ecs::ZombieVariantKind::Normal);
    CHECK(f.api.ZombieCount() == 5);

    CHECK(f.api.SpawnZombie(31.0f, 20.0f, 0, "boss") == 0);
    CHECK(f.api.SpawnZombie(std::numeric_limits<float>::quiet_NaN(), 20.0f, 0) ==
          0);
    CHECK(f.api.ZombieCount() == 5);

    CHECK_FALSE(f.api.EmitSound(4.0f, 5.0f, 10.0f));

    core::EventBus bus;
    f.ctx.events = &bus;
    std::vector<core::SoundEvent> sounds;
    ai::PerceptionSystem perception([](int, int) { return true; });
    bus.Subscribe(core::kOnSound, [&](const core::Event& event) {
        if (const auto* sound = std::get_if<core::SoundEvent>(&event.payload)) {
            sounds.push_back(*sound);
            perception.PushSound(*sound);
        }
    });

    CHECK(f.api.EmitSound(4.0f, 5.0f, 24.0f, "gunshot", "script_shot"));
    CHECK(f.api.EmitSound(6.0f, 7.0f, 12.0f, "zombie_growl"));
    bus.FlushQueued();
    REQUIRE(sounds.size() == 2);
    CHECK(sounds[0].x == doctest::Approx(4.0f));
    CHECK(sounds[0].y == doctest::Approx(5.0f));
    CHECK(sounds[0].radius == doctest::Approx(24.0f));
    CHECK(sounds[0].type == core::SoundType::Gunshot);
    CHECK(sounds[0].clip == "script_shot");
    CHECK(sounds[1].type == core::SoundType::ZombieGrowl);
    CHECK(sounds[1].clip.empty());

    perception.Update(f.ecs, 0.1f);
    CHECK(perception.GetStats().heard > 0);

    CHECK_FALSE(f.api.EmitSound(4.0f, 5.0f, 0.0f));
    CHECK_FALSE(f.api.EmitSound(4.0f, 5.0f, -1.0f));
    CHECK_FALSE(f.api.EmitSound(4.0f, 5.0f,
                                std::numeric_limits<float>::infinity()));
    CHECK_FALSE(f.api.EmitSound(4.0f, 5.0f, 10.0f, "nope"));
    CHECK(bus.QueuedCount() == 0);
}

TEST_CASE("M35-F5-T3：Audio.Play2D/Play3D 只播放已有 clip 且不触发 AI 听觉") {
    Fixture f;
    core::EventBus bus;
    audio::SoundManagerOptions options;
    options.engine.root = std::string(ZL_SOURCE_DIR) + "/assets/audio";
    options.engine.device = "null";
    audio::SoundManager sound;
    REQUIRE(sound.Init(bus, options));
    f.ctx.audio = &sound;

    int soundEvents = 0;
    bus.Subscribe(core::kOnSound, [&soundEvents](const core::Event&) {
        ++soundEvents;
    });

    CHECK(f.api.PlayAudio2D(audio::kUiClick, 0.5f));
    CHECK(f.api.PlayAudio2D(audio::kPickup));
    CHECK(f.api.PlayAudio3D(audio::kImpact, 3.0f, 4.0f, 18.0f, 0.6f));
    CHECK(f.api.PlayAudio3D(audio::kGunshot, 3.0f, 4.0f, 0.0f, 0.8f));
    CHECK_FALSE(f.api.PlayAudio2D(""));
    CHECK_FALSE(f.api.PlayAudio2D("missing_clip"));
    CHECK_FALSE(f.api.PlayAudio2D("../ui_click"));
    CHECK_FALSE(f.api.PlayAudio2D("ui_click/extra"));
    CHECK_FALSE(f.api.PlayAudio2D(audio::kUiClick,
                                  std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(f.api.PlayAudio3D(audio::kImpact, 3.0f, 4.0f, -1.0f));
    CHECK_FALSE(f.api.PlayAudio3D(audio::kImpact, 3.0f, 4.0f, 257.0f));
    CHECK_FALSE(f.api.PlayAudio3D(
        audio::kImpact, std::numeric_limits<float>::quiet_NaN(), 4.0f, 18.0f));
    CHECK_FALSE(f.api.PlayAudio3D(audio::kImpact, 3.0f, 4.0f, 18.0f,
                                  std::numeric_limits<float>::quiet_NaN()));
    CHECK(soundEvents == 0);
    CHECK(bus.QueuedCount() == 0);

    f.ctx.audio = nullptr;
    CHECK_FALSE(f.api.PlayAudio2D(audio::kUiClick));

    audio::SoundManagerOptions offOptions;
    offOptions.engine.root = options.engine.root;
    offOptions.engine.device = "off";
    audio::SoundManager offSound;
    CHECK(offSound.Init(bus, offOptions));
    CHECK_FALSE(offSound.IsReady());
    f.ctx.audio = &offSound;
    CHECK_FALSE(f.api.PlayAudio2D(audio::kUiClick));
    CHECK_FALSE(f.api.PlayAudio3D(audio::kImpact, 3.0f, 4.0f, 18.0f));
}

TEST_CASE("M35-F3-T7：联机写 API fail-closed 与网络模式") {
    Fixture f;
    core::EventBus bus;
    f.ctx.events = &bus;
    const ecs::Entity player = f.MakePlayer(1.0f, 2.0f);
    entt::registry& registry = f.ecs.Registry();
    registry.emplace<ecs::Needs>(
        player, ecs::Needs{0.2f, 0.3f, 0.4f, 1.0f, 1.0f, 0.0f, 0});
    registry.emplace<ecs::Progression>(player);

    const auto expectBlocked = [&](const char* mode) {
        CHECK(f.api.IsMultiplayer());
        CHECK(f.api.GetNetMode() == mode);
        const float healthBefore = registry.get<ecs::Health>(player).current;
        const float hungerBefore = registry.get<ecs::Needs>(player).hunger;
        const float xpBefore = registry.get<ecs::Progression>(player).xp;

        const script::ScriptActionResult damage =
            f.api.ApplyDamage(5.0f, "debug");
        const script::ScriptActionResult needs =
            f.api.AdjustNeeds(0.1f, 0.1f, 0.1f);
        const script::ScriptXpResult xp = f.api.GiveXp("melee", 10.0f);
        const script::ScriptActionResult learn =
            f.api.LearnRecipe("base_bandage");
        const script::ScriptActionResult canPlace =
            f.api.CanPlaceBuild("barricade", 3, 4);
        CHECK_FALSE(damage.ok);
        CHECK_FALSE(needs.ok);
        CHECK_FALSE(xp.result.ok);
        CHECK_FALSE(learn.ok);
        CHECK_FALSE(canPlace.ok);
        CHECK(damage.error.find("联机") != std::string::npos);
        CHECK(needs.error.find("联机") != std::string::npos);
        CHECK(xp.result.error.find("联机") != std::string::npos);
        CHECK(learn.error.find("联机") != std::string::npos);
        CHECK(canPlace.error.find("联机") != std::string::npos);
        CHECK_FALSE(f.api.EmitSound(4.0f, 5.0f, 10.0f));
        CHECK(bus.QueuedCount() == 0);

        CHECK(registry.get<ecs::Health>(player).current ==
              doctest::Approx(healthBefore));
        CHECK(registry.get<ecs::Needs>(player).hunger ==
              doctest::Approx(hungerBefore));
        CHECK(registry.get<ecs::Progression>(player).xp ==
              doctest::Approx(xpBefore));
    };

    net::CommandPipeline::Options loopOptions;
    loopOptions.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(loopOptions);
    f.ctx.commands = &loop;
    expectBlocked("loopback");

    f.ctx.commands = nullptr;
    game::NetworkClient client;
    f.ctx.networkClient = &client;
    expectBlocked("client");

    f.ctx.networkClient = nullptr;
    CHECK_FALSE(f.api.IsMultiplayer());
    CHECK(f.api.GetNetMode() == "direct");
}

TEST_CASE("M35-F4-T2 Mod.Data C++ API、上限与联机拒绝") {
    Fixture f;

    CHECK_FALSE(f.api.GetModData("basic", "missing").has_value());
    CHECK(f.api.ListModDataKeys("basic").empty());
    CHECK(f.api.ClearModData("basic"));
    CHECK_FALSE(f.api.RemoveModData("basic", "missing"));
    CHECK_FALSE(f.api.SetModData("", "k", "v"));
    CHECK_FALSE(f.api.SetModData("basic", "", "v"));
    CHECK_FALSE(f.api.SetModData("basic", std::string(65, 'k'), "v"));
    CHECK_FALSE(f.api.SetModData("basic", "k",
                                 std::string(ecs::kMaxModDataValueBytes + 1, 'v')));
    CHECK_FALSE(f.api.SetModData(std::string(129, 'm'), "k", "v"));
    CHECK_FALSE(f.api.GetModData(std::string(129, 'm'), "k").has_value());
    CHECK(ecs::FindModDataEntity(f.ecs) == ecs::EcsWorld::kNull);

    REQUIRE(f.api.SetModData("basic", "b", "2"));
    REQUIRE(f.api.SetModData("basic", "a", "1"));
    REQUIRE(f.api.SetModData("basic", "a", "3"));
    REQUIRE(f.api.SetModData("other", "a", "9"));
    CHECK(f.api.ListModDataKeys("basic") ==
          std::vector<std::string>{"a", "b"});
    CHECK(f.api.GetModData("basic", "a").value() == "3");
    CHECK(f.api.GetModData("other", "a").value() == "9");
    CHECK(f.api.GetModData("basic", "missing") == std::nullopt);

    CHECK(f.api.RemoveModData("basic", "a"));
    CHECK_FALSE(f.api.RemoveModData("basic", "a"));
    CHECK(f.api.ListModDataKeys("basic") == std::vector<std::string>{"b"});
    CHECK(f.api.ClearModData("basic"));
    CHECK(f.api.ListModDataKeys("basic").empty());
    CHECK(f.api.ClearModData("basic"));
    CHECK(f.api.GetModData("basic", "b") == std::nullopt);
    CHECK(f.api.GetModData("other", "a").value() == "9");

    Fixture keys;
    for (int i = 0; i < 64; ++i) {
        REQUIRE(keys.api.SetModData("keys", "k" + std::to_string(i), "v"));
    }
    CHECK(keys.api.ListModDataKeys("keys").size() == 64u);
    CHECK_FALSE(keys.api.SetModData("keys", "k64", "v"));
    CHECK(keys.api.SetModData("keys", "k0", "updated"));
    CHECK(keys.api.GetModData("keys", "k0").value() == "updated");
    CHECK(keys.api.ListModDataKeys("keys").size() == 64u);

    int largeValues = 0;
    for (int i = 0; i < 33; ++i) {
        if (keys.api.SetModData("large", "v" + std::to_string(i),
                                std::string(ecs::kMaxModDataValueBytes, 'x'))) {
            ++largeValues;
        }
    }
    CHECK(largeValues == 31);
    CHECK(keys.api.ListModDataKeys("large").size() == 31u);

    Fixture mods;
    std::size_t modCount = 0;
    for (; modCount < ecs::kMaxModDataMods; ++modCount) {
        if (!mods.api.SetModData("m" + std::to_string(modCount), "k", "v")) {
            break;
        }
    }
    CHECK(modCount == ecs::kMaxModDataMods);
    CHECK_FALSE(mods.api.SetModData("overflow", "k", "v"));
    CHECK(mods.api.GetModData("m0", "k").value() == "v");
    CHECK(mods.api.GetModData("m255", "k").value() == "v");

    const auto expectBlocked = [&]() {
        CHECK(f.api.IsMultiplayer());
        CHECK_FALSE(f.api.SetModData("mp", "k", "v"));
        CHECK_FALSE(f.api.GetModData("mp", "k").has_value());
        CHECK(f.api.ListModDataKeys("mp").empty());
        CHECK_FALSE(f.api.RemoveModData("mp", "k"));
        CHECK_FALSE(f.api.ClearModData("mp"));
    };

    net::CommandPipeline::Options options;
    options.mode = net::TransportMode::Loopback;
    net::CommandPipeline loop(options);
    f.ctx.commands = &loop;
    expectBlocked();

    f.ctx.commands = nullptr;
    game::NetworkClient client;
    f.ctx.networkClient = &client;
    expectBlocked();

    f.ctx.networkClient = nullptr;
    CHECK(f.api.SetModData("mp", "k", "v"));
    CHECK(f.api.GetModData("mp", "k").value() == "v");
}

TEST_CASE("M35-F4-T2 Mod.Data 存档往返与新世界清空") {
    Fixture source;
    REQUIRE(source.api.SetModData("persist", "score", "7"));
    REQUIRE(source.api.SetModData("persist", "json", R"({"a":1})"));
    REQUIRE(source.api.SetModData("other", "k", "v"));

    const ecs::Entity entity = ecs::FindModDataEntity(source.ecs);
    REQUIRE(entity != ecs::EcsWorld::kNull);

    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source.ecs, components, &snapshot, {},
                                  &error));
    REQUIRE(snapshot.entities.size() == 1u);
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

    game::GameContext targetCtx;
    targetCtx.ecs = &target;
    script::ScriptApi targetApi;
    targetApi.Attach(&targetCtx);
    CHECK(targetApi.GetModData("persist", "score").value() == "7");
    CHECK(targetApi.GetModData("persist", "json").value() == R"({"a":1})");
    CHECK(targetApi.ListModDataKeys("persist") ==
          std::vector<std::string>{"json", "score"});
    CHECK(targetApi.GetModData("other", "k").value() == "v");

    ecs::EcsWorld fresh;
    game::GameContext freshCtx;
    freshCtx.ecs = &fresh;
    script::ScriptApi freshApi;
    freshApi.Attach(&freshCtx);
    CHECK_FALSE(freshApi.GetModData("persist", "score").has_value());
    CHECK(freshApi.ListModDataKeys("persist").empty());
}

TEST_CASE("M7-T3：Game 信息（tick/时间/玩家 ID）与 Items 查询") {
    Fixture f;
    CHECK(f.api.GetTick() == 0);
    f.loop.Frame(1.0 / 60.0, [](double) {}, [](double, double) {});
    f.loop.Frame(1.0 / 60.0, [](double) {}, [](double, double) {});
    CHECK(f.api.GetTick() == 2);

    const core::DateTime now = f.api.GetTime();
    CHECK(now.year == 2000);
    CHECK(now.month == 7);
    CHECK(now.day == 9);
    CHECK(now.hour == 0);
    CHECK(f.api.GetTotalGameMinutes() == doctest::Approx(0.0));

    CHECK(f.api.ItemExists("bandage"));
    CHECK_FALSE(f.api.ItemExists("nope"));
    CHECK(f.api.ItemName("bandage") == "绷带");
    CHECK(f.api.ItemName("axe") == "斧");
    CHECK(f.api.ItemName("nope").empty());
}

TEST_CASE("M9-T7：Inventory API 读取状态、提交动作与点击音效") {
    Fixture f;
    ui::InventoryState source;
    source.open = true;
    source.playerValid = true;
    source.weight = 3.2f;
    source.capacity = 20.0f;
    source.bagStacks = 1;
    source.lootStacks = 2;
    source.status = "已拾取";
    source.statusTone = "ok";
    source.bag[0] = ui::InventorySlot{true, "绷带", 2, true, false, 0};
    source.equipment[0] = ui::InventorySlot{true, "斧", 1, false, true, 0};
    source.loot[0] = ui::InventorySlot{true, "破布", 3, false, false, 77};
    f.ctx.getInventoryState = [&source](ui::InventoryState& out) {
        out = source;
        return true;
    };
    ui::InventoryAction received;
    bool hasAction = false;
    f.ctx.inventoryAction = [&received, &hasAction](const ui::InventoryAction& action) {
        received = action;
        hasAction = true;
        return true;
    };
    int clicks = 0;
    f.ctx.playInventoryClick = [&clicks] { ++clicks; };

    ui::InventoryState state;
    REQUIRE(f.api.GetInventoryState(state));
    CHECK(state.open);
    CHECK(state.playerValid);
    CHECK(state.weight == doctest::Approx(3.2f));
    CHECK(state.capacity == doctest::Approx(20.0f));
    CHECK(state.bagStacks == 1);
    CHECK(state.lootStacks == 2);
    CHECK(state.status == "已拾取");
    CHECK(state.bag.size() == static_cast<std::size_t>(ui::kInventoryBagSlots));
    CHECK(state.bag[0].label == "绷带");
    CHECK(state.bag[0].usable);
    CHECK(state.equipment[0].equippable);
    CHECK(state.loot[0].entityId == 77);

    ui::InventoryAction action;
    action.type = ui::InventoryActionType::Transfer;
    action.from = ui::InventoryArea::Backpack;
    action.fromIndex = 0;
    action.to = ui::InventoryArea::Equipment;
    action.toIndex = 0;
    CHECK(f.api.InventoryAction(action));
    CHECK(hasAction);
    CHECK(received.type == action.type);
    CHECK(received.from == action.from);
    CHECK(received.fromIndex == 0);
    CHECK(received.to == action.to);
    CHECK(received.toIndex == 0);

    f.api.PlayInventoryClick();
    CHECK(clicks == 1);

    f.ctx.inventoryAction = nullptr;
    CHECK_FALSE(f.api.InventoryAction(action));
}

TEST_CASE("脚本存档 UI：Save API 列表、读档、删除与快速保存") {
    Fixture f;
    save::SaveMetadata first;
    first.id = "save-one";
    first.name = "第一次存档";
    first.updatedAt = 1;
    first.mapId = "village_demo";
    first.gameVersion = "0.3.0";
    save::SaveMetadata second;
    second.id = "save-two";
    second.updatedAt = 2;
    std::vector<save::SaveMetadata> source{first, second};

    f.ctx.listSaves = [&source](std::vector<save::SaveMetadata>& out,
                                std::string& error) {
        out = source;
        error.clear();
        return true;
    };
    std::string loadedId;
    f.ctx.loadSave = [&loadedId](const std::string& id, std::string& error) {
        if (id != "save-one") {
            error = "不存在";
            return false;
        }
        loadedId = id;
        return true;
    };
    int quickSaves = 0;
    f.ctx.quickSave = [&quickSaves] {
        ++quickSaves;
        return true;
    };
    std::vector<std::string> deleted;
    f.ctx.deleteSave = [&deleted](const std::string& id, std::string& error) {
        if (id != "save-one") {
            error = "不存在";
            return false;
        }
        deleted.push_back(id);
        return true;
    };

    std::vector<script::ScriptSaveInfo> saves;
    std::string error;
    REQUIRE(f.api.ListSaves(saves, error));
    REQUIRE(saves.size() == 2);
    CHECK(saves[0].id == "save-one");
    CHECK(saves[0].name == "第一次存档");
    CHECK(saves[0].mapId == "village_demo");
    CHECK_FALSE(saves[0].updatedText.empty());
    CHECK(saves[1].name == "save-two");  // 空名称回退到 id

    CHECK(f.api.LoadSave("save-one", error));
    CHECK(loadedId == "save-one");
    CHECK_FALSE(f.api.LoadSave("missing", error));
    CHECK(error == "不存在");
    CHECK_FALSE(f.api.LoadSave("", error));
    CHECK(f.api.QuickSave());
    CHECK(quickSaves == 1);

    CHECK(f.api.DeleteSave("save-one", error));
    CHECK(deleted.size() == 1);
    CHECK(deleted[0] == "save-one");
    CHECK_FALSE(f.api.DeleteSave("missing", error));
    CHECK(error == "不存在");
    CHECK_FALSE(f.api.DeleteSave("", error));
    CHECK(error == "存档 id 为空");

    f.ctx.listSaves = nullptr;
    CHECK_FALSE(f.api.ListSaves(saves, error));
    f.ctx.quickSave = nullptr;
    CHECK_FALSE(f.api.QuickSave());
    f.ctx.deleteSave = nullptr;
    CHECK_FALSE(f.api.DeleteSave("save-one", error));
    CHECK(error == "删除存档不可用");
}

TEST_CASE("X-F2b-2e：Content.RegisterItems/Recipes/Buildings 动态注册") {
    Fixture f;
    int contentChanges = 0;
    f.ctx.contentChanged = [&contentChanges] { ++contentChanges; };
    std::string error;

    const std::string itemJson = R"({
      "format": "items-v1",
      "items": [
        { "id": "script_dynamic", "name": "脚本物品", "icon": "item.dynamic", "weight": 0.2, "maxStack": 2, "type": "generic" }
      ]
    })";
    CHECK(f.api.RegisterItems("test_mod", itemJson, error) == 1);
    CHECK(error.empty());
    CHECK(f.items.Find("script_dynamic") != nullptr);
    CHECK(f.ctx.dynamicContentCount == 1);
    CHECK(contentChanges == 1);

    CHECK(f.api.RegisterItems("test_mod", itemJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.items.Size() == 3);
    CHECK(f.ctx.dynamicContentCount == 1);
    CHECK(contentChanges == 1);

    const std::string recipeJson = R"({
      "format": "recipes-v3",
      "recipes": [
        {
          "id": "script_dynamic_recipe",
          "name": "脚本配方",
          "output": { "item": "script_dynamic", "count": 1 },
          "ingredients": [ { "item": "bandage", "count": 1 } ]
        }
      ]
    })";
    CHECK(f.api.RegisterRecipes("test_mod", recipeJson, error) == 1);
    CHECK(error.empty());
    CHECK(f.recipes.Find("script_dynamic_recipe") != nullptr);
    CHECK(f.ctx.dynamicContentCount == 2);
    CHECK(contentChanges == 2);

    const std::string buildingJson = R"({
      "format": "buildings-v1",
      "types": [
        {
          "id": "script_dynamic_crate",
          "name": "脚本仓库",
          "sprite": "object.script_dynamic_crate",
          "woodCost": 2,
          "storageKg": 12,
          "storageSlots": 2,
          "category": "storage"
        }
      ]
    })";
    CHECK(f.api.RegisterBuildings("test_mod", buildingJson, error) == 1);
    CHECK(error.empty());
    REQUIRE(f.ctx.modBuildables.size() == 1);
    CHECK(f.ctx.modBuildables[0].id == "script_dynamic_crate");
    CHECK(f.ctx.dynamicContentCount == 3);
    CHECK(contentChanges == 3);

    std::vector<game::BuildableDef> buildables = game::LoadBuildableDefs(f.config);
    CHECK(game::AppendBuildableDefs(buildables, f.ctx.modBuildables) == 1);
    CHECK(game::FindBuildableBySprite(buildables, "object.script_dynamic_crate") !=
          nullptr);

    CHECK(f.api.RegisterBuildings("test_mod", buildingJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.ctx.modBuildables.size() == 1);
    CHECK(f.ctx.dynamicContentCount == 3);

    f.ctx.allowDynamicContent = false;
    const std::string anotherItem = R"({
      "format": "items-v1",
      "items": [
        { "id": "script_dynamic_two", "name": "脚本物品二", "icon": "item.dynamic", "weight": 0.2, "maxStack": 2, "type": "generic" }
      ]
    })";
    CHECK(f.api.RegisterItems("test_mod", anotherItem, error) == 0);
    CHECK(error == game::kDynamicContentSinglePlayerOnlyMessage);
    CHECK(f.items.Find("script_dynamic_two") == nullptr);
    CHECK(f.ctx.dynamicContentCount == 3);
    CHECK(contentChanges == 3);
}

TEST_CASE("X3-T5：联机禁用时五类动态内容注册全部拒绝") {
    Fixture f;
    f.ctx.allowDynamicContent = false;
    f.poiConfig.enabled = true;
    f.ctx.poiConfig = &f.poiConfig;
    f.ctx.missionConfig = &f.missionConfig;
    int contentChanges = 0;
    f.ctx.contentChanged = [&contentChanges] { ++contentChanges; };
    std::string error;

    const std::string expected = game::kDynamicContentSinglePlayerOnlyMessage;
    CHECK(f.api.RegisterItems("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterRecipes("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterBuildings("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterPois("test_mod", "{}", error) == 0);
    CHECK(error == expected);
    CHECK(f.api.RegisterMissions("test_mod", "{}", error) == 0);
    CHECK(error == expected);

    CHECK(f.items.Size() == 2);
    CHECK(f.recipes.Size() == 1);
    CHECK(f.ctx.modBuildables.empty());
    CHECK(f.poiConfig.defs.empty());
    CHECK(f.missionConfig.defs.empty());
    CHECK(f.ctx.dynamicContentCount == 0);
    CHECK(contentChanges == 0);
}

TEST_CASE("X-F2b-2e：Content.RegisterPois/RegisterMissions 动态注册") {
    Fixture f;
    std::string error;

    f.poiConfig.enabled = true;
    f.poiConfig.guideRange = 70.0f;
    f.ctx.poiConfig = &f.poiConfig;
    const std::string poiJson = R"({
      "format": "pois-v1",
      "defs": [
        {
          "id": "script_poi",
          "name": "脚本 POI",
          "threat": 1,
          "markerX": 10.0,
          "markerY": 11.0,
          "containers": [
            { "x": 10.5, "y": 11.5, "tableId": "script_table", "sprite": "object.crate" }
          ],
          "enemies": { "count": 0, "kind": "normal", "radius": 4.0 },
          "task": {
            "description": "搜索补给",
            "targetTable": "script_table",
            "rewardXp": 5.0,
            "rewardItem": "bandage",
            "rewardCount": 1
          }
        }
      ]
    })";
    CHECK(f.api.RegisterPois("test_mod", poiJson, error) == 1);
    CHECK(error.empty());
    REQUIRE(f.poiConfig.defs.size() == 1);
    CHECK(f.poiConfig.Find("script_poi") != nullptr);
    CHECK(f.ctx.dynamicContentCount == 1);

    CHECK(f.api.RegisterPois("test_mod", poiJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.poiConfig.defs.size() == 1);

    const std::string baseMissions = R"({
      "enabled": true,
      "randomSeed": 7,
      "defs": [
        {
          "id": "base_script",
          "name": "基础脚本任务",
          "chain": "base",
          "order": 1,
          "kind": "once",
          "source": "base",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 10.0, "skill": "melee", "label": "10 经验" }
        }
      ]
    })";
    f.config.Set("missions", nlohmann::json::parse(baseMissions));
    REQUIRE(game::LoadMissionConfig(f.config, &f.missionConfig, &error));
    f.ctx.missionConfig = &f.missionConfig;
    f.ctx.missionSourceJson = f.config.Raw()["missions"].dump();

    const std::string missionJson = R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "script_mission",
          "name": "脚本任务",
          "chain": "script",
          "order": 1,
          "kind": "once",
          "source": "note",
          "prerequisite": "base_script",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 2, "description": "清理两只僵尸" }
          ],
          "reward": { "xp": 20.0, "skill": "melee", "label": "20 经验" }
        }
      ]
    })";
    CHECK(f.api.RegisterMissions("test_mod", missionJson, error) == 1);
    CHECK(error.empty());
    REQUIRE(f.missionConfig.defs.size() == 2);
    REQUIRE(f.missionConfig.Find("script_mission") != nullptr);
    CHECK(f.missionConfig.Find("script_mission")->prerequisite == "base_script");
    CHECK(f.ctx.dynamicContentCount == 2);

    CHECK(f.api.RegisterMissions("test_mod", missionJson, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.missionConfig.defs.size() == 2);

    const std::string badPrerequisite = R"({
      "format": "missions-v1",
      "defs": [
        {
          "id": "bad_script_mission",
          "name": "坏前置",
          "chain": "bad",
          "order": 1,
          "kind": "once",
          "source": "note",
          "prerequisite": "missing_mission",
          "objectives": [
            { "kind": "kill", "target": "any", "count": 1, "description": "清理僵尸" }
          ],
          "reward": { "xp": 1.0, "skill": "melee", "label": "1 经验" }
        }
      ]
    })";
    CHECK(f.api.RegisterMissions("test_mod", badPrerequisite, error) == 0);
    CHECK_FALSE(error.empty());
    CHECK(f.missionConfig.defs.size() == 2);
    CHECK(f.missionConfig.Find("bad_script_mission") == nullptr);

    f.poiConfig.enabled = false;
    CHECK(f.api.RegisterPois("test_mod", poiJson, error) == 0);
    f.missionConfig.enabled = false;
    CHECK(f.api.RegisterMissions("test_mod", missionJson, error) == 0);
}