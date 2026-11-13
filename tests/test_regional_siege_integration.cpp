#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "characters/components/ProgressionComponents.h"
#include "ecs/components/CommonComponents.h"
#include "game/HomesteadComponents.h"
#include "game/SettlementNpc.h"
#include "game/StrategicDirector.h"
#include "game/StrategicState.h"
#include "game/WorldSimulation.h"
#include "server/ServerHost.h"
#include "world/ChunkManager.h"

namespace {

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& path)
        : m_old(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }
    ~ScopedWorkingDirectory() {
        std::error_code error;
        std::filesystem::current_path(m_old, error);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

private:
    std::filesystem::path m_old;
};

bool HasFarmNpc(zl::game::WorldSimulation& simulation,
                zl::game::SettlementNpcKind kind) {
    bool found = false;
    simulation.Ecs().Registry().view<zl::game::SettlementNpc>().each(
        [&](const zl::ecs::Entity, const zl::game::SettlementNpc& npc) {
            if (npc.settlementId == "south_farmland" && npc.kind == kind) {
                found = true;
            }
        });
    return found;
}

bool HasFarmGuard(zl::game::WorldSimulation& simulation) {
    return HasFarmNpc(simulation, zl::game::SettlementNpcKind::Guard);
}

int DestroyFarmRaiders(zl::game::WorldSimulation& simulation) {
    std::vector<zl::ecs::Entity> raiders;
    simulation.Ecs()
        .Registry()
        .view<zl::game::SettlementRaidZombie>()
        .each([&](const zl::ecs::Entity entity,
                  const zl::game::SettlementRaidZombie& raid) {
            if (raid.settlementId == "south_farmland") {
                raiders.push_back(entity);
            }
        });
    for (const zl::ecs::Entity raider : raiders) {
        simulation.Ecs().Destroy(raider);
    }
    return static_cast<int>(raiders.size());
}

}  // namespace

TEST_CASE("M33-F1-T2 服务器为南部聚落生成独立商人守卫与求助者") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.mapDir = "assets/maps/village_demo_m31";
    options.contentFile = "assets/maps/village_demo_m31_content.json";
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = true;
    options.playerTargetX = 110.0f;
    options.playerTargetY = 216.0f;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    for (int i = 0; i < 2400; ++i) {
        host.RunForTicks(1);
        if (static_cast<bool>(host.Simulation().Player() != entt::null) &&
            HasFarmNpc(host.Simulation(),
                       zl::game::SettlementNpcKind::Merchant) &&
            HasFarmGuard(host.Simulation()) &&
            HasFarmNpc(host.Simulation(),
                       zl::game::SettlementNpcKind::Requester)) {
            break;
        }
    }
    CHECK(static_cast<bool>(host.Simulation().Player() != entt::null));
    CHECK(HasFarmNpc(host.Simulation(),
                     zl::game::SettlementNpcKind::Merchant));
    CHECK(HasFarmGuard(host.Simulation()));
    CHECK(HasFarmNpc(host.Simulation(),
                     zl::game::SettlementNpcKind::Requester));

    bool stockOk = false;
    host.Simulation().Ecs().Registry().view<zl::game::SettlementNpc>().each(
        [&](const zl::ecs::Entity entity,
            const zl::game::SettlementNpc& npc) {
            if (npc.settlementId != "south_farmland" ||
                npc.kind != zl::game::SettlementNpcKind::Merchant) {
                return;
            }
            const auto* stock = host.Simulation()
                                    .Ecs()
                                    .Registry()
                                    .try_get<zl::ecs::MerchantStock>(entity);
            stockOk = stock != nullptr &&
                      stock->stock == std::array<int, 5>{4, 2, 1, 2, 1};
        });
    CHECK(stockOk);

    host.Shutdown();
}

TEST_CASE("M32-T4 真实农场围攻完整打完并回写战略") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.mapDir = "assets/maps/village_demo_m31";
    options.contentFile = "assets/maps/village_demo_m31_content.json";
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = true;
    options.playerTargetX = 110.0f;
    options.playerTargetY = 216.0f;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);

    for (int i = 0; i < 2400; ++i) {
        host.RunForTicks(1);
        if (static_cast<bool>(host.Simulation().Player() != entt::null) &&
            host.Simulation().Chunks().GetStats().loadedChunks > 0 &&
            HasFarmGuard(host.Simulation())) {
            break;
        }
    }
    REQUIRE(static_cast<bool>(host.Simulation().Player() != entt::null));
    REQUIRE(HasFarmGuard(host.Simulation()));

    zl::game::StrategicState* strategic =
        host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    zl::game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = zl::game::StrategicOwner::Human;
    farm->siegePhase = zl::game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    CHECK(host.Simulation().RegionalSiegeStatus().regionId ==
          "south_farmland");

    for (int i = 0; i < 300; ++i) {
        DestroyFarmRaiders(host.Simulation());
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }

    const zl::game::RegionalSiegeResult& result =
        host.Simulation().RegionalSiegeLastResult();
    CHECK(result.held);
    CHECK(result.settlementId == "south_farmland");
    CHECK(result.kills >= 0);
    CHECK(result.wavesSpawned == result.waveCount);
    CHECK(farm->siegePhase == zl::game::StrategicSiegePhase::None);
    CHECK(farm->owner == zl::game::StrategicOwner::Human);
    CHECK(strategic->FindEvent("siege.south_farmland.0") != nullptr);
    CHECK(host.Simulation().SettlementNpcs()->LiveRaiders(
              host.Simulation().Ecs(), "south_farmland") == 0);

    host.Shutdown();
}

TEST_CASE("M32-T14 守城成功按在场玩家发放经验") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.mapDir = "assets/maps/village_demo_m31";
    options.contentFile = "assets/maps/village_demo_m31_content.json";
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = true;
    options.playerTargetX = 110.0f;
    options.playerTargetY = 216.0f;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    for (int i = 0; i < 2400; ++i) {
        host.RunForTicks(1);
        if (static_cast<bool>(host.Simulation().Player() != entt::null) &&
            HasFarmGuard(host.Simulation())) {
            break;
        }
    }
    REQUIRE(static_cast<bool>(host.Simulation().Player() != entt::null));
    REQUIRE(HasFarmGuard(host.Simulation()));
    zl::game::StrategicState* strategic = host.Simulation().Strategic();
    REQUIRE(strategic != nullptr);
    zl::game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = zl::game::StrategicOwner::Human;
    farm->siegePhase = zl::game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;
    farm->pressure = 90.0f;

    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);

    const zl::ecs::Entity player = host.Simulation().Player();
    auto* progression =
        host.Simulation().Ecs().Registry().try_get<zl::ecs::Progression>(
            player);
    REQUIRE(progression != nullptr);
    const float before = progression->xp;

    for (int i = 0; i < 300; ++i) {
        DestroyFarmRaiders(host.Simulation());
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    REQUIRE(host.Simulation().RegionalSiegeLastResult().held);
    CHECK(farm->owner == zl::game::StrategicOwner::Human);

    host.RunForTicks(1);
    CHECK(progression->xp - before == doctest::Approx(80.0f));
    const float stable = progression->xp;
    host.RunForTicks(1);
    CHECK(progression->xp == doctest::Approx(stable));

    host.Shutdown();
}

TEST_CASE("M32-T5 真实失守到反攻恢复并重建守卫") {
    const std::filesystem::path root{ZL_SOURCE_DIR};
    ScopedWorkingDirectory cwd(root);

    zl::server::ServerHost::Options options;
    options.configPath = (root / "config" / "game.json").string();
    options.mapDir = "assets/maps/village_demo_m31";
    options.contentFile = "assets/maps/village_demo_m31_content.json";
    options.spawnDemoPopulation = false;
    options.spawnLocalPlayer = true;
    options.playerTargetX = 110.0f;
    options.playerTargetY = 216.0f;

    zl::server::ServerHost host;
    std::string error;
    REQUIRE_MESSAGE(host.Init(options, &error), error);
    for (int i = 0; i < 2400; ++i) {
        host.RunForTicks(1);
        if (static_cast<bool>(host.Simulation().Player() != entt::null) &&
            HasFarmGuard(host.Simulation())) {
            break;
        }
    }
    REQUIRE(static_cast<bool>(host.Simulation().Player() != entt::null));
    REQUIRE(HasFarmGuard(host.Simulation()));
    zl::game::StrategicState* strategic = host.Simulation().Strategic();
    zl::game::StrategicRegionState* farm =
        strategic->FindRegion("south_farmland");
    REQUIRE(farm != nullptr);
    farm->owner = zl::game::StrategicOwner::Human;
    farm->siegePhase = zl::game::StrategicSiegePhase::Warning;
    farm->nextAttackMinute = 0;
    farm->lastEventMinute = 0;

    zl::game::StrategicDetailedResolution loss;
    loss.regionId = "south_farmland";
    loss.attackMinute = 0;
    loss.resolvedMinute = 0;
    loss.held = false;
    REQUIRE(zl::game::ResolveStrategicSiege(
        *strategic, *host.Simulation().StrategicSettings(), loss, &error));
    CHECK(farm->owner == zl::game::StrategicOwner::Infested);
    CHECK(farm->recoveryStage ==
          zl::game::StrategicRecoveryStage::ClearCore);
    host.Simulation().SettlementNpcs()->MarkFallen(
        host.Simulation().Ecs(), "south_farmland");
    CHECK(host.Simulation().SettlementNpcs()->IsFallen("south_farmland"));

    farm->recoveryReadyMinute = 0;
    REQUIRE(host.Simulation().ClearRegionalRecoveryCore("south_farmland",
                                                        &error));
    REQUIRE(host.Simulation().RepairRegionalRecoveryFacility("south_farmland",
                                                             &error));
    farm->recoveryReadyMinute = 0;
    REQUIRE(host.Simulation().StartRegionalReclaim("south_farmland"));
    REQUIRE(host.Simulation().RegionalSiegeStatus().active);
    CHECK(host.Simulation().RegionalSiegeStatus().reclaim);

    for (int i = 0; i < 400; ++i) {
        DestroyFarmRaiders(host.Simulation());
        host.Simulation().UpdateRegionalSiege(1.0f, false,
                                              "south_farmland");
        if (!host.Simulation().RegionalSiegeStatus().active) {
            break;
        }
    }
    REQUIRE_FALSE(host.Simulation().RegionalSiegeStatus().active);
    CHECK(host.Simulation().RegionalSiegeLastResult().held);
    CHECK(host.Simulation().RegionalSiegeLastResult().reclaim);
    CHECK(farm->owner == zl::game::StrategicOwner::Contested);
    CHECK(farm->recoveryStage ==
          zl::game::StrategicRecoveryStage::Restoring);
    CHECK(host.Simulation().SettlementNpcs()->IsFallen("south_farmland"));

    farm->recoveryReadyMinute = 0;
    host.AdvanceGameMinutes(30.0);
    host.RunForTicks(1);
    for (int i = 0; i < 20 && !HasFarmGuard(host.Simulation()); ++i) {
        host.RunForTicks(1);
    }
    CHECK(farm->owner == zl::game::StrategicOwner::Human);
    CHECK(farm->recoveryStage == zl::game::StrategicRecoveryStage::None);
    CHECK_FALSE(host.Simulation().SettlementNpcs()->IsFallen(
        "south_farmland"));
    CHECK(HasFarmGuard(host.Simulation()));
    CHECK(strategic->FindEvent("recovery.south_farmland." +
                               std::to_string(farm->lastEventMinute)) !=
          nullptr);
    host.Shutdown();
}
