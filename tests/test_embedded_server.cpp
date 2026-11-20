#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "core/Config.h"
#include "game/Animals.h"
#include "game/BuildingRules.h"
#include "game/Companion.h"
#include "game/ContentHash.h"
#include "game/Crops.h"
#include "game/EmbeddedServer.h"
#include "game/Forage.h"
#include "game/Missions.h"
#include "game/ModContent.h"
#include "game/NetworkClient.h"
#include "game/Poi.h"
#include "game/Settlement.h"
#include "game/Traps.h"
#include "game/VehicleMods.h"
#include "game/WorldEvent.h"
#include "items/ItemRegistry.h"
#include "items/LootTable.h"
#include "items/RecipeRegistry.h"

TEST_CASE("M13-T9 内嵌主机：自动内容指纹下可连接并收发社交状态") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    REQUIRE(std::filesystem::exists(root / "config" / "game.json"));
    const std::filesystem::path old = std::filesystem::current_path();
    std::filesystem::current_path(root);

    const std::filesystem::path profiles =
        std::filesystem::temp_directory_path() / "zl-embedded-server-test.bin";
    std::error_code ignored;
    std::filesystem::remove(profiles, ignored);

    zl::game::EmbeddedServer server;
    zl::game::EmbeddedServer::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.bindHost = "127.0.0.1";
    options.port = 0;
    options.contentHash = 0;
    options.spawnDemoPopulation = false;
    options.playerProfilesFile = profiles.string();
    std::string error;
    REQUIRE_MESSAGE(server.Start(options, &error), error);
    REQUIRE(server.Running());
    REQUIRE(server.Port() != 0);
    REQUIRE(server.ContentHash() != 0);

    zl::core::Config clientConfig;
    REQUIRE(clientConfig.LoadFile(root / "config" / "game.json"));
    zl::items::ItemRegistry clientItems;
    REQUIRE(clientItems.LoadFile(
        clientConfig.Get<std::string>("items.file", "assets/items.json")));
    zl::items::RecipeRegistry clientRecipes;
    REQUIRE(clientRecipes.LoadFile(
        clientConfig.Get<std::string>("recipes.file", "assets/recipes.json"),
        clientItems));
    const zl::game::ModContentResult clientModContent =
        zl::game::LoadModContent(clientConfig, clientItems, clientRecipes);
    std::vector<zl::game::BuildableDef> clientBuildables =
        zl::game::LoadBuildableDefs(clientConfig);
    zl::game::AppendBuildableDefs(clientBuildables,
                                  clientModContent.buildables);

    zl::items::LootTableRegistry clientLoot;
    REQUIRE(clientLoot.LoadFile(
        clientConfig.Get<std::string>("loot.file", "assets/loot.json"),
        clientItems));
    for (const std::string& modLoot : clientModContent.lootFiles) {
        REQUIRE_MESSAGE(clientLoot.MergeFile(modLoot, clientItems, &error),
                        error);
    }
    zl::game::PoiConfig clientPois;
    REQUIRE_MESSAGE(zl::game::LoadPoiConfig(clientConfig, &clientPois, &error),
                    error);
    if (clientPois.enabled) {
        for (const std::string& modPoi : clientModContent.poiFiles) {
            REQUIRE_MESSAGE(zl::game::MergePoiFile(modPoi, &clientPois, nullptr,
                                                   &error),
                            error);
        }
    }
    zl::game::ForageRegistry clientForage;
    REQUIRE(clientForage.LoadFile(
        clientConfig.Get<std::string>("forage.file", "assets/forage.json"),
        clientLoot, &error));
    for (const std::string& modForage : clientModContent.forageFiles) {
        REQUIRE_MESSAGE(clientForage.MergeFile(modForage, clientLoot, &error),
                        error);
    }
    zl::game::AnimalRegistry clientAnimals;
    REQUIRE_MESSAGE(clientAnimals.LoadFile(
                        clientConfig.Get<std::string>("animals.file",
                                                       "assets/animals.json"),
                        clientItems, clientLoot, &error),
                    error);
    zl::game::CropRegistry clientCrops;
    REQUIRE_MESSAGE(clientCrops.LoadFile(
                        clientConfig.Get<std::string>("crops.file",
                                                       "assets/crops.json"),
                        clientItems, &error),
                    error);
    for (const std::string& modCrop : clientModContent.cropFiles) {
        REQUIRE_MESSAGE(clientCrops.MergeFile(modCrop, clientItems, &error),
                        error);
    }
    zl::game::MissionConfig clientMissions;
    REQUIRE_MESSAGE(
        zl::game::LoadMissionConfig(clientConfig, &clientMissions, &error),
        error);
    if (clientMissions.enabled) {
        zl::game::MissionMergeStats missionStats;
        REQUIRE_MESSAGE(
            zl::game::MergeMissionFiles(clientModContent.missionFiles,
                                        clientConfig, &clientMissions,
                                        &missionStats, &error),
            error);
    }
    zl::game::SettlementConfig clientSettlements;
    REQUIRE_MESSAGE(
        zl::game::LoadSettlementConfig(clientConfig, &clientSettlements,
                                       &error),
        error);
    for (const std::string& modSettlement :
         clientModContent.settlementFiles) {
        REQUIRE_MESSAGE(
            zl::game::MergeSettlementFile(modSettlement, &clientSettlements,
                                          nullptr, &error),
            error);
    }
    zl::game::WorldEventConfig clientWorldEvents;
    REQUIRE_MESSAGE(zl::game::LoadWorldEventConfig(
                        clientConfig, &clientWorldEvents, &error, &clientItems),
                    error);
    for (const std::string& modWorldEvent :
         clientModContent.worldEventFiles) {
        REQUIRE_MESSAGE(
            zl::game::MergeWorldEventFile(modWorldEvent, &clientWorldEvents,
                                          nullptr, &error, &clientItems),
            error);
    }
    zl::game::CompanionConfig clientCompanions;
    REQUIRE_MESSAGE(zl::game::LoadCompanionConfig(
                        clientConfig, &clientCompanions, &error, &clientItems),
                    error);
    for (const std::string& modCompanion :
         clientModContent.companionFiles) {
        REQUIRE_MESSAGE(
            zl::game::MergeCompanionFile(modCompanion, &clientCompanions,
                                         nullptr, &error, &clientItems),
            error);
    }
    zl::game::VehicleModsConfig clientVehicleMods;
    REQUIRE_MESSAGE(
        zl::game::LoadVehicleModConfig(clientConfig, &clientVehicleMods,
                                       &error, &clientItems),
        error);
    for (const std::string& modVehicleMod :
         clientModContent.vehicleModFiles) {
        REQUIRE_MESSAGE(
            zl::game::MergeVehicleModFile(modVehicleMod, &clientVehicleMods,
                                          nullptr, &error, &clientItems),
            error);
    }
    zl::game::TrapDefsConfig clientTraps;
    REQUIRE_MESSAGE(
        zl::game::LoadTrapDefsConfig(clientConfig, clientBuildables,
                                     &clientTraps, &error),
        error);
    for (const std::string& modTrap : clientModContent.trapFiles) {
        REQUIRE_MESSAGE(
            zl::game::MergeTrapFile(modTrap, &clientTraps, nullptr,
                                    clientBuildables, &error),
            error);
    }
    const std::uint64_t clientModsHash = zl::game::ComputeTreeHash(
        clientConfig.Get<std::string>("script.modsDir", "mods"));
    CHECK(zl::game::ComputeContentHash(clientItems, clientRecipes,
                                       clientBuildables, clientLoot, clientPois,
                                       clientMissions, clientModsHash,
                                       &clientForage, &clientAnimals,
                                       &clientCrops, &clientSettlements,
                                       &clientWorldEvents, &clientCompanions,
                                       &clientVehicleMods, &clientTraps) ==
          server.ContentHash());

    zl::game::NetworkClient::Options clientOptions;
    clientOptions.host = "127.0.0.1";
    clientOptions.port = server.Port();
    clientOptions.contentHash = server.ContentHash();
    clientOptions.autoReconnect = false;
    clientOptions.playerName = "HostTester";
    zl::game::NetworkClient client(clientOptions);
    REQUIRE(client.Connect(&error));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        client.Poll();
        if (client.IsConnected() && client.HasPlayer() && client.HasSocialState() &&
            client.SocialState().players.size() == 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(client.IsConnected());
    CHECK(client.HasPlayer());
    REQUIRE(client.HasSocialState());
    REQUIRE(client.SocialState().players.size() == 1);
    CHECK(client.SocialState().players[0].name == "HostTester");

    client.Close();
    server.Stop();
    CHECK(!server.Running());
    std::filesystem::remove(profiles, ignored);
    std::filesystem::current_path(old);
}
