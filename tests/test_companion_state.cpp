#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/Companion.h"
#include "game/CompanionInventory.h"
#include "game/CompanionSystem.h"
#include "items/InventoryOps.h"
#include "items/ItemRegistry.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/SaveManager.h"
#include "save/SaveTypes.h"

using namespace zl;

namespace {

struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "zomboidlike_f3t4" /
               name) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::create_directories(path, error);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

ecs::Entity AddOwner(ecs::EcsWorld& world, float x, float y) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    registry.emplace<ecs::PlayerTag>(entity);
    registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    registry.emplace<ecs::AttackIntent>(entity, ecs::AttackIntent{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    return entity;
}

ecs::Entity AddCompanion(ecs::EcsWorld& world, ecs::Entity owner, float x,
                         float y, game::CompanionState state = {}) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Velocity>(entity, ecs::Velocity{});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::Facing>(entity, ecs::Facing{});
    registry.emplace<ecs::AnimState>(entity, ecs::AnimState{});
    registry.emplace<ecs::ActionState>(entity, ecs::ActionState{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    registry.emplace<ecs::PathRequest>(entity, ecs::PathRequest{});
    ecs::PathFollow follow;
    follow.speed = 3.6f;
    registry.emplace<ecs::PathFollow>(entity, follow);
    registry.emplace<ecs::FriendlyNpc>(entity);
    registry.emplace<ecs::NoCombatTarget>(entity);
    registry.emplace<game::Companion>(
        entity, game::Companion{"companion_medic_lin",
                                world.IdOf(owner).id, "village_haven"});
    registry.emplace<game::CompanionState>(entity, std::move(state));
    game::CompanionAiState ai;
    ai.anchorX = x;
    ai.anchorY = y;
    ai.homeX = x;
    ai.homeY = y;
    registry.emplace<game::CompanionAiState>(entity, ai);
    return entity;
}

ecs::Entity AddZombie(ecs::EcsWorld& world, float x, float y) {
    const ecs::Entity entity = world.Create();
    entt::registry& registry = world.Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, 0});
    registry.emplace<ecs::Health>(entity, ecs::Health{100.0f, 100.0f});
    registry.emplace<ecs::AiState>(entity, ecs::AiState{});
    registry.emplace<ecs::PerceptionState>(entity, ecs::PerceptionState{});
    registry.emplace<ecs::MoveIntent>(entity, ecs::MoveIntent{});
    return entity;
}

void LoadItems(items::ItemRegistry* items) {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(items->LoadFile(root / "assets" / "items.json"));
}

}  // namespace

TEST_CASE("M33-F3-T4 同伴背包/装备往返与武器伤害护甲") {
    items::ItemRegistry items;
    LoadItems(&items);
    ecs::EcsWorld world;
    const ecs::Entity owner = AddOwner(world, 10.0f, 10.0f);
    game::CompanionState initial;
    initial.inventory.capacityKg = 20.0f;
    const ecs::Entity companion = AddCompanion(world, owner, 10.5f, 10.0f,
                                               std::move(initial));
    game::CompanionState& state =
        world.Registry().get<game::CompanionState>(companion);

    ecs::Container bag;
    bag.capacityKg = 20.0f;
    bag.items.push_back(ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    CHECK(game::TransferItemToCompanion(bag, 0, world, companion, items, 1) ==
          1);
    CHECK(items::CountOf(bag, "bandage") == 1);
    CHECK(items::CountOf(state.inventory, "bandage") == 1);
    CHECK(game::TransferItemFromCompanion(world, companion, items, 0, 1,
                                          bag) == 1);
    CHECK(items::CountOf(bag, "bandage") == 2);
    CHECK(items::CountOf(state.inventory, "bandage") == 0);

    CHECK(game::AddItemToCompanion(
              world, companion, items,
              ecs::ItemInstance{"kitchen_knife", 1, 1.0f, 0}) == 1);
    std::string error;
    REQUIRE(game::EquipCompanionItem(world, companion, items, 0,
                                     ecs::EquipSlot::PrimaryHand, &error));
    REQUIRE(state.equipment.slots[0].has_value());
    CHECK(state.equipment.slots[0]->defId == "kitchen_knife");

    CHECK(game::AddItemToCompanion(
              world, companion, items,
              ecs::ItemInstance{"jacket", 1, 1.0f, 0}) == 1);
    REQUIRE(game::EquipCompanionItem(world, companion, items, 0,
                                     ecs::EquipSlot::Body, &error));
    REQUIRE(state.equipment.slots[1].has_value());
    CHECK(state.equipment.slots[1]->defId == "jacket");
    const auto* resist =
        world.Registry().try_get<ecs::DamageResist>(companion);
    REQUIRE(resist != nullptr);
    CHECK(resist->bluntReduction == doctest::Approx(0.04f));
    CHECK(resist->clawReduction == doctest::Approx(0.04f));

    REQUIRE(game::UnequipCompanionItem(world, companion, items,
                                       ecs::EquipSlot::Body, &error));
    CHECK_FALSE(state.equipment.slots[1].has_value());
    CHECK(items::CountOf(state.inventory, "jacket") == 1);
    resist = world.Registry().try_get<ecs::DamageResist>(companion);
    REQUIRE(resist != nullptr);
    CHECK(resist->bluntReduction == doctest::Approx(0.0f));
    const auto jacketIndex =
        static_cast<std::size_t>(std::distance(
            state.inventory.items.begin(),
            std::find_if(state.inventory.items.begin(),
                         state.inventory.items.end(),
                         [](const ecs::ItemInstance& item) {
                             return item.defId == "jacket";
                         })));
    REQUIRE(game::EquipCompanionItem(world, companion, items, jacketIndex,
                                     ecs::EquipSlot::Body, &error));

    const ecs::Entity zombie = AddZombie(world, 11.2f, 10.0f);
    float dealt = 0.0f;
    game::CompanionSystem system(
        {}, [&dealt, &world](ecs::Entity, ecs::Entity target, float damage,
                             ecs::DamageType) {
            auto* health = world.Registry().try_get<ecs::Health>(target);
            if (health == nullptr || health->current <= 0.0f) {
                return false;
            }
            health->current = std::max(0.0f, health->current - damage);
            dealt += damage;
            return true;
        });
    system.SetItemRegistry(&items);
    system.Update(world, 1.0f / 60.0f);
    CHECK(dealt == doctest::Approx(15.96f));
    CHECK(system.GetStats().attacks == 1u);
    const auto* zombieHealth =
        world.Registry().try_get<ecs::Health>(zombie);
    REQUIRE(zombieHealth != nullptr);
    CHECK(zombieHealth->current == doctest::Approx(84.04f));
}

TEST_CASE("M33-F3-T4 倒地救援、死亡尸体与物品不复制") {
    ecs::EcsWorld world;
    const ecs::Entity owner = AddOwner(world, 10.0f, 10.0f);
    game::CompanionState initial;
    initial.inventory.capacityKg = 20.0f;
    initial.inventory.items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    initial.equipment.slots[0] =
        ecs::ItemInstance{"kitchen_knife", 1, 1.0f, 0};
    initial.equipment.slots[1] =
        ecs::ItemInstance{"jacket", 1, 1.0f, 0};
    const ecs::Entity companion = AddCompanion(world, owner, 10.5f, 10.0f,
                                               std::move(initial));
    entt::registry& registry = world.Registry();
    game::CompanionState& state = registry.get<game::CompanionState>(companion);
    auto& health = registry.get<ecs::Health>(companion);
    health.current = 0.0f;

    game::CompanionSystem::Options options;
    options.downedSeconds = 1.0f;
    game::CompanionSystem system({}, {}, options);
    system.Update(world, 0.1f);
    CHECK(state.downed);
    CHECK_FALSE(state.dead);
    CHECK(state.downedRemaining == doctest::Approx(1.0f));
    CHECK(system.GetStats().downed == 1);

    std::string error;
    REQUIRE(system.TryRescue(world, companion, owner, &error));
    CHECK_FALSE(state.downed);
    CHECK_FALSE(state.dead);
    CHECK(state.downedRemaining == doctest::Approx(0.0f));
    CHECK(health.current == doctest::Approx(35.0f));
    CHECK(registry.view<ecs::Corpse>().size() == 0u);
    CHECK(state.inventory.items.size() == 1u);
    REQUIRE(state.equipment.slots[0].has_value());

    health.current = 0.0f;
    system.Update(world, 0.1f);
    CHECK(state.downed);
    system.Update(world, 1.1f);
    world.Flush();
    CHECK(state.dead);
    CHECK_FALSE(state.downed);
    CHECK(system.GetStats().deaths == 1);
    CHECK(state.inventory.items.empty());
    CHECK_FALSE(state.equipment.slots[0].has_value());
    CHECK_FALSE(state.equipment.slots[1].has_value());

    ecs::Entity corpse = ecs::EcsWorld::kNull;
    registry.view<ecs::Corpse>().each(
        [&](const ecs::Entity entity, const ecs::Corpse&) {
            if (corpse == ecs::EcsWorld::kNull) {
                corpse = entity;
            }
        });
    REQUIRE(corpse != ecs::EcsWorld::kNull);
    const auto* corpseContainer = registry.try_get<ecs::Container>(corpse);
    REQUIRE(corpseContainer != nullptr);
    CHECK(items::CountOf(*corpseContainer, "kitchen_knife") == 1);
    CHECK(items::CountOf(*corpseContainer, "jacket") == 1);
    CHECK(items::CountOf(*corpseContainer, "bandage") == 2);
    int corpseItems = 0;
    for (const ecs::ItemInstance& item : corpseContainer->items) {
        corpseItems += item.count;
    }
    CHECK(corpseItems == 4);

    system.Update(world, 10.0f);
    world.Flush();
    CHECK(registry.view<ecs::Corpse>().size() == 1u);
}

TEST_CASE("M33-F3-T4 药品、弹药与统一使用规则") {
    items::ItemRegistry items;
    LoadItems(&items);
    ecs::EcsWorld world;
    const ecs::Entity owner = AddOwner(world, 10.0f, 10.0f);
    game::CompanionState initial;
    initial.inventory.capacityKg = 20.0f;
    initial.inventory.items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    initial.inventory.items.push_back(
        ecs::ItemInstance{"ammo_9mm", 10, 1.0f, 0});
    initial.equipment.slots[0] =
        ecs::ItemInstance{"pistol", 1, 1.0f, 0};
    const ecs::Entity companion = AddCompanion(world, owner, 10.5f, 10.0f,
                                               std::move(initial));
    entt::registry& registry = world.Registry();
    game::CompanionState& state = registry.get<game::CompanionState>(companion);
    auto& health = registry.get<ecs::Health>(companion);
    health.current = 50.0f;

    items::UseResult use;
    std::string error;
    REQUIRE(game::UseCompanionConsumable(world, companion, items, 0, &use,
                                         &error));
    CHECK(use.defId == "bandage");
    CHECK(health.current == doctest::Approx(65.0f));
    CHECK(items::CountOf(state.inventory, "bandage") == 1);

    CHECK(game::ReloadCompanionWeapon(world, companion, items, &error) == 8);
    REQUIRE(state.equipment.slots[0].has_value());
    CHECK(state.equipment.slots[0]->loadedAmmo == 8);
    CHECK(items::CountOf(state.inventory, "ammo_9mm") == 2);
    CHECK(game::ReloadCompanionWeapon(world, companion, items, &error) == 0);
    CHECK(state.equipment.slots[0]->loadedAmmo == 8);
    CHECK(items::CountOf(state.inventory, "ammo_9mm") == 2);

    game::CompanionSystem system({}, {});
    system.Update(world, 1.0f / 60.0f);
    CHECK(items::CountOf(state.inventory, "bandage") == 1);
    CHECK(state.equipment.slots[0]->loadedAmmo == 8);
}

TEST_CASE("M33-F3-T4 同伴装备背包与倒地状态存档往返") {
    items::ItemRegistry items;
    LoadItems(&items);
    ecs::EcsWorld source;
    const ecs::Entity owner = AddOwner(source, 10.0f, 10.0f);
    game::CompanionState initial;
    initial.loyalty = 61;
    initial.command = game::CompanionCommand::Stay;
    initial.standby = true;
    initial.inventory.capacityKg = 20.0f;
    initial.inventory.items.push_back(
        ecs::ItemInstance{"bandage", 2, 1.0f, 0});
    initial.equipment.slots[0] =
        ecs::ItemInstance{"kitchen_knife", 1, 0.5f, 0};
    initial.equipment.slots[1] =
        ecs::ItemInstance{"jacket", 1, 0.75f, 0};
    const ecs::Entity companion = AddCompanion(source, owner, 12.5f, 8.0f,
                                               std::move(initial));
    source.Registry().get<ecs::Health>(companion).current = 72.0f;

    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    CHECK(components.Size() == 53u);

    save::EntitySnapshot snapshot;
    REQUIRE(save::CaptureEntities(source, components, &snapshot, {}, &error));
    const TempDir dir("save");
    save::SaveManager::Options managerOptions;
    managerOptions.root = dir.path / "saves";
    save::SaveManager manager(managerOptions);
    save::SaveManager::CreateOptions create;
    create.name = "M33-F3-T4";
    create.mapId = "village_demo";
    create.gameVersion = save::kDefaultGameVersion;
    create.entities = &snapshot;
    std::string saveId;
    REQUIRE_MESSAGE(manager.Create(create, &saveId, &error), error);

    ecs::EcsWorld target;
    save::EntitySnapshot loadedSnapshot;
    save::SaveMetadata metadata;
    REQUIRE(manager.Load(saveId, components, &loadedSnapshot, &metadata,
                         &error) == save::SaveManager::LoadResult::Ok);
    std::vector<ecs::Entity> loaded;
    REQUIRE_MESSAGE(save::LoadEntities(loadedSnapshot, target, components,
                                       &loaded, &error),
                    error);
    const ecs::Entity loadedCompanion =
        target.FindById(source.IdOf(companion).id);
    REQUIRE(loadedCompanion != ecs::EcsWorld::kNull);
    auto& loadedRegistry = target.Registry();
    const auto* identity =
        loadedRegistry.try_get<game::Companion>(loadedCompanion);
    const auto* state =
        loadedRegistry.try_get<game::CompanionState>(loadedCompanion);
    const auto* health =
        loadedRegistry.try_get<ecs::Health>(loadedCompanion);
    REQUIRE(identity != nullptr);
    REQUIRE(state != nullptr);
    REQUIRE(health != nullptr);
    CHECK(identity->defId == "companion_medic_lin");
    CHECK(identity->ownerStableId == source.IdOf(owner).id);
    CHECK(identity->homeSettlementId == "village_haven");
    CHECK(state->loyalty == 61);
    CHECK(state->command == game::CompanionCommand::Stay);
    CHECK(state->standby);
    CHECK(health->current == doctest::Approx(72.0f));
    CHECK(health->max == doctest::Approx(100.0f));
    REQUIRE(state->equipment.slots[0].has_value());
    REQUIRE(state->equipment.slots[1].has_value());
    CHECK(state->equipment.slots[0]->defId == "kitchen_knife");
    CHECK(state->equipment.slots[0]->condition == doctest::Approx(0.5f));
    CHECK(state->equipment.slots[1]->defId == "jacket");
    CHECK(items::CountOf(state->inventory, "bandage") == 2);

    REQUIRE(game::PrepareLoadedCompanion(target, loadedCompanion, &items));
    CHECK(loadedRegistry.all_of<ecs::FriendlyNpc>(loadedCompanion));
    CHECK(loadedRegistry.all_of<ecs::NoCombatTarget>(loadedCompanion));
    const auto* resist =
        loadedRegistry.try_get<ecs::DamageResist>(loadedCompanion);
    REQUIRE(resist != nullptr);
    CHECK(resist->bluntReduction > 0.0f);
}
