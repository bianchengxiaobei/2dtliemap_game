#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ecs/EcsWorld.h"
#include "ecs/components/CommonComponents.h"
#include "game/BuildingRules.h"
#include "game/PlayerStorage.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"

using namespace zl;

namespace {

const char* kItemsJson = R"({
  "format": "items-v1",
  "items": [
    { "id": "rag", "name": "Rag", "icon": "i", "weight": 0.1, "maxStack": 10, "type": "generic" },
    { "id": "bandage", "name": "Bandage", "icon": "i", "weight": 0.1, "maxStack": 5, "type": "consumable" },
    { "id": "axe", "name": "Axe", "icon": "i", "weight": 3.0, "maxStack": 1, "type": "weapon" }
  ]
})";

struct Fixture {
    items::ItemRegistry items;

    Fixture() {
        std::string error;
        REQUIRE(items.LoadFromString(kItemsJson, "test-storage-items", &error));
    }
};

int TotalItems(const ecs::Container& container) {
    int total = 0;
    for (const ecs::ItemInstance& item : container.items) {
        total += item.count;
    }
    return total;
}

}  // namespace

TEST_CASE("M18-F1 player storage spawn and nearest lookup") {
    ecs::EcsWorld world;
    const ecs::Entity storage = game::SpawnPlayerStorage(
        world, 10.5f, 20.5f, 0, "object.crate", 10.0f, 2);
    REQUIRE(static_cast<bool>(storage != entt::null));

    entt::registry& registry = world.Registry();
    REQUIRE(registry.all_of<ecs::Position, ecs::SpriteRef, ecs::Container,
                            ecs::PlayerStorage>(storage));
    CHECK(registry.get<ecs::Container>(storage).capacityKg ==
          doctest::Approx(10.0f));
    CHECK(registry.get<ecs::PlayerStorage>(storage).maxStacks == 2);
    CHECK(game::IsPlayerStorage(world, storage));
    CHECK(game::FindNearestPlayerStorage(world, 10.5f, 20.5f, 0, 2.0f) ==
          storage);
    CHECK(static_cast<bool>(game::FindNearestPlayerStorage(world, 10.5f, 20.5f, 1, 2.0f) == entt::null));
    CHECK(static_cast<bool>(game::FindNearestPlayerStorage(world, 40.0f, 40.0f, 0, 2.0f) == entt::null));
}

TEST_CASE("M18-F1 storage transfers preserve item counts and capacity") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity storage = game::SpawnPlayerStorage(
        world, 5.5f, 5.5f, 0, "object.crate", 10.0f, 2);
    REQUIRE(static_cast<bool>(storage != entt::null));

    ecs::Container player{{ecs::ItemInstance{"rag", 5, 1.0f}}, 20.0f};
    const game::StorageTransferResult partial = game::StorePlayerStorageItem(
        world, fixture.items, storage, player, 0, 2);
    CHECK(partial.found);
    CHECK(partial.moved == 2);
    CHECK(player.items[0].count == 3);
    CHECK(TotalItems(world.Registry().get<ecs::Container>(storage)) == 2);

    player.items.push_back(ecs::ItemInstance{"bandage", 3, 1.0f});
    const game::StorageTransferResult all =
        game::StoreAllInPlayerStorage(world, fixture.items, storage, player);
    CHECK(all.moved == 6);
    CHECK(player.items.empty());
    const ecs::Container& stored = world.Registry().get<ecs::Container>(storage);
    CHECK(stored.items.size() == 2);
    CHECK(TotalItems(stored) == 8);

    player.items.push_back(ecs::ItemInstance{"axe", 1, 1.0f});
    const game::StorageTransferResult full =
        game::StoreAllInPlayerStorage(world, fixture.items, storage, player);
    CHECK(full.moved == 0);
    CHECK(player.items.size() == 1);

    const game::StorageTransferResult taken =
        game::TakeAllFromPlayerStorage(world, fixture.items, storage, player);
    CHECK(taken.moved == 8);
    CHECK(TotalItems(world.Registry().get<ecs::Container>(storage)) == 0);
    CHECK(TotalItems(player) == 9);
}

TEST_CASE("M18-F1 storage respects weight capacity and detach") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity storage = game::SpawnPlayerStorage(
        world, 5.5f, 5.5f, 0, "object.crate", 0.25f, 6);
    REQUIRE(static_cast<bool>(storage != entt::null));

    ecs::Container player{{ecs::ItemInstance{"rag", 5, 1.0f}}, 20.0f};
    const game::StorageTransferResult result = game::StorePlayerStorageItem(
        world, fixture.items, storage, player, 0, 5);
    CHECK(result.moved == 2);
    CHECK(player.items[0].count == 3);
    CHECK(TotalItems(world.Registry().get<ecs::Container>(storage)) == 2);

    const std::vector<ecs::ItemInstance> contents =
        game::DetachPlayerStorageContents(world, storage);
    REQUIRE(contents.size() == 1u);
    CHECK(contents[0].count == 2);
    CHECK(world.Registry().get<ecs::Container>(storage).items.empty());
    CHECK(game::DetachPlayerStorageContents(world, storage).empty());
}

TEST_CASE("M18-F1 player storage persists through entity save") {
    save::ComponentRegistry components;
    std::string error;
    REQUIRE(save::RegisterBuiltinComponents(components, &error));
    CHECK(error.empty());

    ecs::EcsWorld source;
    const ecs::Entity storage = source.Create();
    entt::registry& registry = source.Registry();
    registry.emplace<ecs::Position>(storage, ecs::Position{3.5f, 4.5f, 0});
    registry.emplace<ecs::SpriteRef>(
        storage, ecs::SpriteRef{ecs::HashName("object.crate"), 0, 0xFFFFFFFFu});
    registry.emplace<ecs::Container>(
        storage, ecs::Container{{ecs::ItemInstance{"bandage", 2, 0.75f}}, 12.0f});
    registry.emplace<ecs::PlayerStorage>(storage, ecs::PlayerStorage{4});

    save::SavedEntity saved;
    REQUIRE(save::CaptureEntity(source, storage, components, &saved, &error));
    CHECK(error.empty());
    const std::vector<std::uint8_t> bytes =
        save::EncodeEntitySnapshot(save::EntitySnapshot{{saved}}, &error);
    REQUIRE_FALSE(bytes.empty());
    save::EntitySnapshot decoded;
    REQUIRE(save::DecodeEntitySnapshot(bytes, &decoded, &error));
    CHECK(error.empty());

    ecs::EcsWorld target;
    const ecs::Entity loaded =
        target.CreateWithStableId(decoded.entities[0].stableId);
    REQUIRE(loaded != ecs::EcsWorld::kNull);
    REQUIRE(save::ApplyEntity(decoded.entities[0], target, loaded, components,
                              &error));
    const auto* meta = target.Registry().try_get<ecs::PlayerStorage>(loaded);
    REQUIRE(meta != nullptr);
    CHECK(meta->maxStacks == 4);
    const auto* container = target.Registry().try_get<ecs::Container>(loaded);
    REQUIRE(container != nullptr);
    CHECK(container->capacityKg == doctest::Approx(12.0f));
    REQUIRE(container->items.size() == 1u);
    CHECK(container->items[0].defId == "bandage");
    CHECK(container->items[0].count == 2);
}

TEST_CASE("M18-F1 storage sort groups by item type") {
    Fixture fixture;
    ecs::EcsWorld world;
    const ecs::Entity storage = game::SpawnPlayerStorage(
        world, 5.5f, 5.5f, 0, "object.crate", 50.0f, 6);
    REQUIRE(static_cast<bool>(storage != entt::null));
    auto& container = world.Registry().get<ecs::Container>(storage);
    container.items = {
        ecs::ItemInstance{"axe", 1, 1.0f},
        ecs::ItemInstance{"bandage", 2, 1.0f},
        ecs::ItemInstance{"rag", 3, 1.0f},
    };

    game::SortPlayerStorage(world, fixture.items, storage);
    REQUIRE(container.items.size() == 3u);
    CHECK(container.items[0].defId == "rag");
    CHECK(container.items[1].defId == "axe");
    CHECK(container.items[2].defId == "bandage");
    CHECK(TotalItems(container) == 6);
}

TEST_CASE("M18-F1 default buildables include storage") {
    const auto& defs = game::DefaultBuildables();
    REQUIRE(defs.size() == 18u);
    CHECK(defs[8].id == "storage");
    CHECK(defs[8].sprite == "object.crate");
    CHECK(defs[8].storageKg == doctest::Approx(50.0f));
    CHECK(defs[8].storageSlots == 6);
    CHECK(game::IsStorageBuildable(defs[8]));
    CHECK_FALSE(game::IsStorageBuildable(defs[0]));
    CHECK(defs[6].id == "rain_collector");
    CHECK(defs[6].sprite == "object.locker");
    CHECK_FALSE(game::IsStorageBuildable(defs[6]));
    CHECK(defs[7].id == "bed");
    CHECK(defs[7].sprite == "object.bed");
    CHECK_FALSE(game::IsStorageBuildable(defs[7]));
    CHECK(defs[11].id == "fridge");
    CHECK(game::IsStorageBuildable(defs[11]));
}

TEST_CASE("X-F3 性能基线：1/16/64 仓库最近查找与存取") {
    Fixture fixture;
    for (const int warehouseCount : {1, 16, 64}) {
        ecs::EcsWorld world;
        std::vector<ecs::Entity> warehouses;
        warehouses.reserve(static_cast<std::size_t>(warehouseCount));
        for (int i = 0; i < warehouseCount; ++i) {
            const ecs::Entity storage = game::SpawnPlayerStorage(
                world, 10.0f + static_cast<float>(i) * 2.0f, 10.0f, 0,
                "object.crate", 50.0f, 6);
            REQUIRE(static_cast<bool>(storage != entt::null));
            warehouses.push_back(storage);
            world.Registry().get<ecs::Container>(storage).items.assign(
                6, ecs::ItemInstance{"rag", 10, 1.0f});
        }

        constexpr int kQueries = 2000;
        const auto queryStart = std::chrono::steady_clock::now();
        std::uint64_t foundCount = 0;
        for (int i = 0; i < kQueries; ++i) {
            const ecs::Entity found = game::FindNearestPlayerStorage(
                world, 10.0f, 10.0f, 0, 10000.0f);
            foundCount += static_cast<std::uint64_t>(found != entt::null);
        }
        const double queryMs =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - queryStart)
                .count();
        CHECK(foundCount == static_cast<std::uint64_t>(kQueries));

        ecs::Container player{{}, 200.0f};
        const auto runRound = [&]() {
            for (const ecs::Entity storage : warehouses) {
                const game::StorageTransferResult taken =
                    game::TakeAllFromPlayerStorage(world, fixture.items, storage,
                                                   player);
                const game::StorageTransferResult stored =
                    game::StoreAllInPlayerStorage(world, fixture.items, storage,
                                                  player);
                if (taken.moved != 60 || stored.moved != 60 ||
                    !player.items.empty()) {
                    return false;
                }
            }
            return true;
        };

        REQUIRE(runRound());
        constexpr int kRounds = 30;
        int failedRounds = 0;
        const auto transferStart = std::chrono::steady_clock::now();
        for (int i = 0; i < kRounds; ++i) {
            if (!runRound()) {
                ++failedRounds;
            }
        }
        const double transferMs =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - transferStart)
                .count();
        CHECK(failedRounds == 0);
        int storedItems = 0;
        for (const ecs::Entity storage : warehouses) {
            storedItems += TotalItems(world.Registry().get<ecs::Container>(storage));
        }
        CHECK(storedItems == warehouseCount * 60);

        const double queryAvgMs = queryMs / static_cast<double>(kQueries);
        const double transferAvgMs = transferMs / static_cast<double>(kRounds);
        std::printf(
            "[bench] X-F3 仓库：%d 仓库 nearest=%.4f ms/query transfer=%.4f ms/round\n",
            warehouseCount, queryAvgMs, transferAvgMs);
        CHECK(queryAvgMs < 10.0);
        CHECK(transferAvgMs < 100.0);
#ifdef NDEBUG
        CHECK(queryAvgMs < 1.0);
        CHECK(transferAvgMs < 10.0);
#endif
    }
}
