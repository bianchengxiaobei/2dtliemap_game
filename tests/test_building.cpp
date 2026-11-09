#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ai/PathfindingService.h"
#include "core/Config.h"
#include "game/ActionHints.h"
#include "game/BarricadeDefense.h"
#include "game/BuildingRules.h"
#include "items/ItemRegistry.h"
#include "physics/PhysicsWorld.h"
#include "world/Chunk.h"
#include "world/GridSquare.h"

TEST_CASE("C6 structure owner access rules") {
    CHECK(zl::game::CanEditStructure(0, 0));
    CHECK(zl::game::CanEditStructure(0, 7));
    CHECK_FALSE(zl::game::CanEditStructure(7, 0));
    CHECK(zl::game::CanEditStructure(7, 7));
    CHECK_FALSE(zl::game::CanEditStructure(7, 8));
}

TEST_CASE("M16-T1 default buildables and cell rules") {
    const auto& defs = zl::game::DefaultBuildables();
    REQUIRE(defs.size() == 15u);
    CHECK(defs[0].id == "barricade");
    CHECK(defs[0].woodCost == 3);
    CHECK(defs[0].maxHp > 0.0f);
    CHECK(defs[0].blocksMovement);
    CHECK(defs[1].id == "wall");
    CHECK(defs[2].id == "door");
    CHECK(defs[3].id == "floor");
    CHECK(defs[4].id == "campfire");
    CHECK_FALSE(defs[4].blocksMovement);
    CHECK(defs[5].id == "workbench");
    CHECK(defs[6].id == "rain_collector");
    CHECK(defs[6].woodCost > 0);
    CHECK_FALSE(defs[6].blocksMovement);
    CHECK(defs[7].id == "bed");
    CHECK(defs[7].sprite == "object.bed");
    CHECK(defs[7].woodCost > 0);
    CHECK_FALSE(defs[7].blocksMovement);
    CHECK(defs[7].maxHp == doctest::Approx(0.0f));
    CHECK(defs[8].id == "storage");
    CHECK(defs[8].storageKg > 0.0f);
    CHECK(defs[8].storageSlots > 0);
    CHECK(defs[9].id == "farm_plot");
    CHECK(defs[9].sprite == "object.farm_plot");
    CHECK(defs[10].id == "generator");
    CHECK(defs[11].id == "fridge");
    CHECK(defs[11].storageKg > 0.0f);
    CHECK(defs[12].id == "spike_trap");
    CHECK(defs[13].id == "floodlight");
    CHECK(defs[13].sprite == "object.floodlight");
    CHECK_FALSE(defs[13].blocksMovement);
    CHECK(defs[13].maxHp > 0.0f);
    CHECK(defs[14].id == "electric_fence");
    CHECK(defs[14].sprite == "object.electric_fence");
    CHECK(defs[14].maxHp > 0.0f);
    CHECK(defs[14].blocksMovement);

    const zl::game::BuildableDef* barricade =
        zl::game::FindBuildableBySprite(defs, "object.barricade");
    REQUIRE(barricade != nullptr);
    CHECK(barricade->id == "barricade");
    CHECK(zl::game::FindBuildableBySprite(defs, "object.missing") == nullptr);

    zl::world::GridSquare square(0, 0, 0);
    CHECK_FALSE(zl::game::IsBuildCellClear(square));
    square.SetFloor(zl::world::SquareObject{"tile.floor.grass", 0, 0, false});
    CHECK(zl::game::IsBuildCellClear(square));
    square.AddObject(zl::world::SquareLayer::Furniture,
                     zl::world::SquareObject{"decor.grass_tuft", 0, 0, false});
    CHECK(zl::game::IsBuildCellClear(square));
    square.AddObject(zl::world::SquareLayer::Furniture,
                     zl::world::SquareObject{"object.table", 0, 0, false});
    CHECK_FALSE(zl::game::IsBuildCellClear(square));

    zl::world::GridSquare door(1, 0, 0);
    door.SetFloor(zl::world::SquareObject{"tile.floor.wood", 0, 0, false});
    door.AddObject(zl::world::SquareLayer::Wall,
                   zl::world::SquareObject{"wall.door", 0, 0, false});
    CHECK(zl::game::IsBuildCellClear(door));
    CHECK_FALSE(zl::game::IsBuildCellFreeFor(defs[2], door));

    zl::world::GridSquare wall(2, 0, 0);
    wall.SetFloor(zl::world::SquareObject{"tile.floor.wood", 0, 0, false});
    wall.AddObject(zl::world::SquareLayer::Wall,
                   zl::world::SquareObject{"wall.brick", 0, 0, true});
    CHECK_FALSE(zl::game::IsBuildCellClear(wall));
    CHECK_FALSE(zl::game::IsBuildCellFreeFor(defs[1], wall));
}

TEST_CASE("M18-F4 wall/door/floor layers, orientation and open state") {
    const auto& defs = zl::game::DefaultBuildables();
    const zl::game::BuildableDef& wall = defs[1];
    const zl::game::BuildableDef& door = defs[2];
    const zl::game::BuildableDef& floor = defs[3];
    CHECK(wall.layer == zl::world::SquareLayer::Wall);
    CHECK(wall.category == "defense");
    CHECK(wall.sprite == "object.wall_h");
    CHECK(wall.spriteVertical == "object.wall_v");
    CHECK(zl::game::BuildableSupportsVertical(wall));
    CHECK(zl::game::FindBuildableBySprite(defs, "object.wall_v") == &wall);

    CHECK(door.door);
    CHECK(door.openSprite == "object.door_open_h");
    CHECK(zl::game::FindBuildableBySprite(defs, "object.door_open_v") == &door);
    const zl::world::SquareObject closed =
        zl::game::MakeBuiltObject(door, false, false);
    CHECK(closed.sprite == "object.door_closed_h");
    CHECK(closed.blocksMovement);
    const zl::world::SquareObject open =
        zl::game::MakeBuiltObject(door, true, true);
    CHECK(open.sprite == "object.door_open_v");
    CHECK_FALSE(open.blocksMovement);
    CHECK(zl::game::IsOpenDoorSprite(door, open.sprite));
    CHECK(zl::game::IsVerticalBuildSprite(door, open.sprite));

    CHECK(floor.layer == zl::world::SquareLayer::Floor);
    CHECK(zl::game::IsFloorBuildable(floor));
    zl::world::GridSquare square(0, 0, 0);
    square.SetFloor(zl::world::SquareObject{"tile.floor.grass", 0, 0, false});
    CHECK(zl::game::IsBuildCellFreeFor(floor, square));
    zl::world::SquareObject builtFloor =
        zl::game::MakeBuiltObject(floor);
    builtFloor.underlay = "tile.floor.grass";
    square.SetFloor(builtFloor);
    CHECK_FALSE(zl::game::IsBuildCellFreeFor(floor, square));

    const auto* bed = zl::game::FindBuildableBySprite(defs, "object.bed");
    REQUIRE(bed != nullptr);
    CHECK(bed->id == "bed");
}


TEST_CASE("M18-F4 door collision rebuilds physics and pathfinding") {
    const auto& defs = zl::game::DefaultBuildables();
    const zl::game::BuildableDef& door = defs[2];
    zl::world::Chunk chunk(0, 0);
    chunk.Square(5, 5, 0).SetFloor({"tile.floor.grass", 0, 0, false});
    auto& objects = chunk.Square(5, 5, 0).Layer(zl::world::SquareLayer::Wall);
    objects.push_back(zl::game::MakeBuiltObject(door, false, false));

    zl::physics::PhysicsWorld physics;
    physics.BuildChunkStatics(0, 0, chunk);
    CHECK(physics.StaticBodyCount() == 1u);
    zl::ai::PathfindingService pathfinding(nullptr, {0, 0, 1, 1});
    pathfinding.OnChunkLoaded(chunk);
    CHECK_FALSE(pathfinding.IsPassable(5, 5));

    objects[0] = zl::game::MakeBuiltObject(door, false, true);
    physics.BuildChunkStatics(0, 0, chunk);
    pathfinding.OnChunkLoaded(chunk);
    CHECK(physics.StaticBodyCount() == 0u);
    CHECK(pathfinding.IsPassable(5, 5));
}

TEST_CASE("M17-T1 built object carries durability and player-built flag") {
    const auto& defs = zl::game::DefaultBuildables();
    const zl::world::SquareObject barricade =
        zl::game::MakeBuiltObject(defs[0]);
    CHECK(barricade.sprite == "object.barricade");
    CHECK(barricade.blocksMovement);
    CHECK(barricade.hp == doctest::Approx(120.0f));
    CHECK(barricade.playerBuilt);

    const zl::world::SquareObject campfire =
        zl::game::MakeBuiltObject(defs[4]);
    CHECK_FALSE(campfire.blocksMovement);
    CHECK(campfire.hp == doctest::Approx(-1.0f));
    CHECK(campfire.playerBuilt);

    const zl::world::SquareObject legacy{"object.barricade", 0, 0, true};
    CHECK(legacy.hp == doctest::Approx(-1.0f));
    CHECK_FALSE(legacy.playerBuilt);
    CHECK(zl::game::ResolveBarricadeHp(legacy, 120.0f) ==
          doctest::Approx(120.0f));

    zl::world::SquareObject damaged = barricade;
    damaged.hp = 60.0f;
    CHECK(zl::game::ResolveBarricadeHp(damaged, 120.0f) ==
          doctest::Approx(60.0f));
    damaged.hp = 200.0f;
    CHECK(zl::game::ResolveBarricadeHp(damaged, 120.0f) ==
          doctest::Approx(120.0f));
}

TEST_CASE("M17-T2 repair amount and demolish refund rules") {
    const auto& defs = zl::game::DefaultBuildables();
    zl::world::SquareObject barricade = zl::game::MakeBuiltObject(defs[0]);
    barricade.hp = 30.0f;
    CHECK(zl::game::IsRepairableBarricade(barricade, 120.0f));
    CHECK(zl::game::RepairBarricadeHp(barricade.hp, 120.0f, 30.0f) ==
          doctest::Approx(60.0f));

    barricade.hp = 110.0f;
    CHECK(zl::game::RepairBarricadeHp(barricade.hp, 120.0f, 30.0f) ==
          doctest::Approx(120.0f));
    barricade.hp = 120.0f;
    CHECK_FALSE(zl::game::IsRepairableBarricade(barricade, 120.0f));
    CHECK(zl::game::RepairBarricadeHp(barricade.hp, 120.0f, 30.0f) ==
          doctest::Approx(120.0f));

    zl::world::SquareObject mapBarricade =
        zl::world::SquareObject{"object.barricade", 0, 0, true};
    mapBarricade.hp = 30.0f;
    CHECK_FALSE(zl::game::IsRepairableBarricade(mapBarricade, 120.0f));

    zl::world::SquareObject campfire = zl::game::MakeBuiltObject(defs[4]);
    CHECK_FALSE(zl::game::IsRepairableBarricade(campfire, 0.0f));

    CHECK(zl::game::BuildWoodRefund(3, 0.5f) == 2);
    CHECK(zl::game::BuildWoodRefund(2, 0.5f) == 1);
    CHECK(zl::game::BuildWoodRefund(4, 0.5f) == 2);
    CHECK(zl::game::BuildWoodRefund(3, 0.3f) == 1);
    CHECK(zl::game::BuildWoodRefund(3, 0.0f) == 0);
    CHECK(zl::game::BuildWoodRefund(0, 0.5f) == 0);
}

TEST_CASE("M17-T2 repair action hint includes cost and amount") {
    zl::game::ActionHintState state;
    state.barricadeHp = 60.0f;
    state.barricadeMaxHp = 120.0f;
    state.canRepairBarricade = true;
    state.repairWoodCost = 1;
    state.repairAmount = 30.0f;
    std::string tone;
    const std::string text = zl::game::ActionHintText(state, &tone);
    CHECK(text.find("60/120") != std::string::npos);
    CHECK(text.find("H 维修") != std::string::npos);
    CHECK(text.find("1 木材 +30") != std::string::npos);
}

TEST_CASE("C6 owner read-only hints hide edit actions") {
    zl::game::ActionHintState door;
    door.nearbyDoor = "木门 · 归属：玩家 2";
    door.doorOpen = false;
    door.barricadeHp = 90.0f;
    door.barricadeMaxHp = 120.0f;
    door.structureReadOnly = true;
    std::string tone;
    std::string text = zl::game::ActionHintText(door, &tone);
    CHECK(text == "无法开门：木门 · 归属：玩家 2（90/120）（仅所有者）");
    CHECK(text.find("按 E") == std::string::npos);
    door.doorOpen = true;
    text = zl::game::ActionHintText(door, &tone);
    CHECK(text == "无法关门：木门 · 归属：玩家 2（仅所有者）");
    CHECK(text.find("按 E") == std::string::npos);

    zl::game::ActionHintState repair;
    repair.structureName = "木墙 · 归属：玩家 2";
    repair.barricadeHp = 60.0f;
    repair.barricadeMaxHp = 120.0f;
    repair.structureReadOnly = true;
    repair.canRepairBarricade = false;
    repair.repairWoodCost = 1;
    repair.repairAmount = 30.0f;
    text = zl::game::ActionHintText(repair, &tone);
    CHECK(text == "木墙 · 归属：玩家 2 60/120 · 仅所有者");
    CHECK(text.find("H 维修") == std::string::npos);

    zl::game::ActionHintState storage;
    storage.nearbyStorage = "木箱 · 归属：玩家 2（只读）";
    text = zl::game::ActionHintText(storage, &tone);
    CHECK(text.find("按 I 打开仓库：木箱 · 归属：玩家 2（只读）") !=
          std::string::npos);
}
TEST_CASE("M16-T1 build config and wood item") {
    zl::core::Config config;
    REQUIRE(config.LoadFile(std::string(ZL_SOURCE_DIR) + "/config/game.json"));
    CHECK(config.Get<double>("building.range", 0.0) > 0.0);
    CHECK(config.Get<std::string>("building.woodItem", "") == "wood");
    CHECK(config.Get<double>("building.actionRange", 0.0) ==
          doctest::Approx(2.5));
    CHECK(config.Get<double>("building.actionCooldownSeconds", 0.0) ==
          doctest::Approx(0.5));
    CHECK(config.Get<int>("building.repairWoodCost", 0) == 1);
    CHECK(config.Get<double>("building.repairAmount", 0.0) ==
          doctest::Approx(30.0));
    CHECK(config.Get<double>("building.demolishRefundRatio", 0.0) ==
          doctest::Approx(0.5));
    CHECK(config.Get<std::string>("input.bindings.BuildMode", "") == "B");
    CHECK(config.Get<std::string>("input.bindings.RepairStructure", "") == "H");
    CHECK(config.Get<std::string>("input.bindings.RepairAllStructures", "") ==
          "J");
    CHECK(config.Get<std::string>("input.bindings.DemolishStructure", "") ==
          "X");
    CHECK(config.Get<int>("baseEvent.startHour", 0) == 22);
    CHECK(config.Get<int>("baseEvent.endHour", 0) == 5);
    CHECK(config.Get<double>("baseEvent.warningSeconds", 0.0) ==
          doctest::Approx(30.0));
    CHECK(config.Get<int>("baseEvent.maxAlive", 0) == 24);
    CHECK(config.Get<int>("baseEvent.maxWaveSize", 0) == 24);
    CHECK(config.Get<double>("baseEvent.scalePerDay", 0.0) ==
          doctest::Approx(0.12));
    CHECK(config.Get<int>("baseEvent.specialStartWave", -1) == 1);
    CHECK(config.Get<int>("baseEvent.specialCount", 0) == 1);
    CHECK(config.Get<double>("baseEvent.breakerDamage", 0.0) ==
          doctest::Approx(28.0));
    CHECK(config.Get<double>("baseEvent.howlerRadius", 0.0) ==
          doctest::Approx(24.0));
    CHECK(config.Get<double>("baseEvent.triggerRadius", 0.0) ==
          doctest::Approx(12.0));
    const std::vector<int> waves =
        config.Get<std::vector<int>>("baseEvent.waves", {});
    REQUIRE(waves.size() == 3u);
    CHECK(waves[0] == 4);
    CHECK(waves[1] == 6);
    CHECK(waves[2] == 8);
    const auto& root = config.Raw();
    const auto building = root.find("building");
    REQUIRE(building != root.end());
    const auto types = building->find("types");
    REQUIRE(types != building->end());
    REQUIRE(types->is_array());
    CHECK(types->size() == 15u);
    const auto rainCollector = std::find_if(
        types->begin(), types->end(), [](const auto& item) {
            return item.value("id", std::string()) == "rain_collector";
        });
    REQUIRE(rainCollector != types->end());
    CHECK(rainCollector->value("requiresMission", std::string()) ==
          "farm_tools");
    const auto hasType = [&types](std::string_view id) {
        return std::any_of(types->begin(), types->end(), [id](const auto& item) {
            return item.value("id", std::string()) == id;
        });
    };
    CHECK(hasType("farm_plot"));
    CHECK(hasType("generator"));
    CHECK(hasType("fridge"));
    CHECK(hasType("spike_trap"));
    CHECK(hasType("floodlight"));
    CHECK(hasType("electric_fence"));

    zl::items::ItemRegistry registry;
    std::string error;
    REQUIRE(registry.LoadFile(std::string(ZL_SOURCE_DIR) + "/assets/items.json", &error));
    CHECK(error.empty());
    const zl::items::ItemDef* wood = registry.Find("wood");
    REQUIRE(wood != nullptr);
    CHECK(wood->icon == "item.wood");
    CHECK(wood->maxStack == 20);
    CHECK(wood->weight > 0.0f);
}

TEST_CASE("M18-F4 door hint and repair all count") {
    zl::game::ActionHintState door;
    door.nearbyDoor = "木门";
    door.doorOpen = false;
    door.barricadeHp = 90.0f;
    door.barricadeMaxHp = 120.0f;
    door.canRepairBarricade = true;
    door.repairWoodCost = 1;
    door.repairAmount = 30.0f;
    std::string tone;
    std::string text = zl::game::ActionHintText(door, &tone);
    CHECK(text.find("按 E 开门：木门") != std::string::npos);
    CHECK(text.find("90/120") != std::string::npos);
    CHECK(text.find("H 维修（1 木材 +30）") != std::string::npos);
    door.doorOpen = true;
    text = zl::game::ActionHintText(door, &tone);
    CHECK(text.find("按 E 关门：木门") != std::string::npos);

    zl::game::ActionHintState repair;
    repair.barricadeHp = 90.0f;
    repair.barricadeMaxHp = 120.0f;
    repair.structureName = "木墙";
    repair.canRepairAll = true;
    repair.repairAllCount = 3;
    repair.repairAllWood = 3;
    text = zl::game::ActionHintText(repair, &tone);
    CHECK(text.find("木墙 90/120") != std::string::npos);
    CHECK(text.find("J 维修全部（3 个/3 木材）") != std::string::npos);
}

TEST_CASE("M16-T1 barricade action hint") {
    zl::game::ActionHintState state;
    state.barricadeHp = 30.0f;
    state.barricadeMaxHp = 120.0f;
    std::string tone;
    const std::string text = zl::game::ActionHintText(state, &tone);
    CHECK(text.find("木栅栏") != std::string::npos);
    CHECK(text.find("30/120") != std::string::npos);
    CHECK(tone == "fail");
}

TEST_CASE("M16-T2 zombie attacks and destroys nearest barricade") {
    entt::registry registry;
    const entt::entity zombie = registry.create();
    registry.emplace<zl::ecs::AiState>(zombie).status = zl::ecs::AiStatus::Chase;
    registry.emplace<zl::ecs::Health>(zombie);
    registry.emplace<zl::ecs::Position>(zombie, 10.2f, 10.5f, 0);

    std::unordered_map<std::uint64_t, zl::game::BuiltBarricade> barricades;
    const std::uint64_t nearKey = zl::game::BuildCellKey(10, 10);
    const std::uint64_t farKey = zl::game::BuildCellKey(11, 10);
    barricades[nearKey] = zl::game::BuiltBarricade{"object.barricade", 20.0f, 120.0f};
    barricades[farKey] = zl::game::BuiltBarricade{"object.barricade", 20.0f, 120.0f};
    std::unordered_map<std::uint32_t, float> cooldowns;

    const auto hit = zl::game::TickZombieBarricadeAttack(
        registry, zombie, registry.get<zl::ecs::AiState>(zombie),
        registry.get<zl::ecs::Health>(zombie), registry.get<zl::ecs::Position>(zombie),
        10.5f, 10.5f, barricades, cooldowns, 12.0f, 1.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->key == nearKey);
    CHECK(hit->destroyed == false);
    CHECK(barricades[nearKey].hp == doctest::Approx(8.0f));
    CHECK(barricades[farKey].hp == doctest::Approx(20.0f));
    CHECK(cooldowns[static_cast<std::uint32_t>(entt::to_integral(zombie))] ==
          doctest::Approx(1.0f));
    CHECK(registry.get<zl::ecs::ActionState>(zombie).action ==
          zl::ecs::ActorAction::Attacking);

    CHECK_FALSE(zl::game::TickZombieBarricadeAttack(
                    registry, zombie, registry.get<zl::ecs::AiState>(zombie),
                    registry.get<zl::ecs::Health>(zombie),
                    registry.get<zl::ecs::Position>(zombie), 10.5f, 10.5f,
                    barricades, cooldowns, 12.0f, 1.0f)
                    .has_value());
    CHECK(barricades[nearKey].hp == doctest::Approx(8.0f));

    cooldowns.clear();
    const auto destroyed = zl::game::TickZombieBarricadeAttack(
        registry, zombie, registry.get<zl::ecs::AiState>(zombie),
        registry.get<zl::ecs::Health>(zombie), registry.get<zl::ecs::Position>(zombie),
        10.5f, 10.5f, barricades, cooldowns, 12.0f, 1.0f);
    REQUIRE(destroyed.has_value());
    CHECK(destroyed->destroyed);
    CHECK(barricades[nearKey].hp <= 0.0f);

    auto& ai = registry.get<zl::ecs::AiState>(zombie);
    ai.status = zl::ecs::AiStatus::Idle;
    cooldowns.clear();
    CHECK_FALSE(zl::game::TickZombieBarricadeAttack(
                    registry, zombie, ai, registry.get<zl::ecs::Health>(zombie),
                    registry.get<zl::ecs::Position>(zombie), 10.5f, 10.5f,
                    barricades, cooldowns, 12.0f, 1.0f)
                    .has_value());
    CHECK(cooldowns.empty());

    ai.status = zl::ecs::AiStatus::Chase;
    auto& health = registry.get<zl::ecs::Health>(zombie);
    health.current = 0.0f;
    CHECK_FALSE(zl::game::TickZombieBarricadeAttack(
                    registry, zombie, ai, health,
                    registry.get<zl::ecs::Position>(zombie), 10.5f, 10.5f,
                    barricades, cooldowns, 12.0f, 1.0f)
                    .has_value());
}

TEST_CASE("M20-F3 breaker telegraphs before damaging a barricade") {
    entt::registry registry;
    const entt::entity zombie = registry.create();
    registry.emplace<zl::ecs::AiState>(zombie).status = zl::ecs::AiStatus::Chase;
    registry.emplace<zl::ecs::Health>(zombie);
    registry.emplace<zl::ecs::Position>(zombie, 10.2f, 10.5f, 0);
    auto& special = registry.emplace<zl::ecs::ZombieSpecial>(zombie);
    special.kind = zl::ecs::ZombieSpecialKind::Breaker;

    std::unordered_map<std::uint64_t, zl::game::BuiltBarricade> barricades;
    const std::uint64_t key = zl::game::BuildCellKey(10, 10);
    barricades[key] = zl::game::BuiltBarricade{"object.barricade", 120.0f, 120.0f};
    std::unordered_map<std::uint32_t, float> cooldowns;

    const auto warning = zl::game::TickZombieBarricadeAttack(
        registry, zombie, registry.get<zl::ecs::AiState>(zombie),
        registry.get<zl::ecs::Health>(zombie),
        registry.get<zl::ecs::Position>(zombie), 10.5f, 10.5f, barricades,
        cooldowns, 12.0f, 1.0f, 1.5f, 10.0f, 2.2f, 28.0f, 1.2f, 0.9f);
    REQUIRE(warning.has_value());
    CHECK(warning->warningStarted);
    CHECK(barricades[key].hp == doctest::Approx(120.0f));
    CHECK(special.windup == doctest::Approx(0.9f));
    CHECK(registry.get<zl::ecs::ActionState>(zombie).action ==
          zl::ecs::ActorAction::Attacking);

    CHECK_FALSE(zl::game::TickZombieBarricadeAttack(
                    registry, zombie, registry.get<zl::ecs::AiState>(zombie),
                    registry.get<zl::ecs::Health>(zombie),
                    registry.get<zl::ecs::Position>(zombie), 10.5f, 10.5f,
                    barricades, cooldowns, 12.0f, 1.0f, 1.5f, 10.0f, 2.2f,
                    28.0f, 1.2f, 0.9f)
                    .has_value());
    special.windup = 0.0f;
    const auto hit = zl::game::TickZombieBarricadeAttack(
        registry, zombie, registry.get<zl::ecs::AiState>(zombie),
        registry.get<zl::ecs::Health>(zombie),
        registry.get<zl::ecs::Position>(zombie), 10.5f, 10.5f, barricades,
        cooldowns, 12.0f, 1.0f, 1.5f, 10.0f, 2.2f, 28.0f, 1.2f, 0.9f);
    REQUIRE(hit.has_value());
    CHECK_FALSE(hit->warningStarted);
    CHECK(barricades[key].hp == doctest::Approx(92.0f));
    CHECK(cooldowns[static_cast<std::uint32_t>(entt::to_integral(zombie))] ==
          doctest::Approx(1.2f));
}