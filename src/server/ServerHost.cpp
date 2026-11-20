#include "server/ServerHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "ai/PathfindingService.h"
#include "ai/components/AiComponents.h"
#include "audio/SoundIds.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/InputSystem.h"
#include "core/Config.h"
#include "core/EventBus.h"
#include "core/GameEvents.h"
#include "core/GameTime.h"
#include "core/Log.h"
#include "core/SoundEvent.h"
#include "core/TaskScheduler.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/AnimalHarvest.h"
#include "game/AnimalSystem.h"
#include "game/Animals.h"
#include "game/EntityReplication.h"
#include "game/FireSystem.h"
#include "game/FishingRuntime.h"
#include "game/ForageRuntime.h"
#include "game/GroundItems.h"
#include "game/BarricadeDefense.h"
#include "game/BuildingRules.h"
#include "game/ContentHash.h"
#include "game/Crops.h"
#include "game/LootContainers.h"
#include "game/Dialogue.h"
#include "game/Missions.h"
#include "game/Poi.h"
#include "game/PoiComponents.h"
#include "game/PoiStageCombat.h"
#include "game/ZombieVariants.h"
#include "game/RandomEvents.h"
#include "game/RegionEventDirector.h"
#include "game/Homestead.h"
#include "game/ModContent.h"
#include "game/Objectives.h"
#include "game/PlayerInventoryService.h"
#include "game/PlayerCombat.h"
#include "game/StealthConfig.h"
#include "game/SettlementNpc.h"
#include "game/SettlementReputation.h"
#include "game/StrategicDirector.h"
#include "game/StrategicMap.h"
#include "game/PlayerStorage.h"
#include "game/StructureComponents.h"
#include "game/TrapSystem.h"
#include "game/Traps.h"
#include "game/VehicleComponents.h"
#include "game/WorldEventDirector.h"
#include "game/WorldEventRuntime.h"
#include "game/WorldQueries.h"
#include "game/CompanionSystem.h"
#include "game/WorldSimulation.h"
#include "world/WorldGraph.h"
#include "world/WorldSelection.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "net/Command.h"
#include "net/CommandCodec.h"
#include "net/KcpSession.h"
#include "net/PlayerAssignment.h"
#include "net/PlayerIdentity.h"
#include "net/PlayerState.h"
#include "net/Replication.h"
#include "net/StrategicProtocol.h"
#include "net/WorldEventProtocol.h"
#include "net/WorldSound.h"
#include "save/ChunkDiff.h"
#include "save/ComponentRegistry.h"
#include "save/EntityStore.h"
#include "save/RegionEntityBuckets.h"
#include "save/SaveManager.h"
#include "save/SaveService.h"
#include "save/WorldManifest.h"
#include "physics/PhysicsWorld.h"
#include "server/PlayerProfile.h"
#include "ui/InventoryUi.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/GridSquare.h"
#include "world/RegionStreaming.h"
#include "world/WorldDefinition.h"
#include "world/WorldGraph.h"

namespace zl::server {
namespace {

std::uint32_t ReadU32Le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::string ForageNodeKey(const game::ForageNode& node) {
    return node.zoneId + ":" + std::to_string(node.x) + ":" +
           std::to_string(node.y) + ":" + std::to_string(node.z);
}

bool ChunkWithinInterest(const world::Chunk& chunk, float x, float y,
                         float radius) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        radius < 0.0f) {
        return false;
    }
    const float minX =
        static_cast<float>(chunk.ChunkX() * core::kChunkSize);
    const float minY =
        static_cast<float>(chunk.ChunkY() * core::kChunkSize);
    const float maxX = minX + static_cast<float>(core::kChunkSize);
    const float maxY = minY + static_cast<float>(core::kChunkSize);
    const float dx = x < minX ? minX - x : (x > maxX ? x - maxX : 0.0f);
    const float dy = y < minY ? minY - y : (y > maxY ? y - maxY : 0.0f);
    return dx * dx + dy * dy <= radius * radius;
}

constexpr std::uint64_t kServerForageStateStableId = 0x4D3233464F524147ull;

void MergeSettlementReputationSummary(
    ecs::EcsWorld& world, ecs::Entity player,
    const std::vector<net::PlayerSettlementReputation>& summary) {
    if (player == entt::null || !world.Alive(player) || summary.empty()) {
        return;
    }
    auto* reputation = world.Registry().try_get<game::SettlementReputation>(player);
    if (reputation == nullptr) {
        reputation =
            &world.Registry().emplace<game::SettlementReputation>(player);
    }
    for (const net::PlayerSettlementReputation& entry : summary) {
        if (entry.id.empty() ||
            reputation->settlements.size() >=
                game::kMaxSettlementReputationEntries) {
            break;
        }
        const auto found = std::find_if(
            reputation->settlements.begin(), reputation->settlements.end(),
            [&](const game::SettlementReputationEntry& existing) {
                return existing.settlementId == entry.id;
            });
        if (found == reputation->settlements.end()) {
            reputation->settlements.push_back(
                game::SettlementReputationEntry{entry.id, entry.score});
        }
    }
    std::sort(reputation->settlements.begin(), reputation->settlements.end(),
              [](const game::SettlementReputationEntry& left,
                 const game::SettlementReputationEntry& right) {
                  return left.settlementId < right.settlementId;
              });
}

game::VehicleVisualDirection VehicleVisualDirectionFromSprite(
    std::uint32_t spriteHash) {
    for (int i = 0; i < 8; ++i) {
        const auto direction =
            static_cast<game::VehicleVisualDirection>(i);
        if (spriteHash ==
            ecs::HashName(game::VehicleVisualSprite(direction))) {
            return direction;
        }
    }
    return game::VehicleVisualDirection::South;
}

bool DecodeBuildCell(float x, float y, int* wx, int* wy) {
    if (wx == nullptr || wy == nullptr || !std::isfinite(x) ||
        !std::isfinite(y)) {
        return false;
    }
    const long rx = std::lround(x);
    const long ry = std::lround(y);
    if (std::fabs(x - static_cast<float>(rx)) > 0.01f ||
        std::fabs(y - static_cast<float>(ry)) > 0.01f ||
        rx < INT_MIN || rx > INT_MAX || ry < INT_MIN || ry > INT_MAX) {
        return false;
    }
    *wx = static_cast<int>(rx);
    *wy = static_cast<int>(ry);
    return true;
}

const game::BuildableDef* FindBuildableForSprite(
    const std::vector<game::BuildableDef>& defs, std::string_view sprite) {
    for (const game::BuildableDef& def : defs) {
        if (game::BuildableMatchesSprite(def, sprite)) {
            return &def;
        }
    }
    return nullptr;
}

game::BaseSiegeConfig LoadServerBaseSiegeConfig(const core::Config& config) {
    game::BaseSiegeConfig siege;
    siege.enabled = config.Get<bool>("baseEvent.enabled", true);
    siege.minStructures = config.Get<int>("baseEvent.minStructures", 1);
    siege.startHour = config.Get<int>("baseEvent.startHour", 22);
    siege.endHour = config.Get<int>("baseEvent.endHour", 5);
    siege.warningSeconds = static_cast<float>(
        config.Get<double>("baseEvent.warningSeconds", 30.0));
    siege.waveIntervalSeconds = static_cast<float>(
        config.Get<double>("baseEvent.waveIntervalSeconds", 45.0));
    siege.waves = config.Get<std::vector<int>>("baseEvent.waves", {4, 6, 8});
    siege.maxAlive = config.Get<int>("baseEvent.maxAlive", 12);
    siege.maxWaveSize = config.Get<int>("baseEvent.maxWaveSize", 24);
    siege.scalePerDay = static_cast<float>(
        config.Get<double>("baseEvent.scalePerDay", 0.0));
    siege.scalePerStructure = static_cast<float>(
        config.Get<double>("baseEvent.scalePerStructure", 0.0));
    siege.specialStartWave = config.Get<int>("baseEvent.specialStartWave", 1);
    siege.specialCount = config.Get<int>("baseEvent.specialCount", 0);
    siege.triggerRadius = static_cast<float>(
        config.Get<double>("baseEvent.triggerRadius", 12.0));
    siege.spawnRadiusMin = static_cast<float>(
        config.Get<double>("baseEvent.spawnRadiusMin", 10.0));
    siege.spawnRadiusMax = static_cast<float>(
        config.Get<double>("baseEvent.spawnRadiusMax", 14.0));
    siege.leaveRadius = static_cast<float>(
        config.Get<double>("baseEvent.leaveRadius", 25.0));
    siege.directorEnabled =
        config.Get<bool>("baseEvent.director.enabled", true);
    siege.directorMaxAxes =
        config.Get<int>("baseEvent.director.maxAxes", 2);
    siege.intelFullDelayRatio = static_cast<float>(
        config.Get<double>("baseEvent.director.intelFullDelayRatio", 0.5));
    siege.weakestWeight = static_cast<float>(
        config.Get<double>("baseEvent.director.weakestWeight", 0.7));
    siege.rewardEnabled =
        config.Get<bool>("baseEvent.director.rewardEnabled", true);
    return siege;
}

ecs::SkillKind ServerMissionSkillKind(std::string_view id) {
    if (id == "melee") {
        return ecs::SkillKind::Melee;
    }
    if (id == "building") {
        return ecs::SkillKind::Building;
    }
    if (id == "crafting") {
        return ecs::SkillKind::Crafting;
    }
    return ecs::SkillKind::Survival;
}

int ServerCountMissionItem(ecs::EcsWorld& world, ecs::Entity player,
                           std::string_view defId) {
    int count = 0;
    entt::registry& registry = world.Registry();
    if (const auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        const auto find = [&](const auto& item) {
            return item.defId == defId;
        };
        for (const ecs::ItemInstance& item : container->items) {
            if (find(item)) {
                count += std::max(0, item.count);
            }
        }
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        for (const auto& slot : equipment->slots) {
            if (slot.has_value() && slot->defId == defId) {
                count += std::max(0, slot->count);
            }
        }
    }
    return count;
}

#if defined(_WIN32)
std::atomic<ServerHost*> g_activeHost{nullptr};

BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (ServerHost* host = g_activeHost.load()) {
            host->RequestStop();
        }
        return TRUE;
    }
    return FALSE;
}
#endif

}  // namespace

ServerHost::ServerHost() = default;

ServerHost::~ServerHost() {
    Shutdown();
}

int ServerHost::Run(int argc, char** argv) {
    core::InitLogging();
    ServerHost host;
    const std::string configPath = core::Config::FindConfigPath(argc, argv, "config/game.json");

#if defined(_WIN32)
    g_activeHost.store(&host);
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#endif

    Options options;
    options.configPath = configPath;
    options.argc = argc;
    options.argv = argv;
    options.network = true;  // 独立服务器默认监听 UDP；测试可显式关闭。
    options.spawnAnimals = true;  // M23-F2b-T6：正式服务器生成权威动物。
    options.enablePlayerProfiles = true;  // M13-T8：正式服务器默认持久化玩家档案。
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--server.network=false") {
            options.network = false;
        }
    }
    std::string error;
    int result = 1;
    if (!host.Init(options, &error)) {
        ZL_CRITICAL("server", "服务器启动失败：{}", error);
    } else {
        const double tickRate = host.Config().Get<double>("server.tickRate",
                                                          static_cast<double>(
                                                              host.m_options.tickRate));
        const double dt = 1.0 / std::max(1.0, tickRate);
        auto next = std::chrono::steady_clock::now();
        ZL_INFO("server", "无窗口服务器启动：{} Hz，maxTicks={}", tickRate,
                host.m_options.maxTicks);
        while (host.Running() &&
               (host.m_options.maxTicks <= 0 ||
                host.TickCount() <
                    static_cast<uint64_t>(host.m_options.maxTicks))) {
            host.Tick(static_cast<float>(dt));
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(dt));
            const auto now = std::chrono::steady_clock::now();
            if (now - next > std::chrono::milliseconds(500)) {
                next = now;  // 落后太多时重建节拍，避免补帧风暴
            }
            std::this_thread::sleep_until(next);
        }
        host.Shutdown();
        result = 0;
    }

#if defined(_WIN32)
    g_activeHost.store(nullptr);
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
#endif
    ZL_INFO("server", "服务器退出（ticks={}）", host.TickCount());
    core::ShutdownLogging();
    return result;
}

bool ServerHost::Init(const Options& options, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        ZL_ERROR("server", "{}", message);
        Shutdown();
        return false;
    };
    if (m_initialized) {
        return true;
    }
    m_options = options;
    m_networkStats = {};
    m_socialSequence = 0;
    m_lastSocialTick = 0;
    m_socialDirty = true;
    m_chatLog.clear();
    m_ticks = 0;
    m_logTimer = 0.0;
    m_activeSaveId.clear();
    m_activeSaveUseBackup = false;
    m_saveWarning.clear();
    m_demoLootSpawned = false;
    m_demoLootContainersSpawned = false;
    m_houseLootContainersSpawned = false;
    m_worldLoadedFromSave = false;
    m_poiRuntime.clear();
    m_serverPoiBossResetTimers.clear();
    m_serverEventReady.clear();
    m_serverColdSnapEvents.clear();
    m_serverEventDay = 0;
    m_poiScanTimer = 0.0f;
    m_serverVehicles.clear();
    m_vehicleSpawnCount = 3;
    m_vehicleTuning = game::kVehicleDefaultTuning;
    m_vehicleRepairItem = "repair_kit";
    m_vehicleFuelItem = "fuel_can";

    m_config = std::make_unique<core::Config>();
    if (!m_config->LoadFile(m_options.configPath)) {
        return fail("配置文件加载失败：" + m_options.configPath);
    }
    if (m_options.argc > 0 && m_options.argv != nullptr) {
        m_config->ApplyCommandLine(m_options.argc, m_options.argv);
    }
    core::SetLogLevel(m_config->Get<std::string>("log.level", "debug"));
    if (!m_options.saveRoot.empty()) {
        m_config->Set("save.root", m_options.saveRoot);
    }
    world::WorldSelection worldSelection =
        world::ResolveWorldSelection(*m_config, true);
    if (!m_options.mapDir.empty()) {
        worldSelection.mapDir = m_options.mapDir;
        worldSelection.contentFile = m_options.contentFile;
        const std::string m31Map = m_config->Get<std::string>(
            "world.m31.mapDir", "assets/maps/village_demo_m31");
        if (worldSelection.contentFile.empty() &&
            world::NormalizeWorldPath(m_options.mapDir) ==
                world::NormalizeWorldPath(m31Map)) {
            worldSelection.contentFile = m_config->Get<std::string>(
                "world.m31.contentFile",
                "assets/maps/village_demo_m31_content.json");
        }
        worldSelection.m31 = !worldSelection.contentFile.empty();
    }
    m_selectedMapDir = worldSelection.mapDir;
    m_selectedContentFile = worldSelection.contentFile;
    ZL_INFO("world", "服务器地图选择：{}（{}；内容 {}）", m_selectedMapDir,
            worldSelection.m31 ? "M31 垂直切片" : "旧地图",
            m_selectedContentFile.empty() ? "config/game.json"
                                          : m_selectedContentFile);
    if (!m_options.network) {
        m_options.network = m_config->Get<bool>("server.network", false);
    }
    if (m_options.network) {
        // 网络服务器不生成“本地玩家”；每个会话拥有自己的权威玩家实体。
        m_options.spawnLocalPlayer = false;
    }
    m_attackDamage =
        static_cast<float>(m_config->Get<double>("combat.playerDamage", 40.0));
    m_attackRange = static_cast<float>(m_config->Get<double>("combat.playerRange", 1.7));
    m_attackArc =
        static_cast<float>(m_config->Get<double>("combat.playerArcDegrees", 100.0));
    m_attackCooldownSeconds = static_cast<float>(
        m_config->Get<double>("combat.playerCooldownSeconds", 0.5));
    m_attackKnockback =
        static_cast<float>(m_config->Get<double>("combat.playerKnockback", 3.0));
    m_attackStagger =
        static_cast<float>(m_config->Get<double>("combat.playerStaggerSeconds", 0.35));
    m_attackWear =
        static_cast<float>(m_config->Get<double>("combat.playerWearPerHit", 0.0));
    m_stealthRules = game::LoadStealthRuleConfig(*m_config);
    m_throwingConfig = game::LoadThrowingConfig(*m_config);
    m_pvpEnabled = m_config->Get<bool>("combat.pvp", false);
    m_gunshotRadius = static_cast<float>(
        m_config->Get<double>("sounds.gunshotRadius", 30.0));
    m_projectileDamage = static_cast<float>(
        std::max(0.1, m_config->Get<double>("net.projectileDamage", 24.0)));
    m_projectileSpeed = static_cast<float>(
        std::max(1.0, m_config->Get<double>("net.projectileSpeed", 18.0)));
    m_projectileLifetime = static_cast<float>(
        std::max(0.1, m_config->Get<double>("net.projectileLifetimeSeconds", 2.5)));
    m_maxProjectileDistance = static_cast<float>(
        std::max(1.0, m_config->Get<double>("net.maxProjectileDistance", 24.0)));
    m_stateSendIntervalTicks = static_cast<std::uint32_t>(
        std::clamp(m_config->Get<int>("net.playerStateIntervalTicks", 6), 1, 60));
    m_socialStateIntervalTicks = static_cast<std::uint32_t>(
        std::clamp(m_config->Get<int>("net.socialStateIntervalTicks", 30), 1, 600));
    m_structureSyncIntervalTicks = static_cast<std::uint32_t>(
        std::clamp(m_config->Get<int>("net.structureSyncIntervalTicks", 180), 30,
                   3600));
    m_structureInterestRadius = static_cast<float>(std::clamp(
        m_config->Get<double>("net.structureInterestRadius", 48.0), 8.0,
        256.0));
    m_forageInteractRange = static_cast<float>(
        std::clamp(m_config->Get<double>("forage.interactRange", 2.2), 0.5, 8.0));
    m_fishingInteractRange = static_cast<float>(
        std::clamp(m_config->Get<double>("fishing.interactRange", 2.2), 0.5, 8.0));
    m_foragePlanRadius = static_cast<float>(
        std::clamp(m_config->Get<double>("forage.planRadius", 10.0), 4.0, 32.0));
    m_fishingPlanRadius = static_cast<float>(
        std::clamp(m_config->Get<double>("fishing.planRadius", 12.0), 4.0, 32.0));
    // M25-T3：服务器与单机共用车辆调参；载具移动/维修/加油规则不复制。
    m_vehicleTuning.maxSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.maxSpeed", game::kVehicleMaxSpeed), 0.5,
        40.0));
    m_vehicleTuning.acceleration = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.acceleration", game::kVehicleAcceleration),
        0.1, 80.0));
    m_vehicleTuning.braking = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.braking", game::kVehicleBraking), 0.1,
        120.0));
    m_vehicleTuning.radius = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.radius", game::kVehicleRadius), 0.1, 2.0));
    m_vehicleTuning.enterRange = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.enterRange", game::kVehicleEnterRange),
        0.5, 8.0));
    m_vehicleTuning.fuelPerSecond = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.fuelPerSecond", game::kVehicleFuelPerSecond),
        0.0, 1.0));
    m_vehicleTuning.spawnOffsetX = std::clamp(
        m_config->Get<int>("vehicle.spawnOffsetX", game::kVehicleSpawnOffsetX),
        -16, 16);
    m_vehicleTuning.spawnOffsetY = std::clamp(
        m_config->Get<int>("vehicle.spawnOffsetY", game::kVehicleSpawnOffsetY),
        -16, 16);
    m_vehicleTuning.spawnSearchRadius = std::clamp(
        m_config->Get<int>("vehicle.spawnSearchRadius",
                           game::kVehicleSpawnSearchRadius),
        0, 8);
    m_vehicleTuning.crashMinSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.crashMinSpeed",
                              game::kVehicleCrashMinSpeed),
        0.0, 40.0));
    m_vehicleTuning.crashDamagePerSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.crashDamagePerSpeed",
                              game::kVehicleCrashDamagePerSpeed),
        0.0, 2.0));
    m_vehicleTuning.maxCrashDamage = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.maxCrashDamage",
                              game::kVehicleMaxCrashDamage),
        0.0, 1.0));
    m_vehicleTuning.ramMinSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramMinSpeed", game::kVehicleRamMinSpeed),
        0.0, 40.0));
    m_vehicleTuning.ramBaseDamage = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramBaseDamage",
                              game::kVehicleRamBaseDamage),
        0.0, 500.0));
    m_vehicleTuning.ramDamagePerSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramDamagePerSpeed",
                              game::kVehicleRamDamagePerSpeed),
        0.0, 200.0));
    m_vehicleTuning.ramKnockbackPerSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramKnockbackPerSpeed",
                              game::kVehicleRamKnockbackPerSpeed),
        0.0, 50.0));
    m_vehicleTuning.ramKnockbackBase = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramKnockbackBase",
                              game::kVehicleRamKnockbackBase),
        0.0, 50.0));
    m_vehicleTuning.ramStaggerBase = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramStaggerBase",
                              game::kVehicleRamStaggerBase),
        0.0, 5.0));
    m_vehicleTuning.ramStaggerPerSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramStaggerPerSpeed",
                              game::kVehicleRamStaggerPerSpeed),
        0.0, 1.0));
    m_vehicleTuning.ramCooldownSeconds = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramCooldownSeconds",
                              game::kVehicleRamCooldownSeconds),
        0.05, 10.0));
    m_vehicleTuning.ramConditionDamageBase = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramConditionDamageBase",
                              game::kVehicleRamConditionDamageBase),
        0.0, 1.0));
    m_vehicleTuning.ramConditionDamagePerSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramConditionDamagePerSpeed",
                              game::kVehicleRamConditionDamagePerSpeed),
        0.0, 1.0));
    m_vehicleTuning.ramConditionDamageMax = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.ramConditionDamageMax",
                              game::kVehicleRamConditionDamageMax),
        0.0, 1.0));
    m_vehicleTuning.vehicleCollisionMinSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.vehicleCollisionMinSpeed",
                              game::kVehicleCollisionMinSpeed),
        0.0, static_cast<double>(m_vehicleTuning.maxSpeed)));
    m_vehicleTuning.vehicleCollisionConditionDamageBase =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionConditionDamageBase",
                game::kVehicleCollisionConditionDamageBase),
            0.0, 1.0));
    m_vehicleTuning.vehicleCollisionConditionDamagePerSpeed =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionConditionDamagePerSpeed",
                game::kVehicleCollisionConditionDamagePerSpeed),
            0.0, 1.0));
    m_vehicleTuning.vehicleCollisionConditionDamageMax =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionConditionDamageMax",
                game::kVehicleCollisionConditionDamageMax),
            0.0, 1.0));
    m_vehicleTuning.vehicleCollisionCooldownSeconds =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.vehicleCollisionCooldownSeconds",
                                  game::kVehicleCollisionCooldownSeconds),
            0.05, 10.0));
    m_vehicleTuning.vehicleCollisionPushPerSpeed =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.vehicleCollisionPushPerSpeed",
                                  game::kVehicleCollisionPushPerSpeed),
            0.0, 2.0));
    m_vehicleTuning.vehicleCollisionPushMax =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.vehicleCollisionPushMax",
                                  game::kVehicleCollisionPushMax),
            0.0, 4.0));
    m_vehicleTuning.vehicleCollisionRestitution =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionRestitution",
                game::kVehicleCollisionRestitution),
            0.0, 1.0));
    m_vehicleTuning.vehicleCoastBraking =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.vehicleCoastBraking",
                                  game::kVehicleCoastBraking),
            0.5, 20.0));
    m_vehicleTuning.vehicleCollisionFriction =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.vehicleCollisionFriction",
                                  game::kVehicleCollisionFriction),
            0.0, 1.0));
    m_vehicleTuning.vehicleAngularDamping =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.vehicleAngularDamping",
                                  game::kVehicleAngularDamping),
            0.0, 20.0));
    m_vehicleTuning.vehicleCollisionOccupantDamageBase =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionOccupantDamageBase",
                game::kVehicleCollisionOccupantDamageBase),
            0.0, 50.0));
    m_vehicleTuning.vehicleCollisionOccupantDamagePerSpeed =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionOccupantDamagePerSpeed",
                game::kVehicleCollisionOccupantDamagePerSpeed),
            0.0, 20.0));
    m_vehicleTuning.vehicleCollisionOccupantDamageMax =
        static_cast<float>(std::clamp(
            m_config->Get<double>(
                "vehicle.vehicleCollisionOccupantDamageMax",
                game::kVehicleCollisionOccupantDamageMax),
            0.0, 100.0));
    m_vehicleTuning.driverDamageReduction = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.driverDamageReduction",
                              game::kVehicleDriverDamageReduction),
        0.0, 0.95));
    m_vehicleTuning.seatHandoffMaxSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.seatHandoffMaxSpeed",
                              game::kVehicleSeatHandoffMaxSpeed),
        0.0, 2.0));
    m_vehicleTuning.zombieAttackConditionDamage =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.zombieAttackConditionDamage",
                                  game::kVehicleZombieAttackConditionDamage),
            0.0, 1.0));
    m_vehicleTuning.zombieAttackCooldownSeconds =
        static_cast<float>(std::clamp(
            m_config->Get<double>("vehicle.zombieAttackCooldownSeconds",
                                  game::kVehicleZombieAttackCooldownSeconds),
            0.05, 10.0));
    m_vehicleTuning.noiseRadius = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.noiseRadius", game::kVehicleNoiseRadius),
        0.0, 64.0));
    m_vehicleTuning.noiseIntervalSeconds = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.noiseIntervalSeconds",
                              game::kVehicleNoiseIntervalSeconds),
        0.05, 10.0));
    m_vehicleTuning.noiseMinSpeed = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.noiseMinSpeed",
                              game::kVehicleNoiseMinSpeed),
        0.0, 40.0));
    m_vehicleTuning.hornNoiseRadius = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.hornNoiseRadius",
                              game::kVehicleHornNoiseRadius),
        0.0, 64.0));
    m_vehicleTuning.hornCooldownSeconds = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.hornCooldownSeconds",
                              game::kVehicleHornCooldownSeconds),
        0.1, 10.0));
    m_vehicleTuning.dirtMaxSpeedScale = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.dirtMaxSpeedScale",
                              game::kVehicleDirtMaxSpeedScale),
        0.0, 1.0));
    m_vehicleTuning.dirtAccelerationScale = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.dirtAccelerationScale",
                              game::kVehicleDirtAccelerationScale),
        0.0, 1.0));
    m_vehicleTuning.dirtBrakingScale = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.dirtBrakingScale",
                              game::kVehicleDirtBrakingScale),
        0.0, 1.0));
    m_vehicleTuning.grassMaxSpeedScale = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.grassMaxSpeedScale",
                              game::kVehicleGrassMaxSpeedScale),
        0.0, 1.0));
    m_vehicleTuning.grassAccelerationScale = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.grassAccelerationScale",
                              game::kVehicleGrassAccelerationScale),
        0.0, 1.0));
    m_vehicleTuning.grassBrakingScale = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.grassBrakingScale",
                              game::kVehicleGrassBrakingScale),
        0.0, 1.0));
    m_vehicleTuning.repairAmount = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.repairAmount",
                              game::kVehicleRepairAmount),
        0.01, 1.0));
    m_vehicleTuning.fuelPerCan = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.fuelPerCan", game::kVehicleFuelPerCan),
        0.01, 1.0));
    m_vehicleTuning.trunkCapacityKg = static_cast<float>(std::clamp(
        m_config->Get<double>("vehicle.trunkCapacityKg", 40.0), 1.0, 1000.0));
    m_vehicleTuning.trunkStacks = std::clamp(
        m_config->Get<int>("vehicle.trunkStacks", 6), 1, 6);
    m_vehicleSpawnCount = static_cast<std::size_t>(std::clamp(
        m_config->Get<int>("vehicle.count", 3), 1, 16));
    m_vehicleRepairItem = m_config->Get<std::string>(
        "vehicle.repairItem", "repair_kit");
    if (m_vehicleRepairItem.empty()) {
        m_vehicleRepairItem = "repair_kit";
    }
    m_vehicleFuelItem =
        m_config->Get<std::string>("vehicle.fuelItem", "fuel_can");
    if (m_vehicleFuelItem.empty()) {
        m_vehicleFuelItem = "fuel_can";
    }

    // M13-T6：服务器侧命令策略；所有限制都在权威侧执行，客户端参数不可信。
    m_maxCommandsPerTick = static_cast<std::uint32_t>(
        std::max(1, m_config->Get<int>("net.maxCommandsPerTick", 8)));
    m_maxMoveToDistance = static_cast<float>(
        std::max(1.0, m_config->Get<double>("net.maxMoveToDistance", 64.0)));
    m_maxAttackDistance = static_cast<float>(
        std::max(0.5, m_config->Get<double>("net.maxAttackDistance", 3.0)));

    if (m_options.loadSaveId.empty()) {
        m_options.loadSaveId = m_config->Get<std::string>("server.saveId", "");
    }
    if (m_options.maxTicks <= 0) {
        m_options.maxTicks = m_config->Get<int>("server.ticks", 0);
    }
    if (!(m_options.tickRate > 0.0f)) {
        m_options.tickRate = static_cast<float>(
            m_config->Get<double>("server.tickRate", 60.0));
    }
    const double attackCooldownSeconds =
        std::max(0.0, m_config->Get<double>("net.attackCooldownSeconds", 0.35));
    m_attackCooldownTicks = static_cast<std::uint64_t>(std::llround(
        attackCooldownSeconds * static_cast<double>(m_options.tickRate)));
    const double projectileCooldownSeconds =
        std::max(0.0, m_config->Get<double>("net.projectileCooldownSeconds", 0.5));
    m_projectileCooldownTicks = static_cast<std::uint64_t>(std::llround(
        projectileCooldownSeconds * static_cast<double>(m_options.tickRate)));
    m_throwCooldownTicks = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::llround(
               m_throwingConfig.cooldownSeconds *
               static_cast<double>(m_options.tickRate))));
    const double chatCooldownSeconds =
        std::max(0.0, m_config->Get<double>("net.chatCooldownSeconds", 1.0));
    m_chatCooldownTicks = static_cast<std::uint64_t>(std::llround(
        chatCooldownSeconds * static_cast<double>(m_options.tickRate)));

    // M13-T8：复活延迟/档案自动保存间隔。Options 显式值优先于配置（测试用）。
    const double respawnDelaySeconds =
        m_options.respawnDelaySeconds > 0.0f
            ? static_cast<double>(m_options.respawnDelaySeconds)
            : std::max(0.5, m_config->Get<double>("net.respawnDelaySeconds", 5.0));
    m_respawnDelayTicks = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::llround(
               respawnDelaySeconds * static_cast<double>(m_options.tickRate))));
    if (m_options.profileSaveIntervalSeconds > 0.0f) {
        m_profileSaveIntervalSeconds =
            static_cast<double>(m_options.profileSaveIntervalSeconds);
    } else {
        m_profileSaveIntervalSeconds = std::max(
            0.0, m_config->Get<double>("net.playerProfileSaveSeconds", 30.0));
    }
    if (m_options.enablePlayerProfiles) {
        if (m_options.playerProfilesFile.empty()) {
            m_options.playerProfilesFile = m_config->Get<std::string>(
                "net.playerProfilesFile", "saves/multiplayer_players.bin");
        }
        m_profilesEnabled = true;
    }

    m_saveWorldOnExit = m_config->Get<bool>("save.autoSaveOnExit", true);
    m_progressionConfig = game::LoadProgressionConfig(*m_config);
    m_homesteadConfig = game::LoadHomesteadConfig(*m_config);
    m_serverSleepEnabled = m_config->Get<bool>("sleep.enabled", true);
    m_serverSleepConfig.minHours =
        std::clamp(m_config->Get<int>("sleep.minHours", 1), 1, 8);
    m_serverSleepConfig.maxHours = std::clamp(
        m_config->Get<int>("sleep.maxHours", 8), m_serverSleepConfig.minHours,
        8);
    m_serverSleepConfig.dawnHour =
        std::clamp(m_config->Get<int>("sleep.dawnHour", 7), 0, 23);
    m_serverSleepConfig.gameMinutesPerSecond = std::clamp(
        static_cast<float>(
            m_config->Get<double>("sleep.gameMinutesPerSecond", 120.0)),
        1.0f, 10000.0f);
    m_serverSleepConfig.fatigueRecoveryPerHour = std::clamp(
        static_cast<float>(
            m_config->Get<double>("sleep.fatigueRecoveryPerHour", 0.15)),
        0.0f, 1.0f);
    m_serverSleepConfig.zombieRadius = std::clamp(
        static_cast<float>(m_config->Get<double>("sleep.zombieRadius", 8.0)),
        0.0f, 64.0f);
    m_serverSleepConfig.interruptOnDamage =
        m_config->Get<bool>("sleep.interruptOnDamage", true);
    m_serverSleepConfig.interruptOnZombie =
        m_config->Get<bool>("sleep.interruptOnZombie", true);
    m_serverSleepVoteSeconds = std::clamp(
        static_cast<float>(m_config->Get<double>("sleep.voteSeconds", 30.0)),
        5.0f, 300.0f);
    m_serverSleepHungerPerDay = std::max(
        0.0f, static_cast<float>(m_config->Get<double>("needs.hungerPerDay", 1.4)));
    m_serverSleepThirstPerDay = std::max(
        0.0f, static_cast<float>(m_config->Get<double>("needs.thirstPerDay", 1.8)));
    m_serverSleep = {};
    m_serverSleepSession.Stop();
    m_lastHomesteadMinute = -1.0;
    m_buildWoodItem =
        m_config->Get<std::string>("building.woodItem", "wood");
    m_buildRange = std::clamp(
        static_cast<float>(m_config->Get<double>("building.range", 3.5)),
        0.5f, 8.0f);
    m_buildActionRange = std::clamp(
        static_cast<float>(m_config->Get<double>("building.actionRange", 2.5)),
        0.5f, 8.0f);
    m_repairWoodCost = std::clamp(
        m_config->Get<int>("building.repairWoodCost", 1), 0, 99);
    m_structureRepairAmount = std::clamp(
        static_cast<float>(m_config->Get<double>("building.repairAmount", 30.0)),
        0.0f, 100000.0f);
    m_buildZombieDamage = std::clamp(
        static_cast<float>(m_config->Get<double>("building.zombieDamage", 12.0)),
        0.0f, 1000.0f);
    m_buildZombieCooldown = std::clamp(
        static_cast<float>(
            m_config->Get<double>("building.zombieAttackCooldownSeconds", 1.0)),
        0.05f, 60.0f);
    m_breakerDamage = std::clamp(
        static_cast<float>(
            m_config->Get<double>("baseEvent.breakerDamage", 28.0)),
        0.0f, 1000.0f);
    m_breakerCooldown = std::clamp(
        static_cast<float>(
            m_config->Get<double>("baseEvent.breakerCooldownSeconds", 1.2)),
        0.05f, 60.0f);
    m_breakerRange = std::clamp(
        static_cast<float>(m_config->Get<double>("baseEvent.breakerRange", 2.2)),
        0.5f, 8.0f);
    m_demolishRefundRatio = std::clamp(
        static_cast<float>(
            m_config->Get<double>("building.demolishRefundRatio", 0.5)),
        0.0f, 1.0f);
    const double buildCooldownSeconds = std::max(
        0.0, m_config->Get<double>("building.actionCooldownSeconds", 0.5));
    m_buildCooldownTicks = static_cast<std::uint64_t>(std::llround(
        buildCooldownSeconds * static_cast<double>(m_options.tickRate)));
    m_baseCarryKg = std::clamp(
        static_cast<float>(m_config->Get<double>("inventory.playerCapacityKg", 20.0)),
        1.0f, 1000.0f);
    m_baseWalkSpeed = std::clamp(
        static_cast<float>(m_config->Get<double>("player.walkSpeed", 4.0)), 0.5f,
        20.0f);

    m_events = std::make_unique<core::EventBus>();
    m_gameTime = std::make_unique<core::GameTime>();
    const double dayLengthMinutes =
        m_config->Get<double>("game.dayLengthMinutes", 60.0);
    const double gameMinutesPerRealSecond = 1440.0 / (dayLengthMinutes * 60.0);
    core::DateTime startDate;
    startDate.year = m_config->Get<int>("game.startYear", 2000);
    startDate.month = m_config->Get<int>("game.startMonth", 7);
    startDate.day = m_config->Get<int>("game.startDay", 9);
    startDate.hour = std::clamp(m_config->Get<int>("game.startHour", 0), 0, 23);
    m_gameTime->Configure(gameMinutesPerRealSecond, startDate);

    if (!LoadContent(error)) {
        return false;
    }
    m_buildables = game::LoadBuildableDefs(*m_config);
    game::AppendBuildableDefs(m_buildables, m_modBuildables);
    const std::uint64_t modsHash = game::ComputeTreeHash(
        m_config->Get<std::string>("script.modsDir", "mods"));
    const std::uint64_t computedContentHash = game::ComputeContentHash(
        *m_items, *m_recipes, m_buildables, *m_lootTables, *m_poiConfig,
        *m_missionConfig, modsHash, m_forage.get(), m_animals.get(),
        m_crops.get(), m_settlements.get(), m_worldEvents.get(),
        m_companions.get(), m_vehicleMods.get(), m_traps.get());
    if (m_options.contentHash == 0) {
        const std::int64_t configuredContentHash =
            m_config->Get<std::int64_t>("net.contentHash", 0);
        m_options.contentHash =
            configuredContentHash != 0
                ? static_cast<std::uint64_t>(configuredContentHash)
                : computedContentHash;
    }
    ZL_INFO("building", "服务器建造目录：{} 项（mod {} 项）", m_buildables.size(),
            m_modBuildables.size());
    ZL_INFO("mod-content", "服务器内容指纹：计算 {:#x}，使用 {:#x}（mod 文件树 {:#x}）",
            computedContentHash, m_options.contentHash, modsHash);

    m_fireRng.seed(20261029u);
    const int threadCount = m_config->Get<int>("task.threads", 0);
    m_tasks = std::make_unique<core::TaskScheduler>(
        static_cast<unsigned>(std::max(threadCount, 0)));

    game::WorldSimulation::Options simulationOptions;
    simulationOptions.config = m_config.get();
    simulationOptions.events = m_events.get();
    simulationOptions.gameTime = m_gameTime.get();
    simulationOptions.items = m_items.get();
    simulationOptions.lootTables = m_lootTables.get();
    simulationOptions.animals = m_animals.get();
    simulationOptions.pois = m_poiConfig.get();
    simulationOptions.settlements = m_settlements.get();
    simulationOptions.worldEvents = m_worldEvents.get();
    simulationOptions.strategic = m_strategic.get();
    simulationOptions.companions = m_companions.get();
    simulationOptions.spawnAnimals = m_options.spawnAnimals;
    simulationOptions.fire = m_fire.get();
    simulationOptions.tasks = m_tasks.get();
    simulationOptions.saveManager = m_saveManager.get();
    simulationOptions.saveService = m_saveService.get();
    simulationOptions.saveComponents = m_saveComponents.get();
    simulationOptions.activeSaveId = &m_activeSaveId;
    simulationOptions.activeSaveUseBackup = &m_activeSaveUseBackup;
    simulationOptions.saveWarning = &m_saveWarning;
    simulationOptions.mapDir = m_selectedMapDir;
    simulationOptions.focus = glm::vec2(m_options.playerTargetX, m_options.playerTargetY);
    m_simulation = std::make_unique<game::WorldSimulation>(simulationOptions);
    std::string simulationError;
    if (!m_simulation->Init(&simulationError)) {
        return fail("模拟初始化失败：" + simulationError);
    }

    game::InventoryServiceOptions inventoryOptions;
    inventoryOptions.pickupRadius = static_cast<float>(
        m_config->Get<double>("inventory.pickupRadius", 1.6));
    inventoryOptions.inventoryRange = static_cast<float>(
        m_config->Get<double>("inventory.lootRange", 3.0));
    m_animalHarvestRange = inventoryOptions.inventoryRange;
    inventoryOptions.stationRange = static_cast<float>(
        m_config->Get<double>("crafting.stationRange", 2.5));
    m_campfireRange = inventoryOptions.stationRange;
    inventoryOptions.repairKitDefId =
        m_config->Get<std::string>("inventory.repairKit", "repair_kit");
    inventoryOptions.repairKitCount =
        std::max(1, m_config->Get<int>("inventory.repairKitCount", 1));
    inventoryOptions.repairAmount = std::clamp(
        static_cast<float>(m_config->Get<double>("inventory.repairAmount", 0.5)),
        0.01f, 1.0f);
    inventoryOptions.spoiledFoodDamage = std::clamp(
        static_cast<float>(m_config->Get<double>("food.spoiledDamage", 4.0)),
        0.0f, 100.0f);
    inventoryOptions.foodSafety.spoiledPoisonChance = std::clamp(
        static_cast<float>(
            m_config->Get<double>("food.spoiledPoisonChance", 0.85)),
        0.0f, 1.0f);
    inventoryOptions.foodSafety.poisonDurationSeconds = std::max(
        0.0f, static_cast<float>(
                  m_config->Get<double>("food.poisonDurationSeconds", 45.0)));
    inventoryOptions.foodSafety.poisonDamagePerSecond = std::max(
        0.0f, static_cast<float>(
                  m_config->Get<double>("food.poisonDamagePerSecond", 0.12)));
    inventoryOptions.foodSafety.nutritionDecayPerDay = std::max(
        0.0f, static_cast<float>(
                  m_config->Get<double>("food.nutritionDecayPerDay", 0.5)));
    inventoryOptions.foodSafety.nutritionPoisonReduction = std::clamp(
        static_cast<float>(
            m_config->Get<double>("food.nutritionPoisonReduction", 0.5)),
        0.0f, 1.0f);
    inventoryOptions.stationLabels.emplace("workbench", "工作台");
    inventoryOptions.stationLabels.emplace("campfire", "篝火");
    inventoryOptions.stationLabels.emplace("locker", "雨水收集器");
    inventoryOptions.progression = &m_progressionConfig;
    inventoryOptions.lootTables = m_lootTables.get();
    inventoryOptions.lootSearched =
        [this](ecs::Entity player, std::string_view tableId) {
            if (m_simulation == nullptr) {
                return;
            }
            const std::uint64_t stableId =
                m_simulation->Ecs().IdOf(player).id;
            for (auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (session.playerStableId == stableId) {
                    OnServerLootSearched(session, tableId);
                    return;
                }
            }
        };
    const auto& root = m_config->Raw();
    const auto crafting = root.find("crafting");
    if (crafting != root.end() && crafting->is_object()) {
        const auto labels = crafting->find("stationLabels");
        if (labels != crafting->end() && labels->is_object()) {
            for (const auto& [id, label] : labels->items()) {
                if (id.empty() || !label.is_string() ||
                    label.get<std::string>().empty()) {
                    continue;
                }
                inventoryOptions.stationLabels[id] = label.get<std::string>();
            }
        }
    }
    m_inventory = std::make_unique<game::PlayerInventoryService>(
        m_simulation->Ecs(), *m_items, *m_recipes, &m_simulation->World(),
        m_events.get(), std::move(inventoryOptions));
    InitializeServerBaseSiege();

    if (!m_options.loadSaveId.empty()) {
        if (!m_simulation->LoadSave(m_options.loadSaveId, &simulationError)) {
            return fail(simulationError);
        }
        m_worldLoadedFromSave = true;
        m_strategicContributions.clear();
        if (const game::StrategicState* strategic =
                m_simulation->Strategic();
            strategic != nullptr) {
            for (const game::StrategicContributionRecord& record :
                 strategic->contributions) {
                m_strategicContributions[record.stableId]
                                        [record.regionId] = record.points;
            }
        }
        float floodlightHp = 80.0f;
        for (const game::BuildableDef& def : m_buildables) {
            if (def.id == "floodlight") {
                floodlightHp = def.maxHp;
                break;
            }
        }
        if (m_simulation->Ecs().Registry().view<ecs::Floodlight>().size() > 0) {
            game::EnsureFloodlightHealth(m_simulation->Ecs(), floodlightHp);
        }
        RestoreServerForageState();
        m_demoLootSpawned = true;  // 存档世界已包含自己的物品，不再叠加演示散落
    } else if (m_options.spawnDemoPopulation) {
        m_simulation->SpawnDemoPopulation();
    }
    // 读档世界可能已有载具；先绑定，避免首个会话恢复时重复生成。
    EnsureServerVehicles();

    if (m_profilesEnabled && !InitPlayerProfiles(error)) {
        return false;
    }

    if (m_options.network) {
        if (!InitNetwork(error)) {
            return false;
        }
    }

    m_running = true;
    m_initialized = true;
    ZL_INFO("server", "服务器内容就绪：地图 chunks={}，物品 {}，配方 {}，系统 {}{}",
            m_simulation->Chunks().Map().chunksX * m_simulation->Chunks().Map().chunksY,
            m_items->All().size(), m_recipes->All().size(),
            m_simulation->Ecs().Systems().Count(),
            m_networkEnabled ? "，网络 UDP 监听 " + std::to_string(NetworkPort()) : "");
    return true;
}

bool ServerHost::InitNetwork(std::string* error) {
    m_replicationRegistry = std::make_unique<net::ReplicationRegistry>();
    std::string registryError;
    if (!game::RegisterEntityReplicationComponents(*m_replicationRegistry,
                                                   &registryError)) {
        if (error != nullptr) {
            *error = "复制组件注册失败：" + registryError;
        }
        return false;
    }

    if (!m_options.bindHostOverride) {
        m_options.bindHost =
            m_config->Get<std::string>("net.bindHost", m_options.bindHost);
    }
    if (!m_options.portOverride && m_options.port == 7777) {
        m_options.port = static_cast<std::uint16_t>(
            std::clamp(m_config->Get<int>("net.port", 7777), 0, 65535));
    }
    m_options.maxSessions = static_cast<std::size_t>(
        std::max(1, m_config->Get<int>("net.maxSessions",
                                       static_cast<int>(m_options.maxSessions))));
    net::KcpSessionServerOptions options;
    options.contentHash = m_options.contentHash;
    options.maxSessions = m_options.maxSessions;
    m_network = std::make_unique<net::KcpSessionServer>(options);
    std::string listenError;
    if (!m_network->Listen(m_options.bindHost, m_options.port, &listenError)) {
        if (error != nullptr) {
            *error = "服务器网络监听失败：" + listenError;
        }
        m_network.reset();
        return false;
    }
    m_networkEnabled = true;
    ZL_INFO("net", "服务器 UDP 会话监听：{}:{}（maxSessions={}，contentHash={:#x}）",
            m_options.bindHost, m_network->Port(), m_options.maxSessions,
            m_options.contentHash);
    return true;
}

bool ServerHost::LoadContent(std::string* error) {
    core::Config contentProfile;
    const core::Config* contentConfig = m_config.get();
    if (!m_selectedContentFile.empty()) {
        if (!contentProfile.LoadFile(m_selectedContentFile)) {
            *error = "服务器 M31 内容配置加载失败：" + m_selectedContentFile;
            return false;
        }
        contentProfile.ApplyCommandLine(m_options.argc, m_options.argv);
        contentConfig = &contentProfile;
    }
    m_items = std::make_unique<items::ItemRegistry>();
    const std::string itemsFile =
        m_config->Get<std::string>("items.file", "assets/items.json");
    if (!m_items->LoadFile(itemsFile)) {
        ZL_WARN("items", "物品注册表为空，物品相关功能不可用（文件: {}）", itemsFile);
    }

    m_recipes = std::make_unique<items::RecipeRegistry>();
    const std::string recipesFile =
        m_config->Get<std::string>("recipes.file", "assets/recipes.json");
    if (!m_recipes->LoadFile(recipesFile, *m_items)) {
        ZL_WARN("items", "配方注册表为空，制作功能不可用（文件: {}）", recipesFile);
    }

    const game::ModContentResult modContent =
        game::LoadModContent(*m_config, *m_items, *m_recipes);
    m_modBuildables = modContent.buildables;

    m_lootTables = std::make_unique<items::LootTableRegistry>();
    const std::string lootFile =
        m_config->Get<std::string>("loot.file", "assets/loot.json");
    if (!m_lootTables->LoadFile(lootFile, *m_items)) {
        ZL_WARN("items", "服务器战利品表为空（文件: {}）", lootFile);
    }
    for (const std::string& modLoot : modContent.lootFiles) {
        std::string lootError;
        if (!m_lootTables->MergeFile(modLoot, *m_items, &lootError)) {
            ZL_WARN("items", "服务器跳过 mod 战利品表 {}：{}", modLoot,
                    lootError);
        }
    }

    m_forage = std::make_unique<game::ForageRegistry>();
    const std::string forageFile =
        m_config->Get<std::string>("forage.file", "assets/forage.json");
    std::string forageError;
    if (!m_forage->LoadFile(forageFile, *m_lootTables, &forageError)) {
        ZL_WARN("forage", "服务器觅食数据为空（文件: {}）：{}", forageFile,
                forageError);
    }
    for (const std::string& modForage : modContent.forageFiles) {
        if (!m_forage->MergeFile(modForage, *m_lootTables, &forageError)) {
            ZL_WARN("forage", "服务器跳过 mod 觅食数据 {}：{}", modForage,
                    forageError);
        }
    }

    m_animals = std::make_unique<game::AnimalRegistry>();
    const std::string animalsFile =
        m_config->Get<std::string>("animals.file", "assets/animals.json");
    std::string animalsError;
    if (!m_animals->LoadFile(animalsFile, *m_items, *m_lootTables,
                             &animalsError)) {
        ZL_WARN("animals", "服务器动物数据为空（文件: {}）：{}", animalsFile,
                animalsError);
    } else {
        ZL_INFO("animals", "服务器动物物种：{} 项", m_animals->Size());
    }

    m_crops = std::make_unique<game::CropRegistry>();
    const std::string cropsFile =
        m_config->Get<std::string>("crops.file", "assets/crops.json");
    std::string cropsError;
    if (!m_crops->LoadFile(cropsFile, *m_items, &cropsError)) {
        ZL_WARN("crops", "服务器作物数据为空（文件: {}）：{}", cropsFile,
                cropsError);
    }
    int cropsMerged = 0;
    for (const std::string& modCrop : modContent.cropFiles) {
        if (!m_crops->MergeFile(modCrop, *m_items, &cropsError)) {
            ZL_WARN("crops", "服务器跳过 mod 作物数据 {}：{}", modCrop,
                    cropsError);
        } else {
            ++cropsMerged;
        }
    }
    if (m_crops->Size() > 0) {
        ZL_INFO("crops", "服务器作物定义：{} 项（mod 文件 {}/{}）",
                m_crops->Size(), cropsMerged, modContent.cropFiles.size());
    }

    m_fire = std::make_unique<game::FireRegistry>();
    const std::string fireFile =
        m_config->Get<std::string>("fire.file", "assets/fire.json");
    std::string fireError;
    if (!m_fire->LoadFile(fireFile, &fireError)) {
        ZL_WARN("fire", "服务器火焰数据为空（文件: {}）：{}", fireFile, fireError);
    } else {
        ZL_INFO("fire", "服务器火焰材料：{} 项", m_fire->Size());
    }

    m_poiConfig = std::make_unique<game::PoiConfig>();
    std::string poiError;
    if (!game::LoadPoiConfig(*contentConfig, m_poiConfig.get(), &poiError)) {
        ZL_ERROR("poi", "服务器 POI 配置加载失败：{}", poiError);
    } else {
        game::PoiMergeStats poiStats;
        if (m_poiConfig->enabled) {
            for (const std::string& modPoi : modContent.poiFiles) {
                if (!game::MergePoiFile(modPoi, m_poiConfig.get(), &poiStats,
                                        &poiError)) {
                    ZL_WARN("poi", "服务器跳过 mod POI 文件 {}：{}", modPoi,
                            poiError);
                }
            }
            ZL_INFO("poi", "服务器 POI 配置：{} 个区域（mod 文件 {}，新增 {}）",
                    m_poiConfig->defs.size(), poiStats.filesLoaded,
                    poiStats.defsAdded);
        }
    }

    m_missionConfig = std::make_unique<game::MissionConfig>();
    std::string missionError;
    if (!game::LoadMissionConfig(*contentConfig, m_missionConfig.get(),
                                 &missionError)) {
        ZL_ERROR("missions", "服务器任务配置加载失败：{}", missionError);
    } else {
        game::MissionMergeStats missionStats;
        if (m_missionConfig->enabled && !modContent.missionFiles.empty()) {
            if (!game::MergeMissionFiles(modContent.missionFiles, *contentConfig,
                                         m_missionConfig.get(), &missionStats,
                                         &missionError)) {
                ZL_WARN("missions", "服务器 mod 任务合并失败：{}", missionError);
            }
        }
        if (m_missionConfig->enabled) {
            ZL_INFO("missions",
                    "服务器任务配置：{} 条（mod 文件 {}，新增 {}）",
                    m_missionConfig->defs.size(), missionStats.filesLoaded,
                    missionStats.defsAdded);
        }
    }

    m_settlements = std::make_unique<game::SettlementConfig>();
    std::string settlementError;
    if (!game::LoadSettlementConfig(*m_config, m_settlements.get(),
                                    &settlementError)) {
        ZL_ERROR("settlement", "服务器聚落配置加载失败：{}", settlementError);
    } else {
        game::SettlementMergeStats settlementStats;
        if (m_settlements->enabled) {
            for (const std::string& modSettlement :
                 modContent.settlementFiles) {
                if (!game::MergeSettlementFile(
                        modSettlement, m_settlements.get(), &settlementStats,
                        &settlementError)) {
                    ZL_WARN("settlement", "服务器跳过 mod 聚落文件 {}：{}",
                            modSettlement, settlementError);
                }
            }
            ZL_INFO("settlement", "服务器聚落配置：{} 个（mod 文件 {}，新增 {}）",
                    m_settlements->defs.size(), settlementStats.filesLoaded,
                    settlementStats.defsAdded);
        }
    }

    m_worldEvents = std::make_unique<game::WorldEventConfig>();
    std::string worldEventError;
    if (!game::LoadWorldEventConfig(*m_config, m_worldEvents.get(),
                                    &worldEventError, m_items.get())) {
        ZL_ERROR("world-events", "server world event config failed: {}",
                 worldEventError);
    } else {
        game::WorldEventMergeStats worldEventStats;
        if (m_worldEvents->enabled) {
            for (const std::string& modWorldEvent :
                 modContent.worldEventFiles) {
                if (!game::MergeWorldEventFile(
                        modWorldEvent, m_worldEvents.get(), &worldEventStats,
                        &worldEventError, m_items.get())) {
                    ZL_WARN("world-events",
                            "server skipping mod world event file {}: {}",
                            modWorldEvent, worldEventError);
                }
            }
            ZL_INFO("world-events",
                    "server world events: {} definitions (mod files {}, added {})",
                    m_worldEvents->defs.size(), worldEventStats.filesLoaded,
                    worldEventStats.defsAdded);
        }
    }
    m_worldEventDetailRadius = static_cast<float>(std::clamp(
        m_config->Get<double>("worldEvents.detailRadius", 32.0), 0.0, 256.0));

    m_companions = std::make_unique<game::CompanionConfig>();
    std::string companionError;
    if (!game::LoadCompanionConfig(*m_config, m_companions.get(),
                                   &companionError, m_items.get())) {
        ZL_ERROR("companions", "服务器同伴配置加载失败：{}", companionError);
    } else {
        game::CompanionMergeStats companionStats;
        if (m_companions->enabled) {
            for (const std::string& modCompanion :
                 modContent.companionFiles) {
                if (!game::MergeCompanionFile(
                        modCompanion, m_companions.get(), &companionStats,
                        &companionError, m_items.get())) {
                    ZL_WARN("companions", "服务器跳过 mod 同伴文件 {}：{}",
                            modCompanion, companionError);
                }
            }
            ZL_INFO("companions", "服务器同伴配置：{} 个（mod 文件 {}，新增 {}）",
                    m_companions->defs.size(), companionStats.filesLoaded,
                    companionStats.defsAdded);
        }
    }

    m_vehicleMods = std::make_unique<game::VehicleModsConfig>();
    std::string vehicleModError;
    if (!game::LoadVehicleModConfig(*m_config, m_vehicleMods.get(),
                                    &vehicleModError, m_items.get())) {
        ZL_ERROR("vehicle-mods", "服务器车辆改装配置加载失败：{}",
                 vehicleModError);
    } else {
        game::VehicleModMergeStats vehicleModStats;
        if (m_vehicleMods->enabled) {
            for (const std::string& modVehicleMod :
                 modContent.vehicleModFiles) {
                if (!game::MergeVehicleModFile(
                        modVehicleMod, m_vehicleMods.get(), &vehicleModStats,
                        &vehicleModError, m_items.get())) {
                    ZL_WARN("vehicle-mods", "服务器跳过 mod 车辆改装文件 {}：{}",
                            modVehicleMod, vehicleModError);
                }
            }
            ZL_INFO("vehicle-mods",
                    "服务器车辆改装配置：{} 个（mod 文件 {}，新增 {}，跳过 {}）",
                    m_vehicleMods->defs.size(), vehicleModStats.filesLoaded,
                    vehicleModStats.defsAdded, vehicleModStats.defsSkipped);
        }
    }

    m_traps = std::make_unique<game::TrapDefsConfig>();
    std::vector<game::BuildableDef> trapBuildables =
        game::LoadBuildableDefs(*m_config);
    game::AppendBuildableDefs(trapBuildables, m_modBuildables);
    std::string trapError;
    if (!game::LoadTrapDefsConfig(*m_config, trapBuildables, m_traps.get(),
                              &trapError)) {
        ZL_ERROR("traps", "服务器陷阱配置加载失败：{}", trapError);
        m_traps->enabled = false;
        m_traps->defs.clear();
    } else if (m_traps->enabled) {
        game::TrapMergeStats trapStats;
        for (const std::string& modTrap : modContent.trapFiles) {
            if (!game::MergeTrapFile(modTrap, m_traps.get(), &trapStats,
                                     trapBuildables, &trapError)) {
                ZL_WARN("traps", "服务器跳过 mod 陷阱文件 {}：{}", modTrap,
                        trapError);
            }
        }
        ZL_INFO("traps", "服务器陷阱配置：{} 个（mod 文件 {}，新增 {}，跳过 {}）",
                m_traps->defs.size(), trapStats.filesLoaded,
                trapStats.defsAdded, trapStats.defsSkipped);
    }
    if (m_simulation != nullptr) {
        m_simulation->SetTrapDefs(m_traps.get());
    }

    m_strategic = std::make_unique<game::StrategicConfig>();
    const std::string strategicFile = m_config->Get<std::string>(
        "strategic.file", "assets/strategic.json");
    std::string strategicError;
    if (!game::LoadStrategicConfig(strategicFile, m_strategic.get(),
                                   &strategicError)) {
        ZL_WARN("strategic", "服务器战略配置不可用（文件: {}）：{}",
                strategicFile, strategicError);
        m_strategic.reset();
    } else {
        ZL_INFO("strategic",
                "服务器战略配置：{} 个区域（tick {} 分钟，预警 {} / {} 小时）",
                m_strategic->regions.size(), m_strategic->tickMinutes,
                m_strategic->warningHours, m_strategic->remoteWarningHours);
    }

    save::SaveManager::Options saveOptions;
    saveOptions.root = m_config->Get<std::string>("save.root", "saves");
    saveOptions.gameVersion =
        m_config->Get<std::string>("save.gameVersion", save::kDefaultGameVersion);
    m_saveManager = std::make_unique<save::SaveManager>(std::move(saveOptions));
    m_saveComponents = std::make_unique<save::ComponentRegistry>();
    std::string componentError;
    if (!save::RegisterBuiltinComponents(*m_saveComponents, &componentError)) {
        if (error != nullptr) {
            *error = "存档组件注册失败：" + componentError;
        }
        return false;
    }
    m_saveService = std::make_unique<save::SaveService>(*m_saveManager);
    return true;
}

bool ServerHost::InitPlayerProfiles(std::string* error) {
    PlayerProfileStore::Options options;
    options.path = m_options.playerProfilesFile;
    m_playerProfiles = std::make_unique<PlayerProfileStore>(std::move(options));
    std::string loadError;
    if (!m_playerProfiles->Load(&loadError)) {
        if (error != nullptr) {
            *error = "玩家档案加载失败：" + loadError;
        }
        m_playerProfiles.reset();
        return false;
    }
    ZL_INFO("server", "玩家档案已加载：{} 个（{}）", m_playerProfiles->Size(),
            m_playerProfiles->Path().string());
    return true;
}

void ServerHost::Shutdown() {
    if (!m_initialized && m_simulation == nullptr) {
        return;
    }
    m_running = false;
    SettleServerThrows("shutdown");
    if (m_serverBaseSiege.Active()) {
        m_serverBaseSiege.Abort("服务器关闭，守夜中止", false);
    }
    m_serverBaseSiege.Reset();
    m_serverSiegeAnchorStableId = 0;
    if (m_simulation != nullptr) {
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            ReleaseServerVehicleSeat(session, true, "服务器关闭");
        }
    }
    if (m_profilesEnabled && m_playerProfiles != nullptr && m_simulation != nullptr) {
        SavePlayerProfiles("shutdown");
    }
    if (m_saveWorldOnExit && !m_activeSaveId.empty() && m_simulation != nullptr) {
        SaveWorld("shutdown");
    }
    m_sessions.clear();
    m_chatLog.clear();
    m_socialDirty = true;
    m_serverThrows.clear();
    m_serverVehicles.clear();
    m_forageReservations.clear();
    m_animalHarvestReservations.clear();
    m_serverForage = {};
    m_serverForageEntity = entt::null;
    m_survivalScanTimer = 0.0f;
    m_network.reset();
    m_networkEnabled = false;
    m_replicationRegistry.reset();
    m_inventory.reset();
    m_simulation.reset();
    m_playerProfiles.reset();
    m_profilesEnabled = false;
    if (m_saveService != nullptr) {
        m_saveService->WaitIdle();
        m_saveService.reset();
    }
    m_saveManager.reset();
    m_saveComponents.reset();
    m_fire.reset();
    m_recipes.reset();
    m_items.reset();
    m_tasks.reset();
    m_gameTime.reset();
    m_events.reset();
    m_config.reset();
    m_initialized = false;
    ZL_INFO("server", "服务器资源已释放");
}

bool ServerHost::ProfilePosition(const PlayerProfile& profile,
                                 glm::vec2* out) const {
    if (out == nullptr || !profile.hasSnapshot) {
        return false;
    }
    for (const save::SavedComponent& component : profile.entity.components) {
        if (component.type != save::kSaveComponentPosition ||
            component.bytes.size() < sizeof(float) * 2) {
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        std::memcpy(&x, component.bytes.data(), sizeof(float));
        std::memcpy(&y, component.bytes.data() + sizeof(float),
                    sizeof(float));
        if (std::isfinite(x) && std::isfinite(y)) {
            *out = glm::vec2(x, y);
            return true;
        }
    }
    return false;
}

std::vector<glm::vec2> ServerHost::CollectInterestFocuses() const {
    std::vector<glm::vec2> focuses;
    const auto add = [&focuses](float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return;
        }
        for (const glm::vec2& existing : focuses) {
            if (std::abs(existing.x - x) < 0.01f &&
                std::abs(existing.y - y) < 0.01f) {
                return;
            }
        }
        focuses.emplace_back(x, y);
    };

    if (m_simulation->Player() != entt::null) {
        const ecs::Entity local = m_simulation->Player();
        if (const auto* position =
                m_simulation->Ecs().Registry().try_get<ecs::Position>(local);
            position != nullptr) {
            add(position->x, position->y);
        }
    }

    std::vector<std::pair<std::uint64_t, const SessionPlayer*>> sessions;
    sessions.reserve(m_sessions.size());
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        sessions.emplace_back(session.playerStableId, &session);
    }
    std::sort(sessions.begin(), sessions.end(),
              [](const auto& left, const auto& right) {
                  if (left.first != right.first) {
                      return left.first < right.first;
                  }
                  return left.second->identity < right.second->identity;
              });
    for (const auto& [stableId, session] : sessions) {
        const ecs::Entity player = m_simulation->Ecs().FindById(stableId);
        if (player == entt::null) {
            continue;
        }
        if (const auto* position =
                m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
            position != nullptr) {
            add(position->x, position->y);
        }
    }

    // Sessions that are still waiting for a profile/chunk need their target
    // window before the first spawn attempt; otherwise a player returning to
    // a far-away base would wait on the default spawn area forever.
    bool waitingWithoutProfile = false;
    bool waitingForRespawn = false;
    for (const auto& [stableId, session] : sessions) {
        static_cast<void>(stableId);
        if (session->dead) {
            waitingForRespawn = true;
        }
        if (m_simulation->Ecs().FindById(session->playerStableId) !=
            entt::null) {
            continue;
        }
        const PlayerProfile* profile =
            m_profilesEnabled && m_playerProfiles != nullptr
                ? m_playerProfiles->Find(session->identity)
                : nullptr;
        glm::vec2 target{0.0f, 0.0f};
        if (!session->dead && profile != nullptr &&
            ProfilePosition(*profile, &target)) {
            add(target.x, target.y);
        } else {
            waitingWithoutProfile = true;
        }
    }
    if (waitingWithoutProfile || waitingForRespawn || focuses.empty()) {
        add(m_options.playerTargetX, m_options.playerTargetY);
    }
    return focuses;
}

void ServerHost::StreamServerInterest() {
    if (m_simulation == nullptr) {
        return;
    }
    const std::vector<glm::vec2> focuses = CollectInterestFocuses();
    if (focuses.empty()) {
        return;
    }
    m_simulation->StreamAtFocuses(focuses);
}

void ServerHost::Tick(float dt) {
    if (!m_initialized || m_simulation == nullptr) {
        return;
    }
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        dt = 1.0f / 60.0f;
    }

    if (m_networkEnabled) {
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            session.commandsThisTick = 0;
            session.strategicRequestsThisTick = 0;
        }
        PollNetwork();
    }
    StreamServerInterest();
    m_tasks->ProcessResults();
    m_poiScanTimer += dt;
    if (m_poiScanTimer >= 0.5f) {
        m_poiScanTimer = 0.0f;
        EnsureServerLootContainers();
        EnsureServerPoiContent();
    }

    if (m_networkEnabled) {
        for (auto& [sessionId, session] : m_sessions) {
            if (!EnsureSessionPlayer(sessionId, session)) {
                break;  // chunk 尚未就绪；下一 tick 继续
            }
        }
    }

    if (m_options.spawnLocalPlayer && m_simulation->Player() == entt::null) {
        if (m_simulation->EnsureLocalPlayer(m_options.playerTargetX,
                                            m_options.playerTargetY) != entt::null &&
            m_options.spawnDemoPopulation) {
            SpawnDemoLoot();
        }
    }
    EnsureServerVehicles();
    // 读档/重连时坐标可能落在车内；chunk 就绪后再次校正，避免永久卡在车中心。
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.spawned && !session.dead &&
            session.drivingVehicleId == 0 && session.playerStableId != 0) {
            PlacePlayerOutsideServerVehicle(
                m_simulation->Ecs().FindById(session.playerStableId));
        }
    }
    EnsureServerMerchant();
    EnsureServerSettlementNpcs();

    const bool sleptThisTick =
        m_serverSleep.phase == ServerSleepPhase::Sleeping;
    TickServerSleep(dt);
    if (!sleptThisTick) {
        m_gameTime->Tick();
    }
    TickServerBaseSiege(dt);
    if (m_serverSleep.phase == ServerSleepPhase::Sleeping &&
        m_serverBaseSiege.Active()) {
        FinishServerSleep(game::SleepStopReason::SiegeStarted,
                          "守夜事件开始");
    }
    if (m_simulation != nullptr && m_gameTime != nullptr) {
        game::StrategicTickInput strategic;
        strategic.gameMinute =
            static_cast<std::int64_t>(m_gameTime->TotalGameMinutes());
        strategic.anyPlayerOnline =
            !m_sessions.empty() || m_simulation->Player() != entt::null;
        strategic.detailedEnabled = true;
        std::string primaryPlayerRegion;
        for (const auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (!session.spawned || session.dead ||
                session.playerStableId == 0) {
                continue;
            }
            const ecs::Entity entity =
                m_simulation->Ecs().FindById(session.playerStableId);
            if (entity == entt::null) {
                continue;
            }
            const auto* position =
                m_simulation->Ecs().Registry().try_get<ecs::Position>(entity);
            if (position == nullptr || m_simulation->Graph() == nullptr) {
                continue;
            }
            const world::WorldRegion* region =
                m_simulation->Graph()->RegionAt(position->x, position->y);
            if (region == nullptr) {
                continue;
            }
            if (std::find(strategic.playerRegions.begin(),
                          strategic.playerRegions.end(),
                          region->id) == strategic.playerRegions.end()) {
                strategic.playerRegions.push_back(region->id);
            }
            if (primaryPlayerRegion.empty()) {
                primaryPlayerRegion = region->id;
            }
        }
        // 单机/嵌入式服务器没有 session 时仍以本地玩家作为战略在场者。
        if (strategic.playerRegions.empty() &&
            m_simulation->Player() != entt::null &&
            m_simulation->Graph() != nullptr) {
            const auto* position =
                m_simulation->Ecs().Registry().try_get<ecs::Position>(
                    m_simulation->Player());
            if (position != nullptr) {
                if (const world::WorldRegion* region =
                        m_simulation->Graph()->RegionAt(position->x,
                                                        position->y);
                    region != nullptr) {
                    strategic.playerRegions.push_back(region->id);
                    primaryPlayerRegion = region->id;
                }
            }
        }
        std::string strategicError;
        if (!m_simulation->TickStrategic(strategic, nullptr,
                                         &strategicError)) {
            ZL_WARN("strategic", "服务器战略 tick 失败：{}", strategicError);
        }
        // M32-T7：详细围攻只允许一条，但可以选择与当前 Active 战线同区域的
        // 任一玩家；否则第二个玩家在另一区域发起反攻会被 primary 区域误判离场。
        std::string detailedPlayerRegion = primaryPlayerRegion;
        if (m_simulation->RegionalSiegeStatus().active) {
            const std::string& siegeRegion =
                m_simulation->RegionalSiegeStatus().regionId;
            if (std::find(strategic.playerRegions.begin(),
                          strategic.playerRegions.end(),
                          siegeRegion) != strategic.playerRegions.end()) {
                detailedPlayerRegion = siegeRegion;
            }
        }
        m_simulation->UpdateRegionalSiege(
            dt, m_serverSleep.phase == ServerSleepPhase::Sleeping,
            detailedPlayerRegion);
        if (m_serverSleep.phase == ServerSleepPhase::Sleeping &&
            m_simulation->RegionalSiegeStatus().active) {
            FinishServerSleep(game::SleepStopReason::SiegeStarted,
                              "战略围攻开始");
        }
        ProcessRegionalSiegeResult();
        UpdateStrategicContributionRewards();
    }
    m_events->EmitQueued(core::kOnTick, static_cast<int64_t>(m_ticks));
    m_simulation->Tick(dt);
    DrainFireDamageCredits();
    CheckSiegeGuardKills();
    TrackServerWorldEventParticipants();
    SettleTerminalServerWorldEvents();
    TickServerVehicles(dt);
    TickServerFloodlightAttacks(dt);
    TickHomestead(dt);
    TickElectricFences();
    TickTrapTriggers();
    TickProgression();
    TickCrafting(dt);
    TickServerRangedWeapons(dt);
    TickServerSurvival(dt);
    TickServerAnimalHarvest(dt);
    UpdateProjectiles(dt);
    UpdateServerThrows(dt);
    UpdateTransientEffects(dt);

    UpdatePlayerLifecycle();
    UpdateServerStealth();
    m_simulation->Flush();
    m_events->FlushQueued();
    TickServerPoiStages(dt);
    TickServerPoiTasks();
    TickServerMissions();
    TickServerObjectives();

    if (m_saveService != nullptr) {
        m_saveService->PollResults();
    }
    ++m_ticks;
    if (m_networkEnabled) {
        BroadcastSnapshots();
        BroadcastStrategicSnapshots();
        BroadcastWorldEventSnapshots();
        BroadcastStructureDiffs();
        BroadcastPlayerStates();
        BroadcastSocialStates();
    }
    if (m_profilesEnabled && m_profileSaveIntervalSeconds > 0.0) {
        m_profileSaveTimer += static_cast<double>(dt);
        if (m_profileSaveTimer >= m_profileSaveIntervalSeconds) {
            SavePlayerProfiles("interval");
        }
    }
    m_logTimer += static_cast<double>(dt);
    if (m_logTimer >= 5.0) {
        m_logTimer = 0.0;
        LogStatus();
    }
}

void ServerHost::RunForTicks(int count, float dt) {
    for (int i = 0; i < count && m_running; ++i) {
        Tick(dt);
    }
}

void ServerHost::RequestStop() {
    m_running = false;
}

bool ServerHost::Running() const {
    return m_running;
}

uint64_t ServerHost::TickCount() const {
    return m_ticks;
}

double ServerHost::GameMinutes() const {
    return m_gameTime != nullptr ? m_gameTime->TotalGameMinutes() : 0.0;
}

void ServerHost::AdvanceGameMinutes(double minutes) {
    if (m_gameTime != nullptr && std::isfinite(minutes) && minutes > 0.0) {
        m_gameTime->AdvanceGameMinutes(minutes);
    }
}

game::WorldSimulation& ServerHost::Simulation() {
    return *m_simulation;
}

const game::WorldSimulation& ServerHost::Simulation() const {
    return *m_simulation;
}

const core::Config& ServerHost::Config() const {
    return *m_config;
}

void ServerHost::PollNetwork() {
    if (m_network == nullptr) {
        return;
    }
    m_network->Poll();
    net::KcpSessionServerEvent event;
    while (m_network->PollEvent(&event)) {
        switch (event.type) {
            case net::KcpSessionServerEventType::Connected: {
                std::string identity = event.playerIdentity;
                if (identity.empty()) {
                    identity = "session-" + std::to_string(event.sessionId);
                }
                if (!net::IsValidPlayerIdentity(identity)) {
                    ZL_WARN("net", "拒绝非法玩家身份：session={}", event.sessionId);
                    if (m_network != nullptr) {
                        (void)m_network->Disconnect(
                            event.sessionId, net::SessionDisconnectReason::BadRequest,
                            "玩家身份格式非法");
                    }
                    break;
                }
                auto existingSession = m_sessions.end();
                for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
                    if (it->first != event.sessionId && it->second.identity == identity) {
                        existingSession = it;
                        break;
                    }
                }
                if (existingSession != m_sessions.end()) {
                    const std::uint64_t oldSessionId = existingSession->first;
                    ReleaseServerVehicleSeat(existingSession->second, true,
                                               "同身份接管");
                    CaptureSessionProfile(existingSession->second,
                                          existingSession->second.dead);
                    StandbyOwnedCompanion(
                        existingSession->second.playerStableId,
                        "同身份接管");
                    if (m_simulation != nullptr &&
                        existingSession->second.playerStableId != 0) {
                        const ecs::Entity oldPlayer = m_simulation->Ecs().FindById(
                            existingSession->second.playerStableId);
                        if (oldPlayer != entt::null) {
                            m_simulation->DespawnEntity(oldPlayer);
                        }
                    }
                    OnServerSessionRemovedForSleep(
                        existingSession->second.playerStableId);
                    if (m_network != nullptr) {
                        (void)m_network->Disconnect(
                            oldSessionId, net::SessionDisconnectReason::ClientLeft,
                            "同身份新连接接管会话");
                    }
                    m_sessions.erase(existingSession);
                    SavePlayerProfiles("reconnect-replace");
                    ZL_INFO("net", "同身份新连接接管：oldSession={}，newSession={}，身份={}",
                            oldSessionId, event.sessionId, identity);
                }
                const auto [it, inserted] = m_sessions.try_emplace(event.sessionId);
                if (!inserted) {
                    ZL_WARN("net", "忽略重复 Connected 事件：session={}", event.sessionId);
                    break;
                }
                it->second.identity = identity;
                it->second.playerName =
                    event.playerName.empty() ? "Player" : event.playerName;
                if (!net::IsValidChatText(it->second.playerName)) {
                    it->second.playerName = "Player";
                }
                m_socialDirty = true;
                ZL_INFO("net", "客户端加入：session={}，身份={}，名称={}（当前 {} 人）",
                        event.sessionId, it->second.identity, it->second.playerName,
                        m_sessions.size());
                (void)EnsureSessionPlayer(event.sessionId, it->second);
                break;
            }
            case net::KcpSessionServerEventType::Data: {
                const auto it = m_sessions.find(event.sessionId);
                if (it == m_sessions.end()) {
                    ZL_WARN("net", "收到未登记会话的数据：session={}", event.sessionId);
                    break;
                }
                ++m_networkStats.dataPacketsReceived;
                if (event.data.size() > net::kMaxCommandPacketBytes) {
                    ++m_networkStats.oversizedPackets;
                    ++m_networkStats.malformedPackets;
                    ZL_WARN("net", "session={} 命令包超过 {} 字节上限（{}），已拒绝",
                            event.sessionId, net::kMaxCommandPacketBytes,
                            event.data.size());
                    break;
                }
                if (event.data.size() >= 4 &&
                    ReadU32Le(event.data.data()) == net::kChatMessageMagic) {
                    net::ChatMessage chat;
                    std::string chatError;
                    if (!net::DecodeChatMessage(event.data, &chat, &chatError)) {
                        ++m_networkStats.chatMessagesRejected;
                        ZL_WARN("net", "session={} 聊天消息无效：{}", event.sessionId,
                                chatError);
                        break;
                    }
                    ++m_networkStats.chatMessagesReceived;
                    HandleChatMessage(event.sessionId, it->second, chat.text);
                    break;
                }
                if (event.data.size() >= 4 &&
                    ReadU32Le(event.data.data()) ==
                        net::kStrategicRequestMagic) {
                    net::StrategicRequest request;
                    std::string strategicError;
                    if (!net::DecodeStrategicRequest(event.data, &request,
                                                    &strategicError)) {
                        ++m_networkStats.malformedPackets;
                        ++m_networkStats.strategicRequestsRejected;
                        ZL_WARN("strategic", "session={} 战略请求无效：{}",
                                event.sessionId, strategicError);
                        break;
                    }
                    (void)HandleStrategicRequest(event.sessionId, it->second,
                                                 request, &strategicError);
                    break;
                }
                net::CommandPacket packet;
                std::string error;
                if (!net::DecodeCommandPacket(event.data, &packet, &error)) {
                    ++m_networkStats.malformedPackets;
                    ZL_WARN("net", "session={} 命令包解码失败：{}", event.sessionId, error);
                    break;
                }
                SessionPlayer& session = it->second;
                if (packet.sequence <= session.lastCommandSequence) {
                    ++m_networkStats.stalePackets;
                    ZL_WARN("net", "session={} 命令包序列未前进（{} <= {}）",
                            event.sessionId, packet.sequence,
                            session.lastCommandSequence);
                    break;
                }
                session.lastCommandSequence = packet.sequence;
                for (const net::Command& command : packet.commands) {
                    ++m_networkStats.commandsReceived;
                    if (session.commandsThisTick >= m_maxCommandsPerTick) {
                        ++m_networkStats.commandsRejected;
                        ++m_networkStats.rateLimitedCommands;
                        ZL_DEBUG("net", "session={} 命令 {} 超过每 tick 配额 {}",
                                 event.sessionId, net::CommandTypeName(command.type),
                                 m_maxCommandsPerTick);
                        continue;
                    }
                    ++session.commandsThisTick;
                    if (!ExecuteSessionCommand(event.sessionId, session, command, &error)) {
                        ++m_networkStats.commandsRejected;
                        ZL_DEBUG("net", "session={} 命令 {} 被拒绝：{}", event.sessionId,
                                 net::CommandTypeName(command.type), error);
                    } else {
                        ++m_networkStats.commandsAccepted;
                    }
                }
                break;
            }
            case net::KcpSessionServerEventType::Disconnected:
                RemoveSession(event.sessionId, static_cast<std::uint8_t>(event.reason),
                              event.message);
                break;
        }
    }
}

bool ServerHost::EnsureSessionPlayer(std::uint64_t sessionId, SessionPlayer& session) {
    static_cast<void>(sessionId);
    if (m_simulation == nullptr || m_replicationRegistry == nullptr) {
        return false;
    }
    if (session.dead) {
        if (m_ticks < session.respawnAtTick) {
            return true;
        }
        return RespawnSessionPlayer(session);
    }
    return SpawnOrRestoreSessionPlayer(sessionId, session);
}

PlayerProfile* ServerHost::FindSessionProfile(SessionPlayer& session) {
    if (!m_profilesEnabled || m_playerProfiles == nullptr || session.identity.empty()) {
        return nullptr;
    }
    return m_playerProfiles->Find(session.identity);
}

bool ServerHost::SpawnOrRestoreSessionPlayer(std::uint64_t sessionId,
                                             SessionPlayer& session) {
    PlayerProfile* profile = FindSessionProfile(session);
    if (profile == nullptr && m_profilesEnabled && m_playerProfiles != nullptr) {
        profile = &m_playerProfiles->Create(session.identity, session.playerName);
        if (profile->stableId == 0) {
            ZL_ERROR("net", "无法为身份 {} 分配 StableId", session.identity);
            return false;
        }
    }
    if (profile != nullptr) {
        if (profile->stableId == 0) {
            ZL_ERROR("net", "玩家档案缺少 StableId：身份={}", session.identity);
            return false;
        }
        session.playerStableId = profile->stableId;
        if (!profile->alive) {
            session.dead = true;
            session.needsFullSnapshot = true;
            session.stateDirty = true;
            if (session.respawnAtTick == 0) {
                const double seconds = std::max(
                    0.0, static_cast<double>(profile->respawnSeconds));
                const std::uint64_t ticks = static_cast<std::uint64_t>(std::llround(
                    seconds * static_cast<double>(m_options.tickRate)));
                session.respawnAtTick = m_ticks + std::max<std::uint64_t>(1, ticks);
            }
            SetSessionStatus(session, "你已死亡，等待复活", 2);
            m_socialDirty = true;
            return true;
        }
    }

    auto waitForSpawn = [&] {
        session.spawned = false;
        session.playerStableId = 0;
        session.assignmentSent = false;
        return false;
    };
    ecs::Entity player = entt::null;
    if (session.playerStableId != 0) {
        player = m_simulation->Ecs().FindById(session.playerStableId);
        if (player != entt::null) {
            session.spawned = true;
            return true;
        }
    }
    if (profile == nullptr && session.playerStableId != 0) {
        // 旧行为（未启用档案）：实体丢失后重新分配。
        session.playerStableId = 0;
        session.assignmentSent = false;
        session.producer.reset();
    }

    if (profile != nullptr && profile->hasSnapshot) {
        std::string restoreError;
        player = m_simulation->RestoreNetworkPlayer(profile->entity, false,
                                                    &restoreError);
        if (player == entt::null) {
            ZL_WARN("net", "session={} 恢复玩家失败，下一 tick 重试：{}", session.identity,
                    restoreError);
            return waitForSpawn();
        }
        ++m_networkStats.profileRestores;
    } else {
        const int slot = static_cast<int>((sessionId - 1) % 8);
        const float offsetX = static_cast<float>((slot % 3) - 1) * 1.5f;
        const float offsetY = static_cast<float>((slot / 3) - 1) * 1.5f;
        player = m_simulation->SpawnNetworkPlayer(
            m_options.playerTargetX + offsetX, m_options.playerTargetY + offsetY, 24,
            session.playerStableId);
        if (player == entt::null) {
            return waitForSpawn();  // chunk 尚未就绪
        }
    }

    session.playerStableId = m_simulation->Ecs().IdOf(player).id;
    session.spawned = true;
    ResetServerStealth(session, player, "生成/恢复");
    m_socialDirty = true;
    if (session.playerStableId == 0) {
        ZL_ERROR("net", "session={} 玩家实体缺少 StableId", session.identity);
        return waitForSpawn();
    }
    if (profile != nullptr) {
        profile->stableId = session.playerStableId;
        profile->alive = true;
        profile->respawnSeconds = 0.0f;
        profile->state.playerStableId = session.playerStableId;
    } else {
        // 旧路径：没有档案时仍按现分配生成。
    }
    session.lastProgressionMinute =
        m_gameTime != nullptr ? m_gameTime->TotalGameMinutes() : 0.0;
    if (profile != nullptr) {
        MergeSettlementReputationSummary(
            m_simulation->Ecs(), player, profile->state.settlementReputation);
    }
    ApplyProgressionRuntime(player, &session);
    EnsureServerVehicles();
    PlacePlayerOutsideServerVehicle(player);

    game::EntityReplicationProducer::Options producerOptions;
    producerOptions.aoiRadius = static_cast<float>(
        m_config->Get<double>("net.aoiRadius", 48.0));
    producerOptions.maxEntities = static_cast<std::size_t>(
        std::max(1, m_config->Get<int>("net.maxEntities", 256)));
    producerOptions.viewerStableId = session.playerStableId;
    if (session.producer == nullptr) {
        session.producer = std::make_unique<game::EntityReplicationProducer>(
            m_simulation->Ecs(), *m_replicationRegistry, producerOptions);
    } else {
        session.producer->Reset();
    }
    session.assignmentSent = false;
    session.needsFullSnapshot = true;
    session.needsStrategicSnapshot = true;
    session.needsWorldEventSnapshot = true;
    session.lastStrategicRequestSequence = 0;
    session.strategicRequestsThisTick = 0;
    session.stateDirty = true;
    session.lastStateTick = 0;
    session.dead = false;
    session.respawnAtTick = 0;
    session.drivingVehicleId = 0;
    session.lastCommandSequence = 0;
    session.commandsThisTick = 0;
    session.lastAttackTick = 0;
    session.attackUsed = false;
    session.lastProjectileTick = 0;
    session.projectileUsed = false;
    session.throwUsed = false;
    session.lastThrowTick = 0;
    session.rangedWeapon = {};
    session.rangedShotSerial = 0;
    if (profile != nullptr && !profile->hasSnapshot) {
        CaptureSessionProfile(session, false);
    }
    if (m_options.spawnDemoPopulation) {
        SpawnDemoLoot();
    }
    ZL_INFO("net", "身份={} 分配玩家 stableId={}（当前 {} 人）", session.identity,
            session.playerStableId, m_sessions.size());
    return true;
}

bool ServerHost::BuildStrategicSnapshot(net::StrategicSnapshot* snapshot,
                                        std::string* error) const {
    if (snapshot == nullptr || m_simulation == nullptr) {
        if (error != nullptr) {
            *error = "战略快照输出或模拟为空";
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    std::int64_t gameMinute = 0;
    if (m_gameTime != nullptr) {
        gameMinute = static_cast<std::int64_t>(
            std::llround(m_gameTime->TotalGameMinutes()));
    }
    game::StrategicMapState map;
    const game::StrategicState* state = m_simulation->Strategic();
    const game::StrategicConfig* config = m_simulation->StrategicSettings();
    if (state != nullptr && config != nullptr) {
        map = game::BuildStrategicMapState(*config, *state, gameMinute);
    } else {
        map.enabled = false;
        map.gameMinute = std::max<std::int64_t>(0, gameMinute);
    }

    net::StrategicSnapshot out;
    out.serverTick = m_ticks;
    out.enabled = map.enabled;
    out.gameMinute = map.gameMinute;
    out.tickMinutes = map.tickMinutes;
    out.warningHours = map.warningHours;
    out.remoteWarningHours = map.remoteWarningHours;
    out.autoResolveGraceMinutes = map.autoResolveGraceMinutes;
    out.people = map.resources.people;
    out.supplies = map.resources.supplies;
    out.ammo = map.resources.ammo;
    out.fuel = map.resources.fuel;
    out.regions.reserve(map.regions.size());
    for (const game::StrategicMapRegion& region : map.regions) {
        net::StrategicRegionSnapshot entry;
        entry.id = region.id;
        entry.owner = static_cast<std::uint8_t>(region.owner);
        entry.contestedFaction =
            static_cast<std::uint8_t>(region.contestedFaction);
        entry.siegePhase = static_cast<std::uint8_t>(region.siegePhase);
        entry.recoveryStage = static_cast<std::uint8_t>(region.recoveryStage);
        entry.coreSafe = region.coreSafe;
        entry.remote = region.remote;
        entry.pressure = region.pressure;
        entry.defense = region.defense;
        entry.supply = region.supply;
        entry.morale = region.morale;
        entry.population = region.population;
        entry.garrison = region.garrison;
        entry.fortification = region.fortification;
        entry.intelLevel = region.intelLevel;
        entry.strategicValue = region.strategicValue;
        entry.facilityDamage = region.facilityDamage;
        entry.refugees = region.refugees;
        entry.infectionCores = region.infectionCores;
        entry.recoveryReadyMinute = region.recoveryReadyMinute;
        entry.warningValid = region.warning.valid;
        entry.warningMinute = region.warning.warningMinute;
        entry.attackMinute = region.warning.attackMinute;
        entry.reportedAttackMinute = region.warning.reportedAttackMinute;
        entry.warningMinutes = region.warning.warningMinutes;
        entry.remainingMinutes = region.warning.remainingMinutes;
        entry.warningIntelLevel = region.warning.intelLevel;
        entry.enemyStrength = region.warning.enemyStrength;
        entry.reportedEnemyStrength = region.warning.reportedEnemyStrength;
        entry.warningDefenseScore = region.warning.defenseScore;
        out.regions.push_back(std::move(entry));
    }
    out.events.reserve(map.events.size());
    for (const game::StrategicMapEvent& event : map.events) {
        net::StrategicEventSnapshot entry;
        entry.id = event.id;
        entry.regionId = event.regionId;
        entry.kind = static_cast<std::uint8_t>(event.kind);
        entry.ownerAfter = static_cast<std::uint8_t>(event.ownerAfter);
        entry.gameMinute = event.gameMinute;
        entry.people = event.delta.people;
        entry.supplies = event.delta.supplies;
        entry.ammo = event.delta.ammo;
        entry.fuel = event.delta.fuel;
        out.events.push_back(std::move(entry));
    }
    *snapshot = std::move(out);
    return true;
}

void ServerHost::BroadcastStrategicSnapshots() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_sessions.empty()) {
        return;
    }
    bool anyNeeds = false;
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId != 0 && session.needsStrategicSnapshot) {
            anyNeeds = true;
            break;
        }
    }
    // 战略状态按分钟级变化；0.5 秒全量快照足够 HUD/地图，也避免占据
    // 战斗命令的 KCP 发送窗口。请求/任务奖励会置 dirty 立即补发。
    const bool periodic = (m_ticks % 30u) == 0u;
    if (!anyNeeds && !periodic && !m_strategicDirty) {
        return;
    }

    net::StrategicSnapshot snapshot;
    std::string error;
    if (!BuildStrategicSnapshot(&snapshot, &error)) {
        ++m_networkStats.strategicSnapshotFailures;
        ZL_WARN("strategic", "服务器战略快照构建失败：{}", error);
        return;
    }
    ++m_strategicSequence;
    if (m_strategicSequence == 0) {
        m_strategicSequence = 1;
    }
    snapshot.sequence = m_strategicSequence;
    const std::vector<std::uint8_t> bytes =
        net::EncodeStrategicSnapshot(snapshot, &error);
    if (bytes.empty()) {
        ++m_networkStats.strategicSnapshotFailures;
        ZL_WARN("strategic", "服务器战略快照编码失败：{}", error);
        return;
    }
    bool sentAny = false;
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0) {
            continue;
        }
        if (!session.needsStrategicSnapshot && !periodic &&
            !m_strategicDirty) {
            continue;
        }
        std::string sendError;
        if (!m_network->Send(sessionId, bytes, &sendError)) {
            ++m_networkStats.strategicSnapshotFailures;
            session.needsStrategicSnapshot = true;
            ZL_WARN("strategic", "session={} 战略快照发送失败：{}", sessionId,
                    sendError);
            continue;
        }
        session.needsStrategicSnapshot = false;
        sentAny = true;
        ++m_networkStats.strategicSnapshotsSent;
    }
    if (sentAny) {
        m_strategicDirty = false;
    }
}

bool ServerHost::BuildWorldEventSnapshot(
    net::WorldEventSnapshot* snapshot, std::string* error) const {
    if (snapshot == nullptr || m_simulation == nullptr) {
        if (error != nullptr) {
            *error = "world event snapshot output or simulation is null";
        }
        return false;
    }
    net::WorldEventSnapshot out;
    out.serverTick = m_ticks;
    out.gameMinute =
        m_gameTime != nullptr
            ? std::max<std::int64_t>(
                  0, static_cast<std::int64_t>(
                         std::floor(m_gameTime->TotalGameMinutes())))
            : 0;
    const game::WorldEventDirector* director = m_simulation->WorldEvents();
    out.enabled = director != nullptr && director->Enabled();
    if (!out.enabled) {
        *snapshot = std::move(out);
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    std::vector<const game::WorldEventInstance*> ordered;
    ordered.reserve(director->Events().size());
    for (const game::WorldEventInstance& event : director->Events()) {
        ordered.push_back(&event);
    }
    const auto priority = [](const game::WorldEventInstance& event) {
        switch (event.status) {
            case game::WorldEventStatus::Active:
                return 0;
            case game::WorldEventStatus::Warning:
                return 1;
            case game::WorldEventStatus::Resolved:
            case game::WorldEventStatus::Failed:
            case game::WorldEventStatus::Expired:
                return 2;
            case game::WorldEventStatus::Scheduled:
                return 3;
        }
        return 4;
    };
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const game::WorldEventInstance* left,
                         const game::WorldEventInstance* right) {
                         const int leftPriority = priority(*left);
                         const int rightPriority = priority(*right);
                         if (leftPriority != rightPriority) {
                             return leftPriority < rightPriority;
                         }
                         if (left->day != right->day) {
                             return left->day > right->day;
                         }
                         return left->id < right->id;
                     });
    const std::size_t count =
        std::min<std::size_t>(ordered.size(),
                              net::kMaxWorldEventSnapshotEntries);
    out.events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const game::WorldEventInstance& event = *ordered[i];
        net::WorldEventSnapshotEntry entry;
        entry.id = event.id;
        entry.defId = event.defId;
        entry.name = event.name;
        entry.kind = event.kind;
        entry.regionId = event.regionId;
        entry.x = event.x;
        entry.y = event.y;
        entry.status = static_cast<std::uint8_t>(event.status);
        entry.stageKind = static_cast<std::uint8_t>(event.stageKind);
        entry.day = event.day;
        entry.stageIndex = event.stageIndex;
        entry.stageProgress = event.stageProgress;
        entry.stageTarget = event.stageTarget;
        entry.stageRadius = event.stageRadius;
        entry.stageTargetId = event.stageTargetId;
        entry.scheduledMinute = event.scheduledMinute;
        entry.warningMinute = event.warningMinute;
        entry.activeMinute = event.activeMinute;
        entry.deadlineMinute = event.deadlineMinute;
        entry.resolvedMinute = event.resolvedMinute;
        entry.chainDepth = event.chainDepth;
        entry.detail = event.detail;
        entry.downgraded = event.downgraded;
        entry.stagesComplete = event.stagesComplete;
        entry.choiceId = event.choiceId;
        entry.nextEventId = event.nextEventId;
        out.events.push_back(std::move(entry));
    }
    *snapshot = std::move(out);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void ServerHost::BroadcastWorldEventSnapshots() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_sessions.empty() || m_worldEvents == nullptr ||
        !m_worldEvents->enabled) {
        return;
    }
    bool anyNeeds = false;
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId != 0 && session.assignmentSent &&
            session.needsWorldEventSnapshot) {
            anyNeeds = true;
            break;
        }
    }
    const bool periodic = (m_ticks % 30u) == 0u;
    if (!anyNeeds && !periodic && !m_worldEventDirty) {
        return;
    }

    net::WorldEventSnapshot snapshot;
    std::string error;
    if (!BuildWorldEventSnapshot(&snapshot, &error)) {
        ++m_networkStats.worldEventSnapshotFailures;
        ZL_WARN("world-events", "server world event snapshot failed: {}", error);
        return;
    }
    ++m_worldEventSequence;
    if (m_worldEventSequence == 0) {
        m_worldEventSequence = 1;
    }
    snapshot.sequence = m_worldEventSequence;
    const std::vector<std::uint8_t> bytes =
        net::EncodeWorldEventSnapshot(snapshot, &error);
    if (bytes.empty()) {
        ++m_networkStats.worldEventSnapshotFailures;
        ZL_WARN("world-events", "server world event snapshot encode failed: {}",
                error);
        return;
    }
    bool sentAny = false;
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0 || !session.assignmentSent) {
            continue;
        }
        if (!session.needsWorldEventSnapshot && !periodic &&
            !m_worldEventDirty) {
            continue;
        }
        std::string sendError;
        if (!m_network->Send(sessionId, bytes, &sendError)) {
            ++m_networkStats.worldEventSnapshotFailures;
            session.needsWorldEventSnapshot = true;
            ZL_WARN("world-events", "session={} world event snapshot failed: {}",
                    sessionId, sendError);
            continue;
        }
        session.needsWorldEventSnapshot = false;
        sentAny = true;
        ++m_networkStats.worldEventSnapshotsSent;
    }
    if (sentAny) {
        m_worldEventDirty = false;
    }
}

void ServerHost::TrackServerWorldEventParticipants() {
    if (m_simulation == nullptr || m_worldEvents == nullptr ||
        !m_worldEvents->enabled) {
        return;
    }
    game::WorldEventDirector* director = m_simulation->WorldEvents();
    if (director == nullptr || !director->Enabled()) {
        return;
    }
    for (const game::WorldEventInstance& event : director->Events()) {
        if (event.status != game::WorldEventStatus::Active) {
            continue;
        }
        const float radiusSq =
            m_worldEventDetailRadius * m_worldEventDetailRadius;
        for (const auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (session.playerStableId == 0 || !session.spawned ||
                session.dead) {
                continue;
            }
            const ecs::Entity player =
                m_simulation->Ecs().FindById(session.playerStableId);
            if (player == entt::null) {
                continue;
            }
            const auto* position =
                m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
            if (position == nullptr) {
                continue;
            }
            const float dx = position->x - event.x;
            const float dy = position->y - event.y;
            if (dx * dx + dy * dy <= radiusSq) {
                m_worldEventParticipants[event.id].insert(
                    session.playerStableId);
            }
        }
    }
}

void ServerHost::SettleServerWorldEventParticipants(
    std::string_view eventId) {
    if (m_simulation == nullptr || m_simulation->WorldEvents() == nullptr) {
        return;
    }
    const game::WorldEventInstance* event =
        m_simulation->WorldEvents()->Find(eventId);
    if (event == nullptr) {
        return;
    }
    auto& participants = m_worldEventParticipants[std::string(eventId)];
    const float radiusSq =
        m_worldEventDetailRadius * m_worldEventDetailRadius;
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0 || !session.spawned || session.dead) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        const auto* position =
            m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
        if (position == nullptr) {
            continue;
        }
        const float dx = position->x - event->x;
        const float dy = position->y - event->y;
        if (dx * dx + dy * dy <= radiusSq) {
            participants.insert(session.playerStableId);
        }
    }
    if (participants.empty()) {
        m_worldEventParticipants.erase(std::string(eventId));
        return;
    }
    for (const std::uint64_t stableId : participants) {
        const ecs::Entity player = m_simulation->Ecs().FindById(stableId);
        if (player == entt::null) {
            continue;
        }
        game::WorldEventSettlement settlement;
        std::string error;
        if (!m_simulation->SettleWorldEvent(eventId, player, &settlement,
                                            &error) &&
            !error.empty()) {
            ZL_DEBUG("world-events", "participant settlement skipped ({}): {}",
                      eventId, error);
        }
    }
    m_worldEventParticipants.erase(std::string(eventId));
}

void ServerHost::SettleTerminalServerWorldEvents() {
    if (m_simulation == nullptr || m_worldEvents == nullptr ||
        !m_worldEvents->enabled) {
        return;
    }
    game::WorldEventDirector* director = m_simulation->WorldEvents();
    if (director == nullptr || !director->Enabled()) {
        return;
    }
    for (const game::WorldEventInstance& event : director->Events()) {
        if (event.status != game::WorldEventStatus::Resolved &&
            event.status != game::WorldEventStatus::Failed &&
            event.status != game::WorldEventStatus::Expired) {
            continue;
        }
        if (!m_worldEventTerminalHandled.insert(event.id).second) {
            continue;
        }
        SettleServerWorldEventParticipants(event.id);
        m_worldEventDirty = true;
    }
}

bool ServerHost::ChooseServerWorldEvent(SessionPlayer& session,
                                        const net::Command& command,
                                        std::string* error) {
    ++m_networkStats.worldEventChoicesReceived;
    auto fail = [&](std::string message) {
        ++m_networkStats.worldEventChoicesRejected;
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (m_simulation == nullptr || m_worldEvents == nullptr ||
        !m_worldEvents->enabled || m_gameTime == nullptr) {
        return fail("世界事件不可用");
    }
    game::WorldEventDirector* director = m_simulation->WorldEvents();
    if (director == nullptr || !director->Enabled()) {
        return fail("世界事件导演未启用");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return fail("玩家实体不存在");
    }
    const auto* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return fail("玩家位置不存在");
    }
    const game::WorldEventInstance* event = nullptr;
    for (const game::WorldEventInstance& candidate : director->Events()) {
        if (candidate.status != game::WorldEventStatus::Active) {
            continue;
        }
        const game::WorldEventDef* candidateDef =
            m_worldEvents->Find(candidate.defId);
        if (candidateDef != nullptr &&
            game::WorldEventCanChoose(*candidateDef, candidate,
                                      position->x, position->y)) {
            event = &candidate;
            break;
        }
    }
    if (event == nullptr) {
        return fail("当前没有可抉择的世界事件");
    }
    const game::WorldEventDef* def = m_worldEvents->Find(event->defId);
    if (def == nullptr) {
        return fail("世界事件定义缺失");
    }
    if (command.payload.index < 0 ||
        command.payload.index >= static_cast<int>(def->choices.size())) {
        return fail("世界事件选项越界");
    }
    const game::WorldEventChoiceDef& choice =
        def->choices[static_cast<std::size_t>(command.payload.index)];
    const std::string eventId = event->id;
    game::WorldEventSettlement settlement;
    std::string chooseError;
    m_worldEventParticipants[eventId].insert(session.playerStableId);
    if (!m_simulation->ChooseWorldEvent(eventId, choice.id, player,
                                        &settlement, &chooseError)) {
        return fail(chooseError.empty() ? "世界事件选择失败" : chooseError);
    }
    SettleServerWorldEventParticipants(eventId);
    m_worldEventTerminalHandled.insert(eventId);
    m_worldEventDirty = true;
    session.stateDirty = true;
    SetSessionStatus(session, "世界事件已结算：" + choice.label, 1);
    ZL_INFO("world-events", "server world event choice: {} ({}) by {}",
            choice.label, eventId, session.playerStableId);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ServerHost::HandleStrategicRequest(std::uint64_t sessionId,
                                        SessionPlayer& session,
                                        const net::StrategicRequest& request,
                                        std::string* error) {
    ++m_networkStats.strategicRequestsReceived;
    auto finish = [&](bool accepted, std::string reason) {
        net::StrategicRequestResult result;
        result.sequence = request.sequence;
        result.action = request.action;
        result.regionId = request.regionId;
        result.accepted = accepted;
        result.reason = std::move(reason);
        if (accepted) {
            m_strategicDirty = true;
        } else {
            ++m_networkStats.strategicRequestsRejected;
        }
        std::string encodeError;
        const std::vector<std::uint8_t> bytes =
            net::EncodeStrategicResult(result, &encodeError);
        if (!bytes.empty() && m_network != nullptr) {
            std::string sendError;
            if (!m_network->Send(sessionId, bytes, &sendError)) {
                ZL_WARN("strategic", "session={} 战略结果发送失败：{}", sessionId,
                        sendError);
            }
        }
        SetSessionStatus(session, "战略 · " + result.reason,
                         accepted ? 1 : 2);
        if (error != nullptr) {
            *error = result.reason;
        }
        return accepted;
    };

    if (request.sequence <= session.lastStrategicRequestSequence) {
        ++m_networkStats.strategicRequestsRateLimited;
        return finish(false, "重复或过期的战略请求");
    }
    session.lastStrategicRequestSequence = request.sequence;
    if (session.strategicRequestsThisTick >= 4) {
        ++m_networkStats.strategicRequestsRateLimited;
        return finish(false, "战略请求过于频繁");
    }
    ++session.strategicRequestsThisTick;
    if (!session.spawned || session.dead || m_simulation == nullptr ||
        m_simulation->Graph() == nullptr) {
        return finish(false, "玩家尚未在场");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return finish(false, "玩家实体不存在");
    }
    const auto* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return finish(false, "玩家位置不可用");
    }
    const world::WorldRegion* worldRegion =
        m_simulation->Graph()->RegionAt(position->x, position->y);
    if (worldRegion == nullptr || worldRegion->id != request.regionId) {
        return finish(false, "不在目标战略区域");
    }
    const game::StrategicRegionState* region =
        m_simulation->Strategic() != nullptr
            ? m_simulation->Strategic()->FindRegion(request.regionId)
            : nullptr;
    if (region == nullptr) {
        return finish(false, "战略区域不存在");
    }
    if (m_strategic != nullptr) {
        const game::StrategicRegionConfig* def =
            m_strategic->Find(request.regionId);
        if (def != nullptr && def->hasAnchor) {
            const float dx = position->x - def->anchorX;
            const float dy = position->y - def->anchorY;
            if (dx * dx + dy * dy > 4.0f * 4.0f) {
                return finish(false, "离战略锚点太远");
            }
        }
    }

    const game::StrategicContributionConfig* contributionConfig =
        m_simulation->StrategicSettings() != nullptr
            ? &m_simulation->StrategicSettings()->contribution
            : nullptr;
    std::string actionError;
    bool accepted = false;
    int contribution = 0;
    switch (request.action) {
        case net::StrategicRequestAction::DonateSupply: {
            int delivered = 0;
            accepted = m_simulation->DonateStrategicSupply(
                request.regionId, request.amount, session.playerStableId,
                &actionError, &delivered);
            if (accepted && contributionConfig != nullptr) {
                contribution = delivered * contributionConfig->donatePerSupply;
            }
            break;
        }
        case net::StrategicRequestAction::Reclaim:
            accepted = m_simulation->StartRegionalReclaim(request.regionId);
            if (!accepted) {
                actionError = "当前阶段不能反攻";
            } else if (contributionConfig != nullptr) {
                contribution = contributionConfig->reclaim;
            }
            break;
        case net::StrategicRequestAction::ClearCore:
            accepted = m_simulation->ClearRegionalRecoveryCore(
                request.regionId, &actionError);
            if (accepted && contributionConfig != nullptr) {
                contribution = contributionConfig->clearCore;
            }
            break;
        case net::StrategicRequestAction::RepairFacility:
            accepted = m_simulation->RepairRegionalRecoveryFacility(
                request.regionId, &actionError);
            if (accepted && contributionConfig != nullptr) {
                contribution = contributionConfig->repairFacility;
            }
            break;
    }
    if (!accepted &&
        actionError.empty()) {
        actionError = "战略请求被拒绝";
    }
    if (accepted) {
        AddStrategicContribution(session.playerStableId, request.regionId,
                                 contribution);
        ZL_INFO("strategic", "玩家 {} 执行战略 {}：{}，贡献 +{}",
                session.playerStableId,
                net::StrategicRequestActionName(request.action),
                request.regionId, contribution);
    }
    const std::string reason =
        accepted
            ? (contribution > 0
                   ? "已执行 · 贡献 +" + std::to_string(contribution)
                   : "已执行")
            : actionError;
    return finish(accepted, reason);
}

void ServerHost::AddStrategicContribution(std::uint64_t stableId,
                                          std::string_view regionId,
                                          int points) {
    if (stableId == 0 || regionId.empty() || points <= 0) {
        return;
    }
    int& total = m_strategicContributions[stableId][std::string(regionId)];
    total = std::min(total + points, 1000000);
}

void ServerHost::RewardStrategicContributors(
    const game::StrategicRegionState& region) {
    if (m_simulation == nullptr) {
        return;
    }
    const game::StrategicConfig* config =
        m_simulation->StrategicSettings();
    const int rewardPool =
        config != nullptr ? config->contribution.rewardXp : 0;
    const int minContribution =
        config != nullptr ? config->contribution.minContribution : 1;

    struct Share {
        SessionPlayer* session = nullptr;
        std::uint64_t stableId = 0;
        int contribution = 0;
        int xp = 0;
        std::int64_t remainder = 0;
    };
    std::vector<Share> shares;
    std::int64_t totalContribution = 0;
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0 || session.dead) {
            continue;
        }
        const auto playerIt =
            m_strategicContributions.find(session.playerStableId);
        if (playerIt == m_strategicContributions.end()) {
            continue;
        }
        const auto regionIt = playerIt->second.find(region.id);
        if (regionIt == playerIt->second.end() ||
            regionIt->second < minContribution) {
            continue;
        }
        shares.push_back(Share{&session, session.playerStableId,
                               regionIt->second, 0, 0});
        totalContribution += regionIt->second;
    }

    if (totalContribution > 0 && rewardPool > 0) {
        for (Share& share : shares) {
            const std::int64_t weighted =
                static_cast<std::int64_t>(rewardPool) * share.contribution;
            share.xp = static_cast<int>(weighted / totalContribution);
            share.remainder = weighted % totalContribution;
        }
        std::sort(shares.begin(), shares.end(),
                  [](const Share& left, const Share& right) {
                      if (left.remainder != right.remainder) {
                          return left.remainder > right.remainder;
                      }
                      if (left.contribution != right.contribution) {
                          return left.contribution > right.contribution;
                      }
                      return left.stableId < right.stableId;
                  });
        int awarded = 0;
        for (const Share& share : shares) {
            awarded += share.xp;
        }
        int remainder = rewardPool - awarded;
        for (std::size_t i = 0;
             i < shares.size() && remainder > 0; ++i, --remainder) {
            ++shares[i].xp;
        }
        for (const Share& share : shares) {
            if (share.xp <= 0 || share.session == nullptr) {
                continue;
            }
            const ecs::Entity player =
                m_simulation->Ecs().FindById(share.stableId);
            if (player == entt::null) {
                continue;
            }
            AwardProgressionXp(player, ecs::SkillKind::Survival,
                               static_cast<float>(share.xp), share.session);
            SetSessionStatus(
                *share.session,
                "战略贡献奖励：+" + std::to_string(share.xp) +
                    " 生存经验",
                1);
            ZL_INFO("strategic",
                    "区域 {} 转 Human，玩家 {} 贡献 {} 获得 {} 生存经验",
                    region.id, share.stableId, share.contribution,
                    share.xp);
        }
    }

    for (auto it = m_strategicContributions.begin();
         it != m_strategicContributions.end();) {
        it->second.erase(region.id);
        if (it->second.empty()) {
            it = m_strategicContributions.erase(it);
        } else {
            ++it;
        }
    }
}

void ServerHost::RewardSiegeContributors(
    std::string_view regionId,
    const std::unordered_map<std::uint64_t, int>& points, int rewardPool) {
    if (m_simulation == nullptr || rewardPool <= 0 || points.empty()) {
        return;
    }
    struct Share {
        SessionPlayer* session = nullptr;
        ecs::Entity player = entt::null;
        std::uint64_t stableId = 0;
        int contribution = 0;
        int xp = 0;
        std::int64_t remainder = 0;
    };
    std::vector<Share> shares;
    std::int64_t totalContribution = 0;
    for (const auto& [stableId, contribution] : points) {
        if (contribution <= 0) {
            continue;
        }
        SessionPlayer* session = FindSessionByPlayerStableId(stableId);
        if (session != nullptr && session->dead) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(stableId);
        if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
            continue;
        }
        if (session == nullptr && player != m_simulation->Player()) {
            continue;
        }
        shares.push_back(
            Share{session, player, stableId, contribution, 0, 0});
        totalContribution += contribution;
    }
    if (totalContribution <= 0) {
        return;
    }

    for (Share& share : shares) {
        const std::int64_t weighted =
            static_cast<std::int64_t>(rewardPool) * share.contribution;
        share.xp = static_cast<int>(weighted / totalContribution);
        share.remainder = weighted % totalContribution;
    }
    std::sort(shares.begin(), shares.end(),
              [](const Share& left, const Share& right) {
                  if (left.remainder != right.remainder) {
                      return left.remainder > right.remainder;
                  }
                  if (left.contribution != right.contribution) {
                      return left.contribution > right.contribution;
                  }
                  return left.stableId < right.stableId;
              });
    int awarded = 0;
    for (const Share& share : shares) {
        awarded += share.xp;
    }
    int remainder = rewardPool - awarded;
    for (std::size_t i = 0; i < shares.size() && remainder > 0;
         ++i, --remainder) {
        ++shares[i].xp;
    }

    for (const Share& share : shares) {
        if (share.xp <= 0) {
            continue;
        }
        AwardProgressionXp(share.player, ecs::SkillKind::Survival,
                           static_cast<float>(share.xp), share.session);
        if (share.session != nullptr) {
            SetSessionStatus(
                *share.session,
                "守城贡献奖励：+" + std::to_string(share.xp) +
                    " 生存经验",
                1);
        }
        ZL_INFO("strategic",
                "守城区域 {} 玩家 {} 伤害贡献 {} 获得 {} 生存经验",
                regionId, share.stableId, share.contribution, share.xp);
    }

    std::string report = "守城战报 · " + std::string(regionId) + " · 守住";
    int shown = 0;
    for (const Share& share : shares) {
        if (share.xp <= 0 || shown >= 3) {
            continue;
        }
        std::string name = share.session != nullptr
                               ? share.session->playerName
                               : "本地玩家";
        if (name.size() > 16) {
            name.resize(16);
        }
        report += " · " + name + " 贡献+" +
                  std::to_string(share.contribution) + " 经验+" +
                  std::to_string(share.xp);
        ++shown;
    }
    if (shown == 0) {
        return;
    }
    if (report.size() > 480) {
        report.resize(480);
    }
    PublishSiegeReport(report);
}

void ServerHost::ProcessRegionalSiegeResult() {
    if (m_simulation == nullptr) {
        return;
    }
    const game::RegionalSiegeResult& result =
        m_simulation->RegionalSiegeLastResult();
    if (result.regionId.empty()) {
        return;
    }
    const std::string key =
        result.regionId + ":" + std::to_string(result.attackMinute) + ":" +
        std::to_string(result.resolvedMinute) + ":" +
        (result.reclaim ? "r" : "d") + (result.held ? "h" : "l");
    if (key == m_lastSiegeResultKey) {
        return;
    }
    m_lastSiegeResultKey = key;

    const game::StrategicState* strategic = m_simulation->Strategic();
    const game::StrategicConfig* settings = m_simulation->StrategicSettings();
    world::WorldGraph* graph = m_simulation->Graph();
    const game::StrategicRegionState* region =
        strategic != nullptr ? strategic->FindRegion(result.regionId)
                             : nullptr;
    std::unordered_map<std::uint64_t, int> damagePoints;
    if (settings != nullptr) {
        const int damagePerPoint =
            std::max(1, settings->contribution.defenseDamagePerPoint);
        for (const auto& [stableId, damage] : m_siegeDefenseDamage) {
            const int points = static_cast<int>(
                std::ceil(damage / static_cast<float>(damagePerPoint)));
            if (points > 0) {
                damagePoints[stableId] += points;
            }
        }
    }
    const auto previous = m_lastStrategicOwners.find(result.regionId);
    const bool humanDefense =
        result.held && !result.reclaim && result.wavesSpawned > 0 &&
        region != nullptr && region->owner == game::StrategicOwner::Human &&
        previous != m_lastStrategicOwners.end() &&
        previous->second == game::StrategicOwner::Human;
    if (!humanDefense) {
        if (region != nullptr) {
            for (const auto& [stableId, points] : damagePoints) {
                AddStrategicContribution(stableId, result.regionId, points);
            }
        }
ResetSiegeDefenseLedger();
        return;
    }

    const game::SettlementDef* defenseSettlement =
        m_settlements != nullptr && !result.settlementId.empty()
            ? m_settlements->Find(result.settlementId)
            : nullptr;
    const std::string reputationKey =
        "defense:" + result.settlementId + ":" + key;
    const auto awardDefenseReputation = [&](std::uint64_t stableId) {
        if (defenseSettlement == nullptr) {
            return;
        }
        const ecs::Entity player = m_simulation->Ecs().FindById(stableId);
        if (player == entt::null) {
            return;
        }
        (void)game::AwardSettlementReputation(
            m_simulation->Ecs(), player, m_settlements->reputation,
            defenseSettlement->id, game::SettlementReputationEvent::DefenseComplete,
            reputationKey, defenseSettlement->startingReputation,
            m_gameTime != nullptr
                ? 1 + static_cast<int>(
                          m_gameTime->TotalGameMinutes() / (24.0 * 60.0))
                : 1);
    };

    const int rewardPool =
        settings != nullptr ? settings->contribution.defenseRewardXp : 0;
    if (!damagePoints.empty()) {
        for (const auto& [stableId, points] : damagePoints) {
            if (points > 0) {
                awardDefenseReputation(stableId);
            }
        }
        RewardSiegeContributors(result.regionId, damagePoints, rewardPool);
ResetSiegeDefenseLedger();
        return;
    }
    if (rewardPool <= 0 || graph == nullptr) {
ResetSiegeDefenseLedger();
        return;
    }

    std::vector<ecs::Entity> participants;
    std::vector<std::uint64_t> participantIds;
    const auto consider = [&](ecs::Entity entity) {
        if (entity == entt::null || !m_simulation->Ecs().Alive(entity)) {
            return;
        }
        const auto* position =
            m_simulation->Ecs().Registry().try_get<ecs::Position>(entity);
        if (position == nullptr) {
            return;
        }
        const world::WorldRegion* worldRegion =
            graph->RegionAt(position->x, position->y);
        if (worldRegion == nullptr || worldRegion->id != result.regionId) {
            return;
        }
        const std::uint64_t stableId = m_simulation->Ecs().IdOf(entity).id;
        if (std::find(participantIds.begin(), participantIds.end(),
                      stableId) != participantIds.end()) {
            return;
        }
        participantIds.push_back(stableId);
        participants.push_back(entity);
    };
    if (m_simulation->Player() != entt::null) {
        consider(m_simulation->Player());
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.dead || session.playerStableId == 0) {
            continue;
        }
        consider(m_simulation->Ecs().FindById(session.playerStableId));
    }
    if (participants.empty()) {
ResetSiegeDefenseLedger();
        return;
    }
    std::sort(participants.begin(), participants.end(),
              [&](ecs::Entity left, ecs::Entity right) {
                  return m_simulation->Ecs().IdOf(left).id <
                         m_simulation->Ecs().IdOf(right).id;
              });

    const int count = static_cast<int>(participants.size());
    const int base = rewardPool / count;
    const int remainder = rewardPool % count;
    for (int i = 0; i < count; ++i) {
        const int amount = base + (i < remainder ? 1 : 0);
        if (amount <= 0) {
            continue;
        }
        const ecs::Entity player = participants[static_cast<std::size_t>(i)];
        const std::uint64_t stableId = m_simulation->Ecs().IdOf(player).id;
        awardDefenseReputation(stableId);
        SessionPlayer* session = FindSessionByPlayerStableId(stableId);
        AwardProgressionXp(player, ecs::SkillKind::Survival,
                           static_cast<float>(amount), session);
        if (session != nullptr) {
            SetSessionStatus(*session,
                             "守城奖励：+" + std::to_string(amount) +
                                 " 生存经验",
                             1);
        }
    }
    ZL_INFO("strategic",
            "围攻击退：{}，{} 名在场玩家分享 {} 生存经验", result.regionId,
            count, rewardPool);
    PublishSiegeReport(
        "守城战报 · " + result.regionId + " · 守住 · " +
        std::to_string(count) + " 人在场均分 " +
        std::to_string(rewardPool) + " 经验");
ResetSiegeDefenseLedger();
}

void ServerHost::UpdateStrategicContributionRewards() {
    if (m_simulation == nullptr) {
        return;
    }
    const game::StrategicState* strategic = m_simulation->Strategic();
    if (strategic == nullptr) {
        m_lastStrategicOwners.clear();
        return;
    }
    if (m_lastStrategicOwners.empty()) {
        for (const game::StrategicRegionState& region : strategic->regions) {
            m_lastStrategicOwners[region.id] = region.owner;
        }
        return;
    }
    for (const game::StrategicRegionState& region : strategic->regions) {
        auto it = m_lastStrategicOwners.find(region.id);
        if (it == m_lastStrategicOwners.end()) {
            m_lastStrategicOwners[region.id] = region.owner;
            continue;
        }
        if (it->second != game::StrategicOwner::Human &&
            region.owner == game::StrategicOwner::Human) {
            RewardStrategicContributors(region);
        }
        it->second = region.owner;
    }
}

void ServerHost::BroadcastSnapshots() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_replicationRegistry == nullptr || m_ticks == 0) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0) {
            continue;
        }

        if (!session.assignmentSent) {
            net::PlayerAssignment assignment;
            assignment.sessionId = sessionId;
            assignment.playerStableId = session.playerStableId;
            assignment.serverTick = m_ticks;
            std::string error;
            const std::vector<std::uint8_t> bytes =
                net::EncodePlayerAssignment(assignment, &error);
            if (bytes.empty()) {
                ZL_WARN("net", "session={} 玩家分配消息编码失败：{}", sessionId, error);
                continue;
            }
            if (!m_network->Send(sessionId, bytes, &error)) {
                ZL_WARN("net", "session={} 玩家分配消息发送失败：{}", sessionId, error);
                continue;
            }
            session.assignmentSent = true;
            ++m_networkStats.assignmentsSent;
        }

        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null || session.producer == nullptr) {
            continue;
        }
        const auto* position =
            m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
        if (position == nullptr) {
            continue;
        }

        net::ReplicationSnapshot snapshot;
        std::string error;
        if (session.needsFullSnapshot || !session.producer->HasBaseline()) {
            snapshot = session.producer->BuildFull(m_ticks, position->x, position->y,
                                                   &error);
        } else {
            if (!session.producer->BuildDelta(m_ticks, position->x, position->y,
                                              &snapshot, &error)) {
                ++m_networkStats.snapshotFailures;
                ZL_WARN("net", "session={} 复制增量构建失败，下一 tick 重发全量：{}",
                        sessionId, error);
                session.producer->Reset();
                session.needsFullSnapshot = true;
                continue;
            }
        }
        if (!error.empty()) {
            ++m_networkStats.snapshotFailures;
            ZL_WARN("net", "session={} 复制快照构建失败，下一 tick 重发全量：{}", sessionId,
                    error);
            session.producer->Reset();
            session.needsFullSnapshot = true;
            continue;
        }
        const std::vector<std::uint8_t> bytes =
            net::EncodeReplicationSnapshot(snapshot, *m_replicationRegistry, &error);
        if (bytes.empty()) {
            ++m_networkStats.snapshotFailures;
            ZL_WARN("net", "session={} 复制快照编码失败，下一 tick 重发全量：{}", sessionId,
                    error);
            session.producer->Reset();
            session.needsFullSnapshot = true;
            continue;
        }
        if (!m_network->Send(sessionId, bytes, &error)) {
            ++m_networkStats.snapshotFailures;
            ZL_WARN("net", "session={} 复制快照发送失败，下一 tick 重发全量：{}", sessionId,
                    error);
            session.producer->Reset();
            session.needsFullSnapshot = true;
            continue;
        }
        session.needsFullSnapshot = false;
        ++m_networkStats.snapshotsSent;
    }
}

void ServerHost::BroadcastStructureDiffs() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_ticks == 0) {
        return;
    }
    PruneStructureTombstones();
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0 || !session.assignmentSent) {
            continue;
        }
        if (session.lastStructureSyncTick != 0 &&
            m_ticks - session.lastStructureSyncTick <
                m_structureSyncIntervalTicks) {
            continue;
        }
        const ecs::Entity sessionPlayer =
            m_simulation->Ecs().FindById(session.playerStableId);
        const auto* sessionPosition =
            sessionPlayer != entt::null
                ? m_simulation->Ecs().Registry().try_get<ecs::Position>(
                      sessionPlayer)
                : nullptr;
        if (sessionPosition == nullptr) {
            continue;
        }
        session.lastStructureSyncTick = m_ticks;
        bool sendFailed = false;
        std::string error;
        const auto sendDiff = [&](const save::ChunkDiff& one) {
            if (one.squares.empty() || sendFailed) {
                return;
            }
            const std::vector<std::uint8_t> bytes =
                save::EncodeChunkDiff(one, &error);
            if (bytes.empty()) {
                ++m_networkStats.structureDiffFailures;
                ZL_WARN("net", "会话 {} 编码单格 chunk diff 失败：{}", sessionId,
                        error);
                return;
            }
            if (!m_network->Send(sessionId, bytes, &error)) {
                ++m_networkStats.structureDiffFailures;
                ZL_WARN("net", "会话 {} 发送 chunk diff 失败：{}", sessionId,
                        error);
                sendFailed = true;
                return;
            }
            ++m_networkStats.structureDiffsSent;
        };

        m_simulation->Chunks().ForEachLoadedChunk(
            [&](const world::Chunk& chunk) {
                if (sendFailed) {
                    return;
                }
                if (!ChunkWithinInterest(chunk, sessionPosition->x,
                                         sessionPosition->y,
                                         m_structureInterestRadius)) {
                    ++m_networkStats.structureChunksFiltered;
                    return;
                }
                save::ChunkDiff diff;
                if (!save::CaptureChunkDiff(chunk, &diff, &error)) {
                    ++m_networkStats.structureDiffFailures;
                    ZL_WARN("net", "会话 {} 采集 chunk diff 失败：({}, {})：{}",
                            sessionId, chunk.ChunkX(), chunk.ChunkY(), error);
                    return;
                }
                for (const save::ChunkDiff& one :
                     save::SplitChunkDiffBySquare(diff)) {
                    sendDiff(one);
                }
            });

        for (const StructureTombstone& tombstone : m_structureTombstones) {
            if (sendFailed) {
                break;
            }
            if (tombstone.expiresTick < m_ticks) {
                continue;
            }
            const world::Chunk* chunk =
                FindServerLoadedChunk(tombstone.wx, tombstone.wy);
            if (chunk == nullptr ||
                !ChunkWithinInterest(*chunk, sessionPosition->x,
                                     sessionPosition->y,
                                     m_structureInterestRadius)) {
                continue;
            }
            const world::SquareLocal local =
                world::SquareLocalOf(tombstone.wx, tombstone.wy);
            save::ChunkDiff one;
            if (!save::CaptureChunkSquareDiff(*chunk, local.lx, local.ly,
                                              tombstone.z, &one, &error)) {
                ++m_networkStats.structureDiffFailures;
                ZL_WARN("net", "会话 {} 采集墓碑格失败：({}, {})：{}", sessionId,
                        tombstone.wx, tombstone.wy, error);
                continue;
            }
            sendDiff(one);
        }
    }
}

void ServerHost::SetSessionStatus(SessionPlayer& session, std::string message,
                                  std::uint8_t tone) {
    session.status = std::move(message);
    session.statusTone = std::min<std::uint8_t>(tone, 2);
    ++session.statusSequence;
    session.stateDirty = true;
}

bool ServerHost::SetServerStealth(SessionPlayer& session, ecs::Entity player,
                                  bool crouched, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!m_stealthRules.enabled) {
        return fail("服务器未启用潜行");
    }
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return fail("权威玩家实体不存在");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* health = registry.try_get<ecs::Health>(player);
    if (health == nullptr || !std::isfinite(health->current) ||
        health->current <= 0.0f || registry.all_of<ecs::Dying>(player)) {
        return fail("倒地/死亡状态无法切换潜行");
    }
    if (session.drivingVehicleId != 0 ||
        registry.all_of<ecs::VehicleOccupant>(player)) {
        return fail("驾驶中无法切换潜行");
    }
    auto& state = registry.get_or_emplace<ecs::StealthState>(player);
    const characters::StealthScales scales =
        characters::StealthScalesFor(crouched, m_stealthRules);
    state.crouched = crouched;
    state.speedScale = scales.speedScale;
    state.noiseScale = scales.noiseScale;
    state.visibilityScale = scales.visibilityScale;
    session.stealthHealth = crouched ? health->current : -1.0f;
    session.stateDirty = true;
    ZL_INFO("stealth", "服务器潜行切换：玩家 {} {}（速度 x{:.2f}，噪声 x{:.2f}，可视 x{:.2f}）",
            session.playerStableId, crouched ? "进入" : "退出",
            static_cast<double>(state.speedScale),
            static_cast<double>(state.noiseScale),
            static_cast<double>(state.visibilityScale));
    return true;
}

void ServerHost::ResetServerStealth(SessionPlayer& session, ecs::Entity player,
                                    const char* reason) {
    session.stealthHealth = -1.0f;
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return;
    }
    auto* state =
        m_simulation->Ecs().Registry().try_get<ecs::StealthState>(player);
    if (state == nullptr) {
        return;
    }
    const bool wasCrouched = state->crouched;
    state->crouched = false;
    state->speedScale = 1.0f;
    state->noiseScale = 1.0f;
    state->visibilityScale = 1.0f;
    if (wasCrouched) {
        session.stateDirty = true;
        ZL_INFO("stealth", "服务器退出潜行：玩家 {}（{}）",
                session.playerStableId,
                reason != nullptr ? reason : "状态变化");
    }
}

void ServerHost::UpdateServerStealth() {
    if (m_simulation == nullptr) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0 || !session.spawned) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        entt::registry& registry = m_simulation->Ecs().Registry();
        auto* state = registry.try_get<ecs::StealthState>(player);
        if (state == nullptr) {
            session.stealthHealth = -1.0f;
            continue;
        }
        const auto* health = registry.try_get<ecs::Health>(player);
        const float healthNow =
            health != nullptr && std::isfinite(health->current)
                ? health->current
                : -1.0f;
        if (!state->crouched) {
            session.stealthHealth = healthNow;
            continue;
        }
        const char* reason = nullptr;
        if (healthNow <= 0.0f || registry.all_of<ecs::Dying>(player)) {
            reason = "倒地/死亡";
        } else if (session.drivingVehicleId != 0 ||
                   registry.all_of<ecs::VehicleOccupant>(player)) {
            reason = "进入载具";
        } else if (session.stealthHealth >= 0.0f &&
                   healthNow >= 0.0f &&
                   healthNow < session.stealthHealth - 0.01f) {
            reason = "受到伤害";
        }
        session.stealthHealth = healthNow;
        if (reason != nullptr) {
            ResetServerStealth(session, player, reason);
        }
    }
}

void ServerHost::FillPlayerState(const SessionPlayer& session,
                                 net::PlayerState* state) {
    if (state == nullptr) {
        return;
    }
    *state = {};
    state->playerStableId = session.playerStableId;
    state->serverTick = m_ticks;
    state->alive = !session.dead;
    if (m_profilesEnabled && m_playerProfiles != nullptr) {
        if (const PlayerProfile* profile =
                m_playerProfiles->Find(session.identity);
            profile != nullptr) {
            *state = profile->state;
            state->playerStableId = session.playerStableId;
            state->serverTick = m_ticks;
            state->alive = !session.dead;
        }
    }
    if (session.dead) {
        state->health = 0.0f;
        const std::uint64_t remainingTicks =
            session.respawnAtTick > m_ticks ? session.respawnAtTick - m_ticks : 0;
        state->respawnSeconds = static_cast<float>(remainingTicks) /
                                std::max(1.0f, m_options.tickRate);
    } else {
        state->respawnSeconds = 0.0f;
    }

    state->siege = {};
    const game::BaseSiegePhase siegePhase = m_serverBaseSiege.Phase();
    state->siege.phase = static_cast<std::uint8_t>(siegePhase);
    const bool siegeActive = m_serverBaseSiege.Active();
    const bool siegeNormalEnded =
        siegePhase == game::BaseSiegePhase::Finished &&
        m_serverBaseSiege.Result().rating != '-';
    if (siegeActive || siegeNormalEnded) {
        const game::BaseSiegeAttackPlan& plan =
            m_serverBaseSiege.AttackPlan();
        state->siege.primaryDirection =
            static_cast<std::int8_t>(std::clamp(plan.primaryDirection, -1, 7));
        state->siege.secondaryDirection = static_cast<std::int8_t>(
            std::clamp(plan.secondaryDirection, -1, 7));
        state->siege.currentDirection = static_cast<std::int8_t>(
            std::clamp(plan.currentDirection, -1, 7));
        state->siege.breached = m_serverBaseSiege.Breached();
        state->siege.waveIndex = static_cast<std::uint8_t>(
            std::clamp(m_serverBaseSiege.WaveIndex(), 0, 99));
        state->siege.waveCount = static_cast<std::uint8_t>(
            std::clamp(m_serverBaseSiege.WaveCount(), 0, 99));
        state->siege.targetValid = siegeActive && plan.hasTarget;
        if (state->siege.targetValid) {
            state->siege.targetX = plan.targetX;
            state->siege.targetY = plan.targetY;
        }
        state->siege.breachValid = m_serverBaseSiege.Breached();
        if (state->siege.breachValid) {
            const game::BaseSiegePoint& breach =
                m_serverBaseSiege.BreachPoint();
            state->siege.breachX = breach.x;
            state->siege.breachY = breach.y;
        }
        if (siegeActive) {
            state->siege.status = m_serverBaseSiege.HudText();
            const std::string& tone = m_serverBaseSiege.HudTone();
            state->siege.tone = tone == "fail" ? 2 : (tone == "warn" ? 1 : 0);
        }
    }
    state->power = {};
    state->power.fuelMinutes = m_serverPowerSummary.fuelMinutes;
    state->power.load = m_serverPowerSummary.load;
    state->power.lowFuel = m_serverPowerSummary.lowFuel;
    state->power.outage = m_serverPowerSummary.outage;

    state->survival = {};
    state->survival.mode = session.survival.mode;
    state->survival.phase = session.survival.phase;
    state->survival.name = session.survival.name;
    state->survival.zoneId = session.survival.zoneId;
    state->survival.tableId = session.survival.tableId;
    state->survival.spotId = session.survival.spotId;
    state->survival.x = session.survival.x;
    state->survival.y = session.survival.y;
    state->survival.z = session.survival.z;
    state->survival.waterX = session.survival.waterX;
    state->survival.waterY = session.survival.waterY;
    state->survival.waterZ = session.survival.waterZ;
    state->survival.duration = session.survival.duration;
    if (session.survival.duration > 0.0f) {
        state->survival.progress = std::clamp(
            session.survival.elapsed / session.survival.duration, 0.0f, 1.0f);
    }
    if (session.survival.phase == net::kPlayerSurvivalPhaseBiting) {
        state->survival.progress = 1.0f;
        state->survival.biteRemaining =
            std::max(0.0f, session.survival.biteDuration -
                               session.survival.biteElapsed);
    }
    state->animalHarvest = {};
    state->animalHarvest.active = session.animalHarvest.active;
    if (session.animalHarvest.active) {
        state->animalHarvest.corpseStableId =
            session.animalHarvest.corpseStableId;
        state->animalHarvest.speciesId = session.animalHarvest.speciesId;
        state->animalHarvest.name = session.animalHarvest.name;
        state->animalHarvest.x =
            static_cast<int>(std::floor(session.animalHarvest.x));
        state->animalHarvest.y =
            static_cast<int>(std::floor(session.animalHarvest.y));
        state->animalHarvest.z = session.animalHarvest.z;
        state->animalHarvest.duration = session.animalHarvest.duration;
        if (session.animalHarvest.duration > 0.0f) {
            state->animalHarvest.progress = std::clamp(
                session.animalHarvest.elapsed /
                    session.animalHarvest.duration,
                0.0f, 1.0f);
        }
    }
    state->forageNodes = session.nearbyForage;
    state->fishingSpots = session.nearbyFishing;

    // M25-T3/M26：owner-only 载具乘坐态；车辆世界位置仍只走 ZLR1。
    state->vehicle = {};
    const ServerVehicleRuntime* vehicle =
        FindServerVehicle(session.drivingVehicleId);
    if (vehicle == nullptr && m_simulation != nullptr &&
        session.playerStableId != 0) {
        const ecs::Entity statePlayer =
            m_simulation->Ecs().FindById(session.playerStableId);
        const auto* statePosition =
            statePlayer != entt::null
                ? m_simulation->Ecs().Registry().try_get<ecs::Position>(
                      statePlayer)
                : nullptr;
        if (statePosition != nullptr) {
            vehicle = FindNearestServerVehicle(
                statePosition->x, statePosition->y, statePosition->z, 16.0f);
        }
    }
    if (vehicle == nullptr && !m_serverVehicles.empty()) {
        vehicle = &m_serverVehicles.front();
    }
    if (vehicle != nullptr && vehicle->stableId != 0 &&
        m_simulation != nullptr &&
        m_simulation->Ecs().Alive(vehicle->entity)) {
        state->vehicle.valid = true;
        state->vehicle.vehicleStableId = vehicle->stableId;
        state->vehicle.driving =
            !session.dead &&
            vehicle->driverStableId == session.playerStableId;
        state->vehicle.passenger =
            !session.dead &&
            IsServerVehiclePassengerStableId(*vehicle,
                                             session.playerStableId);
        state->vehicle.fuel =
            std::clamp(vehicle->motion.fuel, 0.0f, 1.0f);
        state->vehicle.condition =
            std::clamp(vehicle->motion.condition, 0.0f, 1.0f);
        state->vehicle.vehicleType =
            static_cast<std::uint8_t>(vehicle->motion.type);
        state->vehicle.heading =
            game::VehicleNormalizeAngle(vehicle->motion.heading);
    }

    const ecs::Entity player = m_simulation->Ecs().FindById(session.playerStableId);
    state->combat = {};
    if (player == entt::null) {
        state->statusSequence = session.statusSequence;
        state->statusTone = session.statusTone;
        state->status = session.status;
        return;
    }

    entt::registry& registry = m_simulation->Ecs().Registry();
    if (const auto* health = registry.try_get<ecs::Health>(player);
        health != nullptr) {
        state->health = std::clamp(
            std::isfinite(health->current) ? health->current : 0.0f, 0.0f,
            health->max);
        state->healthMax = health->max;
    }
    const auto* stamina = registry.try_get<ecs::CombatStamina>(player);
    const auto* stance = registry.try_get<ecs::CombatStance>(player);
    if (stamina != nullptr || stance != nullptr) {
        state->combat.valid = true;
        if (stamina != nullptr) {
            state->combat.stamina =
                std::max(0.0f, std::isfinite(stamina->current)
                                   ? stamina->current
                                   : 0.0f);
            state->combat.staminaMax =
                std::max(1.0f, std::isfinite(stamina->max) ? stamina->max
                                                           : 1.0f);
            state->combat.stamina =
                std::min(state->combat.stamina, state->combat.staminaMax);
        }
        if (stance != nullptr) {
            const float dodgeLeft =
                std::isfinite(stance->dodgeLeft)
                    ? std::max(0.0f, stance->dodgeLeft)
                    : 0.0f;
            state->combat.stance =
                dodgeLeft > 0.0f
                    ? net::kPlayerCombatStanceDodge
                    : (stance->blocking ? net::kPlayerCombatStanceBlocking
                                        : net::kPlayerCombatStanceNone);
            state->combat.guardBreakLeft =
                std::isfinite(stance->guardBreakLeft)
                    ? std::max(0.0f, stance->guardBreakLeft)
                    : 0.0f;
            state->combat.dodgeInvulnLeft =
                std::isfinite(stance->dodgeInvulnLeft)
                    ? std::max(0.0f, stance->dodgeInvulnLeft)
                    : 0.0f;
            if (stance->blocking &&
                state->combat.stance == net::kPlayerCombatStanceBlocking) {
                const float window =
                    m_simulation->Combat() != nullptr
                        ? characters::ParryWindowSeconds(
                              m_simulation->Combat()->GetCombatRules())
                        : 0.0f;
                const float elapsed =
                    std::isfinite(stance->blockElapsed)
                        ? std::max(0.0f, stance->blockElapsed)
                        : window;
                state->combat.parryWindowLeft =
                    std::max(0.0f, window - elapsed);
            }
        }
    }
    if (const auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        state->capacityKg = container->capacityKg;
        state->bag.clear();
        state->bag.reserve(container->items.size());
        for (const ecs::ItemInstance& item : container->items) {
            state->bag.push_back(
                net::PlayerStateItem{item.defId, item.count, item.condition});
        }
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        state->equipment = {};
        for (std::size_t i = 0;
             i < equipment->slots.size() && i < state->equipment.size(); ++i) {
            if (!equipment->slots[i].has_value()) {
                continue;
            }
            const ecs::ItemInstance& item = *equipment->slots[i];
            state->equipment[i].present = true;
            state->equipment[i].item =
                net::PlayerStateItem{item.defId, item.count, item.condition};
            state->equipment[i].loadedAmmo =
                std::clamp(item.loadedAmmo, 0, 9999);
        }
    }

    state->ranged = {};
    const auto* equipped = registry.try_get<ecs::Equipment>(player);
    const auto* primarySlot =
        equipped != nullptr
            ? &equipped->slots[static_cast<std::size_t>(
                  ecs::EquipSlot::PrimaryHand)]
            : nullptr;
    const items::ItemDef* primaryDef =
        (primarySlot != nullptr && primarySlot->has_value() && m_items != nullptr)
            ? m_items->Find((*primarySlot)->defId)
            : nullptr;
    if (!session.dead && primaryDef != nullptr &&
        primaryDef->ranged.has_value()) {
        const items::RangedCombat& ranged = *primaryDef->ranged;
        state->ranged.valid = true;
        state->ranged.magazine = std::max(1, ranged.magazine);
        state->ranged.loaded =
            std::clamp((*primarySlot)->loadedAmmo, 0, state->ranged.magazine);
        state->ranged.reloading = session.rangedWeapon.reloading;
        state->ranged.reloadAmount = std::clamp(
            session.rangedWeapon.reloadAmount, 0, state->ranged.magazine);
        state->ranged.shotSerial =
            static_cast<std::uint32_t>(session.rangedShotSerial);
        if (const auto* bag = registry.try_get<ecs::Container>(player);
            bag != nullptr) {
            state->ranged.reserve =
                std::clamp(items::CountOf(*bag, ranged.ammoDefId), 0, 9999);
        }
        if (session.rangedWeapon.reloading) {
            const float duration = std::max(0.01f, ranged.reloadSeconds);
            state->ranged.reloadDuration = duration;
            state->ranged.reloadElapsed =
                std::clamp(duration - session.rangedWeapon.reloadLeft, 0.0f,
                           duration);
        }
    }
    if (const auto* known = registry.try_get<ecs::KnownRecipes>(player);
        known != nullptr) {
        state->knownRecipes = known->recipeIds;
    }
    if (const auto* job = registry.try_get<ecs::CraftingJob>(player);
        job != nullptr) {
        state->craftingActive = job->active;
        state->craftingRecipeId = job->active ? job->recipeId : std::string();
        state->craftingElapsed = job->active ? job->elapsed : 0.0f;
        state->craftingDuration = job->active ? job->duration : 0.0f;
        state->craftingToolEfficiency = job->active ? job->toolEfficiency : 1.0f;
        state->batchTotal = job->active ? job->batchTotal : 0;
        state->batchCompleted = job->active ? job->batchCompleted : 0;
        state->craftQueue.clear();
        state->craftQueue.reserve(job->queue.size());
        for (const ecs::CraftQueueItem& entry : job->queue) {
            state->craftQueue.push_back(
                net::PlayerStateCraftQueue{entry.recipeId, entry.count});
        }
    }
    if (const auto* progression = registry.try_get<ecs::Progression>(player);
        progression != nullptr) {
        state->progressionLevel = progression->level;
        state->progressionXp = progression->xp;
        state->progressionXpNext =
            game::CharacterXpToNext(*progression, m_progressionConfig);
        state->perkPoints = progression->perkPoints;
        for (std::size_t i = 0; i < state->skills.size(); ++i) {
            const ecs::SkillKind kind = static_cast<ecs::SkillKind>(i);
            const ecs::SkillProgress& progress = progression->skills[i];
            const game::SkillRules* rules =
                game::FindSkill(m_progressionConfig, kind);
            state->skills[i].id = rules != nullptr ? rules->id : ecs::kSkillIds[i];
            state->skills[i].level = progress.level;
            state->skills[i].xp = progress.xp;
            state->skills[i].xpNext =
                game::SkillXpToNext(*progression, kind, m_progressionConfig);
        }
        state->perks = progression->perks;
        state->perkChoices.clear();
        const std::array<std::string, 3> choices =
            game::PerkChoiceIds(*progression, m_progressionConfig);
        for (const std::string& choice : choices) {
            if (!choice.empty()) {
                state->perkChoices.push_back(choice);
            }
        }
    }
    if (const auto* infection = registry.try_get<ecs::Infection>(player);
        infection != nullptr) {
        state->infectionStage =
            static_cast<std::uint8_t>(infection->stage);
        state->infectionProgress = infection->progress;
        state->infectionTimeToNextStage = infection->timeToNextStage;
        state->infectionSlowStrength = infection->slowStrength;
        state->infectionSlowRemaining = infection->slowRemaining;
        state->infectionSlowDuration = infection->slowDuration;
    }
    if (const auto* climate = registry.try_get<ecs::Climate>(player);
        climate != nullptr) {
        state->climateBodyC = climate->bodyC;
        state->climateWetness = climate->wetness;
    }
    state->needs = {};
    if (const auto* needs = registry.try_get<ecs::Needs>(player);
        needs != nullptr) {
        state->needs.valid = true;
        state->needs.hunger = needs->hunger;
        state->needs.thirst = needs->thirst;
        state->needs.fatigue = needs->fatigue;
        state->needs.warning = needs->warning;
    }
    state->food = {};
    if (const auto* food = registry.try_get<ecs::FoodState>(player);
        food != nullptr) {
        state->food.valid = true;
        state->food.nutrition = food->nutrition;
        state->food.poisonRemaining = food->poisonRemaining;
        state->food.poisonDamagePerSecond = food->poisonDamagePerSecond;
    }
    state->stealth = {};
    if (const auto* stealth = registry.try_get<ecs::StealthState>(player);
        stealth != nullptr) {
        state->stealth.valid = true;
        state->stealth.crouched = stealth->crouched;
        const float noiseScale =
            std::isfinite(stealth->noiseScale)
                ? std::clamp(stealth->noiseScale, 0.0f, 1.0f)
                : 1.0f;
        state->stealth.noiseRadius = 2.5f * noiseScale;
        state->stealth.visibilityScale =
            std::isfinite(stealth->visibilityScale)
                ? std::clamp(stealth->visibilityScale, 0.1f, 1.0f)
                : 1.0f;
    }
    state->poiProgress.clear();
    state->poiStageProgress.clear();
    if (m_poiConfig != nullptr && m_poiConfig->enabled) {
        const auto* tasks = registry.try_get<ecs::PoiTasks>(player);
        const ecs::PoiWorldProgress* world = ServerPoiWorldProgress();
        for (const game::PoiDef& poi : m_poiConfig->defs) {
            const game::PoiProgress* staged =
                world != nullptr
                    ? game::FindPoiProgress(world->pois, poi.id)
                    : nullptr;
            net::PlayerPoiProgress progress;
            progress.id = poi.id;
            progress.completed =
                staged != nullptr
                    ? staged->completed
                    : (tasks != nullptr &&
                       game::IsPoiCompleted(tasks->completed, poi.id));
            progress.targetSearched = ServerPoiTargetSearched(poi);
            progress.liveGuards = CountServerPoiGuards(poi.id);
            state->poiProgress.push_back(std::move(progress));
            if (poi.stages.empty()) {
                continue;
            }
            net::PlayerPoiStageProgress stage;
            stage.id = poi.id;
            stage.stageCount =
                static_cast<std::uint8_t>(poi.stages.size());
            if (staged != nullptr) {
                stage.stageIndex = static_cast<std::uint8_t>(
                    std::max(0, staged->currentStage));
                stage.completedStageCount = static_cast<std::uint8_t>(
                    std::min<std::size_t>(staged->completedStages.size(),
                                          8));
                if (staged->awaitingChoice) {
                    stage.flags |= net::kPlayerPoiStageAwaitingChoice;
                }
                if (staged->failed) {
                    stage.flags |= net::kPlayerPoiStageFailed;
                }
                if (staged->powered) {
                    stage.flags |= net::kPlayerPoiStagePowered;
                }
                if (staged->currentStage >= 0 &&
                    staged->currentStage <
                        static_cast<int>(poi.stages.size())) {
                    stage.currentStage =
                        poi.stages[static_cast<std::size_t>(
                                       staged->currentStage)]
                            .id;
                }
                stage.chosenBranch = staged->chosenBranch;
            }
            state->poiStageProgress.push_back(std::move(stage));
        }
    }
    state->missions.clear();
    if (const auto* log = registry.try_get<ecs::MissionLog>(player);
        log != nullptr) {
        state->missionLastDay = log->lastDay;
        state->missionRandomDay = log->randomDay;
        state->missions.reserve(
            std::min<std::size_t>(log->missions.size(),
                                  net::kMaxPlayerStateMissions));
        for (const ecs::MissionRunState& run : log->missions) {
            if (state->missions.size() >= net::kMaxPlayerStateMissions) {
                break;
            }
            net::PlayerMissionProgress progress;
            progress.id = run.id;
            progress.progress = run.progress;
            progress.status = static_cast<std::uint8_t>(run.status);
            progress.day = run.day;
            progress.rewardClaimed = run.rewardClaimed;
            state->missions.push_back(std::move(progress));
        }
    }
    state->settlements.clear();
    state->settlementNotice.clear();
    state->settlementNoticeTone = 0;
    if (m_settlements != nullptr && m_settlements->enabled &&
        m_simulation != nullptr) {
        if (game::SettlementNpcSystem* system =
                m_simulation->SettlementNpcs();
            system != nullptr) {
            state->settlementNoticeSerial = system->NoticeSerial();
            state->settlementNotice = system->NoticeText();
            state->settlementNoticeTone =
                system->NoticeTone() == "ok"
                    ? 1
                    : (system->NoticeTone() == "fail" ? 2 : 0);
            for (const game::SettlementDef& def : m_settlements->defs) {
                if (state->settlements.size() >=
                    net::kMaxPlayerStateSettlements) {
                    break;
                }
                if (!def.defense.enabled) {
                    continue;
                }
                game::SettlementNpcSystem::DefenseStatus status;
                if (!system->ReadStatus(m_simulation->Ecs(), def.id,
                                        &status)) {
                    continue;
                }
                net::PlayerSettlementProgress progress;
                progress.id = def.id;
                progress.fallen = status.fallen;
                progress.raidActive = status.raidActive;
                progress.guardHealth = status.guardHealth;
                progress.guardHealthMax = status.guardHealthMax;
                state->settlements.push_back(std::move(progress));
            }
        }
    }
    state->settlementReputation.clear();
    if (const auto* reputation =
            registry.try_get<game::SettlementReputation>(player);
        reputation != nullptr) {
        state->settlementReputation.reserve(
            std::min<std::size_t>(reputation->settlements.size(),
                                  net::kMaxPlayerStateSettlementReputation));
        for (const game::SettlementReputationEntry& entry :
             reputation->settlements) {
            if (state->settlementReputation.size() >=
                net::kMaxPlayerStateSettlementReputation) {
                break;
            }
            state->settlementReputation.push_back(
                net::PlayerSettlementReputation{entry.settlementId,
                                                entry.score});
        }
    }
    state->sleep = {};
    if (m_serverSleep.phase != ServerSleepPhase::None &&
        m_serverSleep.eligible.contains(session.playerStableId)) {
        state->sleep.phase =
            m_serverSleep.phase == ServerSleepPhase::Vote
                ? net::kPlayerSleepPhaseVote
                : net::kPlayerSleepPhaseSleeping;
        state->sleep.tone = m_serverSleep.phase == ServerSleepPhase::Sleeping
                                ? 1
                                : 0;
        state->sleep.voted =
            m_serverSleep.yes.contains(session.playerStableId);
        state->sleep.yesCount = static_cast<std::uint8_t>(
            std::min<std::size_t>(m_serverSleep.yes.size(), 32));
        state->sleep.eligibleCount = static_cast<std::uint8_t>(
            std::min<std::size_t>(m_serverSleep.eligible.size(), 32));
        state->sleep.initiator = m_serverSleep.initiator;
        state->sleep.startMinutes =
            static_cast<float>(m_serverSleep.startMinutes);
        state->sleep.targetMinutes =
            static_cast<float>(m_serverSleep.targetMinutes);
        state->sleep.voteSeconds = std::max(0.0f, m_serverSleep.voteSeconds);
        state->sleep.progress =
            m_serverSleep.phase == ServerSleepPhase::Sleeping
                ? m_serverSleepSession.Progress()
                : 0.0f;
        state->sleep.status = ServerSleepStatus(session);
    }
    state->objective = {};
    if (const auto* objective = registry.try_get<ecs::Objective>(player);
        objective != nullptr) {
        state->objective.valid = true;
        state->objective.step = static_cast<std::uint8_t>(objective->step);
    }
    state->statusSequence = session.statusSequence;
    state->statusTone = session.statusTone;
    state->status = session.status;
}

void ServerHost::BroadcastPlayerStates() {
    if (!m_networkEnabled || m_network == nullptr || m_simulation == nullptr ||
        m_inventory == nullptr || m_ticks == 0) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0 || !session.assignmentSent) {
            continue;
        }
        if (!session.stateDirty && !session.dead &&
            m_ticks < session.lastStateTick + m_stateSendIntervalTicks) {
            continue;
        }

        net::PlayerState state;
        FillPlayerState(session, &state);
        std::string error;
        const std::vector<std::uint8_t> bytes =
            net::EncodePlayerState(state, &error);
        if (bytes.empty()) {
            ++m_networkStats.playerStateFailures;
            ZL_WARN("net", "session={} 玩家状态编码失败：{}", sessionId, error);
            continue;
        }
        if (!m_network->Send(sessionId, bytes, &error)) {
            ++m_networkStats.playerStateFailures;
            ZL_WARN("net", "session={} 玩家状态发送失败：{}", sessionId, error);
            continue;
        }
        session.stateDirty = false;
        session.lastStateTick = m_ticks;
        ++m_networkStats.playerStatesSent;
    }
}

void ServerHost::HandleChatMessage(std::uint64_t sessionId, SessionPlayer& session,
                                   const std::string& text) {
    static_cast<void>(sessionId);
    const std::size_t begin = text.find_first_not_of(" \t");
    const std::size_t end = text.find_last_not_of(" \t");
    if (begin == std::string::npos || end == std::string::npos) {
        ++m_networkStats.chatMessagesRejected;
        return;
    }
    std::string trimmed = text.substr(begin, end - begin + 1);
    if (session.chatUsed &&
        m_ticks < session.lastChatTick + m_chatCooldownTicks) {
        ++m_networkStats.chatMessagesRejected;
        SetSessionStatus(session, "聊天过于频繁，请稍后再试", 2);
        return;
    }
    if (!net::IsValidChatText(trimmed)) {
        ++m_networkStats.chatMessagesRejected;
        return;
    }
    session.lastChatTick = m_ticks;
    session.chatUsed = true;
    m_chatLog.push_back(net::SocialChatLine{m_ticks, session.playerName, trimmed});
    while (m_chatLog.size() > net::kMaxSocialChatMessages) {
        m_chatLog.pop_front();
    }
    m_socialDirty = true;
    ZL_DEBUG("net", "session={} 聊天：{}: {}", sessionId, session.playerName, trimmed);
}

void ServerHost::FillSocialState(net::SocialState* state) const {
    if (state == nullptr) {
        return;
    }
    state->serverTick = m_ticks;
    state->sequence = m_socialSequence;
    state->gameMinutes = m_gameTime != nullptr ? m_gameTime->TotalGameMinutes() : 0.0;
    state->players.clear();
    state->players.reserve(m_sessions.size());
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        state->players.push_back(
            net::SocialPlayer{session.playerStableId, session.playerName});
    }
    std::sort(state->players.begin(), state->players.end(),
              [](const net::SocialPlayer& left, const net::SocialPlayer& right) {
                  return left.stableId < right.stableId;
              });
    state->chat.assign(m_chatLog.begin(), m_chatLog.end());
}

void ServerHost::BroadcastSocialStates() {
    if (!m_networkEnabled || m_network == nullptr || m_sessions.empty()) {
        return;
    }
    if (!m_socialDirty &&
        m_ticks < m_lastSocialTick + m_socialStateIntervalTicks) {
        return;
    }
    ++m_socialSequence;
    net::SocialState state;
    FillSocialState(&state);
    std::string error;
    const std::vector<std::uint8_t> bytes = net::EncodeSocialState(state, &error);
    if (bytes.empty()) {
        ZL_WARN("net", "社交状态编码失败：{}", error);
        return;
    }
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(session);
        if (m_network->Send(sessionId, bytes, &error)) {
            ++m_networkStats.socialStatesSent;
        } else {
            ZL_WARN("net", "session={} 社交状态发送失败：{}", sessionId, error);
        }
    }
    m_socialDirty = false;
    m_lastSocialTick = m_ticks;
}
void ServerHost::SpawnDemoLoot() {
    if (m_demoLootSpawned || m_simulation == nullptr || m_items == nullptr) {
        return;
    }
    const int spawned = game::SpawnDemoGroundItems(
        m_simulation->Ecs(), *m_items, m_simulation->World(),
        m_options.playerTargetX, m_options.playerTargetY);
    m_demoLootSpawned = true;
    ZL_INFO("items", "服务器演示地面物品：{} 处（出生点附近）", spawned);
}

void ServerHost::EnsureServerLootContainers() {
    if (m_simulation == nullptr || m_lootTables == nullptr) {
        return;
    }
    if (m_worldLoadedFromSave) {
        // 旧世界存档可能没有房屋容器：只在 chunk 已加载时补齐，已有容器由
        // SpawnHouseLootContainers 内部按 table/位置去重；演示容器不叠加。
        m_demoLootContainersSpawned = true;
        if (!m_houseLootContainersSpawned &&
            FindServerLoadedChunk(71, 20) != nullptr) {
            const int spawned = game::SpawnHouseLootContainers(
                m_simulation->Ecs(), *m_lootTables, m_simulation->World());
            m_houseLootContainersSpawned = true;
            if (spawned > 0) {
                ZL_INFO("items", "服务器旧存档补齐房屋容器：{} 个", spawned);
            }
        }
        return;
    }
    if (!m_demoLootContainersSpawned) {
        const int spawned = game::SpawnDemoLootContainers(
            m_simulation->Ecs(), *m_lootTables, m_simulation->World(),
            m_options.playerTargetX, m_options.playerTargetY);
        m_demoLootContainersSpawned = true;
        if (spawned > 0) {
            ZL_INFO("items", "服务器演示战利品容器：{} 个", spawned);
        }
    }
    if (!m_houseLootContainersSpawned &&
        FindServerLoadedChunk(71, 20) != nullptr) {
        const int spawned = game::SpawnHouseLootContainers(
            m_simulation->Ecs(), *m_lootTables, m_simulation->World());
        m_houseLootContainersSpawned = true;
        ZL_INFO("items", "服务器房屋战利品容器：{} 个", spawned);
    }
}

void ServerHost::EnsureServerPoiContent() {
    if (m_simulation == nullptr || m_poiConfig == nullptr ||
        !m_poiConfig->enabled || m_lootTables == nullptr) {
        return;
    }
    if (m_poiRuntime.empty()) {
        for (const game::PoiDef& poi : m_poiConfig->defs) {
            m_poiRuntime.emplace(poi.id, ServerPoiRuntime{});
        }
    }
    ecs::PoiWorldProgress* world = ServerPoiWorldProgress();
    for (const game::PoiDef& poi : m_poiConfig->defs) {
        const int markerX = static_cast<int>(std::floor(poi.markerX));
        const int markerY = static_cast<int>(std::floor(poi.markerY));
        if (FindServerLoadedChunk(markerX, markerY) == nullptr) {
            continue;
        }
        const game::PoiProgress* staged =
            world != nullptr
                ? game::FindPoiProgress(world->pois, poi.id)
                : nullptr;
        ServerPoiRuntime& runtime = m_poiRuntime[poi.id];
        if (!runtime.containersSpawned) {
            int spawned = 0;
            for (const game::PoiContainerDef& container : poi.containers) {
                if (game::HasLootContainerNear(
                        m_simulation->Ecs(), container.tableId, container.x,
                        container.y, 0, 1.0f)) {
                    continue;
                }
                if (game::SpawnLootContainer(
                        m_simulation->Ecs(), *m_lootTables, container.tableId,
                        container.x, container.y, 0, container.sprite) !=
                    entt::null) {
                    ++spawned;
                }
            }
            runtime.containersSpawned = true;
            if (spawned > 0) {
                ZL_INFO("items", "服务器 POI 容器生成：{}（{} 个）", poi.name,
                        spawned);
            }
        }
        if (runtime.guardsSpawned) {
            continue;
        }
        if (staged != nullptr && staged->completed) {
            runtime.guardsSpawned = true;
            continue;
        }
        if (m_worldLoadedFromSave && m_networkEnabled && m_sessions.empty()) {
            continue;
        }
        if (m_worldLoadedFromSave && !m_sessions.empty()) {
            bool checked = false;
            bool allCompleted = true;
            for (const auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (!session.spawned || session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                const auto* tasks =
                    player != entt::null
                        ? m_simulation->Ecs().Registry().try_get<ecs::PoiTasks>(
                              player)
                        : nullptr;
                checked = true;
                if (tasks == nullptr ||
                    !game::IsPoiCompleted(tasks->completed, poi.id)) {
                    allCompleted = false;
                }
            }
            if (!checked) {
                continue;  // 等待至少一个档案玩家生成后再判断完成态
            }
            if (allCompleted) {
                runtime.guardsSpawned = true;
                continue;
            }
        }
        runtime.guardsSpawned = true;
        if (poi.enemies.count <= 0 || CountServerPoiGuards(poi.id) > 0) {
            continue;
        }
        int spawned = 0;
        for (int i = 0; i < poi.enemies.count; ++i) {
            int wx = 0;
            int wy = 0;
            if (!FindServerPoiGuardCell(poi, i, &wx, &wy)) {
                continue;
            }
            ecs::ZombieVariantKind variant = ecs::ZombieVariantKind::Normal;
            switch (game::PoiGuardKindForSpawn(poi.enemies, i)) {
                case game::PoiGuardKind::Fast:
                    variant = ecs::ZombieVariantKind::Fast;
                    break;
                case game::PoiGuardKind::Strong:
                    variant = ecs::ZombieVariantKind::Strong;
                    break;
                case game::PoiGuardKind::Armored:
                    variant = ecs::ZombieVariantKind::Armored;
                    break;
                case game::PoiGuardKind::Mixed:
                case game::PoiGuardKind::Normal:
                default:
                    variant = ecs::ZombieVariantKind::Normal;
                    break;
            }
            const ecs::Entity zombie = m_simulation->SpawnZombieAt(
                static_cast<float>(wx) + 0.5f, static_cast<float>(wy) + 0.5f,
                variant);
            if (zombie == entt::null) {
                continue;
            }
            m_simulation->Ecs().Registry().emplace_or_replace<ecs::PoiGuard>(
                zombie, ecs::PoiGuard{poi.id});
            ++spawned;
        }
        ZL_INFO("poi", "服务器 POI 守军生成：{}（{}/{}）", poi.name, spawned,
                poi.enemies.count);
    }
}

bool ServerHost::FindServerPoiGuardCell(const game::PoiDef& poi, int index,
                                        int* wx, int* wy) const {
    if (wx == nullptr || wy == nullptr || m_simulation == nullptr) {
        return false;
    }
    for (int attempt = 0; attempt < 32; ++attempt) {
        const float angle = static_cast<float>(index) * 2.3999631f +
                            static_cast<float>(attempt) * 0.6180339f;
        const float radius =
            2.5f + static_cast<float>((index + attempt) % 4) * 1.2f;
        const int x = static_cast<int>(
            std::floor(poi.markerX + std::cos(angle) * radius));
        const int y = static_cast<int>(
            std::floor(poi.markerY + std::sin(angle) * radius));
        if (FindServerLoadedChunk(x, y) == nullptr ||
            game::IsSquareBlocked(m_simulation->World(), x, y, 0)) {
            continue;
        }
        bool occupied = false;
        m_simulation->Ecs().Registry().view<ecs::Position>().each(
            [&](const ecs::Position& position) {
                if (!occupied && position.z == 0 &&
                    static_cast<int>(std::floor(position.x)) == x &&
                    static_cast<int>(std::floor(position.y)) == y) {
                    occupied = true;
                }
            });
        if (occupied) {
            continue;
        }
        *wx = x;
        *wy = y;
        return true;
    }
    return false;
}

bool ServerHost::FindServerPoiStageEnemyCell(
    const game::PoiDef& poi, const game::PoiStageDef& stage, int index,
    float originX, float originY, int* wx, int* wy) const {
    if (wx == nullptr || wy == nullptr || m_simulation == nullptr) {
        return false;
    }
    const float baseX = std::isfinite(originX) ? originX : stage.x;
    const float baseY = std::isfinite(originY) ? originY : stage.y;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const float angle = static_cast<float>(index) * 2.3999631f +
                            static_cast<float>(attempt) * 0.6180339f;
        const float radius =
            1.5f + static_cast<float>((index + attempt) % 4) * 0.8f;
        const int x =
            static_cast<int>(std::floor(baseX + std::cos(angle) * radius));
        const int y =
            static_cast<int>(std::floor(baseY + std::sin(angle) * radius));
        if (FindServerLoadedChunk(x, y) == nullptr ||
            game::IsSquareBlocked(m_simulation->World(), x, y, 0)) {
            continue;
        }
        *wx = x;
        *wy = y;
        return true;
    }
    static_cast<void>(poi);
    return false;
}

ecs::Entity ServerHost::SpawnServerPoiStageEnemy(
    const game::PoiDef& poi, const game::PoiStageDef& stage,
    game::PoiStageEnemyRole role, game::PoiGuardKind kind, int index,
    float originX, float originY) {
    if (m_simulation == nullptr) {
        return entt::null;
    }
    int wx = 0;
    int wy = 0;
    if (!FindServerPoiStageEnemyCell(poi, stage, index, originX, originY,
                                     &wx, &wy)) {
        return entt::null;
    }
    ecs::ZombieVariantKind variant = ecs::ZombieVariantKind::Normal;
    switch (kind) {
        case game::PoiGuardKind::Fast:
            variant = ecs::ZombieVariantKind::Fast;
            break;
        case game::PoiGuardKind::Strong:
            variant = ecs::ZombieVariantKind::Strong;
            break;
        case game::PoiGuardKind::Armored:
            variant = ecs::ZombieVariantKind::Armored;
            break;
        case game::PoiGuardKind::Mixed:
        case game::PoiGuardKind::Normal:
        default:
            variant = ecs::ZombieVariantKind::Normal;
            break;
    }
    const ecs::Entity zombie = m_simulation->SpawnZombieAt(
        static_cast<float>(wx) + 0.5f, static_cast<float>(wy) + 0.5f,
        variant);
    if (zombie == entt::null) {
        return entt::null;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    registry.emplace_or_replace<ecs::PoiGuard>(zombie,
                                               ecs::PoiGuard{poi.id});
    registry.emplace_or_replace<ecs::PoiStageEnemy>(
        zombie, ecs::PoiStageEnemy{poi.id, stage.id, role});
    if (role == game::PoiStageEnemyRole::Boss) {
        if (auto* health = registry.try_get<ecs::Health>(zombie);
            health != nullptr) {
            health->max = std::max(
                1.0f, health->max * m_poiConfig->boss.healthScale);
            health->current = health->max;
        }
        const game::PoiBossMechanic mechanic =
            game::PoiBossMechanicFor(poi.boss.mechanic);
        if (mechanic == game::PoiBossMechanic::Fortify) {
            ecs::ZombieSpecial special;
            special.kind = ecs::ZombieSpecialKind::Breaker;
            registry.emplace_or_replace<ecs::ZombieSpecial>(zombie, special);
        } else if (mechanic == game::PoiBossMechanic::Summon ||
                   mechanic == game::PoiBossMechanic::Charge) {
            ecs::PoiBossRuntime runtime;
            runtime.baseSpeedScale =
                game::ZombieVariantProfileFor(variant).speedScale;
            runtime.abilityCooldown =
                mechanic == game::PoiBossMechanic::Summon
                    ? m_poiConfig->boss.summonIntervalSeconds
                    : m_poiConfig->boss.chargeIntervalSeconds;
            registry.emplace_or_replace<ecs::PoiBossRuntime>(zombie,
                                                             runtime);
        }
    }
    return zombie;
}

void ServerHost::EnsureServerPoiStageEnemies(
    const game::PoiDef& poi, game::PoiProgress* progress) {
    if (progress == nullptr || m_simulation == nullptr) {
        return;
    }
    const game::PoiStageDef* stage =
        game::CurrentPoiStage(poi, *progress);
    if (stage == nullptr || stage->kind != game::PoiStageKind::Kill) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (game::HasPoiStageEnemyRecords(registry, poi.id, stage->id)) {
        return;
    }
    const std::vector<game::PoiStageEnemySpec> plan =
        game::BuildPoiStageEnemyPlan(poi, *stage);
    if (plan.empty()) {
        return;
    }
    int spawned = 0;
    for (const game::PoiStageEnemySpec& spec : plan) {
        if (SpawnServerPoiStageEnemy(poi, *stage, spec.role, spec.kind,
                                     spec.index, stage->x, stage->y) !=
            entt::null) {
            ++spawned;
        }
    }
    if (spawned <= 0) {
        return;
    }
    if (game::PoiStageHasBoss(poi, *stage)) {
        const std::string text =
            "Boss 现身：" + poi.boss.name + "（" +
            game::PoiBossMechanicText(
                game::PoiBossMechanicFor(poi.boss.mechanic)) +
            "）";
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (session.spawned && !session.dead) {
                SetSessionStatus(session, text, 2);
            }
        }
    }
    ZL_INFO("poi", "服务器阶段敌人生成：{} · {}（{}/{}，boss={}）",
            poi.name, stage->id, spawned, plan.size(),
            game::PoiStageHasBoss(poi, *stage) ? "是" : "否");
}

void ServerHost::UpdateServerPoiBossMechanics(
    const game::PoiDef& poi, const game::PoiProgress& progress, float dt) {
    if (m_simulation == nullptr || dt <= 0.0f) {
        return;
    }
    const game::PoiStageDef* stage =
        game::CurrentPoiStage(poi, progress);
    if (stage == nullptr || !game::PoiStageHasBoss(poi, *stage)) {
        return;
    }
    const game::PoiBossMechanic mechanic =
        game::PoiBossMechanicFor(poi.boss.mechanic);
    if (mechanic == game::PoiBossMechanic::None) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<ecs::Entity> bosses;
    registry.view<ecs::PoiStageEnemy, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::PoiStageEnemy& enemy,
            const ecs::Health& health) {
            if (enemy.poiId == poi.id && enemy.stageId == stage->id &&
                enemy.role == game::PoiStageEnemyRole::Boss &&
                health.current > 0.0f && registry.valid(entity)) {
                bosses.push_back(entity);
            }
        });
    if (bosses.empty()) {
        return;
    }

    if (mechanic == game::PoiBossMechanic::Fortify) {
        for (const ecs::Entity entity : bosses) {
            if (registry.try_get<ecs::ZombieSpecial>(entity) == nullptr) {
                ecs::ZombieSpecial special;
                special.kind = ecs::ZombieSpecialKind::Breaker;
                registry.emplace<ecs::ZombieSpecial>(entity, special);
            }
        }
        return;
    }

    for (const ecs::Entity entity : bosses) {
        auto* variant = registry.try_get<ecs::ZombieVariant>(entity);
        bool createdRuntime = false;
        auto* runtime = registry.try_get<ecs::PoiBossRuntime>(entity);
        if (runtime == nullptr) {
            ecs::PoiBossRuntime created;
            created.baseSpeedScale =
                variant != nullptr
                    ? game::ZombieVariantProfileFor(variant->kind).speedScale
                    : 1.0f;
            created.abilityCooldown =
                mechanic == game::PoiBossMechanic::Summon
                    ? m_poiConfig->boss.summonIntervalSeconds
                    : m_poiConfig->boss.chargeIntervalSeconds;
            runtime =
                &registry.emplace<ecs::PoiBossRuntime>(entity, created);
            createdRuntime = true;
        }
        if (variant != nullptr && createdRuntime) {
            variant->speedScale = runtime->baseSpeedScale;
        }
        const float baseSpeed =
            runtime->baseSpeedScale > 0.0f
                ? runtime->baseSpeedScale
                : (variant != nullptr
                       ? game::ZombieVariantProfileFor(variant->kind)
                             .speedScale
                       : 1.0f);
        if (mechanic == game::PoiBossMechanic::Summon) {
            runtime->abilityCooldown -= dt;
            if (runtime->abilityCooldown > 0.0f) {
                continue;
            }
            const int liveAdds =
                game::CountPoiStageSummons(registry, poi.id, stage->id);
            if (runtime->summoned < m_poiConfig->boss.summonTotal &&
                liveAdds < m_poiConfig->boss.summonMax) {
                const auto* position =
                    registry.try_get<ecs::Position>(entity);
                const float originX =
                    position != nullptr ? position->x : stage->x;
                const float originY =
                    position != nullptr ? position->y : stage->y;
                int added = 0;
                for (int i = 0;
                     i < m_poiConfig->boss.summonCount &&
                     runtime->summoned < m_poiConfig->boss.summonTotal;
                     ++i) {
                    if (SpawnServerPoiStageEnemy(
                            poi, *stage, game::PoiStageEnemyRole::Summon,
                            game::PoiGuardKind::Fast,
                            runtime->summoned + i, originX, originY) !=
                        entt::null) {
                        ++added;
                        ++runtime->summoned;
                    }
                }
                if (added > 0) {
                    for (auto& [sessionId, session] : m_sessions) {
                        static_cast<void>(sessionId);
                        if (session.spawned && !session.dead) {
                            SetSessionStatus(
                                session,
                                "Boss 召唤增援：" + poi.boss.name, 2);
                        }
                    }
                }
            }
            runtime->abilityCooldown =
                m_poiConfig->boss.summonIntervalSeconds;
            continue;
        }

        if (runtime->chargeRemaining > 0.0f) {
            runtime->chargeRemaining =
                std::max(0.0f, runtime->chargeRemaining - dt);
            if (variant != nullptr) {
                variant->speedScale =
                    runtime->chargeRemaining > 0.0f
                        ? baseSpeed * m_poiConfig->boss.chargeSpeedScale
                        : baseSpeed;
            }
            continue;
        }
        runtime->abilityCooldown -= dt;
        if (runtime->abilityCooldown > 0.0f) {
            continue;
        }
        const auto* position = registry.try_get<ecs::Position>(entity);
        if (position == nullptr) {
            continue;
        }
        SessionPlayer* nearest = nullptr;
        float nearestDistance = 14.0f * 14.0f;
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (!session.spawned || session.dead ||
                session.playerStableId == 0) {
                continue;
            }
            const ecs::Entity player =
                m_simulation->Ecs().FindById(session.playerStableId);
            if (player == entt::null) {
                continue;
            }
            const auto* playerPosition =
                registry.try_get<ecs::Position>(player);
            if (playerPosition == nullptr) {
                continue;
            }
            const float dx = playerPosition->x - position->x;
            const float dy = playerPosition->y - position->y;
            const float distance = dx * dx + dy * dy;
            if (distance <= nearestDistance) {
                nearestDistance = distance;
                nearest = &session;
            }
        }
        if (nearest == nullptr) {
            continue;
        }
        runtime->chargeRemaining = m_poiConfig->boss.chargeSeconds;
        runtime->abilityCooldown =
            m_poiConfig->boss.chargeIntervalSeconds;
        if (variant != nullptr) {
            variant->speedScale =
                baseSpeed * m_poiConfig->boss.chargeSpeedScale;
        }
        SetSessionStatus(*nearest,
                         "Boss 冲锋：" + poi.boss.name + "（走位/格挡）", 2);
    }
}

void ServerHost::ClearServerPoiStageEnemies(std::string_view poiId,
                                            std::string_view stageId) {
    if (m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const std::vector<ecs::Entity> enemies =
        game::CollectPoiStageEnemies(registry, poiId, stageId);
    for (const ecs::Entity entity : enemies) {
        if (entity != entt::null && registry.valid(entity)) {
            m_simulation->DespawnEntity(entity);
        }
    }
}

bool ServerHost::ServerPoiPlayerNearStart(const game::PoiDef& poi, float x,
                                          float y) const {
    const float markerDx = poi.markerX - x;
    const float markerDy = poi.markerY - y;
    if (markerDx * markerDx + markerDy * markerDy <= 16.0f * 16.0f) {
        return true;
    }
    for (const game::PoiEntryDef& entry : poi.entries) {
        const float dx = entry.x - x;
        const float dy = entry.y - y;
        if (dx * dx + dy * dy <= 6.0f * 6.0f) {
            return true;
        }
    }
    return false;
}

void ServerHost::FailServerPoiOnWipe() {
    if (m_simulation == nullptr || m_poiConfig == nullptr ||
        !m_poiConfig->enabled) {
        return;
    }
    ecs::PoiWorldProgress* world = ServerPoiWorldProgress();
    if (world == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    for (const game::PoiDef& poi : m_poiConfig->defs) {
        if (poi.stages.empty()) {
            continue;
        }
        game::PoiProgress* progress =
            game::FindPoiProgress(&world->pois, poi.id);
        if (progress == nullptr || progress->completed || progress->failed) {
            continue;
        }
        const game::PoiStageDef* stage =
            game::CurrentPoiStage(poi, *progress);
        if (stage == nullptr || !game::PoiStageHasBoss(poi, *stage) ||
            game::CountPoiStageTargets(registry, poi.id, stage->id) <= 0) {
            continue;
        }
        bool aliveNear = false;
        for (const auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (!session.spawned || session.dead ||
                session.playerStableId == 0) {
                continue;
            }
            const ecs::Entity player =
                m_simulation->Ecs().FindById(session.playerStableId);
            if (player == entt::null) {
                continue;
            }
            const auto* position = registry.try_get<ecs::Position>(player);
            if (position == nullptr) {
                continue;
            }
            const float dx = position->x - stage->x;
            const float dy = position->y - stage->y;
            if (dx * dx + dy * dy <=
                m_poiConfig->boss.resetRange * m_poiConfig->boss.resetRange) {
                aliveNear = true;
                break;
            }
        }
        if (aliveNear || !game::FailPoiProgress(progress)) {
            continue;
        }
        ClearServerPoiStageEnemies(poi.id);
        m_serverPoiBossResetTimers.erase(poi.id);
        ZL_INFO("poi", "服务器 Boss 团灭失败：{}（{}）", poi.name, poi.id);
    }
}

ecs::PoiWorldProgress* ServerHost::ServerPoiWorldProgress() {
    if (m_simulation == nullptr) {
        return nullptr;
    }
    ecs::EcsWorld& ecs = m_simulation->Ecs();
    ecs::Entity entity = ecs.FindById(ecs::kPoiWorldProgressStableId);
    if (entity == entt::null) {
        entity = ecs.CreateWithStableId(ecs::kPoiWorldProgressStableId);
    }
    if (entity == entt::null) {
        return nullptr;
    }
    entt::registry& registry = ecs.Registry();
    auto* progress = registry.try_get<ecs::PoiWorldProgress>(entity);
    if (progress == nullptr) {
        progress = &registry.emplace<ecs::PoiWorldProgress>(entity);
    }
    return progress;
}

int ServerHost::CountServerPoiGuards(std::string_view poiId) const {
    if (m_simulation == nullptr) {
        return 0;
    }
    int count = 0;
    entt::registry& registry = m_simulation->Ecs().Registry();
    registry.view<ecs::PoiGuard, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::PoiGuard& guard,
            const ecs::Health& health) {
            if (m_simulation->Ecs().Alive(entity) && guard.poiId == poiId &&
                health.current > 0.0f) {
                ++count;
            }
        });
    return count;
}

bool ServerHost::ServerPoiTargetSearched(const game::PoiDef& poi) const {
    if (m_simulation == nullptr || poi.task.targetTable.empty()) {
        return false;
    }
    bool found = false;
    m_simulation->Ecs().Registry().view<ecs::LootContainer>().each(
        [&](const ecs::Entity entity, const ecs::LootContainer& loot) {
            if (!found && m_simulation->Ecs().Alive(entity) && loot.searched &&
                loot.tableId == poi.task.targetTable) {
                found = true;
            }
        });
    return found;
}

void ServerHost::GrantServerPoiStageReward(SessionPlayer& session,
                                           const game::PoiDef& poi,
                                           const game::PoiStageDef& stage) {
    if (m_simulation == nullptr || session.playerStableId == 0) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    auto* tasks = registry.try_get<ecs::PoiTasks>(player);
    if (tasks == nullptr) {
        tasks = &registry.emplace<ecs::PoiTasks>(player);
    }
    if (!game::ClaimPoiStageReward(&tasks->rewardClaims, poi.id, stage.id)) {
        return;
    }
    if (stage.reward.xp > 0.0f) {
        AwardProgressionXp(player, ecs::SkillKind::Survival, stage.reward.xp,
                           &session);
    }
    bool dropped = false;
    if (!stage.reward.item.empty() && m_items != nullptr &&
        m_items->Find(stage.reward.item) != nullptr) {
        const auto* position = registry.try_get<ecs::Position>(player);
        if (position != nullptr) {
            dropped = ReturnServerItem(
                player,
                ecs::ItemInstance{stage.reward.item,
                                  std::max(1, stage.reward.count), 1.0f},
                position->x, position->y, position->z);
        }
    }
    session.stateDirty = true;
    SetSessionStatus(
        session,
        "POI 阶段完成：" + poi.name + " · " + stage.id +
            (dropped ? "（背包已满，奖励掉落脚下）" : ""),
        1);
}

bool ServerHost::ServerPlayerHasPoiKeyItem(ecs::Entity player,
                                           const game::PoiKeyDef& key) const {
    if (m_simulation == nullptr || player == entt::null ||
        key.item.empty()) {
        return false;
    }
    if (m_items == nullptr || m_items->Find(key.item) == nullptr) {
        // 内容缺物品定义时不让玩家无提示卡死；门禁仍可用进度钥匙通过。
        return true;
    }
    const auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    return container != nullptr && items::CountOf(*container, key.item) > 0;
}

bool ServerHost::ServerConsumePoiKeyItem(ecs::Entity player,
                                         const game::PoiKeyDef& key) {
    if (m_simulation == nullptr || player == entt::null ||
        key.item.empty()) {
        return false;
    }
    if (m_items == nullptr || m_items->Find(key.item) == nullptr) {
        ZL_WARN("poi", "钥匙物品未注册，跳过消耗：{}（{}）", key.item, key.id);
        return true;
    }
    auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    return container != nullptr &&
           items::RemoveItem(*container, key.item, 1) == 1;
}

void ServerHost::GrantServerPoiStageKeys(SessionPlayer& session,
                                         const game::PoiDef& poi,
                                         const game::PoiStageDef& stage) {
    if (m_simulation == nullptr || session.playerStableId == 0 ||
        m_items == nullptr) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
        return;
    }
    const auto* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    const float dx = poi.markerX - position->x;
    const float dy = poi.markerY - position->y;
    constexpr float kRewardRange = 32.0f;
    if (dx * dx + dy * dy > kRewardRange * kRewardRange) {
        return;
    }
    std::string names;
    bool dropped = false;
    for (const game::PoiKeyDef* key :
         game::PoiStageKeyAwards(poi, stage.id)) {
        if (key == nullptr || key->item.empty()) {
            continue;
        }
        if (m_items->Find(key->item) == nullptr) {
            ZL_WARN("poi", "钥匙物品未注册，无法发放：{}（{}）", key->item,
                    key->id);
            continue;
        }
        if (!names.empty()) {
            names += "、";
        }
        names += game::PoiKeyDisplayName(*key);
        dropped = ReturnServerItem(
                      player,
                      ecs::ItemInstance{key->item, 1, 1.0f}, position->x,
                      position->y, position->z) ||
                  dropped;
    }
    if (names.empty()) {
        return;
    }
    SetSessionStatus(session,
                     "获得钥匙：" + names +
                         (dropped ? "（背包已满，钥匙掉落脚下）" : ""),
                     1);
    ZL_INFO("poi", "服务器发放阶段钥匙：{} · {} → 玩家 {}", poi.id, stage.id,
            session.playerStableId);
}

void ServerHost::ApplyServerPoiStageUpdate(
    const game::PoiDef& poi, game::PoiProgress& progress,
    const game::PoiStageUpdate& update) {
    if (m_simulation == nullptr) {
        return;
    }
    constexpr float kRewardRange = 32.0f;
    if (update.stageCompleted && !update.stageId.empty()) {
        const game::PoiStageDef* stage =
            game::FindPoiStage(poi, update.stageId);
        if (stage != nullptr) {
            for (auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (!session.spawned || session.dead ||
                    session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                if (player == entt::null) {
                    continue;
                }
                const auto* position =
                    m_simulation->Ecs().Registry().try_get<ecs::Position>(
                        player);
                if (position == nullptr) {
                    continue;
                }
                const float dx = poi.markerX - position->x;
                const float dy = poi.markerY - position->y;
                if (dx * dx + dy * dy > kRewardRange * kRewardRange) {
                    continue;
                }
                if (update.rewardAvailable) {
                    GrantServerPoiStageReward(session, poi, *stage);
                }
                GrantServerPoiStageKeys(session, poi, *stage);
            }
        }
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.dead ||
            session.playerStableId == 0) {
            continue;
        }
        session.stateDirty = true;
        if (!update.poiCompleted) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        auto* tasks =
            m_simulation->Ecs().Registry().try_get<ecs::PoiTasks>(player);
        if (tasks == nullptr) {
            tasks = &m_simulation->Ecs().Registry().emplace<ecs::PoiTasks>(
                player);
        }
        game::MarkPoiCompleted(&tasks->completed, poi.id);
    }
    if (update.stageCompleted && !update.stageId.empty()) {
        ClearServerPoiStageEnemies(poi.id, update.stageId);
        m_serverPoiBossResetTimers.erase(poi.id);
    }
    if (update.poiCompleted) {
        ClearServerPoiStageEnemies(poi.id);
        m_serverPoiBossResetTimers.erase(poi.id);
        ZL_INFO("poi", "服务器阶段探索完成：{}（{}），尝试 {}", poi.name,
                poi.id, progress.attempts);
    } else if (update.awaitingChoice) {
        ZL_INFO("poi", "服务器阶段路线选择：{}（{}）", poi.name, poi.id);
    } else if (update.stageCompleted) {
        ZL_INFO("poi", "服务器阶段完成：{} · {}", poi.name, update.stageId);
    }
}

void ServerHost::TickServerPoiStages(float dt) {
    if (m_simulation == nullptr || m_poiConfig == nullptr ||
        !m_poiConfig->enabled) {
        return;
    }
    ecs::PoiWorldProgress* world = ServerPoiWorldProgress();
    if (world == nullptr) {
        return;
    }
    constexpr float kActivateRange = 16.0f;
    constexpr float kEntryRange = 3.5f;
    for (const game::PoiDef& poi : m_poiConfig->defs) {
        if (poi.stages.empty()) {
            continue;
        }
        game::PoiProgress* progress =
            game::FindPoiProgress(&world->pois, poi.id);
        if (progress != nullptr && progress->completed) {
            continue;
        }
        if (progress != nullptr && progress->failed) {
            bool retry = false;
            for (const auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (!session.spawned || session.dead ||
                    session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
                    continue;
                }
                const auto* position =
                    m_simulation->Ecs().Registry().try_get<ecs::Position>(
                        player);
                if (position != nullptr &&
                    ServerPoiPlayerNearStart(poi, position->x, position->y)) {
                    retry = true;
                    break;
                }
            }
            if (!retry || !game::RetryPoiProgress(poi, progress)) {
                continue;
            }
            ClearServerPoiStageEnemies(poi.id);
            m_serverPoiBossResetTimers.erase(poi.id);
            ZL_INFO("poi", "服务器阶段重试：{}（{}），第 {} 次", poi.name,
                    poi.id, progress->attempts);
            if (progress->completed || progress->currentStage < 0) {
                continue;
            }
        }
        if (progress == nullptr || progress->currentStage < 0) {
            bool markerStart = false;
            const game::PoiEntryDef* entry = nullptr;
            ecs::Entity starter = entt::null;
            float bestDistance = std::numeric_limits<float>::max();
            for (const auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (!session.spawned || session.dead ||
                    session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                if (player == entt::null ||
                    !m_simulation->Ecs().Alive(player)) {
                    continue;
                }
                const auto* position =
                    m_simulation->Ecs().Registry().try_get<ecs::Position>(
                        player);
                if (position == nullptr) {
                    continue;
                }
                const float dx = poi.markerX - position->x;
                const float dy = poi.markerY - position->y;
                if (dx * dx + dy * dy <= kActivateRange * kActivateRange) {
                    markerStart = true;
                    starter = player;
                    break;
                }
            }
            if (!markerStart) {
                for (const auto& [sessionId, session] : m_sessions) {
                    static_cast<void>(sessionId);
                    if (!session.spawned || session.dead ||
                        session.playerStableId == 0) {
                        continue;
                    }
                    const ecs::Entity player =
                        m_simulation->Ecs().FindById(
                            session.playerStableId);
                    if (player == entt::null ||
                        !m_simulation->Ecs().Alive(player)) {
                        continue;
                    }
                    const auto* position =
                        m_simulation->Ecs().Registry()
                            .try_get<ecs::Position>(player);
                    if (position == nullptr) {
                        continue;
                    }
                    for (const game::PoiEntryDef& candidate :
                         poi.entries) {
                        const float dx = candidate.x - position->x;
                        const float dy = candidate.y - position->y;
                        const float distance = dx * dx + dy * dy;
                        if (distance > kEntryRange * kEntryRange ||
                            distance >= bestDistance) {
                            continue;
                        }
                        if (!candidate.requiredKey.empty()) {
                            const game::PoiKeyDef* key =
                                game::FindPoiKey(poi,
                                                 candidate.requiredKey);
                            if (key == nullptr ||
                                !ServerPlayerHasPoiKeyItem(player, *key)) {
                                continue;
                            }
                        }
                        bestDistance = distance;
                        entry = &candidate;
                        starter = player;
                    }
                }
            }
            if (!markerStart && entry == nullptr) {
                continue;
            }
            progress = &game::EnsurePoiProgress(&world->pois, poi.id);
            const bool entered = !markerStart && entry != nullptr;
            if (entered && !entry->stage.empty()) {
                if (!game::StartPoiProgressAtStage(poi, progress,
                                                   entry->stage)) {
                    continue;
                }
            } else if (!game::StartPoiProgress(poi, progress)) {
                continue;
            }
            if (entered && !entry->requiredKey.empty()) {
                const game::PoiKeyDef* key =
                    game::FindPoiKey(poi, entry->requiredKey);
                if (key != nullptr) {
                    if (key->consumed &&
                        !ServerConsumePoiKeyItem(starter, *key)) {
                        ZL_WARN("poi", "服务器入口钥匙消耗失败：{}（{}）",
                                poi.id, key->id);
                    }
                    game::MarkPoiKeyAcquired(&progress->acquiredKeys,
                                             key->id);
                }
            }
            ClearServerPoiStageEnemies(poi.id);
            m_serverPoiBossResetTimers.erase(poi.id);
            progress->lastLiveGuards = 0;
            ZL_INFO("poi", "服务器开始阶段探索：{}（{} 阶段，入口 {}）",
                    poi.name, poi.stages.size(),
                    entered ? entry->id : "主入口");
        }

        const game::PoiStageDef* current =
            game::CurrentPoiStage(poi, *progress);
        game::PoiStageUpdate update;
        if (current != nullptr &&
            current->kind == game::PoiStageKind::Kill) {
            EnsureServerPoiStageEnemies(poi, progress);
            UpdateServerPoiBossMechanics(poi, *progress, dt);
            entt::registry& registry = m_simulation->Ecs().Registry();
            const int liveTargets =
                game::CountPoiStageTargets(registry, poi.id, current->id);
            if (game::PoiStageHasBoss(poi, *current) && liveTargets > 0) {
                bool bossNear = false;
                for (const auto& [sessionId, session] : m_sessions) {
                    static_cast<void>(sessionId);
                    if (!session.spawned || session.dead ||
                        session.playerStableId == 0) {
                        continue;
                    }
                    const ecs::Entity player =
                        m_simulation->Ecs().FindById(session.playerStableId);
                    if (player == entt::null ||
                        !m_simulation->Ecs().Alive(player)) {
                        continue;
                    }
                    const auto* position =
                        m_simulation->Ecs().Registry().try_get<ecs::Position>(
                            player);
                    if (position == nullptr) {
                        continue;
                    }
                    const float dx = position->x - current->x;
                    const float dy = position->y - current->y;
                    if (dx * dx + dy * dy <=
                        m_poiConfig->boss.resetRange *
                            m_poiConfig->boss.resetRange) {
                        bossNear = true;
                        break;
                    }
                }
                float& timer = m_serverPoiBossResetTimers[poi.id];
                if (!bossNear) {
                    timer += std::max(0.0f, dt);
                    if (timer >= m_poiConfig->boss.resetSeconds) {
                        if (game::FailPoiProgress(progress)) {
                            ClearServerPoiStageEnemies(poi.id);
                            m_serverPoiBossResetTimers.erase(poi.id);
                            for (auto& [sessionId, session] : m_sessions) {
                                static_cast<void>(sessionId);
                                if (session.spawned && !session.dead) {
                                    SetSessionStatus(
                                        session,
                                        "Boss 战失败：离开区域过久，返回可重试",
                                        2);
                                }
                            }
                            ZL_INFO("poi", "服务器 Boss 战失败：{}（{}），玩家脱战",
                                    poi.name, poi.id);
                        }
                        continue;
                    }
                } else {
                    timer = 0.0f;
                }
            }
            if (progress->lastLiveGuards > 0 &&
                liveTargets < progress->lastLiveGuards) {
                update = game::SignalPoiStage(
                    poi, progress, game::PoiStageSignal::Kill,
                    progress->lastLiveGuards - liveTargets);
            }
            progress->lastLiveGuards = liveTargets;
        } else if (current != nullptr) {
            progress->lastLiveGuards = 0;
        }
        if (!update.stageCompleted && !update.awaitingChoice) {
            for (const auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (!session.spawned || session.dead ||
                    session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                if (player == entt::null) {
                    continue;
                }
                const auto* position =
                    m_simulation->Ecs().Registry().try_get<ecs::Position>(
                        player);
                if (position == nullptr) {
                    continue;
                }
                update =
                    game::TickPoiStage(poi, progress, position->x, position->y);
                if (update.stageCompleted || update.awaitingChoice) {
                    break;
                }
            }
        }
        current = game::CurrentPoiStage(poi, *progress);
        if (!update.stageCompleted && current != nullptr &&
            current->kind == game::PoiStageKind::Loot) {
            for (const auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (!session.spawned || session.dead ||
                    session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                if (player == entt::null) {
                    continue;
                }
                int owned = 0;
                const auto& registry = m_simulation->Ecs().Registry();
                if (const auto* container =
                        registry.try_get<ecs::Container>(player);
                    container != nullptr) {
                    for (const ecs::ItemInstance& item : container->items) {
                        if (item.defId == current->target) {
                            owned += std::max(1, item.count);
                        }
                    }
                }
                if (const auto* equipment =
                        registry.try_get<ecs::Equipment>(player);
                    equipment != nullptr) {
                    for (const auto& slot : equipment->slots) {
                        if (slot.has_value() &&
                            slot->defId == current->target) {
                            owned += std::max(1, slot->count);
                        }
                    }
                }
                if (owned >= current->count) {
                    update = game::SignalPoiStage(
                        poi, progress, game::PoiStageSignal::Loot, owned);
                    break;
                }
            }
        }
        if (!update.stageCompleted && !update.poiCompleted &&
            !update.awaitingChoice) {
            continue;
        }
        ApplyServerPoiStageUpdate(poi, *progress, update);
    }
}

bool ServerHost::InteractPoi(ecs::Entity player, SessionPlayer& session,
                             const net::Command& command,
                             std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (m_simulation == nullptr || m_poiConfig == nullptr ||
        !m_poiConfig->enabled) {
        return fail("POI 系统不可用");
    }
    if (m_items == nullptr) {
        return fail("物品注册表不可用");
    }
    const int poiIndex = command.payload.index;
    if (poiIndex < 0 ||
        poiIndex >= static_cast<int>(m_poiConfig->defs.size())) {
        return fail("POI 索引非法");
    }
    const game::PoiDef& poi =
        m_poiConfig->defs[static_cast<std::size_t>(poiIndex)];
    if (poi.stages.empty()) {
        return fail("该 POI 没有阶段探索");
    }
    const auto* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return fail("玩家缺少位置");
    }
    ecs::PoiWorldProgress* world = ServerPoiWorldProgress();
    game::PoiProgress* progress =
        world != nullptr ? game::FindPoiProgress(&world->pois, poi.id)
                         : nullptr;
    if (progress == nullptr) {
        return fail("阶段探索尚未开始");
    }
    if (progress->completed || progress->failed) {
        return fail("当前 POI 阶段不可交互");
    }
    const game::PoiStageDef* stage =
        game::CurrentPoiStage(poi, *progress);
    if (stage == nullptr) {
        return fail("当前阶段不存在");
    }

    const auto checkKeys = [&](const game::PoiStageDef& current,
                               std::string* message) {
        std::unordered_map<std::string, int> consumed;
        for (const std::string& keyId : current.requiredKeys) {
            const game::PoiKeyDef* key = game::FindPoiKey(poi, keyId);
            if (key == nullptr) {
                *message = "阶段钥匙配置无效：" + keyId;
                return false;
            }
            if (!ServerPlayerHasPoiKeyItem(player, *key)) {
                *message = "缺少钥匙：" + game::PoiKeyDisplayName(*key);
                return false;
            }
            if (key->consumed) {
                ++consumed[key->item];
            }
        }
        const auto* container =
            m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
        for (const auto& [item, count] : consumed) {
            if (m_items->Find(item) != nullptr &&
                (container == nullptr ||
                 items::CountOf(*container, item) < count)) {
                *message = "钥匙数量不足";
                return false;
            }
        }
        return true;
    };
    const auto consumeKeys = [&](const game::PoiStageDef& current) {
        for (const std::string& keyId : current.requiredKeys) {
            const game::PoiKeyDef* key = game::FindPoiKey(poi, keyId);
            if (key == nullptr) {
                return false;
            }
            if (key->consumed &&
                !ServerConsumePoiKeyItem(player, *key)) {
                return false;
            }
            game::MarkPoiKeyAcquired(&progress->acquiredKeys, key->id);
        }
        return true;
    };

    const float dx = stage->x - position->x;
    const float dy = stage->y - position->y;
    const bool choosing =
        progress->awaitingChoice ||
        stage->kind == game::PoiStageKind::Choice;
    if (choosing) {
        constexpr float kChoiceRange = 16.0f;
        if (dx * dx + dy * dy > kChoiceRange * kChoiceRange) {
            return fail("路线选择距离过远");
        }
        const auto branches =
            game::PoiAvailableBranches(poi, *progress);
        const int branchIndex = command.payload.targetIndex;
        if (branchIndex < 0) {
            return fail("需要选择路线：1/2");
        }
        if (branchIndex >= static_cast<int>(branches.size())) {
            return fail("路线索引非法");
        }
        std::string keyError;
        if (!checkKeys(*stage, &keyError)) {
            return fail(std::move(keyError));
        }
        if (!consumeKeys(*stage)) {
            return fail("钥匙扣除失败");
        }
        const std::string branchId =
            branches[static_cast<std::size_t>(branchIndex)]->id;
        const game::PoiStageUpdate update =
            game::ChoosePoiBranch(poi, progress, branchId);
        if (!update.advanced && !update.stageCompleted) {
            return fail("路线选择无效");
        }
        SetSessionStatus(
            session,
            "路线选择：" +
                std::string(
                    branches[static_cast<std::size_t>(branchIndex)]->name
                        .empty()
                        ? branchId
                        : branches[static_cast<std::size_t>(branchIndex)]
                              ->name),
            1);
        ApplyServerPoiStageUpdate(poi, *progress, update);
        session.stateDirty = true;
        ZL_INFO("poi", "服务器路线选择：{}（{}）→ {}，玩家 {}", poi.name,
                poi.id, branchId, session.playerStableId);
        return true;
    }

    if (stage->kind != game::PoiStageKind::Interact &&
        stage->kind != game::PoiStageKind::Power) {
        return fail("当前阶段无需交互");
    }
    const float range = std::max(2.5f, stage->radius + 1.5f);
    if (dx * dx + dy * dy > range * range) {
        return fail("交互距离过远");
    }
    std::string keyError;
    if (!checkKeys(*stage, &keyError)) {
        return fail(std::move(keyError));
    }
    const bool needsFuel =
        stage->kind == game::PoiStageKind::Power &&
        m_homesteadConfig.power.enabled;
    const std::string fuelItem = m_homesteadConfig.power.fuelItem;
    auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    if (needsFuel &&
        (container == nullptr ||
         items::CountOf(*container, fuelItem) < 1)) {
        return fail("发电机需要 " + fuelItem);
    }
    if (needsFuel &&
        items::RemoveItem(*container, fuelItem, 1) != 1) {
        return fail("燃料扣除失败");
    }
    if (!consumeKeys(*stage)) {
        return fail("钥匙扣除失败");
    }
    if (stage->kind == game::PoiStageKind::Power &&
        m_homesteadConfig.power.enabled) {
        const float radius = std::max(8.0f, stage->radius + 4.0f);
        m_simulation->Ecs().Registry()
            .view<ecs::Generator, ecs::Position>()
            .each([&](const ecs::Entity, ecs::Generator& generator,
                      const ecs::Position& generatorPosition) {
                const float gx = generatorPosition.x - stage->x;
                const float gy = generatorPosition.y - stage->y;
                if (gx * gx + gy * gy > radius * radius) {
                    return;
                }
                generator.fuelMinutes +=
                    m_homesteadConfig.power.fuelMinutesPerCan;
                generator.running = true;
            });
    }
    const game::PoiStageUpdate update = game::SignalPoiStage(
        poi, progress,
        stage->kind == game::PoiStageKind::Power
            ? game::PoiStageSignal::Power
            : game::PoiStageSignal::Interact);
    if (update.blocked) {
        return fail("阶段条件未满足");
    }
    if (!update.stageCompleted && !update.poiCompleted) {
        return fail("阶段交互未完成");
    }
    SetSessionStatus(
        session,
        stage->kind == game::PoiStageKind::Power
            ? "发电机已启动：" + poi.name
            : "门禁已打开：" + poi.name,
        1);
    ApplyServerPoiStageUpdate(poi, *progress, update);
    session.stateDirty = true;
    ZL_INFO("poi", "服务器阶段交互：{} · {}，玩家 {}", poi.name, stage->id,
            session.playerStableId);
    return true;
}

void ServerHost::TickServerPoiTasks() {
    if (m_simulation == nullptr || m_poiConfig == nullptr ||
        !m_poiConfig->enabled) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.dead || session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
            continue;
        }
        entt::registry& registry = m_simulation->Ecs().Registry();
        auto* tasks = registry.try_get<ecs::PoiTasks>(player);
        if (tasks == nullptr) {
            tasks = &registry.emplace<ecs::PoiTasks>(player);
        }
        for (const game::PoiDef& poi : m_poiConfig->defs) {
            if (!poi.stages.empty() ||
                game::IsPoiCompleted(tasks->completed, poi.id)) {
                continue;
            }
            const bool searched = ServerPoiTargetSearched(poi);
            const int guards = CountServerPoiGuards(poi.id);
            if (!game::ShouldCompletePoiTask(poi, searched, guards)) {
                continue;
            }
            game::MarkPoiCompleted(&tasks->completed, poi.id);
            if (poi.task.rewardXp > 0.0f) {
                AwardProgressionXp(player, ecs::SkillKind::Survival,
                                   poi.task.rewardXp, &session);
            }
            bool rewardDropped = false;
            if (!poi.task.rewardItem.empty() && m_items != nullptr &&
                m_items->Find(poi.task.rewardItem) != nullptr) {
                const auto* position = registry.try_get<ecs::Position>(player);
                if (position != nullptr) {
                    rewardDropped = ReturnServerItem(
                        player,
                        ecs::ItemInstance{poi.task.rewardItem,
                                          std::max(1, poi.task.rewardCount),
                                          1.0f},
                        position->x, position->y, position->z);
                }
            }
            session.stateDirty = true;
            std::string status =
                "POI 任务完成：" + poi.name + "（" +
                std::to_string(static_cast<int>(poi.task.rewardXp)) +
                " 经验）";
            if (rewardDropped) {
                status += "（背包已满，奖励掉落脚下）";
            }
            SetSessionStatus(session, status, 1);
            ZL_INFO("poi", "服务器任务完成：{}（{}），玩家 {}", poi.name, poi.id,
                    session.playerStableId);
        }
    }
}

void ServerHost::TickServerObjectives() {
    if (m_simulation == nullptr || m_config == nullptr) {
        return;
    }
    const float extractionX = static_cast<float>(
        m_config->Get<double>("objective.extractionX", 66.5));
    const float extractionY = static_cast<float>(
        m_config->Get<double>("objective.extractionY", 28.5));
    const float extractionRadius = std::max(
        0.1f, static_cast<float>(
                  m_config->Get<double>("objective.extractionRadius", 1.5)));
    const float radiusSq = extractionRadius * extractionRadius;
    const std::string radioPart =
        m_config->Get<std::string>("objective.radioPart", "radio_part");

    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.dead || session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
            continue;
        }
        entt::registry& registry = m_simulation->Ecs().Registry();
        auto* objective = registry.try_get<ecs::Objective>(player);
        const bool created = objective == nullptr;
        if (objective == nullptr) {
            objective = &registry.emplace<ecs::Objective>(player);
        }
        const auto* container = registry.try_get<ecs::Container>(player);
        const bool hasPart =
            container != nullptr && items::CountOf(*container, radioPart) > 0;
        const auto* position = registry.try_get<ecs::Position>(player);
        const bool atExtraction =
            position != nullptr &&
            (position->x - extractionX) * (position->x - extractionX) +
                    (position->y - extractionY) *
                        (position->y - extractionY) <=
                radiusSq;
        const ecs::ObjectiveStep next =
            game::AdvanceObjective(objective->step, hasPart, atExtraction);

        if (created || next != objective->step) {
            objective->step = next;
            session.stateDirty = true;
        }
    }
}

void ServerHost::ActivateServerMission(SessionPlayer& session,
                                       const game::MissionDef& def, int day,
                                       std::string_view settlementId) {
    if (m_simulation == nullptr || session.playerStableId == 0) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    auto* log = registry.try_get<ecs::MissionLog>(player);
    if (log == nullptr) {
        log = &registry.emplace<ecs::MissionLog>(player);
    }
    ecs::MissionRunState* state = game::FindMissionState(log, def.id);
    if (state == nullptr) {
        log->missions.push_back(ecs::MissionRunState{});
        state = &log->missions.back();
    }
    state->id = def.id;
    state->progress.assign(def.objectives.size(), 0);
    state->status = ecs::MissionRunStatus::Active;
    state->day = day;
    state->rewardClaimed = false;
    state->settlementId.assign(settlementId);
    session.stateDirty = true;
}

void ServerHost::UpdateServerMissionAvailability(SessionPlayer& session,
                                                 int day) {
    if (m_missionConfig == nullptr || m_simulation == nullptr ||
        session.playerStableId == 0) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    auto* log = registry.try_get<ecs::MissionLog>(player);
    if (log == nullptr) {
        return;
    }
    for (const game::MissionDef& def : m_missionConfig->defs) {
        if (!def.autoActivate) {
            continue;
        }
        if (game::MissionCanActivate(def, *log, day)) {
            ActivateServerMission(session, def, day);
        }
    }
}

void ServerHost::RecordServerMissionEvent(SessionPlayer& session,
                                          game::MissionObjectiveKind kind,
                                          std::string_view target, int amount) {
    if (m_simulation == nullptr || m_missionConfig == nullptr ||
        !m_missionConfig->enabled || amount <= 0 ||
        session.playerStableId == 0) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    auto* log = registry.try_get<ecs::MissionLog>(player);
    if (log == nullptr) {
        return;
    }
    for (ecs::MissionRunState& state : log->missions) {
        if (state.status != ecs::MissionRunStatus::Active) {
            continue;
        }
        const game::MissionDef* def = m_missionConfig->Find(state.id);
        if (def == nullptr) {
            continue;
        }
        state.progress.resize(def->objectives.size(), 0);
        for (std::size_t i = 0; i < def->objectives.size(); ++i) {
            const game::MissionObjectiveDef& objective = def->objectives[i];
            if (objective.kind != kind) {
                continue;
            }
            if (objective.target != "any" && objective.target != target) {
                continue;
            }
            state.progress[i] =
                std::min(objective.count, state.progress[i] + amount);
        }
    }
    session.stateDirty = true;
}

void ServerHost::OnServerLootSearched(SessionPlayer& session,
                                      std::string_view tableId) {
    RecordServerMissionEvent(session, game::MissionObjectiveKind::Loot,
                             tableId);
}

bool ServerHost::GrantServerMissionReward(ecs::Entity player,
                                          const game::MissionDef& def,
                                          bool* itemDropped) {
    if (itemDropped != nullptr) {
        *itemDropped = false;
    }
    if (m_simulation == nullptr || player == entt::null) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (def.reward.xp > 0.0f) {
        AwardProgressionXp(player, ServerMissionSkillKind(def.reward.skill),
                           def.reward.xp, nullptr);
    }
    const auto* position = registry.try_get<ecs::Position>(player);
    const auto grantItem = [&](std::string_view itemId, int count) {
        if (itemId.empty() || m_items == nullptr ||
            m_items->Find(itemId) == nullptr || position == nullptr) {
            return;
        }
        const bool dropped = ReturnServerItem(
            player,
            ecs::ItemInstance{std::string(itemId), std::max(1, count), 1.0f},
            position->x, position->y, position->z);
        if (itemDropped != nullptr && dropped) {
            *itemDropped = true;
        }
    };
    grantItem(def.reward.item, def.reward.itemCount);
    grantItem(def.reward.extraItem, def.reward.extraItemCount);
    if (!def.reward.blueprint.empty()) {
        auto* known = registry.try_get<ecs::KnownRecipes>(player);
        if (known == nullptr) {
            known = &registry.emplace<ecs::KnownRecipes>(player);
        }
        if (std::find(known->recipeIds.begin(), known->recipeIds.end(),
                      def.reward.blueprint) == known->recipeIds.end()) {
            known->recipeIds.push_back(def.reward.blueprint);
        }
    }
    return true;
}

void ServerHost::CompleteServerMission(SessionPlayer& session,
                                       ecs::MissionRunState& state,
                                       const game::MissionDef& def) {
    if (state.status != ecs::MissionRunStatus::Active) {
        return;
    }
    state.status = ecs::MissionRunStatus::Completed;
    state.rewardClaimed = true;
    bool rewardDropped = false;
    int strategicContribution = 0;
    int reputationDelta = 0;
    if (m_simulation != nullptr) {
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player != entt::null) {
            GrantServerMissionReward(player, def, &rewardDropped);
            if (!state.settlementId.empty() && m_settlements != nullptr) {
                if (const game::SettlementDef* settlement =
                        m_settlements->Find(state.settlementId);
                    settlement != nullptr) {
                    const std::string key =
                        "mission:" + state.settlementId + ":" + def.id + ":" +
                        std::to_string(state.day) + ":complete";
                    const game::SettlementReputationChange change =
                        game::AwardSettlementReputation(
                            m_simulation->Ecs(), player,
                            m_settlements->reputation, state.settlementId,
                            game::SettlementReputationEvent::MissionComplete,
                            key, settlement->startingReputation, state.day);
                    reputationDelta = change.delta;
                    if (change.applied && change.delta != 0) {
                        ZL_INFO("settlement",
                                "服务器任务声望：{} {} {} → {}",
                                settlement->name, def.id, change.delta,
                                change.current);
                    }
                }
            }
        }
        std::string strategicError;
        if (m_simulation->GrantStrategicMissionReward(def.id,
                                                       &strategicError)) {
            m_strategicDirty = true;
            const game::StrategicConfig* settings =
                m_simulation->StrategicSettings();
            const game::StrategicMissionRewardConfig* reward =
                settings != nullptr
                    ? settings->economy.FindMission(def.id)
                    : nullptr;
            if (reward != nullptr && !reward->regionId.empty() &&
                m_simulation->Strategic() != nullptr &&
                m_simulation->Strategic()->FindRegion(reward->regionId) !=
                    nullptr &&
                settings->contribution.mission > 0) {
                strategicContribution = settings->contribution.mission;
                AddStrategicContribution(session.playerStableId,
                                         reward->regionId,
                                         strategicContribution);
            }
        } else if (!strategicError.empty()) {
            ZL_WARN("strategic", "服务器任务战略奖励失败：{}（{}）", def.id,
                    strategicError);
        }
    }
    session.stateDirty = true;
    std::string text = "任务完成：" + def.name;
    if (!def.reward.label.empty()) {
        text += " · " + def.reward.label;
    }
    if (strategicContribution > 0) {
        text += " · 战略贡献 +" + std::to_string(strategicContribution);
    }
    if (reputationDelta != 0) {
        text += " · 声望 " + std::to_string(reputationDelta);
    }
    if (rewardDropped) {
        text += "（背包已满，奖励掉落脚下）";
    }
    SetSessionStatus(session, text, 1);
    ZL_INFO("missions", "服务器任务完成：{}（{}），玩家 {}", def.name, def.id,
            session.playerStableId);
    if (strategicContribution > 0) {
        ZL_INFO("strategic", "任务 {} 计入战略贡献 +{}，玩家 {}", def.id,
                strategicContribution, session.playerStableId);
    }
}

void ServerHost::EnsureServerRandomEventContent(int day) {
    if (m_simulation == nullptr || m_lootTables == nullptr ||
        m_missionConfig == nullptr) {
        return;
    }
    game::RandomEventSpawnCallbacks callbacks;
    callbacks.walkable = [this](int x, int y) {
        return !game::IsSquareBlocked(m_simulation->World(), x, y, 0);
    };
    callbacks.spawnZombie =
        [this](float x, float y, ecs::ZombieVariantKind variant,
               std::uint64_t stableId) {
            return m_simulation->SpawnZombieAt(x, y, variant, stableId);
        };
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        const auto* position =
            m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
        const auto* log =
            m_simulation->Ecs().Registry().try_get<ecs::MissionLog>(player);
        if (position == nullptr || log == nullptr) {
            continue;
        }
        for (const ecs::MissionRunState& state : log->missions) {
            if (state.status != ecs::MissionRunStatus::Active) {
                continue;
            }
            const game::MissionDef* def = m_missionConfig->Find(state.id);
            if (def == nullptr || def->kind != game::MissionKind::Random) {
                continue;
            }
            const game::RandomEventKind kind =
                game::RandomEventKindForMission(def->id);
            if (kind == game::RandomEventKind::None) {
                continue;
            }
            int progress = 0;
            for (const int value : state.progress) {
                progress += std::max(0, value);
            }
            const std::string key = def->id + ":" + std::to_string(day);
            if (progress > 0 || m_serverEventReady.contains(key)) {
                continue;
            }
            game::RandomEventSpawnResult result;
            if (!game::EnsureRandomEventContent(
                    m_simulation->Ecs(), *m_lootTables, def->id, day, progress,
                    position->x, position->y, callbacks, &result)) {
                continue;
            }
            m_serverEventReady.insert(key);
            if (kind == game::RandomEventKind::ColdSnap &&
                m_serverColdSnapEvents.insert(key).second) {
                m_simulation->SetWeatherOverride(game::WeatherKind::ColdSnap,
                                                 15.0f);
            }
            ZL_INFO("events", "服务器随机事件就绪：{}（{}，站点 ({:.1f},{:.1f})）",
                    def->name, game::RandomEventKindName(kind),
                    static_cast<double>(result.siteX),
                    static_cast<double>(result.siteY));
        }
    }
}

void ServerHost::RecordSiegeDefenseDamage(std::uint64_t playerStableId,
                                          ecs::Entity target, float damage) {
    if (m_simulation == nullptr || playerStableId == 0 ||
        !(damage > 0.0f) || !std::isfinite(damage) ||
        FindSessionByPlayerStableId(playerStableId) == nullptr) {
        return;
    }
    const game::RegionalSiegeSnapshot& siege =
        m_simulation->RegionalSiegeStatus();
    if (!siege.active || siege.settlementId.empty()) {
        return;
    }
    if (siege.regionId != m_siegeDefenseRegion) {
        ResetSiegeDefenseLedger();
        m_siegeDefenseRegion = siege.regionId;
    }
    const auto* raid =
        m_simulation->Ecs().Registry().try_get<game::SettlementRaidZombie>(
            target);
    if (raid == nullptr || raid->settlementId != siege.settlementId) {
        return;
    }
    m_siegeDefenseDamage[playerStableId] += damage;
    m_siegeTargetDamage[m_simulation->Ecs().IdOf(target).id][playerStableId] +=
        damage;
}

void ServerHost::ResetSiegeDefenseLedger() {
    m_siegeDefenseRegion.clear();
    m_siegeDefenseDamage.clear();
    m_siegeTargetDamage.clear();
    m_siegeGuardKills.clear();
}

void ServerHost::CheckSiegeGuardKills() {
    if (m_simulation == nullptr) {
        return;
    }
    const game::RegionalSiegeSnapshot& siege =
        m_simulation->RegionalSiegeStatus();
    const game::StrategicConfig* settings =
        m_simulation->StrategicSettings();
    if (!siege.active || siege.settlementId.empty() || settings == nullptr ||
        settings->contribution.defenseKillAssistPoints <= 0) {
        return;
    }
    auto& registry = m_simulation->Ecs().Registry();
    registry.view<game::SettlementRaidZombie, ecs::Health>().each(
        [&](const ecs::Entity entity, const game::SettlementRaidZombie& raid,
            const ecs::Health& health) {
            if (raid.settlementId != siege.settlementId ||
                health.current > 0.0f) {
                return;
            }
            const std::uint64_t targetId =
                m_simulation->Ecs().IdOf(entity).id;
            if (!m_siegeGuardKills.insert(targetId).second) {
                return;
            }
            const auto* last =
                registry.try_get<ecs::LastDamage>(entity);
            if (last == nullptr || last->sourceId == 0) {
                return;
            }
            const ecs::Entity source =
                m_simulation->Ecs().FindById(last->sourceId);
            if (source == entt::null) {
                return;
            }
            const auto* npc = registry.try_get<game::SettlementNpc>(source);
            if (npc == nullptr || npc->kind != game::SettlementNpcKind::Guard ||
                npc->settlementId != siege.settlementId) {
                return;
            }
            const auto damageIt = m_siegeTargetDamage.find(targetId);
            if (damageIt == m_siegeTargetDamage.end()) {
                return;
            }
            const auto points = game::AllocateSiegeKillAssist(
                damageIt->second,
                settings->contribution.defenseKillAssistPoints);
            for (const auto& [stableId, value] : points) {
                if (value > 0) {
                    m_siegeDefenseDamage[stableId] +=
                        static_cast<float>(value);
                    ZL_INFO("strategic",
                            "守卫击杀助攻：目标 {} 玩家 {} +{} 点",
                            targetId, stableId, value);
                }
            }
        });
}

void ServerHost::DrainFireDamageCredits() {
    if (m_simulation == nullptr || m_simulation->Fire() == nullptr) {
        return;
    }
    for (const game::FireDamageCredit& credit :
         m_simulation->Fire()->TakeDamageCredits()) {
        if (credit.amount <= 0.0f || credit.ownerStableId == 0 ||
            credit.targetStableId == 0) {
            continue;
        }
        const ecs::Entity target =
            m_simulation->Ecs().FindById(credit.targetStableId);
        if (target != entt::null) {
            RecordSiegeDefenseDamage(credit.ownerStableId, target,
                                     credit.amount);
        }
    }
}

void ServerHost::TickServerMissions() {
    if (m_simulation == nullptr || m_missionConfig == nullptr ||
        !m_missionConfig->enabled) {
        return;
    }
    // 击杀目标按本 tick 的实际命中记录归属到攻击者；与单机 CombatStats 不同，
    // 服务器必须按玩家拆分，避免多人共享击杀数。
    const game::RegionalSiegeSnapshot& siege =
        m_simulation->RegionalSiegeStatus();
    if (siege.active && siege.regionId != m_siegeDefenseRegion) {
        ResetSiegeDefenseLedger();
        m_siegeDefenseRegion = siege.regionId;
    }
    if (characters::CombatSystem* combat = m_simulation->Combat();
        combat != nullptr) {
        for (const characters::CombatSystem::HitInfo& hit : combat->GetHits()) {
            if (hit.attacker == entt::null || hit.target == entt::null) {
                continue;
            }
            const std::uint64_t attackerId =
                m_simulation->Ecs().IdOf(hit.attacker).id;
            SessionPlayer* attackerSession =
                FindSessionByPlayerStableId(attackerId);
            if (attackerSession == nullptr) {
                continue;
            }
            RecordSiegeDefenseDamage(attackerId, hit.target, hit.damage);
            if (!hit.lethal) {
                continue;
            }
            std::string target = "any";
            if (const auto* actor =
                    m_simulation->Ecs().Registry().try_get<ecs::RandomEventActor>(
                        hit.target);
                actor != nullptr) {
                target = actor->eventId;
            }
            RecordServerMissionEvent(*attackerSession,
                                     game::MissionObjectiveKind::Kill, target,
                                     1);
        }
    }

    const int day =
        m_gameTime != nullptr
            ? 1 + static_cast<int>(
                      m_gameTime->TotalGameMinutes() / (24.0 * 60.0))
            : 1;
    if (m_serverEventDay == 0) {
        m_serverEventDay = day;
    } else if (m_serverEventDay != day) {
        std::vector<ecs::Entity> actors;
        m_simulation->Ecs().Registry().view<ecs::RandomEventActor>().each(
            [&](const ecs::Entity entity, const ecs::RandomEventActor&) {
                actors.push_back(entity);
            });
        for (const ecs::Entity actor : actors) {
            m_simulation->Ecs().Destroy(actor);
        }
        m_serverEventReady.clear();
        m_serverColdSnapEvents.clear();
        m_serverEventDay = day;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.dead || session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
            continue;
        }
        entt::registry& registry = m_simulation->Ecs().Registry();
        auto* log = registry.try_get<ecs::MissionLog>(player);
        if (log == nullptr) {
            log = &registry.emplace<ecs::MissionLog>(player);
        }
        if (log->lastDay != day) {
            log->missions.erase(
                std::remove_if(log->missions.begin(), log->missions.end(),
                               [&](const ecs::MissionRunState& state) {
                                   const game::MissionDef* def =
                                       m_missionConfig->Find(state.id);
                                   return def != nullptr &&
                                          def->kind == game::MissionKind::Random;
                               }),
                log->missions.end());
            log->lastDay = day;
            session.stateDirty = true;
        }
        bool dailyRefreshed = false;
        for (const game::MissionDef& def : m_missionConfig->defs) {
            if (def.kind != game::MissionKind::Daily || day < def.minDay ||
                !def.autoActivate) {
                continue;
            }
            ecs::MissionRunState* state =
                game::FindMissionState(log, def.id);
            if (state == nullptr || state->day != day) {
                dailyRefreshed = dailyRefreshed || state != nullptr;
                ActivateServerMission(session, def, day);
            }
        }
        if (log->randomDay != day) {
            for (const game::MissionDef& def : m_missionConfig->defs) {
                if (def.kind != game::MissionKind::Random ||
                    day < def.minDay || !def.autoActivate) {
                    continue;
                }
                if (game::MissionRandomRoll(m_missionConfig->randomSeed, day,
                                            def.id, def.chance)) {
                    ActivateServerMission(session, def, day);
                }
            }
            log->randomDay = day;
            session.stateDirty = true;
        }
        UpdateServerMissionAvailability(session, day);

        const auto* position = registry.try_get<ecs::Position>(player);
        const auto* poiTasks = registry.try_get<ecs::PoiTasks>(player);
        for (ecs::MissionRunState& state : log->missions) {
            if (state.status != ecs::MissionRunStatus::Active) {
                continue;
            }
            const game::MissionDef* def = m_missionConfig->Find(state.id);
            if (def == nullptr) {
                state.status = ecs::MissionRunStatus::Failed;
                session.stateDirty = true;
                continue;
            }
            if (game::MissionIsExpired(*def, state, day)) {
                state.status = ecs::MissionRunStatus::Failed;
                state.rewardClaimed = false;
                state.day = day;
                int reputationDelta = 0;
                if (!state.settlementId.empty() && m_settlements != nullptr) {
                    if (const game::SettlementDef* settlement =
                            m_settlements->Find(state.settlementId);
                        settlement != nullptr) {
                        const std::string key =
                            "mission:" + state.settlementId + ":" + def->id +
                            ":" + std::to_string(day) + ":fail";
                        const game::SettlementReputationChange change =
                            game::AwardSettlementReputation(
                                m_simulation->Ecs(), player,
                                m_settlements->reputation,
                                state.settlementId,
                                game::SettlementReputationEvent::MissionTimeout,
                                key, settlement->startingReputation, day);
                        reputationDelta = change.delta;
                    }
                }
                session.stateDirty = true;
                std::string text =
                    "任务失败：" + def->name + "（超时）";
                if (reputationDelta != 0) {
                    text += " · 声望 " + std::to_string(reputationDelta);
                }
                SetSessionStatus(session, std::move(text), 2);
                ZL_INFO("missions", "服务器任务超时：{}（{}），玩家 {}",
                        def->name, def->id, session.playerStableId);
                continue;
            }
            const bool hadProgress = !state.progress.empty();
            state.progress.resize(def->objectives.size(), 0);
            if (!hadProgress) {
                session.stateDirty = true;
            }
            for (std::size_t i = 0; i < def->objectives.size(); ++i) {
                const game::MissionObjectiveDef& objective = def->objectives[i];
                int value = state.progress[i];
                if (objective.kind == game::MissionObjectiveKind::CollectItem) {
                    value = std::min(
                        objective.count,
                        ServerCountMissionItem(m_simulation->Ecs(), player,
                                               objective.target));
                } else if (objective.kind == game::MissionObjectiveKind::Poi) {
                    if (poiTasks != nullptr &&
                        game::IsPoiCompleted(poiTasks->completed,
                                             objective.target)) {
                        value = objective.count;
                    }
                } else if (objective.kind == game::MissionObjectiveKind::Loot &&
                           objective.target != "any") {
                    bool found = false;
                    registry.view<ecs::LootContainer>().each(
                        [&](const ecs::Entity entity,
                            const ecs::LootContainer& loot) {
                            if (!found && m_simulation->Ecs().Alive(entity) &&
                                loot.searched &&
                                loot.tableId == objective.target) {
                                found = true;
                            }
                        });
                    if (found) {
                        value = objective.count;
                    }
                } else if (objective.kind ==
                               game::MissionObjectiveKind::Reach &&
                           position != nullptr) {
                    const float dx = position->x - objective.x;
                    const float dy = position->y - objective.y;
                    if (dx * dx + dy * dy <=
                        objective.radius * objective.radius) {
                        value = objective.count;
                    }
                }
                if (value > state.progress[i]) {
                    state.progress[i] = value;
                    session.stateDirty = true;
                }
            }
            if (game::IsMissionComplete(*def, state)) {
                CompleteServerMission(session, state, *def);
            }
        }
        if (dailyRefreshed) {
            session.stateDirty = true;
        }
        // 每帧标脏一次，确保客户端 6 tick 的状态包能收到重连/新任务；
        // 任务数量很小，状态编码成本可接受。
        session.stateDirty = true;
    }
    EnsureServerRandomEventContent(day);
}

void ServerHost::TickCrafting(float dt) {
    if (m_inventory == nullptr || m_simulation == nullptr) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        game::InventoryResult result;
        if (!m_inventory->TickCrafting(player, dt, &result)) {
            continue;
        }
        session.stateDirty = true;
        if (result.ok && result.tone == game::InventoryTone::Ok) {
            SpawnCraftEffect(session.playerStableId);
            AwardProgressionXp(player, ecs::SkillKind::Crafting,
                               m_progressionConfig.craftXpPerItem, &session);
        }
        if (!result.message.empty()) {
            SetSessionStatus(session, result.message,
                             static_cast<std::uint8_t>(result.tone));
        }
    }
}

void ServerHost::ApplyProgressionRuntime(ecs::Entity player, SessionPlayer* session) {
    if (m_simulation == nullptr || player == entt::null) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    auto* progression = registry.try_get<ecs::Progression>(player);
    if (progression == nullptr) {
        return;
    }
    game::RefreshDerived(*progression, m_progressionConfig);
    if (auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        container->capacityKg =
            m_baseCarryKg + progression->carryCapacityBonusKg;
    }
    if (auto* control = registry.try_get<ecs::LocalControl>(player);
        control != nullptr) {
        control->walkSpeed =
            m_baseWalkSpeed *
            game::MoveSpeedMultiplier(*progression, m_progressionConfig);
    }
    if (session != nullptr) {
        session->stateDirty = true;
    }
}

void ServerHost::MarkProgressionDirty(ecs::Entity player) {
    if (m_simulation == nullptr || player == entt::null) {
        return;
    }
    const std::uint64_t stableId = m_simulation->Ecs().IdOf(player).id;
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == stableId) {
            session.stateDirty = true;
            return;
        }
    }
}

void ServerHost::AwardProgressionXp(ecs::Entity player, ecs::SkillKind kind,
                                    float baseAmount, SessionPlayer* session) {
    if (m_simulation == nullptr || player == entt::null ||
        !(baseAmount > 0.0f)) {
        return;
    }
    auto* progression =
        m_simulation->Ecs().Registry().try_get<ecs::Progression>(player);
    if (progression == nullptr) {
        return;
    }
    const int oldLevel = progression->level;
    const game::ProgressionAward award =
        game::AwardXp(*progression, kind, baseAmount, m_progressionConfig);
    ApplyProgressionRuntime(player, session);
    MarkProgressionDirty(player);
    if (award.characterLevels > 0 && session != nullptr) {
        SetSessionStatus(*session,
                         "升级到 Lv." + std::to_string(progression->level) +
                             "，可分配 Perk ×" +
                             std::to_string(progression->perkPoints),
                         1);
    } else if (award.characterLevels > 0 && oldLevel != progression->level) {
        ZL_INFO("progression", "服务器玩家升级：Lv.{}", progression->level);
    }
}

void ServerHost::TickProgression() {
    if (m_simulation == nullptr || m_simulation->Combat() == nullptr ||
        !m_progressionConfig.enabled) {
        return;
    }
    std::vector<ecs::Entity> changed;
    game::AwardCombatHits(m_simulation->Ecs(), *m_simulation->Combat(),
                          m_progressionConfig, &changed);
    std::unordered_set<std::uint64_t> changedIds;
    for (const ecs::Entity entity : changed) {
        if (entity != entt::null && m_simulation->Ecs().Alive(entity)) {
            changedIds.insert(m_simulation->Ecs().IdOf(entity).id);
        }
    }

    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0 || m_simulation == nullptr) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        auto* progression =
            m_simulation->Ecs().Registry().try_get<ecs::Progression>(player);
        if (progression == nullptr) {
            continue;
        }
        if (changedIds.contains(session.playerStableId)) {
            ApplyProgressionRuntime(player, &session);
            session.stateDirty = true;
        }

        const double now = m_gameTime != nullptr ? m_gameTime->TotalGameMinutes()
                                                 : 0.0;
        if (session.lastProgressionMinute < 0.0 || session.dead) {
            session.lastProgressionMinute = now;
            continue;
        }
        const double delta = now - session.lastProgressionMinute;
        if (delta < 60.0) {
            continue;
        }
        const int hours = static_cast<int>(std::floor(delta / 60.0));
        session.lastProgressionMinute += static_cast<double>(hours) * 60.0;
        AwardProgressionXp(
            player, ecs::SkillKind::Survival,
            m_progressionConfig.survivalXpPerHour * static_cast<float>(hours),
            &session);
    }
}

bool ServerHost::FireProjectile(SessionPlayer& session, const net::Command& command) {
    if (m_simulation == nullptr) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    if (player == entt::null || position == nullptr) {
        return false;
    }

    float dirX = command.payload.x;
    float dirY = command.payload.y;
    if (command.payload.entityId != 0) {
        const ecs::Entity target =
            m_simulation->Ecs().FindById(command.payload.entityId);
        const auto* targetPosition =
            target != entt::null ? registry.try_get<ecs::Position>(target) : nullptr;
        const auto* health =
            target != entt::null ? registry.try_get<ecs::Health>(target) : nullptr;
        if (target == entt::null || !registry.all_of<ecs::AiState>(target) ||
            targetPosition == nullptr || health == nullptr ||
            health->current <= 0.0f) {
            return false;
        }
        dirX = targetPosition->x - position->x;
        dirY = targetPosition->y - position->y;
        const float distance = std::sqrt(dirX * dirX + dirY * dirY);
        if (distance > m_maxProjectileDistance) {
            ++m_networkStats.rangeRejected;
            return false;
        }
    }
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001f) {
        const auto* facing = registry.try_get<ecs::Facing>(player);
        const ecs::Direction dir =
            facing != nullptr ? facing->dir : ecs::Direction::Down;
        switch (dir) {
            case ecs::Direction::Down:
                dirX = 1.0f;
                dirY = 1.0f;
                break;
            case ecs::Direction::Left:
                dirX = -1.0f;
                dirY = 1.0f;
                break;
            case ecs::Direction::Right:
                dirX = 1.0f;
                dirY = -1.0f;
                break;
            case ecs::Direction::Up:
                dirX = -1.0f;
                dirY = -1.0f;
                break;
        }
        length = std::sqrt(dirX * dirX + dirY * dirY);
    }
    if (length < 0.0001f) {
        return false;
    }
    dirX /= length;
    dirY /= length;
    if (session.projectileUsed &&
        m_ticks < session.lastProjectileTick + m_projectileCooldownTicks) {
        ++m_networkStats.cooldownRejected;
        return false;
    }

    const ecs::Entity projectile = m_simulation->Ecs().Create();
    if (projectile == entt::null) {
        return false;
    }
    registry.emplace<ecs::Position>(
        projectile, ecs::Position{position->x + dirX * 0.35f,
                                  position->y + dirY * 0.35f, position->z});
    registry.emplace<ecs::Velocity>(
        projectile, ecs::Velocity{dirX * m_projectileSpeed, dirY * m_projectileSpeed});
    registry.emplace<ecs::SpriteRef>(
        projectile, ecs::SpriteRef{ecs::HashName("projectile.bullet"), 1,
                                   0xFFFFC040u});
    ecs::Projectile value;
    value.ownerStableId = session.playerStableId;
    value.lifetime = m_projectileLifetime;
    value.damage = m_projectileDamage;
    value.startX = position->x;
    value.startY = position->y;
    registry.emplace<ecs::Projectile>(projectile, value);

    session.projectileUsed = true;
    session.lastProjectileTick = m_ticks;
    auto& action = registry.get_or_emplace<ecs::ActionState>(player);
    action.action = ecs::ActorAction::Attacking;
    action.elapsed = 0.0f;
    action.duration = 0.28f;
    ++m_networkStats.projectilesFired;
    ResetServerStealth(session, player, "远程攻击");
    return true;
}

bool ServerHost::FireRanged(SessionPlayer& session, const net::Command& command) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    auto* equipment =
        player != entt::null ? registry.try_get<ecs::Equipment>(player) : nullptr;
    if (player == entt::null || position == nullptr || equipment == nullptr ||
        session.dead) {
        return false;
    }
    if (const auto* health = registry.try_get<ecs::Health>(player);
        health != nullptr && health->current <= 0.0f) {
        return false;
    }
    auto& slot =
        equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
    if (!slot.has_value()) {
        return false;
    }
    const items::ItemDef* def = m_items->Find(slot->defId);
    if (def == nullptr || !def->ranged.has_value()) {
        return false;
    }
    std::string reason;
    if (!session.rangedWeapon.CanFire(*def, *slot, &reason)) {
        if (reason == "cooldown") {
            ++m_networkStats.cooldownRejected;
        }
        SetSessionStatus(session, "fire: " + reason, reason == "cooldown" ? 1 : 2);
        return false;
    }

    float dirX = command.payload.x;
    float dirY = command.payload.y;
    if (!std::isfinite(dirX) || !std::isfinite(dirY)) {
        return false;
    }
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001f) {
        const auto* facing = registry.try_get<ecs::Facing>(player);
        const ecs::Direction dir =
            facing != nullptr ? facing->dir : ecs::Direction::Down;
        switch (dir) {
            case ecs::Direction::Down:
                dirX = 1.0f;
                dirY = 1.0f;
                break;
            case ecs::Direction::Left:
                dirX = -1.0f;
                dirY = 1.0f;
                break;
            case ecs::Direction::Right:
                dirX = 1.0f;
                dirY = -1.0f;
                break;
            case ecs::Direction::Up:
                dirX = -1.0f;
                dirY = -1.0f;
                break;
        }
        length = std::sqrt(dirX * dirX + dirY * dirY);
    }
    if (length < 0.0001f) {
        return false;
    }
    dirX /= length;
    dirY /= length;

    const items::RangedCombat& ranged = *def->ranged;
    --slot->loadedAmmo;
    session.rangedWeapon.cooldownLeft = std::max(0.0f, ranged.cooldownSeconds);
    if (ranged.wearPerShot > 0.0f) {
        (void)items::WearEquippedWeapon(*equipment, ranged.wearPerShot);
    }
    ++session.rangedShotSerial;

    const float speed = std::max(0.1f, ranged.projectileSpeed);
    const float range = std::max(0.1f, ranged.range);
    const float baseAngle = std::atan2(dirY, dirX);
    for (int i = 0; i < ranged.pellets; ++i) {
        const float offset = glm::radians(items::RangedSpreadOffsetDegrees(
            ranged, session.rangedShotSerial, static_cast<std::uint32_t>(i)));
        const float angle = baseAngle + offset;
        const float px = std::cos(angle);
        const float py = std::sin(angle);
        const ecs::Entity projectile = m_simulation->Ecs().Create();
        if (projectile == entt::null) {
            continue;
        }
        registry.emplace<ecs::Position>(
            projectile,
            ecs::Position{position->x + px * 0.35f,
                          position->y + py * 0.35f, position->z});
        registry.emplace<ecs::Velocity>(
            projectile, ecs::Velocity{px * speed, py * speed});
        registry.emplace<ecs::SpriteRef>(
            projectile, ecs::SpriteRef{ecs::HashName("projectile.bullet"), 1,
                                       0xFFFFC040u});
        ecs::Projectile value;
        value.ownerStableId = session.playerStableId;
        value.lifetime = std::clamp(range / speed + 0.25f, 0.1f, 10.0f);
        value.radius = 0.2f;
        value.damage = ranged.damage;
        value.knockback = 0.0f;
        value.staggerSeconds = 0.0f;
        value.hitPlayers = m_pvpEnabled;
        value.startX = position->x;
        value.startY = position->y;
        value.maxDistance = range;
        value.falloffStart = ranged.falloffStart;
        value.falloffEnd = ranged.falloffEnd;
        value.falloffScale = ranged.falloffScale;
        registry.emplace<ecs::Projectile>(projectile, value);
        ++m_networkStats.projectilesFired;
    }

    EmitGunshot(session.playerStableId, ranged.noiseRadius);
    auto& action = registry.get_or_emplace<ecs::ActionState>(player);
    action.action = ecs::ActorAction::Attacking;
    action.elapsed = 0.0f;
    action.duration = 0.22f;
    session.stateDirty = true;
    ResetServerStealth(session, player, "远程攻击");
    SetSessionStatus(session,
                     "fire: " + def->name + " " +
                         std::to_string(slot->loadedAmmo) + "/" +
                         std::to_string(ranged.magazine),
                     0);
    return true;
}

bool ServerHost::ReloadRanged(SessionPlayer& session) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    auto* equipment =
        player != entt::null ? registry.try_get<ecs::Equipment>(player) : nullptr;
    auto* bag = player != entt::null ? registry.try_get<ecs::Container>(player)
                                     : nullptr;
    if (player == entt::null || equipment == nullptr || bag == nullptr ||
        session.dead) {
        return false;
    }
    if (const auto* health = registry.try_get<ecs::Health>(player);
        health != nullptr && health->current <= 0.0f) {
        return false;
    }
    auto& slot =
        equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
    if (!slot.has_value()) {
        return false;
    }
    const items::ItemDef* def = m_items->Find(slot->defId);
    if (def == nullptr || !def->ranged.has_value()) {
        return false;
    }
    const int available = items::CountOf(*bag, def->ranged->ammoDefId);
    std::string reason;
    if (!session.rangedWeapon.StartReload(*def, *slot, available, &reason)) {
        SetSessionStatus(session, "reload: " + reason, 2);
        return false;
    }
    session.stateDirty = true;
    SetSessionStatus(session,
                     "reloading " + def->name + " (" +
                         std::to_string(session.rangedWeapon.reloadAmount) +
                         " rounds)",
                     0);
    return true;
}

bool ServerHost::ThrowItemAction(SessionPlayer& session,
                                 const net::Command& command,
                                 std::string* error) {
    auto fail = [&](const std::string& message) {
        ++m_networkStats.throwsRejected;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_items == nullptr) {
        return fail("投掷系统未启用");
    }
    if (command.payload.index < -1 ||
        command.payload.index > 1000000 ||
        command.payload.targetIndex != -1 ||
        command.payload.flags != 0 || command.payload.area != 0 ||
        command.payload.targetArea != 0 ||
        command.payload.entityId != net::kNoEntity ||
        command.payload.targetEntityId != net::kNoEntity) {
        return fail("投掷命令字段非法");
    }
    if (command.payload.x < -1000000.0f ||
        command.payload.x > 1000000.0f ||
        command.payload.y < -1000000.0f ||
        command.payload.y > 1000000.0f) {
        return fail("投掷落点非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    if (player == entt::null || position == nullptr || session.dead ||
        position->z < 0 || position->z >= core::kMaxZ) {
        return fail("玩家不可投掷");
    }
    if (const auto* health = registry.try_get<ecs::Health>(player);
        health == nullptr || !std::isfinite(health->current) ||
        health->current <= 0.0f ||
        registry.all_of<ecs::Dying>(player)) {
        return fail("倒地/死亡状态无法投掷");
    }
    if (session.drivingVehicleId != 0 ||
        registry.all_of<ecs::VehicleOccupant>(player)) {
        return fail("载具中无法投掷");
    }

    auto* equipment = registry.try_get<ecs::Equipment>(player);
    auto* container = registry.try_get<ecs::Container>(player);
    const items::ItemDef* def = nullptr;
    ecs::ItemInstance sourceItem;
    if (command.payload.index == -1) {
        if (equipment == nullptr) {
            return fail("主手没有可投掷物");
        }
        auto& slot = equipment->slots[static_cast<std::size_t>(
            ecs::EquipSlot::PrimaryHand)];
        if (!slot.has_value() || !slot->Valid()) {
            return fail("主手没有可投掷物");
        }
        sourceItem = *slot;
    } else {
        if (container == nullptr ||
            static_cast<std::size_t>(command.payload.index) >=
                container->items.size()) {
            return fail("投掷物来源无效");
        }
        sourceItem =
            container->items[static_cast<std::size_t>(command.payload.index)];
    }
    def = m_items->Find(sourceItem.defId);
    if (def == nullptr || !def->throwable.has_value() ||
        !sourceItem.Valid()) {
        return fail("该物品不可投掷");
    }

    const float dx = command.payload.x - position->x;
    const float dy = command.payload.y - position->y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float range =
        std::min(std::clamp(def->throwable->range, 1.0f, 64.0f),
                 m_throwingConfig.maxRange);
    if (!std::isfinite(distance) || distance < m_throwingConfig.minRange ||
        distance > range + 0.001f) {
        ++m_networkStats.rangeRejected;
        return fail("投掷落点超出射程");
    }

    const game::ThrowPathResult path = game::ClipThrowPath(
        position->x, position->y, command.payload.x, command.payload.y,
        [this, z = position->z](int wx, int wy) {
            return FindServerLoadedChunk(wx, wy) == nullptr ||
                   game::IsSquareBlocked(m_simulation->World(), wx, wy, z);
        });
    if (!path.valid || !(path.distance > 0.0001f)) {
        return fail("投掷落点不可达");
    }
    const int targetX = static_cast<int>(std::floor(path.x));
    const int targetY = static_cast<int>(std::floor(path.y));
    if (FindServerLoadedChunk(targetX, targetY) == nullptr) {
        return fail("投掷落点未加载");
    }

    if (session.throwUsed &&
        m_ticks < session.lastThrowTick + m_throwCooldownTicks) {
        ++m_networkStats.cooldownRejected;
        return fail("投掷冷却中");
    }

    ecs::ItemInstance thrown;
    if (!game::ConsumeThrowableSource(equipment, container, command.payload.index,
                                def->id, &thrown)) {
        return fail("投掷物已变化或数量不足");
    }

    session.throwUsed = true;
    session.lastThrowTick = m_ticks;
    session.stateDirty = true;
    ResetServerStealth(session, player, "投掷");

    ServerThrow pending;
    pending.ownerStableId = session.playerStableId;
    pending.defId = def->id;
    pending.name = def->name;
    pending.z = position->z;
    pending.targetX = path.x;
    pending.targetY = path.y;
    const float pathDx = path.x - position->x;
    const float pathDy = path.y - position->y;
    const float pathLength = std::sqrt(pathDx * pathDx + pathDy * pathDy);
    if (pathLength > 0.0001f) {
        pending.directionX = pathDx / pathLength;
        pending.directionY = pathDy / pathLength;
    }
    pending.remainingSeconds = game::ThrowFlightSeconds(path.distance);
    pending.damage = def->throwable->damage;
    pending.impactRadius = def->throwable->impactRadius;
    pending.noiseRadius = def->throwable->noiseRadius;
    if (def->throwable->fire.has_value()) {
        pending.fireRadius = def->throwable->fire->radius;
        pending.fireMinutes = def->throwable->fire->minutes;
    }
    pending.returnable = def->throwable->returnable;
    pending.breakChance = def->throwable->breakChance;
    pending.itemCondition = thrown.condition;
    pending.wallClipped = path.hitWall;
    m_serverThrows.push_back(std::move(pending));
    ++m_networkStats.throwsStarted;

    char text[192];
    std::snprintf(text, sizeof(text),
                  "投掷：%s → (%.1f, %.1f)，%.1f 格%s",
                  def->name.c_str(), static_cast<double>(path.x),
                  static_cast<double>(path.y),
                  static_cast<double>(path.distance),
                  path.hitWall ? "，墙前落点" : "");
    SetSessionStatus(session, text, 0);
    ZL_INFO("throwing",
            "服务器投掷开始：{}（{}），玩家 {}，来源 {}，落点 ({:.2f},{:.2f})，距离 {:.2f}，飞行 {:.2f}s，墙前={}，扣除 1 件",
            def->name, def->id, session.playerStableId,
            command.payload.index < 0
                ? "主手"
                : "背包 " + std::to_string(command.payload.index),
            static_cast<double>(path.x), static_cast<double>(path.y),
            static_cast<double>(path.distance),
            static_cast<double>(game::ThrowFlightSeconds(path.distance)),
            path.hitWall);
    return true;
}

void ServerHost::UpdateServerThrows(float dt) {
    if (m_simulation == nullptr || m_serverThrows.empty() || !(dt > 0.0f)) {
        return;
    }
    std::vector<ServerThrow> ready;
    for (auto it = m_serverThrows.begin(); it != m_serverThrows.end();) {
        it->remainingSeconds -= dt;
        if (it->remainingSeconds <= 0.0f) {
            ready.push_back(std::move(*it));
            it = m_serverThrows.erase(it);
        } else {
            ++it;
        }
    }
    for (ServerThrow& pending : ready) {
        ResolveServerThrow(pending);
    }
}

void ServerHost::SettleServerThrows(const char* reason,
                                    std::uint64_t ownerStableId) {
    if (m_serverThrows.empty()) {
        return;
    }
    std::vector<ServerThrow> settled;
    for (auto it = m_serverThrows.begin(); it != m_serverThrows.end();) {
        if (ownerStableId == 0 || it->ownerStableId == ownerStableId) {
            settled.push_back(std::move(*it));
            it = m_serverThrows.erase(it);
        } else {
            ++it;
        }
    }
    for (ServerThrow& pending : settled) {
        ResolveServerThrow(pending);
    }
    if (!settled.empty()) {
        ZL_INFO("throwing", "服务器结算在飞投掷物：{} 件（{}）",
                settled.size(), reason != nullptr ? reason : "manual");
    }
}

void ServerHost::ResolveServerThrow(ServerThrow& pending) {
    if (m_simulation == nullptr) {
        return;
    }
    ++m_networkStats.throwsResolved;
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<game::ThrowTargetCandidate> candidates;
    registry.view<ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Health& health,
            const ecs::Position& position) {
            if (!m_simulation->Ecs().Alive(entity) ||
                entity == m_simulation->Ecs().FindById(pending.ownerStableId) ||
                health.current <= 0.0f || position.z != pending.z ||
                registry.any_of<ecs::PlayerTag, ecs::NoCombatTarget,
                                ecs::FriendlyNpc>(entity)) {
                return;
            }
            candidates.push_back(game::ThrowTargetCandidate{
                m_simulation->Ecs().IdOf(entity).id, position.x, position.y,
                position.z});
        });
    const std::uint64_t targetId =
        game::SelectThrowTarget(candidates, pending.targetX, pending.targetY,
                          pending.z, pending.impactRadius,
                          pending.ownerStableId);

    bool damaged = false;
    if (targetId != 0 && m_simulation->Combat() != nullptr) {
        const ecs::Entity target = m_simulation->Ecs().FindById(targetId);
        if (m_simulation->Ecs().Alive(target)) {
            ecs::DamageEvent event;
            event.sourceId = pending.ownerStableId;
            event.type = ecs::DamageType::Blunt;
            event.amount = pending.damage;
            event.dirX = pending.directionX;
            event.dirY = pending.directionY;
            event.knockback = 0.0f;
            event.staggerSeconds = 0.0f;
            damaged = m_simulation->Combat()->ApplyDamage(
                m_simulation->Ecs(), target, event);
            if (damaged) {
                RecordSiegeDefenseDamage(pending.ownerStableId, target,
                                         std::max(0.0f, pending.damage));
            }
        }
    }

    if (pending.noiseRadius > 0.0f) {
        core::SoundEvent noise;
        noise.x = pending.targetX;
        noise.y = pending.targetY;
        noise.radius = pending.noiseRadius;
        noise.type = core::SoundType::Impact;
        noise.clip = audio::kImpact;
        if (m_events != nullptr) {
            m_events->EmitQueued(core::kOnSound, noise);
        }
        net::WorldSoundEvent remote;
        remote.sourcePlayerStableId = pending.ownerStableId;
        remote.x = pending.targetX;
        remote.y = pending.targetY;
        remote.z = pending.z;
        remote.radius = pending.noiseRadius;
        remote.type = static_cast<std::uint8_t>(core::SoundType::Impact);
        remote.clip = audio::kImpact;
        BroadcastWorldSound(remote);
    }
    SpawnImpactEffect(pending.targetX, pending.targetY, pending.z);

    std::size_t fireIgnited = 0;
    std::size_t fireExisting = 0;
    std::size_t fireCells = 0;
    std::string fireError;
    if (pending.fireRadius > 0.0f && pending.fireMinutes > 0.0f &&
        m_simulation->Fire() != nullptr) {
        double dayLengthMinutes = 60.0;
        if (m_config != nullptr) {
            dayLengthMinutes = m_config->Get<double>(
                "game.dayLengthMinutes", dayLengthMinutes);
        }
        const float gameMinutesPerRealSecond =
            std::isfinite(dayLengthMinutes) && dayLengthMinutes > 0.0
                ? static_cast<float>(1440.0 / (dayLengthMinutes * 60.0))
                : 0.4f;
        const float burnSeconds = game::ThrowFireBurnSeconds(
            pending.fireMinutes, gameMinutesPerRealSecond);
        const std::vector<game::ThrowFireCell> cells = game::ThrowFireAreaCells(
            pending.targetX, pending.targetY, pending.fireRadius);
        fireCells = cells.size();
        for (const game::ThrowFireCell& cell : cells) {
            std::string cellError;
            const game::FireIgnitionResult result = m_simulation->Fire()->IgniteAt(
                m_simulation->Ecs(), m_simulation->World(), cell.cellX,
                cell.cellY, pending.z,
                m_simulation->CurrentWeather().wetnessIntensity, &cellError,
                pending.ownerStableId);
            if (result == game::FireIgnitionResult::Ignited) {
                ++fireIgnited;
                ecs::FireSource* source = m_simulation->Fire()->FindAt(
                    m_simulation->Ecs(), cell.cellX, cell.cellY, pending.z);
                if (source != nullptr && burnSeconds > 0.0f) {
                    source->burnDuration = burnSeconds;
                    source->burnRemaining = burnSeconds;
                }
            } else if (result == game::FireIgnitionResult::AlreadyActive) {
                ++fireExisting;
            } else if (!cellError.empty()) {
                fireError = std::move(cellError);
            }
        }
        if (fireError.empty() && fireIgnited == 0 && fireExisting == 0) {
            fireError = "落点没有可燃物";
        }
        ZL_INFO("throwing",
                "服务器投掷火焰：范围 {:.1f} 格，持续 {:.1f} 游戏分钟（{:.1f}s），点燃 {}/{} 格，已存在 {}{}",
                static_cast<double>(pending.fireRadius),
                static_cast<double>(pending.fireMinutes),
                static_cast<double>(burnSeconds), fireIgnited, cells.size(),
                fireExisting,
                fireError.empty() ? std::string() : "，失败：" + fireError);
    }

    const float roll =
        std::uniform_real_distribution<float>(0.0f, 1.0f)(m_throwRng);
    const game::ThrowLandingOutcome outcome = game::ResolveThrowLanding(
        pending.returnable, pending.breakChance, roll);
    ecs::Entity ground = entt::null;
    if (outcome == game::ThrowLandingOutcome::Recovered && m_items != nullptr) {
        const ecs::ItemInstance landed{pending.defId, 1,
                                       pending.itemCondition, 0};
        ground = game::SpawnGroundItem(m_simulation->Ecs(), *m_items, landed,
                                 pending.targetX, pending.targetY, pending.z);
        if (ground != entt::null && m_events != nullptr) {
            m_events->EmitQueued(
                core::kOnItemDrop,
                core::ItemEvent{m_simulation->Ecs().IdOf(ground).id,
                                pending.defId, 1});
        }
    }
    if (outcome == game::ThrowLandingOutcome::Recovered) {
        ++m_networkStats.throwsRecovered;
    } else if (outcome == game::ThrowLandingOutcome::Broken) {
        ++m_networkStats.throwsBroken;
    }

    SessionPlayer* session =
        FindSessionByPlayerStableId(pending.ownerStableId);
    const char* outcomeLabel = outcome == game::ThrowLandingOutcome::Recovered
                                   ? "回收"
                                   : outcome == game::ThrowLandingOutcome::Broken
                                         ? "损坏"
                                         : "消耗";
    if (session != nullptr) {
        std::string text =
            targetId != 0 ? "投掷命中：" : "投掷落空：";
        text += pending.name;
        text += " · ";
        text += outcomeLabel;
        if (pending.fireRadius > 0.0f && pending.fireMinutes > 0.0f) {
            text += (fireIgnited > 0 || fireExisting > 0) ? " · 已点燃"
                                                          : " · 未点燃";
        }
        SetSessionStatus(*session, std::move(text),
                         outcome == game::ThrowLandingOutcome::Broken ? 2
                         : targetId != 0 ? 1
                                         : 0);
    }
    ZL_INFO("throwing",
            "服务器投掷落地：{}（{}），玩家 {}，落点 ({:.2f},{:.2f})，z={}，墙前={}，目标 {}，伤害 {:.0f}，结算={}，地面物品 {}，火焰 {}/{}，roll={:.3f}",
            pending.name, pending.defId, pending.ownerStableId,
            static_cast<double>(pending.targetX),
            static_cast<double>(pending.targetY), pending.z,
            pending.wallClipped, targetId,
            static_cast<double>(pending.damage), outcomeLabel,
            ground != entt::null ? m_simulation->Ecs().IdOf(ground).id : 0,
            fireIgnited + fireExisting, fireCells,
            static_cast<double>(roll));
}

bool ServerHost::IgniteFireAction(SessionPlayer& session,
                                     const net::Command& command,
                                     std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_simulation->Fire() == nullptr ||
        m_fire == nullptr) {
        return fail("火焰系统未启用");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    if (player == entt::null || position == nullptr || session.dead) {
        return fail("玩家不可用");
    }
    if (command.payload.targetIndex < 0 || command.payload.targetIndex > 16 ||
        command.payload.targetIndex != position->z ||
        command.payload.x < -1000000.0f || command.payload.x > 1000000.0f ||
        command.payload.y < -1000000.0f || command.payload.y > 1000000.0f) {
        return fail("点火目标非法");
    }
    const int wx = static_cast<int>(std::lround(command.payload.x));
    const int wy = static_cast<int>(std::lround(command.payload.y));
    int campfireX = 0;
    int campfireY = 0;
    if (!game::FindCampfireNearby(m_simulation->World(), position->x,
                                  position->y, position->z,
                                  m_campfireRange, &campfireX,
                                  &campfireY)) {
        return fail("附近没有篝火");
    }
    if (wx != campfireX || wy != campfireY) {
        return fail("点火目标不是附近篝火");
    }
    std::string reason;
    const game::FireIgnitionResult result =
        m_simulation->Fire()->IgniteAt(
            m_simulation->Ecs(), m_simulation->World(), campfireX, campfireY,
            position->z, m_simulation->CurrentWeather().wetnessIntensity,
            &reason, session.playerStableId);
    if (result == game::FireIgnitionResult::Ignited) {
        SetSessionStatus(session, "篝火点燃中", 1);
    } else if (result == game::FireIgnitionResult::AlreadyActive) {
        SetSessionStatus(session, "篝火仍在燃烧", 0);
    } else {
        SetSessionStatus(session, "无法点燃篝火：" + reason, 2);
    }
    return true;
}

bool ServerHost::ExtinguishFireAction(SessionPlayer& session,
                                         const net::Command& command,
                                         std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_simulation->Fire() == nullptr ||
        m_items == nullptr) {
        return fail("火焰系统未启用");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    auto* container =
        player != entt::null ? registry.try_get<ecs::Container>(player) : nullptr;
    if (player == entt::null || position == nullptr || container == nullptr ||
        session.dead) {
        return fail("玩家不可用");
    }
    if (command.payload.targetIndex < 0 || command.payload.targetIndex > 16 ||
        command.payload.targetIndex != position->z ||
        command.payload.x < -1000000.0f || command.payload.x > 1000000.0f ||
        command.payload.y < -1000000.0f || command.payload.y > 1000000.0f) {
        return fail("灭火目标非法");
    }
    const int wx = static_cast<int>(std::lround(command.payload.x));
    const int wy = static_cast<int>(std::lround(command.payload.y));
    game::FireSystem* fire = m_simulation->Fire();
    ecs::FireSource* source = fire->FindAt(m_simulation->Ecs(), wx, wy,
                                           position->z);
    if (source == nullptr || !game::FireIsActive(*source)) {
        return fail("目标火源不存在");
    }
    const float centerX = static_cast<float>(wx) + 0.5f;
    const float centerY = static_cast<float>(wy) + 0.5f;
    const float dx = centerX - position->x;
    const float dy = centerY - position->y;
    const float range = fire->Params().extinguishRange + 0.25f;
    if (dx * dx + dy * dy > range * range) {
        ++m_networkStats.rangeRejected;
        return fail("火源超出灭火范围");
    }

    const game::FireExtinguishPlan plan =
        game::PlanFireExtinguish(*container, *m_items, fire->Params());
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    const bool success = game::FireExtinguishSucceeds(
        plan.chance, distribution(m_fireRng));
    if (plan.usesItem) {
        (void)game::ConsumeFireExtinguishItem(*container, *m_items, plan);
        session.stateDirty = true;
    }
    if (!success && plan.bareHands && plan.selfDamage > 0.0f &&
        m_simulation->Combat() != nullptr) {
        ecs::DamageEvent event;
        event.type = ecs::DamageType::Fire;
        event.amount = plan.selfDamage;
        (void)m_simulation->Combat()->ApplyDamage(m_simulation->Ecs(), player,
                                                  event);
    }
    if (success) {
        (void)fire->ExtinguishAt(m_simulation->Ecs(), wx, wy, position->z);
        SetSessionStatus(session,
                         plan.bareHands ? "扑灭了火焰"
                                        : "使用 " + plan.name + " 扑灭了火焰",
                         1);
    } else {
        SetSessionStatus(session,
                         plan.bareHands
                             ? "徒手拍打失败，被火焰灼伤"
                             : "使用 " + plan.name + " 灭火失败",
                         2);
    }
    return true;
}

void ServerHost::TickServerRangedWeapons(float dt) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        session.rangedWeapon.Tick(dt);
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null || session.dead) {
            session.rangedWeapon.CancelReload();
            continue;
        }
        if (!session.rangedWeapon.reloading) {
            continue;
        }
        auto* equipment = registry.try_get<ecs::Equipment>(player);
        std::optional<ecs::ItemInstance>* slot =
            equipment != nullptr
                ? &equipment->slots[static_cast<std::size_t>(
                      ecs::EquipSlot::PrimaryHand)]
                : nullptr;
        const items::ItemDef* def =
            (slot != nullptr && slot->has_value())
                ? m_items->Find((*slot)->defId)
                : nullptr;
        const ecs::ItemInstance* weapon =
            (slot != nullptr && slot->has_value()) ? &**slot : nullptr;
        std::string reason;
        const items::RangedReloadEvent event = session.rangedWeapon.TickReload(
            dt, def, weapon, &reason);
        if (event == items::RangedReloadEvent::Cancelled) {
            SetSessionStatus(session, "reload cancelled: " + reason, 2);
            session.stateDirty = true;
            continue;
        }
        if (event != items::RangedReloadEvent::Completed) {
            continue;
        }
        const int requested = session.rangedWeapon.reloadAmount;
        int loaded = 0;
        if (auto* bag = registry.try_get<ecs::Container>(player);
            bag != nullptr && def != nullptr && def->ranged.has_value() &&
            slot != nullptr && slot->has_value()) {
            auto& weaponRef = **slot;
            const int space =
                std::max(0, def->ranged->magazine - weaponRef.loadedAmmo);
            const int available =
                items::CountOf(*bag, def->ranged->ammoDefId);
            const int toLoad = std::min({requested, space, available});
            if (toLoad > 0) {
                loaded = items::RemoveItem(*bag, def->ranged->ammoDefId, toLoad);
                weaponRef.loadedAmmo =
                    std::min(def->ranged->magazine,
                             weaponRef.loadedAmmo + loaded);
            }
        }
        session.rangedWeapon.CancelReload();
        session.stateDirty = true;
        SetSessionStatus(session,
                         "reload complete: +" + std::to_string(loaded), 1);
    }
}

void ServerHost::ReleaseServerForageReservation(SessionPlayer& session) {
    const std::string& key = session.survival.reservationKey;
    if (key.empty()) {
        return;
    }
    const auto it = m_forageReservations.find(key);
    if (it != m_forageReservations.end() &&
        it->second == session.playerStableId) {
        m_forageReservations.erase(it);
    }
}

void ServerHost::ClearServerSurvival(SessionPlayer& session,
                                     std::string_view reason,
                                     std::uint8_t tone) {
    ReleaseServerForageReservation(session);
    session.survival = {};
    session.stateDirty = true;
    if (!reason.empty()) {
        SetSessionStatus(session, std::string(reason), tone);
    }
}

bool ServerHost::ServerHasForageTool(const ecs::Entity player,
                                     const game::ForageZoneDef& zone) const {
    if (zone.toolDefId.empty() || m_simulation == nullptr) {
        return zone.toolDefId.empty();
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (const auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr &&
        items::CountUsableTools(*container, zone.toolDefId) > 0) {
        return true;
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        for (const auto& slot : equipment->slots) {
            if (slot.has_value() && slot->defId == zone.toolDefId &&
                slot->condition > 0.0f) {
                return true;
            }
        }
    }
    return false;
}

bool ServerHost::ServerWearForageTool(const ecs::Entity player,
                                      const game::ForageZoneDef& zone) {
    if (zone.toolDefId.empty() || zone.toolWear <= 0.0f ||
        m_simulation == nullptr) {
        return true;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        for (auto it = container->items.begin(); it != container->items.end();
             ++it) {
            if (it->defId != zone.toolDefId || it->condition <= 0.0f) {
                continue;
            }
            it->condition = std::max(0.0f, it->condition - zone.toolWear);
            if (it->condition <= 0.0f) {
                if (it->count > 1) {
                    --it->count;
                    it->condition = 1.0f;
                } else {
                    container->items.erase(it);
                }
            }
            return true;
        }
    }
    if (auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        for (auto& slot : equipment->slots) {
            if (!slot.has_value() || slot->defId != zone.toolDefId ||
                slot->condition <= 0.0f) {
                continue;
            }
            slot->condition = std::max(0.0f, slot->condition - zone.toolWear);
            if (slot->condition <= 0.0f) {
                slot.reset();
            }
            return true;
        }
    }
    return false;
}

bool ServerHost::ServerHasFishingRod(const ecs::Entity player,
                                     const game::FishingSpotDef& spot) const {
    if (spot.rodDefId.empty() || m_simulation == nullptr) {
        return spot.rodDefId.empty();
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (const auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr &&
        items::CountUsableTools(*container, spot.rodDefId) > 0) {
        return true;
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        for (const auto& slot : equipment->slots) {
            if (slot.has_value() && slot->defId == spot.rodDefId &&
                slot->condition > 0.0f) {
                return true;
            }
        }
    }
    return false;
}

int ServerHost::ServerFishingBaitCount(const ecs::Entity player,
                                       const game::FishingSpotDef& spot) const {
    if (spot.baitDefId.empty() || spot.baitCost <= 0 ||
        m_simulation == nullptr) {
        return 0;
    }
    const auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    return container != nullptr
               ? items::CountOf(*container, spot.baitDefId)
               : 0;
}

bool ServerHost::ServerConsumeFishingBait(const ecs::Entity player,
                                          const game::FishingSpotDef& spot) {
    if (spot.baitDefId.empty() || spot.baitCost <= 0) {
        return true;
    }
    if (m_simulation == nullptr) {
        return false;
    }
    auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    return container != nullptr &&
           items::RemoveItem(*container, spot.baitDefId, spot.baitCost) ==
               spot.baitCost;
}

void ServerHost::ServerWearFishingRod(const ecs::Entity player,
                                      const game::FishingSpotDef& spot) {
    if (spot.rodDefId.empty() || spot.rodWear <= 0.0f ||
        m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        for (auto it = container->items.begin(); it != container->items.end();
             ++it) {
            if (it->defId != spot.rodDefId || it->condition <= 0.0f) {
                continue;
            }
            it->condition = std::max(0.0f, it->condition - spot.rodWear);
            if (it->condition <= 0.0f) {
                container->items.erase(it);
            }
            return;
        }
    }
    if (auto* equipment = registry.try_get<ecs::Equipment>(player);
        equipment != nullptr) {
        for (auto& slot : equipment->slots) {
            if (!slot.has_value() || slot->defId != spot.rodDefId ||
                slot->condition <= 0.0f) {
                continue;
            }
            slot->condition = std::max(0.0f, slot->condition - spot.rodWear);
            if (slot->condition <= 0.0f) {
                slot.reset();
            }
            return;
        }
    }
}

void ServerHost::RefreshServerSurvival(SessionPlayer& session,
                                       ecs::Entity player) {
    session.nearbyForage.clear();
    session.nearbyFishing.clear();
    if (m_simulation == nullptr || m_forage == nullptr || m_poiConfig == nullptr ||
        m_gameTime == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* position = registry.try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    const int centerX = static_cast<int>(std::floor(position->x));
    const int centerY = static_cast<int>(std::floor(position->y));
    const int forageRadius = static_cast<int>(std::ceil(m_foragePlanRadius));
    const int fishingRadius = static_cast<int>(std::ceil(m_fishingPlanRadius));
    const game::ForageBounds forageBounds{
        centerX - forageRadius, centerY - forageRadius,
        centerX + forageRadius, centerY + forageRadius};
    const game::ForageBounds fishingBounds{
        centerX - fishingRadius, centerY - fishingRadius,
        centerX + fishingRadius, centerY + fishingRadius};
    const game::WeatherState weather =
        m_simulation->CurrentWeather();
    const int survivalLevel =
        [&]() {
            const auto* progression = registry.try_get<ecs::Progression>(player);
            return progression != nullptr
                       ? ecs::SkillLevel(*progression, ecs::SkillKind::Survival)
                       : 0;
        }();
    const double now = m_gameTime->TotalGameMinutes();
    struct NearbyNode {
        float distanceSq = 0.0f;
        net::PlayerForageNodeState state;
    };
    std::vector<NearbyNode> nodes;
    for (const game::ForageNode& node :
         game::PlanForageNodes(*m_forage, m_simulation->World(), *m_poiConfig,
                               forageBounds)) {
        const game::ForageZoneDef* zone = m_forage->FindZone(node.zoneId);
        if (zone == nullptr) {
            continue;
        }
        const float dx = static_cast<float>(node.x) + 0.5f - position->x;
        const float dy = static_cast<float>(node.y) + 0.5f - position->y;
        NearbyNode nearby;
        nearby.distanceSq = dx * dx + dy * dy;
        nearby.state.zoneId = node.zoneId;
        nearby.state.x = node.x;
        nearby.state.y = node.y;
        nearby.state.z = node.z;
        nearby.state.ready =
            game::ForageNodeReady(m_serverForage, node, now);
        nearby.state.respawnMinutes = nearby.state.ready
                                          ? 0.0f
                                          : static_cast<float>(
                                                game::ForageRespawnRemainingMinutes(
                                                    m_serverForage, node, now));
        nearby.state.seconds =
            game::ForageSearchSeconds(*zone, weather, survivalLevel);
        nodes.push_back(std::move(nearby));
    }
    std::sort(nodes.begin(), nodes.end(),
              [](const NearbyNode& left, const NearbyNode& right) {
                  return left.distanceSq < right.distanceSq;
              });
    const std::size_t forageCount =
        std::min(nodes.size(), net::kMaxPlayerStateForageNodes);
    session.nearbyForage.reserve(forageCount);
    for (std::size_t i = 0; i < forageCount; ++i) {
        session.nearbyForage.push_back(std::move(nodes[i].state));
    }
    struct NearbySpot {
        float distanceSq = 0.0f;
        net::PlayerFishingSpotState state;
    };
    std::vector<NearbySpot> spots;
    for (const game::FishingSpot& spot :
         game::PlanFishingSpots(*m_forage, m_simulation->World(), *m_poiConfig,
                                fishingBounds)) {
        const game::FishingSpotDef* def = m_forage->FindSpot(spot.spotId);
        if (def == nullptr ||
            !game::FishingSpotWaterValid(*def, m_simulation->World(), spot)) {
            continue;
        }
        const float dx = static_cast<float>(spot.x) + 0.5f - position->x;
        const float dy = static_cast<float>(spot.y) + 0.5f - position->y;
        NearbySpot nearby;
        nearby.distanceSq = dx * dx + dy * dy;
        nearby.state.spotId = spot.spotId;
        nearby.state.x = spot.x;
        nearby.state.y = spot.y;
        nearby.state.z = spot.z;
        nearby.state.waterX = spot.waterX;
        nearby.state.waterY = spot.waterY;
        nearby.state.waterZ = spot.waterZ;
        nearby.state.seconds =
            game::FishingWaitSeconds(*def, weather, survivalLevel);
        spots.push_back(std::move(nearby));
    }
    std::sort(spots.begin(), spots.end(),
              [](const NearbySpot& left, const NearbySpot& right) {
                  return left.distanceSq < right.distanceSq;
              });
    const std::size_t spotCount =
        std::min(spots.size(), net::kMaxPlayerStateFishingSpots);
    session.nearbyFishing.reserve(spotCount);
    for (std::size_t i = 0; i < spotCount; ++i) {
        session.nearbyFishing.push_back(std::move(spots[i].state));
    }
    session.stateDirty = true;
}

bool ServerHost::StartServerForage(SessionPlayer& session,
                                   const net::Command& command,
                                   std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_forage == nullptr || m_simulation == nullptr || m_items == nullptr ||
        m_lootTables == nullptr || m_gameTime == nullptr) {
        return fail("觅食系统不可用");
    }
    if (session.survival.mode != net::kPlayerSurvivalModeNone) {
        return fail("已有采集或钓鱼动作");
    }
    if (session.animalHarvest.active) {
        return fail("正在剥取动物尸体");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return fail("权威玩家实体不存在");
    }
    const int x = static_cast<int>(std::floor(command.payload.x));
    const int y = static_cast<int>(std::floor(command.payload.y));
    const int z = command.payload.targetIndex;
    if (z < 0 || z > 8) {
        return fail("觅食目标层级非法");
    }
    auto findNode = [&]() -> const net::PlayerForageNodeState* {
        for (const net::PlayerForageNodeState& node : session.nearbyForage) {
            if (node.x == x && node.y == y && node.z == z) {
                return &node;
            }
        }
        return nullptr;
    };
    const net::PlayerForageNodeState* nearby = findNode();
    if (nearby == nullptr) {
        RefreshServerSurvival(session, player);
        nearby = findNode();
    }
    if (nearby == nullptr) {
        ++m_networkStats.rangeRejected;
        return fail("觅食目标不在服务器规划范围内");
    }
    const auto* position = m_simulation->Ecs().Registry().try_get<ecs::Position>(
        player);
    if (position == nullptr) {
        return fail("玩家缺少位置");
    }
    const float dx = static_cast<float>(x) + 0.5f - position->x;
    const float dy = static_cast<float>(y) + 0.5f - position->y;
    if (dx * dx + dy * dy >
        m_forageInteractRange * m_forageInteractRange) {
        ++m_networkStats.rangeRejected;
        return fail("觅食目标超出范围");
    }
    const game::ForageZoneDef* zone = m_forage->FindZone(nearby->zoneId);
    if (zone == nullptr) {
        return fail("觅食区域不存在");
    }
    const game::ForageNode node{zone->id, zone->tableId, x, y, z};
    if (!game::ForageNodeReady(m_serverForage, node,
                               m_gameTime->TotalGameMinutes())) {
        const int minutes = std::max(
            1, static_cast<int>(std::ceil(
                   game::ForageRespawnRemainingMinutes(
                       m_serverForage, node, m_gameTime->TotalGameMinutes()))));
        return fail("该资源点已耗尽（约 " + std::to_string(minutes) +
                    " 分钟后刷新）");
    }
    const std::string key = ForageNodeKey(node);
    const auto reservation = m_forageReservations.find(key);
    if (reservation != m_forageReservations.end() &&
        reservation->second != session.playerStableId) {
        return fail("该资源点正在被其他玩家采集");
    }
    if (!ServerHasForageTool(player, *zone)) {
        return fail("缺少觅食工具");
    }
    const game::WeatherState weather = m_simulation->CurrentWeather();
    const auto* progression =
        m_simulation->Ecs().Registry().try_get<ecs::Progression>(player);
    const int survivalLevel =
        progression != nullptr
            ? ecs::SkillLevel(*progression, ecs::SkillKind::Survival)
            : 0;
    const float duration =
        game::ForageSearchSeconds(*zone, weather, survivalLevel);
    session.survival = {};
    session.survival.mode = net::kPlayerSurvivalModeForage;
    session.survival.phase = net::kPlayerSurvivalPhaseActive;
    session.survival.name = zone->name;
    session.survival.zoneId = zone->id;
    session.survival.tableId = zone->tableId;
    session.survival.x = x;
    session.survival.y = y;
    session.survival.z = z;
    session.survival.duration = duration;
    session.survival.reservationKey = key;
    m_forageReservations[key] = session.playerStableId;
    session.stateDirty = true;
    char text[160];
    std::snprintf(text, sizeof(text), "开始觅食：%s（%.1f 秒）",
                  zone->name.c_str(), static_cast<double>(duration));
    SetSessionStatus(session, text, 0);
    ZL_INFO("forage", "服务器开始觅食：{} ({},{})，玩家 {}", zone->id, x, y,
            session.playerStableId);
    return true;
}

bool ServerHost::StartServerFishing(SessionPlayer& session,
                                    const net::Command& command,
                                    std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_forage == nullptr || m_simulation == nullptr || m_gameTime == nullptr) {
        return fail("钓鱼系统不可用");
    }
    if (session.survival.mode != net::kPlayerSurvivalModeNone) {
        return fail("已有采集或钓鱼动作");
    }
    if (session.animalHarvest.active) {
        return fail("正在剥取动物尸体");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return fail("权威玩家实体不存在");
    }
    const int x = static_cast<int>(std::floor(command.payload.x));
    const int y = static_cast<int>(std::floor(command.payload.y));
    const int z = command.payload.targetIndex;
    if (z < 0 || z > 8) {
        return fail("钓鱼目标层级非法");
    }
    auto findSpot = [&]() -> const net::PlayerFishingSpotState* {
        for (const net::PlayerFishingSpotState& spot : session.nearbyFishing) {
            if (spot.x == x && spot.y == y && spot.z == z) {
                return &spot;
            }
        }
        return nullptr;
    };
    const net::PlayerFishingSpotState* nearby = findSpot();
    if (nearby == nullptr) {
        RefreshServerSurvival(session, player);
        nearby = findSpot();
    }
    if (nearby == nullptr) {
        ++m_networkStats.rangeRejected;
        return fail("钓鱼点不在服务器规划范围内");
    }
    const auto* position = m_simulation->Ecs().Registry().try_get<ecs::Position>(
        player);
    if (position == nullptr) {
        return fail("玩家缺少位置");
    }
    const float dx = static_cast<float>(x) + 0.5f - position->x;
    const float dy = static_cast<float>(y) + 0.5f - position->y;
    if (dx * dx + dy * dy >
        m_fishingInteractRange * m_fishingInteractRange) {
        ++m_networkStats.rangeRejected;
        return fail("钓鱼点超出范围");
    }
    const game::FishingSpotDef* def = m_forage->FindSpot(nearby->spotId);
    if (def == nullptr) {
        return fail("钓鱼点数据不存在");
    }
    const game::FishingSpot spot{nearby->spotId, def->tableId, x, y, z,
                                 nearby->waterX, nearby->waterY,
                                 nearby->waterZ};
    if (!game::FishingSpotWaterValid(*def, m_simulation->World(), spot)) {
        return fail("钓鱼点水面已变化");
    }
    if (!ServerHasFishingRod(player, *def)) {
        return fail("缺少鱼竿");
    }
    if (ServerFishingBaitCount(player, *def) < def->baitCost) {
        return fail("缺少鱼饵");
    }
    if (!ServerConsumeFishingBait(player, *def)) {
        return fail("鱼饵消耗失败");
    }
    const game::WeatherState weather = m_simulation->CurrentWeather();
    const auto* progression =
        m_simulation->Ecs().Registry().try_get<ecs::Progression>(player);
    const int survivalLevel =
        progression != nullptr
            ? ecs::SkillLevel(*progression, ecs::SkillKind::Survival)
            : 0;
    session.survival = {};
    session.survival.mode = net::kPlayerSurvivalModeFishing;
    session.survival.phase = net::kPlayerSurvivalPhaseActive;
    session.survival.name = def->name;
    session.survival.tableId = def->tableId;
    session.survival.spotId = def->id;
    session.survival.x = x;
    session.survival.y = y;
    session.survival.z = z;
    session.survival.waterX = nearby->waterX;
    session.survival.waterY = nearby->waterY;
    session.survival.waterZ = nearby->waterZ;
    session.survival.duration =
        game::FishingWaitSeconds(*def, weather, survivalLevel);
    session.survival.biteDuration = def->biteWindowSeconds;
    session.survival.catchChance =
        game::FishingCatchChance(*def, weather, survivalLevel);
    session.stateDirty = true;
    char text[160];
    std::snprintf(text, sizeof(text), "开始钓鱼：%s（等待 %.1f 秒）",
                  def->name.c_str(),
                  static_cast<double>(session.survival.duration));
    SetSessionStatus(session, text, 0);
    ZL_INFO("fishing", "服务器开始钓鱼：{} ({},{})，玩家 {}", def->id, x, y,
            session.playerStableId);
    return true;
}

void ServerHost::CompleteServerForage(SessionPlayer& session) {
    const ServerSurvivalAction action = session.survival;
    ReleaseServerForageReservation(session);
    session.survival = {};
    session.stateDirty = true;
    if (m_simulation == nullptr || m_forage == nullptr || m_items == nullptr ||
        m_lootTables == nullptr || m_gameTime == nullptr) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return;
    }
    const game::ForageZoneDef* zone = m_forage->FindZone(action.zoneId);
    if (zone == nullptr) {
        return;
    }
    const game::ForageNode node{action.zoneId, action.tableId, action.x,
                                action.y, action.z};
    if (!ServerHasForageTool(player, *zone)) {
        SetSessionStatus(session, "工具已不在背包中", 2);
        return;
    }
    const items::LootTable* table = m_lootTables->Find(action.tableId);
    if (table == nullptr) {
        return;
    }
    const ecs::Position* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    game::ForageMarkHarvested(m_serverForage, node, zone->respawnMinutes,
                              m_gameTime->TotalGameMinutes());
    const std::uint64_t seed = items::DeriveLootSeed(
        m_lootTables->Seed(), table->id,
        game::ForageLootSeed(m_forage->Seed(), node));
    ecs::Container rolled;
    rolled.capacityKg = table->capacityKg;
    (void)items::RollLootTable(*table, *m_items, rolled, seed);
    std::string found;
    for (const ecs::ItemInstance& item : rolled.items) {
        const items::ItemDef* def = m_items->Find(item.defId);
        if (!found.empty()) {
            found += "、";
        }
        found += (def != nullptr ? def->name : item.defId) + "×" +
                 std::to_string(item.count);
    }
    auto* container = m_simulation->Ecs().Registry().try_get<ecs::Container>(
        player);
    int addedTotal = 0;
    bool dropped = false;
    for (const ecs::ItemInstance& item : rolled.items) {
        if (container == nullptr) {
            dropped = ReturnServerItem(player, item, position->x, position->y,
                                       position->z) ||
                      dropped;
            continue;
        }
        const int added = items::AddItem(*container, *m_items, item);
        addedTotal += added;
        if (added < item.count) {
            ecs::ItemInstance remainder = item;
            remainder.count -= added;
            dropped = ReturnServerItem(player, remainder, position->x,
                                       position->y, position->z) ||
                      dropped;
        }
    }
    (void)ServerWearForageTool(player, *zone);
    AwardProgressionXp(player, ecs::SkillKind::Survival, zone->survivalXp,
                       &session);
    if (rolled.items.empty()) {
        SetSessionStatus(session, "采集了" + zone->name + "：什么也没找到", 0);
    } else {
        std::string text = "采集了" + zone->name + "：" + found;
        if (dropped) {
            text += "（背包已满，部分掉落脚下）";
        }
        SetSessionStatus(session, std::move(text),
                         addedTotal > 0 ? 1 : 2);
    }
    ZL_INFO("forage", "服务器完成觅食：{} ({},{})，获得 {} 件，掉落={}",
            zone->id, action.x, action.y, addedTotal, dropped);
}

void ServerHost::CompleteServerFishing(SessionPlayer& session, bool caught) {
    const ServerSurvivalAction action = session.survival;
    session.survival = {};
    session.stateDirty = true;
    if (m_simulation == nullptr || m_forage == nullptr || m_items == nullptr ||
        m_lootTables == nullptr) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return;
    }
    const game::FishingSpotDef* def = m_forage->FindSpot(action.spotId);
    if (def == nullptr) {
        return;
    }
    const game::FishingSpot spot{action.spotId, def->tableId, action.x,
                                 action.y, action.z, action.waterX,
                                 action.waterY, action.waterZ};
    const std::string tableId = caught ? def->tableId : def->trashTableId;
    const items::LootTable* table =
        tableId.empty() ? nullptr : m_lootTables->Find(tableId);
    ecs::Container rolled;
    if (table != nullptr) {
        rolled.capacityKg = table->capacityKg;
        const std::uint64_t seed = items::DeriveLootSeed(
            m_lootTables->Seed(), table->id,
            game::FishingLootSeed(m_forage->Seed(), spot, caught));
        (void)items::RollLootTable(*table, *m_items, rolled, seed);
    }
    std::string found;
    for (const ecs::ItemInstance& item : rolled.items) {
        const items::ItemDef* itemDef = m_items->Find(item.defId);
        if (!found.empty()) {
            found += "、";
        }
        found += (itemDef != nullptr ? itemDef->name : item.defId) + "×" +
                 std::to_string(item.count);
    }
    const ecs::Position* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    auto* container = m_simulation->Ecs().Registry().try_get<ecs::Container>(
        player);
    int addedTotal = 0;
    bool dropped = false;
    for (const ecs::ItemInstance& item : rolled.items) {
        if (container == nullptr) {
            dropped = ReturnServerItem(player, item, position->x, position->y,
                                       position->z) ||
                      dropped;
            continue;
        }
        const int added = items::AddItem(*container, *m_items, item);
        addedTotal += added;
        if (added < item.count) {
            ecs::ItemInstance remainder = item;
            remainder.count -= added;
            dropped = ReturnServerItem(player, remainder, position->x,
                                       position->y, position->z) ||
                      dropped;
        }
    }
    ServerWearFishingRod(player, *def);
    if (caught) {
        AwardProgressionXp(player, ecs::SkillKind::Survival, def->survivalXp,
                           &session);
    }
    if (!caught) {
        SetSessionStatus(
            session,
            found.empty() ? "收杆太早，鱼跑了" : "没钓到鱼，捞到：" + found, 0);
    } else if (rolled.items.empty()) {
        SetSessionStatus(session, "钓到鱼了，但鱼篓是空的", 0);
    } else {
        std::string text = "钓到：" + found;
        if (dropped) {
            text += "（背包已满，部分掉落脚下）";
        }
        SetSessionStatus(session, std::move(text),
                         addedTotal > 0 ? 1 : 2);
    }
    ZL_INFO("fishing", "服务器完成钓鱼：{}，成功={}，玩家 {}", def->id, caught,
            session.playerStableId);
}

bool ServerHost::ReelServerFishing(SessionPlayer& session, std::string* error) {
    if (session.survival.mode != net::kPlayerSurvivalModeFishing) {
        if (error != nullptr) {
            *error = "当前没有钓鱼动作";
        }
        return false;
    }
    if (session.survival.phase != net::kPlayerSurvivalPhaseBiting) {
        if (error != nullptr) {
            *error = "还没有咬钩";
        }
        return false;
    }
    const ServerSurvivalAction action = session.survival;
    const game::FishingSpotDef* def =
        m_forage != nullptr ? m_forage->FindSpot(action.spotId) : nullptr;
    if (def == nullptr || m_forage == nullptr) {
        if (error != nullptr) {
            *error = "钓鱼点数据不存在";
        }
        return false;
    }
    const game::FishingSpot spot{action.spotId, def->tableId, action.x,
                                 action.y, action.z, action.waterX,
                                 action.waterY, action.waterZ};
    const bool caught = game::FishingAttemptSucceeds(
        m_forage->Seed(), spot, action.catchChance);
    CompleteServerFishing(session, caught);
    return true;
}

void ServerHost::TickServerSurvival(float dt) {
    if (m_simulation == nullptr || m_forage == nullptr || m_gameTime == nullptr) {
        return;
    }
    const double now = m_gameTime->TotalGameMinutes();
    game::ForagePrune(m_serverForage, now);
    m_survivalScanTimer += std::max(0.0f, dt);
    const bool scan = m_survivalScanTimer >= 1.0f;
    if (scan) {
        m_survivalScanTimer = 0.0f;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        if (session.dead) {
            if (session.survival.mode != net::kPlayerSurvivalModeNone) {
                ClearServerSurvival(session, "", 0);
            }
            continue;
        }
        if (scan || !session.survivalScanned) {
            RefreshServerSurvival(session, player);
            session.survivalScanned = true;
        }
        ServerSurvivalAction& action = session.survival;
        if (action.mode == net::kPlayerSurvivalModeNone) {
            continue;
        }
        const auto* position =
            m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
        if (position == nullptr) {
            ClearServerSurvival(session, "目标失效", 2);
            continue;
        }
        const float dx = static_cast<float>(action.x) + 0.5f - position->x;
        const float dy = static_cast<float>(action.y) + 0.5f - position->y;
        const float range =
            (action.mode == net::kPlayerSurvivalModeForage
                 ? m_forageInteractRange
                 : m_fishingInteractRange) +
            1.0f;
        if (dx * dx + dy * dy > range * range) {
            ZL_INFO("survival", "服务器取消动作：离开范围，玩家 {}，mode={}",
                    session.playerStableId, session.survival.mode);
            ClearServerSurvival(session, "离开范围", 0);
            continue;
        }
        if (action.mode == net::kPlayerSurvivalModeForage) {
            action.elapsed += std::max(0.0f, dt);
            if (action.elapsed >= action.duration) {
                CompleteServerForage(session);
            }
            continue;
        }
        if (action.phase == net::kPlayerSurvivalPhaseActive) {
            action.elapsed += std::max(0.0f, dt);
            if (action.elapsed >= action.duration) {
                action.phase = net::kPlayerSurvivalPhaseBiting;
                action.biteElapsed = 0.0f;
                action.elapsed = action.duration;
                session.stateDirty = true;
                SetSessionStatus(session,
                                 "咬钩了！按 E 收杆：" + action.name, 0);
                ZL_INFO("fishing", "服务器咬钩：{}，玩家 {}", action.spotId,
                        session.playerStableId);
            }
        } else if (action.phase == net::kPlayerSurvivalPhaseBiting) {
            action.biteElapsed += std::max(0.0f, dt);
            if (action.biteElapsed >= action.biteDuration) {
                if (const game::FishingSpotDef* def =
                        m_forage->FindSpot(action.spotId);
                    def != nullptr) {
                    ServerWearFishingRod(player, *def);
                }
                const std::string name = action.name;
                ClearServerSurvival(session, "", 0);
                SetSessionStatus(session, "错过收杆，鱼跑了：" + name, 0);
                ZL_INFO("fishing", "服务器错过收杆：{}，玩家 {}", name,
                        session.playerStableId);
            }
        }
    }
}

bool ServerHost::StartServerAnimalHarvest(SessionPlayer& session,
                                           const net::Command& command,
                                           std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_animals == nullptr || m_items == nullptr ||
        m_lootTables == nullptr) {
        return fail("动物剥取系统不可用");
    }
    if (session.dead) {
        return fail("死亡状态无法剥取");
    }
    if (session.survival.mode != net::kPlayerSurvivalModeNone) {
        return fail("已有采集或钓鱼动作");
    }
    if (session.animalHarvest.active) {
        return fail("正在剥取其他尸体");
    }
    const std::uint64_t corpseStableId = command.payload.entityId;
    if (corpseStableId == 0) {
        return fail("剥取目标 StableId 为空");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const ecs::Entity corpse =
        m_simulation->Ecs().FindById(corpseStableId);
    if (player == entt::null || corpse == entt::null) {
        return fail("剥取目标无效");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* corpseData = registry.try_get<ecs::AnimalCorpse>(corpse);
    const auto* corpsePosition = registry.try_get<ecs::Position>(corpse);
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    const auto* playerHealth = registry.try_get<ecs::Health>(player);
    if (playerHealth == nullptr || playerHealth->current <= 0.0f) {
        return fail("死亡状态无法剥取");
    }
    if (corpseData == nullptr || corpsePosition == nullptr ||
        playerPosition == nullptr || corpseData->harvested) {
        return fail("动物尸体已被剥取或不存在");
    }
    if (corpsePosition->z != playerPosition->z) {
        return fail("剥取目标层级不一致");
    }
    const float dx = corpsePosition->x - playerPosition->x;
    const float dy = corpsePosition->y - playerPosition->y;
    if (dx * dx + dy * dy > m_animalHarvestRange * m_animalHarvestRange) {
        ++m_networkStats.rangeRejected;
        return fail("动物尸体超出剥取范围");
    }
    const auto reserved = m_animalHarvestReservations.find(corpseStableId);
    if (reserved != m_animalHarvestReservations.end() &&
        reserved->second != session.playerStableId) {
        return fail("该动物尸体正在被其他玩家剥取");
    }
    const game::AnimalDef* def = m_animals->Find(corpseData->speciesId);
    if (def == nullptr) {
        return fail("动物物种不存在");
    }
    auto* bag = registry.try_get<ecs::Container>(player);
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    if (!game::AnimalCorpseToolAvailable(*def, bag, equipment, nullptr)) {
        std::string toolName = def->corpseTool;
        if (!def->corpseTool.empty()) {
            if (const items::ItemDef* tool = m_items->Find(def->corpseTool);
                tool != nullptr) {
                toolName = tool->name;
            }
        }
        return fail("缺少剥取工具" +
                    (toolName.empty() ? std::string() : "：" + toolName));
    }
    const float duration =
        std::clamp(def->corpseSearchSeconds, 0.1f, 30.0f);
    session.animalHarvest = {};
    session.animalHarvest.active = true;
    session.animalHarvest.corpseStableId = corpseStableId;
    session.animalHarvest.speciesId = def->id;
    session.animalHarvest.name = def->name + "尸体";
    session.animalHarvest.x = corpsePosition->x;
    session.animalHarvest.y = corpsePosition->y;
    session.animalHarvest.z = corpsePosition->z;
    session.animalHarvest.duration = duration;
    if (const auto* hit = registry.try_get<ecs::HitReaction>(player);
        hit != nullptr) {
        session.animalHarvest.startHitSerial = hit->hitSerial;
    }
    m_animalHarvestReservations[corpseStableId] = session.playerStableId;
    session.stateDirty = true;
    char text[160];
    std::snprintf(text, sizeof(text), "开始剥取：%s（%.1f 秒）",
                  session.animalHarvest.name.c_str(),
                  static_cast<double>(duration));
    SetSessionStatus(session, text, 0);
    ZL_INFO("animals", "服务器开始剥取：{} 实体 {}，玩家 {}", def->id,
            corpseStableId, session.playerStableId);
    return true;
}

void ServerHost::ReleaseServerAnimalHarvest(SessionPlayer& session) {
    if (!session.animalHarvest.active) {
        return;
    }
    const auto it = m_animalHarvestReservations.find(
        session.animalHarvest.corpseStableId);
    if (it != m_animalHarvestReservations.end() &&
        it->second == session.playerStableId) {
        m_animalHarvestReservations.erase(it);
    }
}

void ServerHost::CancelServerAnimalHarvest(SessionPlayer& session,
                                            std::string_view reason,
                                            std::uint8_t tone) {
    if (!session.animalHarvest.active) {
        return;
    }
    const std::string name = session.animalHarvest.name;
    ReleaseServerAnimalHarvest(session);
    session.animalHarvest = {};
    session.stateDirty = true;
    if (!reason.empty()) {
        SetSessionStatus(
            session, name + "：" + std::string(reason), tone);
    }
    ZL_INFO("animals", "服务器取消剥取：{}（{}），玩家 {}", name,
            reason.empty() ? "无" : std::string(reason),
            session.playerStableId);
}

void ServerHost::CompleteServerAnimalHarvest(SessionPlayer& session) {
    const ServerAnimalHarvestAction action = session.animalHarvest;
    ReleaseServerAnimalHarvest(session);
    session.animalHarvest = {};
    session.stateDirty = true;
    if (!action.active || m_simulation == nullptr || m_animals == nullptr ||
        m_items == nullptr || m_lootTables == nullptr) {
        return;
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const ecs::Entity corpse =
        m_simulation->Ecs().FindById(action.corpseStableId);
    if (player == entt::null || corpse == entt::null) {
        SetSessionStatus(session, "剥取失败：目标失效", 2);
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* corpseData = registry.try_get<ecs::AnimalCorpse>(corpse);
    if (corpseData == nullptr || corpseData->harvested) {
        SetSessionStatus(session, "剥取失败：尸体已被处理", 2);
        return;
    }
    auto* bag = registry.try_get<ecs::Container>(player);
    auto* equipment = registry.try_get<ecs::Equipment>(player);
    const game::AnimalHarvestResult result = game::HarvestAnimalCorpse(
        m_simulation->Ecs(), *m_items, *m_lootTables, *m_animals, corpse,
        bag, equipment);
    if (result.missingTool) {
        SetSessionStatus(session, "剥取失败：缺少工具", 2);
        return;
    }
    if (!result.error.empty() && !result.harvested) {
        SetSessionStatus(session, "剥取失败：" + result.error, 2);
        return;
    }
    std::string found;
    if (const auto* container = registry.try_get<ecs::Container>(corpse);
        container != nullptr) {
        for (const ecs::ItemInstance& item : container->items) {
            const items::ItemDef* def = m_items->Find(item.defId);
            if (!found.empty()) {
                found += "、";
            }
            found += (def != nullptr ? def->name : item.defId) + "×" +
                     std::to_string(item.count);
        }
    }
    std::string text = "剥取了" + action.name + "：" +
                       (found.empty() ? std::string("没有可用材料") : found);
    if (result.toolBroke) {
        text += "（工具已损坏）";
    }
    SetSessionStatus(session, std::move(text),
                     found.empty() ? 0 : 1);
    ZL_INFO("animals", "服务器完成剥取：{} 实体 {}，{} 堆/{} 件，玩家 {}",
            action.speciesId, action.corpseStableId, result.itemStacks,
            result.itemsAdded, session.playerStableId);
}

void ServerHost::TickServerAnimalHarvest(float dt) {
    if (m_simulation == nullptr || m_animals == nullptr || m_items == nullptr ||
        m_lootTables == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.animalHarvest.active) {
            continue;
        }
        if (session.dead) {
            CancelServerAnimalHarvest(session, "玩家死亡", 0);
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        const ecs::Entity corpse = m_simulation->Ecs().FindById(
            session.animalHarvest.corpseStableId);
        if (player == entt::null || corpse == entt::null) {
            CancelServerAnimalHarvest(session, "目标失效", 0);
            continue;
        }
        const auto* data = registry.try_get<ecs::AnimalCorpse>(corpse);
        if (data == nullptr || data->harvested) {
            CancelServerAnimalHarvest(session, "目标已被处理", 0);
            continue;
        }
        const auto* health = registry.try_get<ecs::Health>(player);
        if (health == nullptr || health->current <= 0.0f) {
            CancelServerAnimalHarvest(session, "玩家死亡", 0);
            continue;
        }
        const auto* position = registry.try_get<ecs::Position>(player);
        const auto* corpsePosition = registry.try_get<ecs::Position>(corpse);
        if (position == nullptr || corpsePosition == nullptr) {
            CancelServerAnimalHarvest(session, "目标失效", 0);
            continue;
        }
        const float dx = corpsePosition->x - position->x;
        const float dy = corpsePosition->y - position->y;
        const float limit = m_animalHarvestRange + 1.0f;
        if (dx * dx + dy * dy > limit * limit) {
            CancelServerAnimalHarvest(session, "离开范围", 0);
            continue;
        }
        if (const auto* hit = registry.try_get<ecs::HitReaction>(player);
            hit != nullptr &&
            hit->hitSerial != session.animalHarvest.startHitSerial) {
            CancelServerAnimalHarvest(session, "受击", 0);
            continue;
        }
        session.animalHarvest.elapsed += std::max(0.0f, dt);
        if (session.animalHarvest.elapsed >=
            session.animalHarvest.duration) {
            CompleteServerAnimalHarvest(session);
        }
    }
}

void ServerHost::SpawnImpactEffect(float x, float y, int z) {
    if (m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity effect = m_simulation->Ecs().Create();
    if (effect == entt::null) {
        return;
    }
    registry.emplace<ecs::Position>(effect, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        effect, ecs::SpriteRef{ecs::HashName("effect.impact"), 2, 0xFF7040FFu});
    registry.emplace<ecs::TransientEffect>(effect, ecs::TransientEffect{0.35f});
    ++m_networkStats.effectsSpawned;
}

void ServerHost::SpawnCraftEffect(std::uint64_t playerStableId) {
    if (m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player = m_simulation->Ecs().FindById(playerStableId);
    const auto* position =
        player != entt::null ? registry.try_get<ecs::Position>(player) : nullptr;
    if (position == nullptr) {
        return;
    }
    SpawnImpactEffect(position->x, position->y, position->z);
}

void ServerHost::UpdateProjectiles(float dt) {
    if (m_simulation == nullptr || !(dt > 0.0f)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<ecs::Entity> destroy;
    struct Impact {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
    };
    std::vector<Impact> impacts;

    registry.view<ecs::Projectile, ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity projectile, ecs::Projectile& value,
            const ecs::Position& position, const ecs::Velocity& velocity) {
            value.lifetime -= dt;
            bool hit = false;
            Impact impact{position.x, position.y, position.z};
            const float travelled =
                std::sqrt((position.x - value.startX) *
                              (position.x - value.startX) +
                          (position.y - value.startY) *
                              (position.y - value.startY));

            const int wx = static_cast<int>(std::floor(position.x));
            const int wy = static_cast<int>(std::floor(position.y));
            if (value.lifetime <= 0.0f ||
                (value.maxDistance > 0.0f &&
                 travelled >= value.maxDistance) ||
                game::IsSquareBlocked(m_simulation->World(), wx, wy, position.z)) {
                hit = true;
            }

            ecs::Entity best = entt::null;
            float bestDistanceSq = 1.0e9f;
            if (!hit) {
                registry.view<ecs::Health, ecs::Position>().each(
                    [&](const ecs::Entity target, const ecs::Health& health,
                        const ecs::Position& targetPosition) {
                        if (target == projectile ||
                            (target ==
                                 m_simulation->Ecs().FindById(value.ownerStableId) &&
                             value.ownerStableId != 0) ||
                            health.current <= 0.0f ||
                            registry.any_of<ecs::NoCombatTarget>(target) ||
                            (!value.hitPlayers &&
                             registry.all_of<ecs::PlayerTag>(target))) {
                            return;
                        }
                        if (targetPosition.z != position.z) {
                            return;
                        }
                        const float dx = targetPosition.x - position.x;
                        const float dy = targetPosition.y - position.y;
                        const float distanceSq = dx * dx + dy * dy;
                        const float reach = value.radius + 0.35f;
                        if (distanceSq <= reach * reach &&
                            distanceSq < bestDistanceSq) {
                            bestDistanceSq = distanceSq;
                            best = target;
                        }
                    });
            }
            if (best != entt::null) {
                hit = true;
                const auto* targetPosition = registry.try_get<ecs::Position>(best);
                if (targetPosition != nullptr) {
                    impact = Impact{targetPosition->x, targetPosition->y,
                                    targetPosition->z};
                }
                if (m_simulation->Combat() != nullptr) {
                    const float length =
                        std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
                    float damage = value.damage;
                    if (value.maxDistance > 0.0f) {
                        const float distance =
                            std::sqrt((impact.x - value.startX) *
                                          (impact.x - value.startX) +
                                      (impact.y - value.startY) *
                                          (impact.y - value.startY));
                        damage = items::RangedDamageAtDistance(
                            value.damage, distance, value.falloffStart,
                            value.falloffEnd, value.falloffScale);
                    }
                    ecs::DamageEvent event;
                    event.sourceId = value.ownerStableId;
                    event.type = ecs::DamageType::Blunt;
                    event.amount = damage;
                    event.dirX = length > 0.0001f ? velocity.x / length : 0.0f;
                    event.dirY = length > 0.0001f ? velocity.y / length : 0.0f;
                    event.knockback = value.knockback;
                    event.staggerSeconds = value.staggerSeconds;
                    const auto* healthBefore =
                        registry.try_get<ecs::Health>(best);
                    const float healthValue =
                        healthBefore != nullptr ? healthBefore->current : 0.0f;
                    if (m_simulation->Combat()->ApplyDamage(
                            m_simulation->Ecs(), best, event)) {
                        ++m_networkStats.projectilesHit;
                        const auto* healthAfter =
                            registry.try_get<ecs::Health>(best);
                        const float applied =
                            healthAfter != nullptr
                                ? std::max(0.0f,
                                           healthValue - healthAfter->current)
                                : 0.0f;
                        RecordSiegeDefenseDamage(value.ownerStableId, best,
                                                 applied);
                    }
                }
            }
            if (hit) {
                destroy.push_back(projectile);
                impacts.push_back(impact);
            }
        });

    for (const ecs::Entity projectile : destroy) {
        m_simulation->DespawnEntity(projectile);
    }
    for (const Impact& impact : impacts) {
        SpawnImpactEffect(impact.x, impact.y, impact.z);
    }
}

void ServerHost::UpdateTransientEffects(float dt) {
    if (m_simulation == nullptr || !(dt > 0.0f)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<ecs::Entity> destroy;
    registry.view<ecs::TransientEffect>().each(
        [&](const ecs::Entity effect, ecs::TransientEffect& value) {
            value.remaining -= dt;
            if (value.remaining <= 0.0f) {
                destroy.push_back(effect);
            }
        });
    for (const ecs::Entity effect : destroy) {
        m_simulation->DespawnEntity(effect);
    }
}

void ServerHost::CaptureSessionProfile(SessionPlayer& session, bool markDead) {
    if (!m_profilesEnabled || m_playerProfiles == nullptr ||
        session.playerStableId == 0) {
        return;
    }
    if (markDead) {
        session.dead = true;
    }
    PlayerProfile* profile = FindSessionProfile(session);
    if (profile == nullptr) {
        profile = &m_playerProfiles->Create(session.identity, session.playerName);
    }
    profile->name = session.playerName;
    profile->stableId = session.playerStableId;
    profile->alive = !session.dead;
    if (session.dead) {
        const std::uint64_t remainingTicks =
            session.respawnAtTick > m_ticks ? session.respawnAtTick - m_ticks : 0;
        profile->respawnSeconds = static_cast<float>(remainingTicks) /
                                  std::max(1.0f, m_options.tickRate);
        profile->state.health = 0.0f;
        profile->state.alive = false;
    } else {
        profile->respawnSeconds = 0.0f;
        profile->state.alive = true;
    }

    net::PlayerState state;
    FillPlayerState(session, &state);
    state.status.clear();
    state.statusSequence = 0;
    state.statusTone = 0;
    state.stealth = {};  // M34-F1-T5：潜行只运行时，不写玩家档案/存档。
    if (session.dead) {
        state.health = 0.0f;
    }
    profile->state = std::move(state);

    const ecs::Entity player = m_simulation->Ecs().FindById(session.playerStableId);
    if (player != entt::null) {
        save::SavedEntity snapshot;
        std::string captureError;
        if (save::CaptureEntity(m_simulation->Ecs(), player, *m_saveComponents,
                                &snapshot, &captureError)) {
            profile->entity = std::move(snapshot);
            profile->hasSnapshot = true;
        } else {
            ZL_WARN("server", "玩家档案快照失败：identity={}，原因={}",
                    session.identity, captureError);
        }
    }
}

void ServerHost::SavePlayerProfiles(const char* reason) {
    if (!m_profilesEnabled || m_playerProfiles == nullptr) {
        return;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0) {
            continue;
        }
        CaptureSessionProfile(session, session.dead);
    }
    std::string saveError;
    if (!m_playerProfiles->Save(&saveError)) {
        ZL_WARN("server", "玩家档案保存失败（{}）：{}",
                reason != nullptr ? reason : "manual", saveError);
    } else {
        ++m_networkStats.profileSaves;
        ZL_DEBUG("server", "玩家档案已保存（{}，{} 个）",
                 reason != nullptr ? reason : "manual", m_playerProfiles->Size());
    }
    m_profileSaveTimer = 0.0;
}

void ServerHost::RestoreServerForageState() {
    m_serverForage = {};
    m_serverForageEntity = entt::null;
    if (m_simulation == nullptr) {
        return;
    }
    ecs::EcsWorld& world = m_simulation->Ecs();
    ecs::Entity entity = world.FindById(kServerForageStateStableId);
    if (entity == entt::null) {
        entity = world.CreateWithStableId(kServerForageStateStableId);
        if (entity == entt::null) {
            ZL_WARN("forage", "服务器世界觅食状态实体创建失败");
            return;
        }
        world.Registry().emplace<ecs::ForageState>(entity);
        ZL_INFO("forage", "服务器世界觅食状态实体已创建：stableId={}",
                kServerForageStateStableId);
    } else if (const auto* loaded =
                   world.Registry().try_get<ecs::ForageState>(entity);
               loaded != nullptr) {
        m_serverForage = *loaded;
    } else {
        world.Registry().emplace<ecs::ForageState>(entity);
    }
    m_serverForageEntity = entity;
    if (m_gameTime != nullptr) {
        game::ForagePrune(m_serverForage, m_gameTime->TotalGameMinutes());
    }
    StoreServerForageState();
    ZL_INFO("forage", "服务器世界觅食状态已恢复：{} 个耗尽点",
            m_serverForage.harvested.size());
}

void ServerHost::StoreServerForageState() {
    if (m_simulation == nullptr) {
        return;
    }
    ecs::EcsWorld& world = m_simulation->Ecs();
    if (m_serverForageEntity == entt::null ||
        !world.Alive(m_serverForageEntity)) {
        m_serverForageEntity = world.FindById(kServerForageStateStableId);
    }
    if (m_serverForageEntity == entt::null) {
        m_serverForageEntity = world.CreateWithStableId(kServerForageStateStableId);
    }
    if (m_serverForageEntity == entt::null) {
        return;
    }
    world.Registry().emplace_or_replace<ecs::ForageState>(
        m_serverForageEntity, m_serverForage);
}

void ServerHost::StoreServerSettlementState() {
    if (m_simulation == nullptr) {
        return;
    }
    game::SettlementNpcSystem* system = m_simulation->SettlementNpcs();
    if (system == nullptr) {
        return;
    }
    ecs::EcsWorld& world = m_simulation->Ecs();
    ecs::Entity entity = world.FindById(game::kSettlementStateStableId);
    if (entity == entt::null) {
        entity = world.CreateWithStableId(game::kSettlementStateStableId);
    }
    if (entity == entt::null) {
        return;
    }
    game::SettlementState state;
    system->CaptureState(world, &state);
    world.Registry().emplace_or_replace<game::SettlementState>(
        entity, std::move(state));
}

ServerHost::SessionPlayer* ServerHost::FindSessionByPlayerStableId(
    std::uint64_t stableId) {
    if (stableId == 0) {
        return nullptr;
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == stableId) {
            return &session;
        }
    }
    return nullptr;
}

bool ServerHost::IsServerVehicleCellWalkable(int wx, int wy, int z) const {
    if (m_simulation == nullptr || FindServerLoadedChunk(wx, wy) == nullptr) {
        return false;
    }
    const bool blocked = game::IsSquareBlocked(m_simulation->World(), wx, wy, z);
    world::WorldGraph* graph = m_simulation->Graph();
    if (graph == nullptr) {
        return !blocked;
    }
    const world::GridSquare* square =
        m_simulation->World().GetSquare(wx, wy, z);
    std::string_view floorSprite;
    if (square != nullptr) {
        const world::SquareObject* floor = square->Floor();
        if (floor != nullptr) {
            floorSprite = floor->sprite;
        }
    }
    return graph
        ->ClassifyTerrain(static_cast<float>(wx) + 0.5f,
                          static_cast<float>(wy) + 0.5f, blocked, floorSprite)
        .vehiclePassable;
}

game::VehicleSurface ServerHost::ServerVehicleSurfaceAt(float x, float y,
                                                         int z) const {
    if (m_simulation == nullptr) {
        return game::VehicleSurface::Grass;
    }
    const world::GridSquare* square = m_simulation->World().GetSquare(
        static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), z);
    if (square == nullptr) {
        return game::VehicleSurface::Grass;
    }
    const world::SquareObject* floor = square->Floor();
    if (floor == nullptr || floor->sprite.empty()) {
        return game::VehicleSurface::Grass;
    }
    return game::VehicleSurfaceForFloor(floor->sprite);
}

std::vector<std::uint64_t> ServerHost::VehicleStableIds() const {
    std::vector<std::uint64_t> ids;
    ids.reserve(m_serverVehicles.size());
    for (const ServerVehicleRuntime& vehicle : m_serverVehicles) {
        if (vehicle.stableId != 0) {
            ids.push_back(vehicle.stableId);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

ServerHost::ServerVehicleRuntime* ServerHost::FindServerVehicle(
    std::uint64_t stableId) {
    if (stableId == 0) {
        return nullptr;
    }
    for (ServerVehicleRuntime& vehicle : m_serverVehicles) {
        if (vehicle.stableId == stableId) {
            return &vehicle;
        }
    }
    return nullptr;
}

const ServerHost::ServerVehicleRuntime* ServerHost::FindServerVehicle(
    std::uint64_t stableId) const {
    if (stableId == 0) {
        return nullptr;
    }
    for (const ServerVehicleRuntime& vehicle : m_serverVehicles) {
        if (vehicle.stableId == stableId) {
            return &vehicle;
        }
    }
    return nullptr;
}

ServerHost::ServerVehicleRuntime* ServerHost::FindServerVehicleForSession(
    const SessionPlayer& session) {
    return FindServerVehicle(session.drivingVehicleId);
}

ServerHost::ServerVehicleRuntime* ServerHost::FindNearestServerVehicle(
    float x, float y, int z, float range, float* outDistance) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(range) ||
        range < 0.0f || m_simulation == nullptr || m_serverVehicles.empty()) {
        return nullptr;
    }
    ServerVehicleRuntime* best = nullptr;
    float bestDistanceSq = range * range;
    for (ServerVehicleRuntime& vehicle : m_serverVehicles) {
        if (vehicle.entity == entt::null ||
            !m_simulation->Ecs().Alive(vehicle.entity) ||
            vehicle.motion.z != z) {
            continue;
        }
        const float dx = vehicle.motion.x - x;
        const float dy = vehicle.motion.y - y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            best = &vehicle;
            bestDistanceSq = distanceSq;
        }
    }
    if (best != nullptr && outDistance != nullptr) {
        *outDistance = std::sqrt(bestDistanceSq);
    }
    return best;
}

bool ServerHost::IsServerVehiclePassengerStableId(
    const ServerVehicleRuntime& vehicle, std::uint64_t stableId) const {
    return stableId != 0 &&
           (vehicle.passengerStableId == stableId ||
            vehicle.secondPassengerStableId == stableId);
}

std::uint64_t* ServerHost::FindFreeServerVehiclePassengerSeat(
    ServerVehicleRuntime& vehicle) {
    if (vehicle.passengerStableId == 0) {
        return &vehicle.passengerStableId;
    }
    if (vehicle.secondPassengerStableId == 0) {
        return &vehicle.secondPassengerStableId;
    }
    return nullptr;
}

void ServerHost::RemoveServerVehiclePassengerStableId(
    ServerVehicleRuntime& vehicle, std::uint64_t stableId) {
    if (stableId == 0) {
        return;
    }
    if (vehicle.passengerStableId == stableId) {
        vehicle.passengerStableId = vehicle.secondPassengerStableId;
        vehicle.secondPassengerStableId = 0;
    } else if (vehicle.secondPassengerStableId == stableId) {
        vehicle.secondPassengerStableId = 0;
    }
}

void ServerHost::SyncServerVehiclePassengers(
    ServerVehicleRuntime& vehicle) {
    if (m_simulation == nullptr || vehicle.entity == entt::null ||
        !m_simulation->Ecs().Alive(vehicle.entity)) {
        return;
    }
    const std::array<std::uint64_t, game::kVehicleMaxPassengers> passengers{
        vehicle.passengerStableId, vehicle.secondPassengerStableId};
    for (const std::uint64_t stableId : passengers) {
        if (stableId == 0 ||
            !IsServerVehiclePassengerStableId(vehicle, stableId)) {
            continue;
        }
        SessionPlayer* session = FindSessionByPlayerStableId(stableId);
        const ecs::Entity passenger =
            m_simulation->Ecs().FindById(stableId);
        const auto* health =
            passenger != entt::null
                ? m_simulation->Ecs().Registry().try_get<ecs::Health>(passenger)
                : nullptr;
        if (session != nullptr && !session->dead && session->spawned &&
            passenger != entt::null &&
            (health == nullptr || health->current > 0.0f)) {
            if (session->drivingVehicleId != 0 &&
                session->drivingVehicleId != vehicle.stableId) {
                RemoveServerVehiclePassengerStableId(vehicle, stableId);
                SyncServerVehicleToEcs(vehicle);
                continue;
            }
            if (session->drivingVehicleId != vehicle.stableId) {
                session->drivingVehicleId = vehicle.stableId;
                session->stateDirty = true;
            }
            SyncVehiclePassenger(vehicle, passenger);
            continue;
        }
        if (session != nullptr) {
            ReleaseServerVehicleSeat(*session, true, "乘客不可用");
        } else {
            RemoveServerVehiclePassengerStableId(vehicle, stableId);
            SyncServerVehicleToEcs(vehicle);
        }
    }
}

void ServerHost::SyncServerVehicleToEcs(ServerVehicleRuntime& vehicle) {
    if (m_simulation == nullptr || vehicle.entity == entt::null ||
        !m_simulation->Ecs().Alive(vehicle.entity)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity entity = vehicle.entity;
    auto* position = registry.try_get<ecs::Position>(entity);
    if (position == nullptr) {
        position = &registry.emplace<ecs::Position>(
            entity, ecs::Position{vehicle.motion.x, vehicle.motion.y,
                                  vehicle.motion.z});
    } else {
        position->x = vehicle.motion.x;
        position->y = vehicle.motion.y;
        position->z = vehicle.motion.z;
    }
    auto* state = registry.try_get<ecs::Vehicle>(entity);
    if (state == nullptr) {
        state = &registry.emplace<ecs::Vehicle>(entity);
    }
    vehicle.motion.fuel = std::clamp(vehicle.motion.fuel, 0.0f, 1.0f);
    vehicle.motion.condition =
        std::clamp(vehicle.motion.condition, 0.0f, 1.0f);
    vehicle.motion.heading = game::VehicleNormalizeAngle(
        std::isfinite(vehicle.motion.heading) ? vehicle.motion.heading
                                              : 0.0f);
    state->fuel = vehicle.motion.fuel;
    state->condition = vehicle.motion.condition;
    state->headlights = vehicle.motion.headlights;
    state->type = vehicle.motion.type;
    state->heading = vehicle.motion.heading;
    auto* velocity = registry.try_get<ecs::Velocity>(entity);
    if (velocity == nullptr) {
        velocity = &registry.emplace<ecs::Velocity>(entity);
    }
    velocity->x = vehicle.motion.vx;
    velocity->y = vehicle.motion.vy;
    vehicle.visualDirection = game::VehicleVisualDirectionForMotion(
        vehicle.motion, vehicle.visualDirection);
    auto* sprite = registry.try_get<ecs::SpriteRef>(entity);
    if (sprite == nullptr) {
        sprite = &registry.emplace<ecs::SpriteRef>(
            entity, ecs::SpriteRef{0, 0, 0xFFFFFFFFu});
    }
    sprite->sprite =
        ecs::HashName(game::VehicleVisualSprite(vehicle.visualDirection));
    registry.emplace_or_replace<ecs::VehicleDriver>(
        entity, ecs::VehicleDriver{vehicle.driverStableId});
    if (vehicle.passengerStableId != 0 ||
        vehicle.secondPassengerStableId != 0) {
        registry.emplace_or_replace<ecs::VehiclePassenger>(
            entity, ecs::VehiclePassenger{vehicle.passengerStableId,
                                          vehicle.secondPassengerStableId});
    } else if (registry.all_of<ecs::VehiclePassenger>(entity)) {
        registry.remove<ecs::VehiclePassenger>(entity);
    }
}

void ServerHost::PlaceServerPlayer(ecs::Entity player, float x, float y,
                                   int z) {
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    auto* position = registry.try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    position->x = x;
    position->y = y;
    position->z = z;
    if (auto* velocity = registry.try_get<ecs::Velocity>(player);
        velocity != nullptr) {
        *velocity = {};
    }
    if (auto* move = registry.try_get<ecs::MoveInput>(player);
        move != nullptr) {
        *move = {};
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(player);
        intent != nullptr) {
        *intent = {};
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(player);
        request != nullptr) {
        request->newRequest = false;
        request->queryId = 0;
        request->state = ecs::PathStatus::None;
    }
    if (auto* path = registry.try_get<ecs::PathFollow>(player);
        path != nullptr) {
        path->waypoints.clear();
        path->index = 0;
        path->status = ecs::PathStatus::None;
        path->anchorX = x;
        path->anchorY = y;
        path->stuckTimer = 0.0f;
    }
    if (auto* attack = registry.try_get<ecs::AttackIntent>(player);
        attack != nullptr) {
        attack->pending = false;
    }
    if (auto* rigid = registry.try_get<ecs::RigidBody>(player);
        rigid != nullptr && rigid->body != nullptr) {
        m_simulation->Physics().TeleportBody(rigid->body, x, y, z);
    }
}

void ServerHost::SyncVehicleDriver(ServerVehicleRuntime& vehicle,
                                   ecs::Entity player) {
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return;
    }
    PlaceServerPlayer(player, vehicle.motion.x, vehicle.motion.y,
                      vehicle.motion.z);
    m_simulation->Ecs().Registry().emplace_or_replace<ecs::VehicleOccupant>(
        player,
        ecs::VehicleOccupant{m_vehicleTuning.driverDamageReduction});
}

void ServerHost::SyncVehiclePassenger(ServerVehicleRuntime& vehicle,
                                      ecs::Entity player) {
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return;
    }
    PlaceServerPlayer(player, vehicle.motion.x, vehicle.motion.y,
                      vehicle.motion.z);
    m_simulation->Ecs().Registry().emplace_or_replace<ecs::VehicleOccupant>(
        player,
        ecs::VehicleOccupant{m_vehicleTuning.driverDamageReduction});
}

void ServerHost::PlacePlayerOutsideServerVehicle(ecs::Entity player) {
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return;
    }
    const auto* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return;
    }
    for (ServerVehicleRuntime& vehicle : m_serverVehicles) {
        if (vehicle.entity == entt::null ||
            !m_simulation->Ecs().Alive(vehicle.entity) ||
            position->z != vehicle.motion.z) {
            continue;
        }
        const float safeRadius =
            std::max(0.75f,
                     game::VehicleRadiusForType(vehicle.motion.type));
        const float dx = position->x - vehicle.motion.x;
        const float dy = position->y - vehicle.motion.y;
        if (dx * dx + dy * dy > safeRadius * safeRadius) {
            continue;
        }
        float exitX = 0.0f;
        float exitY = 0.0f;
        if (!FindServerVehicleExitCell(vehicle, &exitX, &exitY)) {
            continue;
        }
        PlaceServerPlayer(player, exitX, exitY, vehicle.motion.z);
        ZL_INFO("vehicle",
                "座位恢复：玩家 stableId={} 从载具 {} 移到格 ({:.1f}, {:.1f})",
                m_simulation->Ecs().IdOf(player).id, vehicle.stableId, exitX,
                exitY);
        return;
    }
}

bool ServerHost::FindServerVehicleExitCell(
    const ServerVehicleRuntime& vehicle, float* x, float* y) const {
    if (vehicle.entity == entt::null) {
        return false;
    }
    const int z = vehicle.motion.z;
    return game::FindVehicleExitCell(
        vehicle.motion.x, vehicle.motion.y,
        [this, z](int wx, int wy) {
            return IsServerVehicleCellWalkable(wx, wy, z);
        },
        x, y, 3);
}

game::VehicleModStats ServerHost::ServerVehicleModStats(
    const ServerVehicleRuntime& vehicle) const {
    if (m_simulation == nullptr || m_vehicleMods == nullptr ||
        !m_vehicleMods->enabled || vehicle.entity == entt::null) {
        return {};
    }
    const auto* mods = m_simulation->Ecs().Registry().try_get<ecs::VehicleMods>(
        vehicle.entity);
    return mods != nullptr ? game::VehicleModStatsFor(*m_vehicleMods, *mods)
                           : game::VehicleModStats{};
}

void ServerHost::SyncServerVehicleModStorage(
    ServerVehicleRuntime& vehicle) {
    if (m_simulation == nullptr || vehicle.entity == entt::null) {
        return;
    }
    auto* container = m_simulation->Ecs().Registry().try_get<ecs::Container>(
        vehicle.entity);
    if (container == nullptr) {
        return;
    }
    const float before = container->capacityKg;
    if (game::ApplyVehicleModTrunkCapacity(
            ServerVehicleModStats(vehicle), m_vehicleTuning.trunkCapacityKg,
            container)) {
        ZL_INFO("vehicle",
                "服务器载具改装后备箱容量：stableId={}，{:.1f}kg → {:.1f}kg",
                vehicle.stableId, before, container->capacityKg);
    }
}

void ServerHost::AdoptServerVehicle(ecs::Entity entity, bool restored) {
    if (m_simulation == nullptr || entity == entt::null ||
        !m_simulation->Ecs().Alive(entity)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const std::uint64_t stableId = m_simulation->Ecs().IdOf(entity).id;
    if (stableId == 0 || FindServerVehicle(stableId) != nullptr) {
        return;
    }
    auto* position = registry.try_get<ecs::Position>(entity);
    auto* state = registry.try_get<ecs::Vehicle>(entity);
    if (position == nullptr || state == nullptr) {
        return;
    }
    ServerVehicleRuntime& vehicle = m_serverVehicles.emplace_back();
    vehicle.entity = entity;
    vehicle.stableId = stableId;
    vehicle.motion.x = position->x;
    vehicle.motion.y = position->y;
    vehicle.motion.z = position->z;
    if (const auto* velocity = registry.try_get<ecs::Velocity>(entity);
        velocity != nullptr) {
        vehicle.motion.vx = velocity->x;
        vehicle.motion.vy = velocity->y;
    }
    vehicle.motion.fuel =
        std::isfinite(state->fuel) ? std::clamp(state->fuel, 0.0f, 1.0f) : 0.0f;
    vehicle.motion.condition =
        std::isfinite(state->condition)
            ? std::clamp(state->condition, 0.0f, 1.0f)
            : 0.0f;
    vehicle.motion.headlights = state->headlights;
    vehicle.motion.type = game::VehicleTypeIsValid(state->type)
                              ? state->type
                              : game::VehicleTypeForStableId(stableId);
    if (std::isfinite(state->heading)) {
        vehicle.motion.heading =
            game::VehicleNormalizeAngle(state->heading);
    }
    if (!registry.all_of<ecs::SpriteRef>(entity)) {
        registry.emplace<ecs::SpriteRef>(
            entity, ecs::SpriteRef{ecs::HashName("object.vehicle_south"), 0,
                                   0xFFFFFFFFu});
    }
    if (const auto* sprite = registry.try_get<ecs::SpriteRef>(entity);
        sprite != nullptr) {
        vehicle.visualDirection =
            VehicleVisualDirectionFromSprite(sprite->sprite);
        if (!std::isfinite(state->heading)) {
            float headingX = 0.0f;
            float headingY = 0.0f;
            game::VehicleVisualDirectionVector(vehicle.visualDirection,
                                               &headingX, &headingY);
            vehicle.motion.heading = std::atan2(headingY, headingX);
        }
    }
    if (!registry.all_of<ecs::SpriteScale>(entity)) {
        registry.emplace<ecs::SpriteScale>(entity, ecs::SpriteScale{1.0f});
    }
    if (!registry.all_of<ecs::Collider>(entity)) {
        registry.emplace<ecs::Collider>(
            entity,
            ecs::Collider{game::VehicleRadiusForType(vehicle.motion.type),
                          1.2f, ecs::kColliderCharacter, ecs::kColliderAll});
    }
    if (!registry.all_of<ecs::Container>(entity)) {
        registry.emplace<ecs::Container>(
            entity, ecs::Container{{}, m_vehicleTuning.trunkCapacityKg});
    }
    if (!registry.all_of<ecs::PlayerStorage>(entity)) {
        registry.emplace<ecs::PlayerStorage>(
            entity, ecs::PlayerStorage{m_vehicleTuning.trunkStacks});
    }
    SyncServerVehicleModStorage(vehicle);
    registry.emplace_or_replace<ecs::VehicleDriver>(
        entity, ecs::VehicleDriver{0});
    if (registry.all_of<ecs::VehiclePassenger>(entity)) {
        registry.remove<ecs::VehiclePassenger>(entity);
    }
    SyncServerVehicleToEcs(vehicle);
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        session.stateDirty = true;
    }
    if (restored) {
        const auto* container = registry.try_get<ecs::Container>(entity);
        ZL_INFO("vehicle",
                "载具恢复：stableId={}，格 ({:.1f}, {:.1f})，燃料 {:.0f}%，"
                "车况 {:.0f}%，后备箱 {} 件",
                vehicle.stableId, vehicle.motion.x, vehicle.motion.y,
                vehicle.motion.fuel * 100.0f,
                vehicle.motion.condition * 100.0f,
                container != nullptr ? container->items.size() : 0u);
    }
}

void ServerHost::EnsureServerVehicles() {
    if (m_simulation == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    std::vector<std::uint64_t> removed;
    for (auto it = m_serverVehicles.begin(); it != m_serverVehicles.end();) {
        if (it->entity == entt::null ||
            !m_simulation->Ecs().Alive(it->entity)) {
            removed.push_back(it->stableId);
            it = m_serverVehicles.erase(it);
        } else {
            ++it;
        }
    }
    for (const std::uint64_t stableId : removed) {
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (session.drivingVehicleId != stableId) {
                continue;
            }
            session.drivingVehicleId = 0;
            session.stateDirty = true;
            if (session.playerStableId != 0 && m_simulation != nullptr) {
                const ecs::Entity rider =
                    m_simulation->Ecs().FindById(session.playerStableId);
                if (rider != entt::null &&
                    m_simulation->Ecs().Alive(rider)) {
                    registry.remove<ecs::VehicleOccupant>(rider);
                }
            }
        }
    }

    registry.view<ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Vehicle&) {
            if (!m_simulation->Ecs().Alive(entity) ||
                registry.try_get<ecs::Position>(entity) == nullptr) {
                return;
            }
            AdoptServerVehicle(entity);
        });

    if (m_serverVehicles.size() >= m_vehicleSpawnCount) {
        return;
    }

    ecs::Entity anchor = entt::null;
    std::uint64_t anchorSessionId = std::numeric_limits<std::uint64_t>::max();
    for (const auto& [sessionId, session] : m_sessions) {
        if (!session.spawned || session.dead || session.playerStableId == 0 ||
            sessionId >= anchorSessionId) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        if (player == entt::null) {
            continue;
        }
        anchor = player;
        anchorSessionId = sessionId;
    }
    if (anchor == entt::null && m_simulation->Player() != entt::null) {
        anchor = m_simulation->Player();
    }
    if (anchor == entt::null) {
        return;
    }
    const auto* anchorPosition = registry.try_get<ecs::Position>(anchor);
    if (anchorPosition == nullptr) {
        return;
    }

    static constexpr int kSpawnOffsets[][2] = {
        {6, 6},  {6, -6},  {-6, 6},  {-6, -6},
        {10, 0}, {0, 10},  {-10, 0}, {0, -10},
        {10, 10}, {10, -10}, {-10, 10}, {-10, -10}};
    constexpr std::size_t kOffsetCount =
        sizeof(kSpawnOffsets) / sizeof(kSpawnOffsets[0]);
    const auto overlapsVehicle = [&](float x, float y) {
        for (const ServerVehicleRuntime& vehicle : m_serverVehicles) {
            const float dx = vehicle.motion.x - x;
            const float dy = vehicle.motion.y - y;
            if (dx * dx + dy * dy < 9.0f) {
                return true;
            }
        }
        return false;
    };
    const auto spawnOne = [&](int offsetX, int offsetY) {
        int cellX = 0;
        int cellY = 0;
        if (!game::FindVehicleSpawnCell(
                static_cast<int>(std::floor(anchorPosition->x)) + offsetX,
                static_cast<int>(std::floor(anchorPosition->y)) + offsetY,
                m_vehicleTuning.spawnSearchRadius,
                [this](int wx, int wy) {
                    return IsServerVehicleCellWalkable(wx, wy, 0);
                },
                &cellX, &cellY)) {
            return false;
        }
        const float spawnX = static_cast<float>(cellX) + 0.5f;
        const float spawnY = static_cast<float>(cellY) + 0.5f;
        const float dx = spawnX - anchorPosition->x;
        const float dy = spawnY - anchorPosition->y;
        if (dx * dx + dy * dy < 4.0f || overlapsVehicle(spawnX, spawnY)) {
            return false;
        }
        const ecs::Entity entity = m_simulation->Ecs().Create();
        if (entity == entt::null) {
            return false;
        }
        const std::uint64_t stableId = m_simulation->Ecs().IdOf(entity).id;
        const ecs::VehicleType type = game::VehicleTypeForStableId(stableId);
        const game::VehicleProfile& profile =
            game::VehicleProfileFor(type);
        ecs::Vehicle vehicleState;
        vehicleState.type = type;
        vehicleState.heading = 1.5707963267948966f;
        registry.emplace<ecs::Position>(entity,
                                        ecs::Position{spawnX, spawnY, 0});
        registry.emplace<ecs::SpriteRef>(
            entity, ecs::SpriteRef{ecs::HashName("object.vehicle_south"), 0,
                                   0xFFFFFFFFu});
        registry.emplace<ecs::SpriteScale>(entity, ecs::SpriteScale{1.0f});
        registry.emplace<ecs::Collider>(
            entity, ecs::Collider{profile.radius, 1.2f,
                                  ecs::kColliderCharacter,
                                  ecs::kColliderAll});
        registry.emplace<ecs::Vehicle>(entity, vehicleState);
        registry.emplace<ecs::Container>(
            entity, ecs::Container{{}, m_vehicleTuning.trunkCapacityKg});
        registry.emplace<ecs::PlayerStorage>(
            entity, ecs::PlayerStorage{m_vehicleTuning.trunkStacks});
        registry.emplace<ecs::VehicleDriver>(entity, ecs::VehicleDriver{0});
        AdoptServerVehicle(entity, false);
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            session.stateDirty = true;
        }
        ZL_INFO("vehicle",
                "服务器载具生成：stableId={}，{}（{:.0f}kg），格 ({}, {})，"
                "燃料 100%，车况 100%",
                stableId, game::VehicleTypeName(type), profile.curbMassKg,
                cellX, cellY);
        return true;
    };

    std::size_t nextOffset = 0;
    while (m_serverVehicles.size() < m_vehicleSpawnCount &&
           nextOffset < kOffsetCount) {
        bool spawned = false;
        for (std::size_t attempt = 0; attempt < kOffsetCount; ++attempt) {
            const std::size_t index = (nextOffset + attempt) % kOffsetCount;
            if (spawnOne(kSpawnOffsets[index][0], kSpawnOffsets[index][1])) {
                spawned = true;
                nextOffset = index + 1;
                break;
            }
        }
        if (!spawned) {
            break;
        }
    }
    if (m_serverVehicles.size() < m_vehicleSpawnCount) {
        ZL_DEBUG("vehicle", "服务器载具数量 {} < 目标 {}，等待可用空地",
                 m_serverVehicles.size(), m_vehicleSpawnCount);
    }
}

bool ServerHost::ApplyServerVehicleVehicleCollisions(
    ServerVehicleRuntime& vehicle,
    const std::vector<std::uint64_t>& targets,
    const game::VehicleMotion& previousMotion,
    const game::VehicleModStats& modStats) {
    if (m_simulation == nullptr || targets.empty()) {
        return false;
    }
    bool damaged = false;
    for (const std::uint64_t targetId : targets) {
        ServerVehicleRuntime* other = FindServerVehicle(targetId);
        if (other == nullptr || other == &vehicle ||
            other->entity == entt::null ||
            !m_simulation->Ecs().Alive(other->entity)) {
            continue;
        }
        if (vehicle.vehicleCollisionCooldowns.find(targetId) !=
                vehicle.vehicleCollisionCooldowns.end() ||
            other->vehicleCollisionCooldowns.find(vehicle.stableId) !=
                other->vehicleCollisionCooldowns.end()) {
            continue;
        }
        const float impactSpeed = game::VehicleCollisionImpactSpeed(
            previousMotion, other->motion);
        const game::VehicleModStats otherModStats =
            ServerVehicleModStats(*other);
        const float vehicleWear =
            game::VehicleApplyCollisionConditionDamageForType(
                &vehicle.motion, impactSpeed, m_vehicleTuning,
                game::VehicleModCrashWearScale(modStats));
        const float otherWear =
            game::VehicleApplyCollisionConditionDamageForType(
                &other->motion, impactSpeed, m_vehicleTuning,
                game::VehicleModCrashWearScale(otherModStats));
        const bool hasDamage = vehicleWear > 0.0f || otherWear > 0.0f;
        const bool collisionSpeed =
            std::isfinite(impactSpeed) &&
            std::isfinite(m_vehicleTuning.vehicleCollisionMinSpeed) &&
            impactSpeed >= m_vehicleTuning.vehicleCollisionMinSpeed;
        game::VehicleMotion impactMotion = vehicle.motion;
        impactMotion.vx = previousMotion.vx;
        impactMotion.vy = previousMotion.vy;
        const float firstMass = game::VehicleMassForType(vehicle.motion.type);
        const float secondMass =
            game::VehicleMassForType(other->motion.type);
        const float firstRadius =
            game::VehicleRadiusForType(vehicle.motion.type);
        const float secondRadius =
            game::VehicleRadiusForType(other->motion.type);
        const game::VehicleCollisionImpulseResult impulse =
            collisionSpeed
                ? game::VehicleCollisionImpulseFor(
                      impactMotion, other->motion, m_vehicleTuning, firstMass,
                      secondMass, firstRadius, secondRadius)
                : game::VehicleContactImpulseFor(
                      impactMotion, other->motion, m_vehicleTuning, firstMass,
                      secondMass, firstRadius, secondRadius);
        if (!hasDamage && !impulse.applied) {
            continue;
        }
        if (hasDamage) {
            vehicle.vehicleCollisionCooldowns[targetId] =
                m_vehicleTuning.vehicleCollisionCooldownSeconds;
            other->vehicleCollisionCooldowns[vehicle.stableId] =
                m_vehicleTuning.vehicleCollisionCooldownSeconds;
            damaged = true;
        }
        float collisionNormalX = 1.0f;
        float collisionNormalY = 0.0f;
        if (impulse.applied) {
            vehicle.motion.vx = impulse.firstVx;
            vehicle.motion.vy = impulse.firstVy;
            vehicle.motion.angularVelocity = impulse.firstAngularVelocity;
            other->motion.vx = impulse.secondVx;
            other->motion.vy = impulse.secondVy;
            other->motion.angularVelocity = impulse.secondAngularVelocity;
            collisionNormalX = impulse.normalX;
            collisionNormalY = impulse.normalY;
            if (hasDamage) {
                ZL_INFO("vehicle",
                        "服务器载具冲量：{} + {}，法线 ({:.2f}, {:.2f})，"
                        "质量 {:.2f}/{:.2f}，接近速度 {:.1f}，速度 "
                        "{:.2f}/{:.2f}，角速度 {:.2f}/{:.2f}",
                        vehicle.stableId, other->stableId, impulse.normalX,
                        impulse.normalY, firstMass, secondMass,
                        impulse.closingSpeed,
                        game::VehicleSpeed(vehicle.motion),
                        game::VehicleSpeed(other->motion),
                        vehicle.motion.angularVelocity,
                        other->motion.angularVelocity);
            } else {
                ZL_DEBUG("vehicle",
                         "服务器载具接触：{} + {}，接近速度 {:.1f}，速度 "
                         "{:.2f}/{:.2f}",
                         vehicle.stableId, other->stableId,
                         impulse.closingSpeed,
                         game::VehicleSpeed(vehicle.motion),
                         game::VehicleSpeed(other->motion));
            }
        } else {
            float dx = other->motion.x - vehicle.motion.x;
            float dy = other->motion.y - vehicle.motion.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 1.0e-4f) {
                collisionNormalX = dx / length;
                collisionNormalY = dy / length;
            }
        }
        if (!hasDamage) {
            SyncServerVehicleToEcs(*other);
            if (other->driverStableId != 0) {
                const ecs::Entity driver =
                    m_simulation->Ecs().FindById(other->driverStableId);
                if (driver != entt::null) {
                    SyncVehicleDriver(*other, driver);
                }
            }
            SyncServerVehiclePassengers(*other);
            continue;
        }
        const auto damageOccupants =
            [this, impactSpeed](ServerVehicleRuntime& target,
                                std::uint64_t sourceId, float dirX,
                                float dirY) {
                const float occupantDamage =
                    game::VehicleCollisionOccupantDamageForType(
                        impactSpeed, m_vehicleTuning, target.motion.type);
                if (occupantDamage <= 0.0f) {
                    return;
                }
                const std::uint64_t occupantIds[] = {
                    target.driverStableId, target.passengerStableId,
                    target.secondPassengerStableId};
                for (const std::uint64_t occupantId : occupantIds) {
                    if (occupantId == 0) {
                        continue;
                    }
                    const ecs::Entity occupant =
                        m_simulation->Ecs().FindById(occupantId);
                    if (occupant == entt::null ||
                        !m_simulation->Ecs().Alive(occupant)) {
                        continue;
                    }
                    auto* health =
                        m_simulation->Ecs().Registry().try_get<ecs::Health>(
                            occupant);
                    if (health == nullptr || health->current <= 0.0f) {
                        continue;
                    }
                    const float healthBefore = health->current;
                    ecs::DamageEvent event;
                    event.sourceId = sourceId;
                    event.type = ecs::DamageType::Blunt;
                    event.amount = occupantDamage;
                    event.dirX = dirX;
                    event.dirY = dirY;
                    if (!m_simulation->Combat()->ApplyDamage(
                            m_simulation->Ecs(), occupant, event)) {
                        continue;
                    }
                    const float applied = std::max(
                        0.0f, healthBefore - health->current);
                    ZL_INFO("vehicle",
                            "服务器载具碰撞乘员伤害：载具 {}，玩家 {}，伤害 "
                            "{:.1f}，剩余 {:.0f} HP",
                            target.stableId,
                            static_cast<unsigned long long>(occupantId),
                            applied, health->current);
                    if (SessionPlayer* session =
                            FindSessionByPlayerStableId(occupantId);
                        session != nullptr) {
                        session->stateDirty = true;
                        SetSessionStatus(
                            *session,
                            "车辆碰撞：乘员 -" +
                                std::to_string(static_cast<int>(
                                    std::lround(applied))) +
                                " HP",
                            2);
                    }
                }
            };
        damageOccupants(vehicle, other->stableId, -collisionNormalX,
                        -collisionNormalY);
        damageOccupants(*other, vehicle.stableId, collisionNormalX,
                        collisionNormalY);
        const auto notify = [this](ServerVehicleRuntime& target,
                                   float wear) {
            if (target.driverStableId == 0) {
                return;
            }
            SessionPlayer* session =
                FindSessionByPlayerStableId(target.driverStableId);
            if (session == nullptr) {
                return;
            }
            session->stateDirty = true;
            SetSessionStatus(
                *session,
                "车辆碰撞：车况 " +
                    std::to_string(static_cast<int>(std::lround(
                        std::clamp(target.motion.condition, 0.0f, 1.0f) *
                        100.0f))) +
                    "%（- " +
                    std::to_string(static_cast<int>(
                        std::lround(wear * 100.0f))) +
                    "%）",
                2);
        };
        notify(vehicle, vehicleWear);
        notify(*other, otherWear);

        net::WorldSoundEvent sound;
        sound.sourcePlayerStableId =
            vehicle.driverStableId != 0 ? vehicle.driverStableId
                                        : other->driverStableId;
        sound.x = (vehicle.motion.x + other->motion.x) * 0.5f;
        sound.y = (vehicle.motion.y + other->motion.y) * 0.5f;
        sound.z = vehicle.motion.z;
        sound.radius = 18.0f;
        sound.type = static_cast<std::uint8_t>(core::SoundType::Impact);
        sound.clip = audio::kVehicleRam;
        BroadcastWorldSound(sound);

        float pushedDistance = 0.0f;
        const float pushDistance =
            game::VehicleCollisionPushDistanceForMasses(
                impactSpeed, m_vehicleTuning,
                game::VehicleMassForType(vehicle.motion.type),
                game::VehicleMassForType(other->motion.type));
        if (pushDistance > 0.0f) {
            float dirX = other->motion.x - vehicle.motion.x;
            float dirY = other->motion.y - vehicle.motion.y;
            float length = std::sqrt(dirX * dirX + dirY * dirY);
            if (length > 1.0e-4f) {
                dirX /= length;
                dirY /= length;
            } else {
                const float previousSpeed = game::VehicleSpeed(previousMotion);
                if (previousSpeed > 1.0e-4f) {
                    dirX = previousMotion.vx / previousSpeed;
                    dirY = previousMotion.vy / previousSpeed;
                } else {
                    dirX = 1.0f;
                    dirY = 0.0f;
                }
            }
            const auto blocked = [this](int x, int y, int z) {
                return !IsServerVehicleCellWalkable(x, y, z);
            };
            const auto bodyBlocked = [this, other, &vehicle](
                                         float x, float y, int z, float radius) {
                for (const ServerVehicleRuntime& third : m_serverVehicles) {
                    if (&third == other || &third == &vehicle ||
                        third.motion.z != z) {
                        continue;
                    }
                    if (game::VehicleBodiesOverlap(
                            x, y, radius, third.motion.x, third.motion.y,
                            game::VehicleRadiusForType(third.motion.type))) {
                        return true;
                    }
                }
                return false;
            };
            const game::VehiclePushResult push = game::VehiclePushBody(
                other->motion.x, other->motion.y, other->motion.z, dirX, dirY,
                pushDistance,
                game::VehicleRadiusForType(other->motion.type), blocked,
                bodyBlocked);
            other->motion.x = push.x;
            other->motion.y = push.y;
            pushedDistance = push.distance;
        }
        SyncServerVehicleToEcs(*other);
        if (other->driverStableId != 0) {
            const ecs::Entity driver =
                m_simulation->Ecs().FindById(other->driverStableId);
            if (driver != entt::null) {
                SyncVehicleDriver(*other, driver);
            }
        }
        SyncServerVehiclePassengers(*other);

        ZL_INFO("vehicle",
                "服务器载具间碰撞：{} + {}，相对速度 {:.1f}，车况 -{:.1f}%/"
                "-{:.1f}%，推挤 {:.2f} 格，剩余 {:.0f}%/{:.0f}%",
                vehicle.stableId, other->stableId, impactSpeed,
                vehicleWear * 100.0f, otherWear * 100.0f, pushedDistance,
                vehicle.motion.condition * 100.0f,
                other->motion.condition * 100.0f);
    }
    return damaged;
}

bool ServerHost::ApplyServerVehicleRam(
    ServerVehicleRuntime& vehicle,
    const game::VehicleMotion* previousMotion,
    const game::VehicleModStats& modStats) {
    if (m_simulation == nullptr || m_simulation->Combat() == nullptr ||
        vehicle.entity == entt::null) {
        return false;
    }
    const game::VehicleMotion& ramMotion =
        previousMotion != nullptr &&
                game::VehicleSpeed(*previousMotion) >
                    game::VehicleSpeed(vehicle.motion)
            ? *previousMotion
            : vehicle.motion;
    const float speed = game::VehicleSpeed(ramMotion);
    const float ramDamageScale = game::VehicleModRamDamageScale(modStats);
    if (game::VehicleRamDamage(speed, m_vehicleTuning, ramDamageScale) <=
        0.0f) {
        return false;
    }
    auto& registry = m_simulation->Ecs().Registry();
    const float dirX = speed > 0.0001f ? ramMotion.vx / speed : 1.0f;
    const float dirY = speed > 0.0001f ? ramMotion.vy / speed : 0.0f;
    bool rammed = false;
    const auto applyRam = [&](ecs::Entity entity,
                              const ecs::Position& position,
                              const ecs::Health& health,
                              game::VehicleRamTargetKind kind) {
        if (kind == game::VehicleRamTargetKind::None ||
            health.current <= 0.0f) {
            return;
        }
        const std::uint64_t targetId = m_simulation->Ecs().IdOf(entity).id;
        if (targetId == 0 ||
            vehicle.ramCooldowns.find(targetId) !=
                vehicle.ramCooldowns.end()) {
            return;
        }
        if (kind == game::VehicleRamTargetKind::Player &&
            (targetId == vehicle.driverStableId ||
             registry.any_of<ecs::VehicleOccupant>(entity) ||
             registry.any_of<ecs::NoCombatTarget>(entity))) {
            return;
        }
        float targetRadius = 0.35f;
        if (const auto* collider = registry.try_get<ecs::Collider>(entity);
            collider != nullptr) {
            targetRadius = collider->radius;
        }
        if (!game::VehicleRamOverlap(
                ramMotion, position.x, position.y, targetRadius,
                game::VehicleRadiusForType(ramMotion.type))) {
            return;
        }
        const ecs::Animal* animal =
            kind == game::VehicleRamTargetKind::Animal
                ? registry.try_get<ecs::Animal>(entity)
                : nullptr;
        if (kind == game::VehicleRamTargetKind::Animal && animal == nullptr) {
            return;
        }
        ecs::DamageEvent event;
        event.sourceId =
            kind == game::VehicleRamTargetKind::Player &&
                    vehicle.driverStableId != 0
                ? vehicle.driverStableId
                : vehicle.stableId;
        event.type = ecs::DamageType::Blunt;
        event.amount =
            game::VehicleRamDamage(speed, m_vehicleTuning, ramDamageScale);
        event.dirX = dirX;
        event.dirY = dirY;
        event.knockback = game::VehicleRamKnockback(speed, m_vehicleTuning);
        event.staggerSeconds =
            game::VehicleRamStagger(speed, m_vehicleTuning);
        const float healthBefore = health.current;
        if (!m_simulation->Combat()->ApplyDamage(m_simulation->Ecs(), entity,
                                                 event)) {
            return;
        }
        RecordSiegeDefenseDamage(
            vehicle.driverStableId, entity,
            std::max(0.0f, healthBefore - health.current));
        vehicle.ramCooldowns[targetId] =
            m_vehicleTuning.ramCooldownSeconds;
        rammed = true;
        const bool lethal =
            registry.get<ecs::Health>(entity).current <= 0.0f;
        if (kind == game::VehicleRamTargetKind::Animal) {
            const game::AnimalDef* def =
                m_animals != nullptr ? m_animals->Find(animal->speciesId)
                                     : nullptr;
            const std::string species =
                def != nullptr ? def->name : animal->speciesId;
            ZL_INFO("vehicle",
                    "服务器载具撞动物：stableId={}，目标 {}，物种 {}，伤害 "
                    "{:.0f}，速度 {:.1f}",
                    vehicle.stableId,
                    static_cast<unsigned long long>(targetId), species,
                    event.amount, speed);
            if (lethal) {
                ZL_INFO("vehicle",
                        "服务器载具撞杀动物：stableId={}，目标 {}，物种 {}",
                        vehicle.stableId,
                        static_cast<unsigned long long>(targetId), species);
            }
            return;
        }
        if (kind == game::VehicleRamTargetKind::Player) {
            ZL_INFO("vehicle",
                    "服务器载具撞玩家：stableId={}，目标 {}，驾驶员 {}，伤害 "
                    "{:.0f}，速度 {:.1f}",
                    vehicle.stableId,
                    static_cast<unsigned long long>(targetId),
                    vehicle.driverStableId, event.amount, speed);
            if (lethal) {
                ZL_INFO("vehicle",
                        "服务器载具撞杀玩家：stableId={}，目标 {}，驾驶员 {}",
                        vehicle.stableId,
                        static_cast<unsigned long long>(targetId),
                        vehicle.driverStableId);
            }
            return;
        }
        ZL_INFO("vehicle",
                "服务器载具撞僵尸：stableId={}，目标 {}，伤害 {:.0f}，速度 {:.1f}",
                vehicle.stableId,
                static_cast<unsigned long long>(targetId), event.amount, speed);
        if (lethal && m_progressionConfig.enabled &&
            m_progressionConfig.meleeXpPerKill > 0.0f) {
            const std::uint64_t driverId = vehicle.driverStableId;
            const ecs::Entity driver =
                m_simulation->Ecs().FindById(driverId);
            SessionPlayer* driverSession =
                FindSessionByPlayerStableId(driverId);
            if (driverId != 0 && driver != entt::null &&
                driverSession != nullptr) {
                AwardProgressionXp(driver, ecs::SkillKind::Melee,
                                   m_progressionConfig.meleeXpPerKill,
                                   driverSession);
                ZL_INFO("vehicle",
                        "服务器载具撞杀：stableId={}，目标 {}，驾驶员 "
                        "{}，+{:.0f} 近战经验",
                        vehicle.stableId,
                        static_cast<unsigned long long>(targetId),
                        driverId, m_progressionConfig.meleeXpPerKill);
            }
        }
    };
    registry.view<ecs::AiState, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::AiState&,
            const ecs::Position& position, const ecs::Health& health) {
            const game::VehicleRamTargetKind kind =
                game::VehicleRamTargetKindFor(
                    true, registry.any_of<ecs::Animal>(entity),
                    registry.any_of<ecs::PlayerTag>(entity),
                    registry.any_of<ecs::FriendlyNpc>(entity), health.current,
                    position.z, vehicle.motion.z);
            if (kind == game::VehicleRamTargetKind::Zombie) {
                applyRam(entity, position, health, kind);
            }
        });
    registry.view<ecs::Animal, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::Animal&,
            const ecs::Position& position, const ecs::Health& health) {
            const game::VehicleRamTargetKind kind =
                game::VehicleRamTargetKindFor(
                    registry.any_of<ecs::AiState>(entity), true,
                    registry.any_of<ecs::PlayerTag>(entity),
                    registry.any_of<ecs::FriendlyNpc>(entity), health.current,
                    position.z, vehicle.motion.z);
            if (kind == game::VehicleRamTargetKind::Animal) {
                applyRam(entity, position, health, kind);
            }
        });
    registry.view<ecs::PlayerTag, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::Health& health) {
            const game::VehicleRamTargetKind kind =
                game::VehicleRamTargetKindFor(
                    false, false, true,
                    registry.any_of<ecs::FriendlyNpc>(entity), health.current,
                    position.z, vehicle.motion.z, m_pvpEnabled);
            if (kind == game::VehicleRamTargetKind::Player) {
                applyRam(entity, position, health, kind);
            }
        });
    if (rammed) {
        if (vehicle.ramWearCooldown <= 0.0f) {
            const float wear = game::VehicleApplyRamConditionDamage(
                &vehicle.motion, speed, m_vehicleTuning,
                game::VehicleModRamWearScale(modStats));
            if (wear > 0.0f) {
                vehicle.ramWearCooldown =
                    m_vehicleTuning.ramCooldownSeconds;
                ZL_INFO("vehicle",
                        "服务器撞击车况：stableId={} 速度 {:.1f}，扣车况 "
                        "{:.1f}%，剩余 {:.0f}%",
                        vehicle.stableId, speed, wear * 100.0f,
                        vehicle.motion.condition * 100.0f);
            }
        }
        net::WorldSoundEvent remote;
        remote.sourcePlayerStableId = vehicle.driverStableId;
        remote.x = vehicle.motion.x;
        remote.y = vehicle.motion.y;
        remote.z = vehicle.motion.z;
        remote.radius = 18.0f;
        remote.type = static_cast<std::uint8_t>(core::SoundType::Impact);
        remote.clip = audio::kVehicleRam;
        BroadcastWorldSound(remote);
    }
    return rammed;
}

bool ServerHost::ApplyServerVehicleAttacks(
    ServerVehicleRuntime& vehicle, SessionPlayer& session,
    const game::VehicleModStats& modStats) {
    if (m_simulation == nullptr || m_simulation->Combat() == nullptr ||
        vehicle.driverStableId == 0 || vehicle.zombieAttackCooldown > 0.0f) {
        return false;
    }
    const ecs::Entity driver =
        m_simulation->Ecs().FindById(vehicle.driverStableId);
    if (driver == entt::null || !m_simulation->Ecs().Alive(driver)) {
        return false;
    }
    auto& registry = m_simulation->Ecs().Registry();
    game::VehicleAttackerKind attackerKind =
        game::VehicleAttackerKind::None;
    std::string attackerSpecies;
    if (m_simulation->Animals() != nullptr) {
        for (const game::AnimalSystem::AttackInfo& attack :
             m_simulation->Animals()->GetAttacks()) {
            if (attack.target != driver || !(attack.damage > 0.0f) ||
                attack.attacker == entt::null ||
                !m_simulation->Ecs().Alive(attack.attacker)) {
                continue;
            }
            const ecs::Animal* animal =
                registry.try_get<ecs::Animal>(attack.attacker);
            const game::VehicleAttackerKind kind =
                game::VehicleAttackerKindFor(
                    registry.try_get<ecs::AiState>(attack.attacker) != nullptr,
                    animal != nullptr,
                    animal != nullptr &&
                        animal->kind == ecs::AnimalKind::Dangerous,
                    registry.any_of<ecs::PlayerTag>(attack.attacker),
                    registry.any_of<ecs::FriendlyNpc>(attack.attacker));
            if (kind == game::VehicleAttackerKind::None) {
                continue;
            }
            attackerKind = kind;
            attackerSpecies = attack.speciesId;
            break;
        }
    }
    for (const characters::CombatSystem::HitInfo& hit :
         m_simulation->Combat()->GetHits()) {
        if (attackerKind != game::VehicleAttackerKind::None) {
            break;
        }
        if (hit.target != driver || !(hit.damage > 0.0f) ||
            hit.attacker == entt::null ||
            !m_simulation->Ecs().Alive(hit.attacker)) {
            continue;
        }
        const ecs::Animal* animal =
            registry.try_get<ecs::Animal>(hit.attacker);
        const game::VehicleAttackerKind kind = game::VehicleAttackerKindFor(
            registry.try_get<ecs::AiState>(hit.attacker) != nullptr,
            animal != nullptr,
            animal != nullptr &&
                animal->kind == ecs::AnimalKind::Dangerous,
            registry.any_of<ecs::PlayerTag>(hit.attacker),
            registry.any_of<ecs::FriendlyNpc>(hit.attacker));
        if (kind == game::VehicleAttackerKind::None) {
            continue;
        }
        attackerKind = kind;
        if (animal != nullptr) {
            attackerSpecies = animal->speciesId;
        }
        break;
    }
    if (attackerKind == game::VehicleAttackerKind::None) {
        return false;
    }
    const float wear = game::VehicleApplyZombieAttackConditionDamage(
        &vehicle.motion, m_vehicleTuning,
        game::VehicleModZombieWearScale(modStats));
    if (wear <= 0.0f) {
        return false;
    }
    vehicle.zombieAttackCooldown =
        m_vehicleTuning.zombieAttackCooldownSeconds;
    session.stateDirty = true;
    net::WorldSoundEvent hit;
    hit.sourcePlayerStableId = vehicle.driverStableId;
    hit.x = vehicle.motion.x;
    hit.y = vehicle.motion.y;
    hit.z = vehicle.motion.z;
    hit.radius = 18.0f;
    hit.type = static_cast<std::uint8_t>(core::SoundType::Impact);
    hit.clip = audio::kVehicleHit;
    BroadcastWorldSound(hit);
    SetSessionStatus(
        session,
        "载具受击：车况 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(vehicle.motion.condition, 0.0f, 1.0f) *
                100.0f))) +
            "%",
        2);
    if (attackerKind == game::VehicleAttackerKind::Animal) {
        const game::AnimalDef* def =
            m_animals != nullptr
                ? m_animals->Find(attackerSpecies)
                : nullptr;
        const std::string species =
            def != nullptr ? def->name : attackerSpecies;
        ZL_INFO("vehicle",
                "服务器载具受动物攻击：stableId={}，驾驶员 {}，物种 {}，"
                "扣车况 {:.1f}%，剩余 {:.0f}%",
                vehicle.stableId, vehicle.driverStableId,
                species, wear * 100.0f,
                vehicle.motion.condition * 100.0f);
        return true;
    }
    ZL_INFO("vehicle",
            "服务器载具受僵尸攻击：stableId={}，驾驶员 {}，扣车况 {:.1f}%，"
            "剩余 {:.0f}%",
            vehicle.stableId, vehicle.driverStableId,
            wear * 100.0f, vehicle.motion.condition * 100.0f);
    return true;
}

void ServerHost::EmitServerVehicleNoise(ServerVehicleRuntime& vehicle,
                                        SessionPlayer& session) {
    if (m_events == nullptr || m_simulation == nullptr ||
        game::VehicleSpeed(vehicle.motion) <
            m_vehicleTuning.noiseMinSpeed) {
        vehicle.noiseCooldown = 0.0f;
        return;
    }
    if (vehicle.noiseCooldown > 0.0f) {
        return;
    }
    core::SoundEvent noise;
    noise.x = vehicle.motion.x;
    noise.y = vehicle.motion.y;
    noise.radius = m_vehicleTuning.noiseRadius;
    noise.type = core::SoundType::Vehicle;

    net::WorldSoundEvent remote;
    remote.sourcePlayerStableId = session.playerStableId;
    remote.x = noise.x;
    remote.y = noise.y;
    remote.z = vehicle.motion.z;
    remote.radius = noise.radius;
    remote.type = static_cast<std::uint8_t>(core::SoundType::Vehicle);
    BroadcastWorldSound(remote);

    m_events->EmitQueued(core::kOnSound, std::move(noise));
    vehicle.noiseCooldown = m_vehicleTuning.noiseIntervalSeconds;
}

void ServerHost::TickServerVehicles(float dt) {
    for (ServerVehicleRuntime& vehicle : m_serverVehicles) {
        TickServerVehicle(vehicle, dt);
    }
}

void ServerHost::TickServerVehicle(ServerVehicleRuntime& vehicle, float dt) {
    if (m_simulation == nullptr || vehicle.entity == entt::null ||
        !m_simulation->Ecs().Alive(vehicle.entity)) {
        return;
    }
    const auto stopVehicle = [&]() {
        vehicle.motion.vx = 0.0f;
        vehicle.motion.vy = 0.0f;
        vehicle.inputX = 0.0f;
        vehicle.inputY = 0.0f;
        vehicle.noiseCooldown = 0.0f;
        vehicle.ramWearCooldown = 0.0f;
        vehicle.hornCooldown = 0.0f;
        vehicle.zombieAttackCooldown = 0.0f;
        vehicle.ramCooldowns.clear();
        vehicle.vehicleCollisionCooldowns.clear();
        SyncServerVehicleToEcs(vehicle);
        SyncServerVehiclePassengers(vehicle);
    };
    const game::VehicleTuning vehicleTuning =
        game::VehicleTuningForType(vehicle.motion.type, m_vehicleTuning);
    const game::VehicleModStats modStats = ServerVehicleModStats(vehicle);
    if (vehicle.driverStableId == 0) {
        const float coastSpeed = game::VehicleSpeed(vehicle.motion);
        if (vehicle.motion.condition <= 0.0f ||
            (coastSpeed <= game::kVehicleCoastStopSpeed &&
             std::fabs(vehicle.motion.angularVelocity) <= 0.01f)) {
            stopVehicle();
            return;
        }
        if (coastSpeed <= game::kVehicleCoastStopSpeed) {
            game::VehicleIntegrateAngular(&vehicle.motion, dt,
                                           vehicleTuning);
            SyncServerVehicleToEcs(vehicle);
            SyncServerVehiclePassengers(vehicle);
            return;
        }
        const int coastZ = vehicle.motion.z;
        const game::VehicleSurface coastSurface = ServerVehicleSurfaceAt(
            vehicle.motion.x, vehicle.motion.y, coastZ);
        game::VehicleTuning coastTuning = vehicleTuning;
        coastTuning.braking =
            std::isfinite(m_vehicleTuning.vehicleCoastBraking)
                ? m_vehicleTuning.vehicleCoastBraking
                : game::kVehicleCoastBraking;
        const auto coastBodyBlocked = [this, &vehicle](
                                          float x, float y, int wz,
                                          float radius) {
            for (const ServerVehicleRuntime& other : m_serverVehicles) {
                if (&other == &vehicle || other.motion.z != wz) {
                    continue;
                }
                if (game::VehicleBodiesOverlap(
                        x, y, radius, other.motion.x, other.motion.y,
                        game::VehicleRadiusForType(other.motion.type))) {
                    return true;
                }
            }
            return false;
        };
        (void)game::VehiclePredictStep(
            &vehicle.motion, 0.0f, 0.0f, dt,
            [this, coastZ](int wx, int wy, int wz) {
                if (wz != coastZ) {
                    return true;
                }
                return !IsServerVehicleCellWalkable(wx, wy, wz);
            },
            coastTuning, coastSurface, coastBodyBlocked, modStats);
        if (game::VehicleSpeed(vehicle.motion) <=
            game::kVehicleCoastStopSpeed) {
            vehicle.motion.vx = 0.0f;
            vehicle.motion.vy = 0.0f;
        }
        SyncServerVehicleToEcs(vehicle);
        SyncServerVehiclePassengers(vehicle);
        return;
    }

    SessionPlayer* session =
        FindSessionByPlayerStableId(vehicle.driverStableId);
    const ecs::Entity player =
        m_simulation->Ecs().FindById(vehicle.driverStableId);
    const auto* health =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Health>(player)
            : nullptr;
    if (session == nullptr || player == entt::null || !session->spawned ||
        session->dead || (health != nullptr && health->current <= 0.0f)) {
        if (session != nullptr) {
            ReleaseServerVehicleSeat(*session, true, "驾驶者不可用");
        } else {
            vehicle.driverStableId = 0;
            stopVehicle();
        }
        return;
    }

    if (session->drivingVehicleId != vehicle.stableId) {
        session->drivingVehicleId = vehicle.stableId;
        session->stateDirty = true;
    }
    vehicle.noiseCooldown = std::max(0.0f, vehicle.noiseCooldown - dt);
    vehicle.ramWearCooldown = std::max(0.0f, vehicle.ramWearCooldown - dt);
    vehicle.hornCooldown = std::max(0.0f, vehicle.hornCooldown - dt);
    vehicle.zombieAttackCooldown =
        std::max(0.0f, vehicle.zombieAttackCooldown - dt);
    for (auto it = vehicle.ramCooldowns.begin();
         it != vehicle.ramCooldowns.end();) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = vehicle.ramCooldowns.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = vehicle.vehicleCollisionCooldowns.begin();
         it != vehicle.vehicleCollisionCooldowns.end();) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            it = vehicle.vehicleCollisionCooldowns.erase(it);
        } else {
            ++it;
        }
    }
    const int z = vehicle.motion.z;
    const game::VehicleSurface surface = ServerVehicleSurfaceAt(
        vehicle.motion.x, vehicle.motion.y, z);
    if (surface != vehicle.surface) {
        const game::VehicleSurfaceProfile profile =
            game::VehicleSurfaceProfileFor(surface, vehicleTuning);
        ZL_INFO("vehicle",
                "服务器载具 {} 路面：{}（极速 x{:.2f}，加速 x{:.2f}，制动 "
                "x{:.2f}）",
                vehicle.stableId, game::VehicleSurfaceLabel(surface),
                profile.maxSpeedScale, profile.accelerationScale,
                profile.brakingScale);
        vehicle.surface = surface;
    }
    const game::VehicleMotion previousMotion = vehicle.motion;
    std::vector<std::uint64_t> vehicleCollisionTargets;
    const auto bodyBlocked = [this, &vehicle,
                              &vehicleCollisionTargets](float x, float y,
                                                        int wz, float radius) {
        for (const ServerVehicleRuntime& other : m_serverVehicles) {
            if (other.stableId == 0 ||
                other.stableId == vehicle.stableId ||
                other.motion.z != wz) {
                continue;
            }
            if (!game::VehicleBodiesOverlap(
                    x, y, radius, other.motion.x, other.motion.y,
                    game::VehicleRadiusForType(other.motion.type))) {
                continue;
            }
            vehicleCollisionTargets.push_back(other.stableId);
            return true;
        }
        return false;
    };
    const game::VehicleStepResult result = game::VehicleStep(
        &vehicle.motion, vehicle.inputX, vehicle.inputY, dt,
        [this, z](int wx, int wy, int wz) {
            if (wz != z) {
                return true;
            }
            return !IsServerVehicleCellWalkable(wx, wy, wz);
        },
        vehicleTuning, surface, bodyBlocked, modStats);
    std::sort(vehicleCollisionTargets.begin(), vehicleCollisionTargets.end());
    vehicleCollisionTargets.erase(
        std::unique(vehicleCollisionTargets.begin(),
                    vehicleCollisionTargets.end()),
        vehicleCollisionTargets.end());
    if ((result.bodyCollision || result.bodyContact) &&
        !vehicleCollisionTargets.empty()) {
        ApplyServerVehicleVehicleCollisions(vehicle, vehicleCollisionTargets,
                                            previousMotion, modStats);
    }
    const bool vehicleRammed =
        ApplyServerVehicleRam(vehicle, &previousMotion, modStats);
    ApplyServerVehicleAttacks(vehicle, *session, modStats);
    EmitServerVehicleNoise(vehicle, *session);
    SyncServerVehicleToEcs(vehicle);
    SyncVehicleDriver(vehicle, player);
    SyncServerVehiclePassengers(vehicle);

    if (result.damaged) {
        session->stateDirty = true;
        SetSessionStatus(
            *session,
            "碰撞：车况 " +
                std::to_string(static_cast<int>(std::lround(
                    std::clamp(vehicle.motion.condition, 0.0f, 1.0f) * 100.0f))) +
                "%",
            2);
        ZL_INFO("vehicle",
                "服务器碰撞：stableId={} 速度 {:.1f}，扣车况 {:.1f}%，剩余 "
                "{:.0f}%",
                vehicle.stableId, result.impactSpeed, result.damage * 100.0f,
                vehicle.motion.condition * 100.0f);
    } else if (result.collided) {
        ZL_DEBUG("vehicle", "服务器载具碰撞：stableId={}，速度 {:.1f}",
                 vehicle.stableId, result.impactSpeed);
    } else if (vehicleRammed) {
        session->stateDirty = true;
        SetSessionStatus(
            *session,
            "撞击：车况 " +
                std::to_string(static_cast<int>(std::lround(
                    std::clamp(vehicle.motion.condition, 0.0f, 1.0f) * 100.0f))) +
                "%",
            2);
    }

    if (vehicle.motion.fuel <= 0.0f && !vehicle.fuelWarned) {
        vehicle.fuelWarned = true;
        session->stateDirty = true;
        SetSessionStatus(*session, "燃料耗尽", 2);
        ZL_INFO("vehicle", "服务器载具燃料耗尽：stableId={}",
                vehicle.stableId);
    }
}

bool ServerHost::EnterServerVehicle(SessionPlayer& session,
                                    const net::Command& command,
                                    std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target =
        FindServerVehicle(command.payload.entityId);
    if (target == nullptr || target->entity == entt::null ||
        !m_simulation->Ecs().Alive(target->entity)) {
        ++m_networkStats.rangeRejected;
        return fail("上车目标不是服务器载具");
    }
    ServerVehicleRuntime& vehicle = *target;
    if (session.drivingVehicleId != 0) {
        return fail("玩家已在载具中");
    }
    if (!game::VehicleSeatAvailable(vehicle.driverStableId)) {
        return fail("驾驶位已被占用");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* playerPosition =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Position>(player)
            : nullptr;
    if (playerPosition == nullptr) {
        return fail("玩家缺少位置");
    }
    if (playerPosition->z != vehicle.motion.z) {
        return fail("玩家与载具不在同一层");
    }
    if (!game::VehicleCanEnter(vehicle.motion, playerPosition->x,
                               playerPosition->y,
                               m_vehicleTuning.enterRange)) {
        ++m_networkStats.rangeRejected;
        return fail("距离载具太远");
    }
    ReleaseServerForageReservation(session);
    ReleaseServerAnimalHarvest(session);
    session.survival = {};
    session.animalHarvest = {};
    vehicle.driverStableId = session.playerStableId;
    session.drivingVehicleId = vehicle.stableId;
    vehicle.inputX = 0.0f;
    vehicle.inputY = 0.0f;
    vehicle.motion.vx = 0.0f;
    vehicle.motion.vy = 0.0f;
    vehicle.noiseCooldown = 0.0f;
    vehicle.ramWearCooldown = 0.0f;
    vehicle.hornCooldown = 0.0f;
    vehicle.zombieAttackCooldown = 0.0f;
    vehicle.ramCooldowns.clear();
    vehicle.fuelWarned = vehicle.motion.fuel <= 0.0f;
    SyncServerVehicleToEcs(vehicle);
    SyncVehicleDriver(vehicle, player);
    session.stateDirty = true;
    SetSessionStatus(
        session,
        "已上车：燃料 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(vehicle.motion.fuel, 0.0f, 1.0f) * 100.0f))) +
            "% · 车况 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(vehicle.motion.condition, 0.0f, 1.0f) *
                100.0f))) +
            "% · 防护 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(m_vehicleTuning.driverDamageReduction, 0.0f, 1.0f) *
                100.0f))) +
            "%",
        1);
    ZL_INFO("vehicle",
            "服务器上车：playerStableId={} 载具 stableId={} 燃料 {:.0f}% "
            "车况 {:.0f}% 驾驶员防护 {:.0f}%",
            session.playerStableId, vehicle.stableId,
            vehicle.motion.fuel * 100.0f,
            vehicle.motion.condition * 100.0f,
            m_vehicleTuning.driverDamageReduction * 100.0f);
    return true;
}

bool ServerHost::EnterServerVehiclePassenger(SessionPlayer& session,
                                              const net::Command& command,
                                              std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target =
        FindServerVehicle(command.payload.entityId);
    if (target == nullptr || target->entity == entt::null ||
        !m_simulation->Ecs().Alive(target->entity)) {
        ++m_networkStats.rangeRejected;
        return fail("上车目标不是服务器载具");
    }
    ServerVehicleRuntime& vehicle = *target;
    if (session.drivingVehicleId != 0) {
        return fail("玩家已在载具中");
    }
    if (!game::VehiclePassengerSeatAvailable(
            vehicle.passengerStableId, vehicle.secondPassengerStableId)) {
        return fail("乘客位已满");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* playerPosition =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Position>(player)
            : nullptr;
    if (playerPosition == nullptr) {
        return fail("玩家缺少位置");
    }
    if (playerPosition->z != vehicle.motion.z) {
        return fail("玩家与载具不在同一层");
    }
    if (!game::VehicleCanEnter(vehicle.motion, playerPosition->x,
                               playerPosition->y,
                               m_vehicleTuning.enterRange)) {
        ++m_networkStats.rangeRejected;
        return fail("距离载具太远");
    }
    std::uint64_t* passengerSeat =
        FindFreeServerVehiclePassengerSeat(vehicle);
    if (passengerSeat == nullptr) {
        return fail("乘客位已满");
    }
    ReleaseServerForageReservation(session);
    ReleaseServerAnimalHarvest(session);
    session.survival = {};
    session.animalHarvest = {};
    *passengerSeat = session.playerStableId;
    session.drivingVehicleId = vehicle.stableId;
    SyncServerVehicleToEcs(vehicle);
    SyncVehiclePassenger(vehicle, player);
    session.stateDirty = true;
    SetSessionStatus(
        session,
        "已上乘客位：燃料 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(vehicle.motion.fuel, 0.0f, 1.0f) * 100.0f))) +
            "% · 车况 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(vehicle.motion.condition, 0.0f, 1.0f) *
                100.0f))) +
            "% · Enter 下车",
        1);
    ZL_INFO("vehicle",
            "服务器上乘客位：playerStableId={} 载具 stableId={} 驾驶员 {}",
            session.playerStableId, vehicle.stableId,
            vehicle.driverStableId);
    return true;
}

bool ServerHost::TakeServerVehicleDriverSeat(SessionPlayer& session,
                                             std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target = FindServerVehicleForSession(session);
    if (target == nullptr ||
        !IsServerVehiclePassengerStableId(*target, session.playerStableId)) {
        return fail("当前不在乘客位");
    }
    ServerVehicleRuntime& vehicle = *target;
    if (!game::VehicleCanTakeDriverSeat(
            vehicle.driverStableId, vehicle.passengerStableId,
            vehicle.secondPassengerStableId, session.playerStableId)) {
        return fail("驾驶位已被占用");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* health =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Health>(player)
            : nullptr;
    if (player == entt::null || !m_simulation->Ecs().Alive(player) ||
        (health != nullptr && health->current <= 0.0f)) {
        return fail("乘客不可用");
    }
    RemoveServerVehiclePassengerStableId(vehicle, session.playerStableId);
    vehicle.driverStableId = session.playerStableId;
    vehicle.motion.vx = 0.0f;
    vehicle.motion.vy = 0.0f;
    vehicle.inputX = 0.0f;
    vehicle.inputY = 0.0f;
    SyncServerVehicleToEcs(vehicle);
    SyncVehicleDriver(vehicle, player);
    session.drivingVehicleId = vehicle.stableId;
    session.stateDirty = true;
    SetSessionStatus(session, "已接管驾驶位：WASD 驾驶 · Enter 下车", 1);
    ZL_INFO("vehicle",
            "服务器接管驾驶位：playerStableId={} 载具 stableId={} 燃料 "
            "{:.0f}% 车况 {:.0f}%",
            session.playerStableId, vehicle.stableId,
            vehicle.motion.fuel * 100.0f,
            vehicle.motion.condition * 100.0f);
    return true;
}

bool ServerHost::GiveServerVehicleDriverSeat(SessionPlayer& session,
                                             std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target = FindServerVehicleForSession(session);
    if (target == nullptr ||
        target->driverStableId != session.playerStableId) {
        return fail("当前不在驾驶位");
    }
    ServerVehicleRuntime& vehicle = *target;
    const std::uint64_t newDriver =
        vehicle.passengerStableId != 0
            ? vehicle.passengerStableId
            : vehicle.secondPassengerStableId;
    if (newDriver == 0) {
        return fail("当前没有乘客");
    }
    const ecs::Entity passenger =
        m_simulation->Ecs().FindById(newDriver);
    const auto* health =
        passenger != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Health>(passenger)
            : nullptr;
    if (passenger == entt::null || !m_simulation->Ecs().Alive(passenger) ||
        (health != nullptr && health->current <= 0.0f)) {
        return fail("乘客不可用");
    }
    const float speed = game::VehicleSpeed(vehicle.motion);
    if (!game::VehicleCanGiveDriverSeatWithSecondPassenger(
            vehicle.driverStableId,
            vehicle.passengerStableId,
            vehicle.secondPassengerStableId, session.playerStableId,
            speed, m_vehicleTuning.seatHandoffMaxSpeed)) {
        if (std::isfinite(speed) &&
            speed > m_vehicleTuning.seatHandoffMaxSpeed) {
            return fail("车辆未停稳，无法让座");
        }
        return fail("无法让座");
    }
    const std::uint64_t previousDriver = vehicle.driverStableId;
    const ecs::Entity previousDriverEntity =
        m_simulation->Ecs().FindById(previousDriver);
    RemoveServerVehiclePassengerStableId(vehicle, newDriver);
    vehicle.driverStableId = newDriver;
    if (std::uint64_t* seat = FindFreeServerVehiclePassengerSeat(vehicle);
        seat != nullptr) {
        *seat = previousDriver;
    }
    vehicle.motion.vx = 0.0f;
    vehicle.motion.vy = 0.0f;
    vehicle.inputX = 0.0f;
    vehicle.inputY = 0.0f;
    SyncServerVehicleToEcs(vehicle);
    SyncVehicleDriver(vehicle, passenger);
    if (previousDriverEntity != entt::null &&
        m_simulation->Ecs().Alive(previousDriverEntity)) {
        SyncVehiclePassenger(vehicle, previousDriverEntity);
    }
    session.stateDirty = true;
    SetSessionStatus(session, "已让出驾驶位：Enter 下车", 1);
    if (SessionPlayer* passengerSession =
            FindSessionByPlayerStableId(newDriver);
        passengerSession != nullptr) {
        passengerSession->drivingVehicleId = vehicle.stableId;
        passengerSession->stateDirty = true;
        SetSessionStatus(*passengerSession,
                         "已接管驾驶位：WASD 驾驶 · Enter 下车", 1);
    }
    ZL_INFO("vehicle",
            "服务器让座：载具 stableId={} 驾驶员 {} -> {}，车辆已停",
            vehicle.stableId, previousDriver, newDriver);
    return true;
}

bool ServerHost::ExitServerVehicle(SessionPlayer& session,
                                   std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("当前不在载具中");
    }
    ServerVehicleRuntime* target = FindServerVehicleForSession(session);
    if (target == nullptr) {
        return fail("当前不在载具中");
    }
    ServerVehicleRuntime& vehicle = *target;
    const bool isDriver =
        vehicle.driverStableId == session.playerStableId;
    const bool isPassenger =
        IsServerVehiclePassengerStableId(vehicle, session.playerStableId);
    if (!isDriver && !isPassenger) {
        return fail("当前不在载具中");
    }
    float exitX = 0.0f;
    float exitY = 0.0f;
    if (!FindServerVehicleExitCell(vehicle, &exitX, &exitY)) {
        return fail("载具附近没有可下车位置");
    }
    ReleaseServerVehicleSeat(session, true,
                             isDriver ? "下车" : "乘客下车");
    SetSessionStatus(session, isDriver ? "已下车" : "已离开乘客位", 1);
    ZL_INFO("vehicle",
            "服务器{}：playerStableId={} 载具 stableId={} 格 ({:.1f}, {:.1f})",
            isDriver ? "下车" : "乘客下车", session.playerStableId,
            vehicle.stableId, exitX, exitY);
    return true;
}

bool ServerHost::ToggleServerVehicleHeadlights(SessionPlayer& session,
                                                std::string* error) {
    auto fail = [&](const std::string& message) {
        SetSessionStatus(session, message, 2);
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target = FindServerVehicleForSession(session);
    if (target == nullptr ||
        target->driverStableId != session.playerStableId) {
        return fail("当前不在驾驶位");
    }
    ServerVehicleRuntime& vehicle = *target;
    vehicle.motion.headlights = !vehicle.motion.headlights;
    SyncServerVehicleToEcs(vehicle);
    session.stateDirty = true;
    const bool on = vehicle.motion.headlights;
    SetSessionStatus(session, on ? "车灯已开启" : "车灯已关闭", 1);
    ZL_INFO("vehicle", "服务器车灯：载具 stableId={} {}",
            vehicle.stableId, on ? "开启" : "关闭");
    return true;
}

bool ServerHost::HonkServerVehicleHorn(SessionPlayer& session,
                                       std::string* error) {
    auto fail = [&](const std::string& message) {
        SetSessionStatus(session, message, 2);
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_events == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target = FindServerVehicleForSession(session);
    if (target == nullptr ||
        target->driverStableId != session.playerStableId) {
        return fail("当前不在驾驶位");
    }
    ServerVehicleRuntime& vehicle = *target;
    if (vehicle.motion.condition <= 0.0f ||
        !std::isfinite(vehicle.motion.condition)) {
        return fail("车辆损坏，无法鸣笛");
    }
    if (!game::VehicleHornReady(vehicle.motion.condition,
                               vehicle.hornCooldown)) {
        return fail("喇叭冷却中");
    }
    vehicle.hornCooldown = m_vehicleTuning.hornCooldownSeconds;

    core::SoundEvent noise;
    noise.x = vehicle.motion.x;
    noise.y = vehicle.motion.y;
    noise.radius = m_vehicleTuning.hornNoiseRadius;
    noise.type = core::SoundType::Vehicle;
    noise.clip = audio::kVehicleHorn;
    m_events->EmitQueued(core::kOnSound, std::move(noise));

    net::WorldSoundEvent remote;
    remote.sourcePlayerStableId = session.playerStableId;
    remote.x = vehicle.motion.x;
    remote.y = vehicle.motion.y;
    remote.z = vehicle.motion.z;
    remote.radius = m_vehicleTuning.hornNoiseRadius;
    remote.type = static_cast<std::uint8_t>(core::SoundType::Vehicle);
    remote.clip = audio::kVehicleHorn;
    BroadcastWorldSound(remote);

    SetSessionStatus(session, "喇叭：鸣笛（吸引 " +
                                  std::to_string(static_cast<int>(std::lround(
                                      m_vehicleTuning.hornNoiseRadius))) +
                                  " 格）",
                     1);
    ZL_INFO("vehicle",
            "服务器载具喇叭：stableId={} 驾驶员 {}，吸引半径 {:.0f} 格",
            vehicle.stableId, session.playerStableId,
            m_vehicleTuning.hornNoiseRadius);
    return true;
}

bool ServerHost::ServiceServerVehicle(SessionPlayer& session,
                                      const net::Command& command,
                                      std::string* error) {
    auto fail = [&](const std::string& message) {
        SetSessionStatus(session, message, 2);
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器载具不存在");
    }
    ServerVehicleRuntime* target =
        FindServerVehicle(command.payload.entityId);
    if (target == nullptr || target->entity == entt::null ||
        !m_simulation->Ecs().Alive(target->entity)) {
        ++m_networkStats.rangeRejected;
        return fail("维修/加油目标不是服务器载具");
    }
    ServerVehicleRuntime& vehicle = *target;
    if (session.drivingVehicleId != 0) {
        return fail("驾驶中无法维修或加油");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* playerPosition =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Position>(player)
            : nullptr;
    auto* bag =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Container>(player)
            : nullptr;
    if (playerPosition == nullptr || bag == nullptr) {
        return fail("玩家背包或位置不可用");
    }
    if (playerPosition->z != vehicle.motion.z) {
        return fail("玩家与载具不在同一层");
    }
    if (!game::VehicleCanEnter(vehicle.motion, playerPosition->x,
                               playerPosition->y,
                               m_vehicleTuning.enterRange)) {
        ++m_networkStats.rangeRejected;
        return fail("距离载具太远");
    }

    const int repairItems = items::CountOf(*bag, m_vehicleRepairItem);
    const int fuelItems = items::CountOf(*bag, m_vehicleFuelItem);
    const game::VehicleServiceAction action = game::PlanVehicleService(
        vehicle.motion, repairItems, fuelItems);
    const auto consumeOne = [bag](const std::string& defId) {
        for (std::size_t i = 0; i < bag->items.size(); ++i) {
            if (bag->items[i].defId != defId) {
                continue;
            }
            ecs::ItemInstance removed;
            return items::TakeAt(*bag, i, 1, &removed);
        }
        return false;
    };
    const auto percent = [](float value) {
        return static_cast<int>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 100.0f));
    };

    if (action == game::VehicleServiceAction::None) {
        return fail("车辆无需维修或加油");
    }
    if (action == game::VehicleServiceAction::MissingRepairKit) {
        return fail("缺少维修包");
    }
    if (action == game::VehicleServiceAction::MissingFuelCan) {
        return fail("缺少燃料罐");
    }
    if (action == game::VehicleServiceAction::Repair) {
        const int before = percent(vehicle.motion.condition);
        if (!consumeOne(m_vehicleRepairItem)) {
            return fail("维修失败：无法消耗维修包");
        }
        (void)game::VehicleAddCondition(&vehicle.motion,
                                        m_vehicleTuning.repairAmount);
        SyncServerVehicleToEcs(vehicle);
        session.stateDirty = true;
        const int after = percent(vehicle.motion.condition);
        SetSessionStatus(session,
                         "已维修：车况 " + std::to_string(after) + "%", 1);
        ZL_INFO("vehicle",
                "服务器维修：载具 stableId={} 车况 {}% → {}%，消耗 {} x1",
                vehicle.stableId, before, after, m_vehicleRepairItem);
        return true;
    }

    const int before = percent(vehicle.motion.fuel);
    if (!consumeOne(m_vehicleFuelItem)) {
        return fail("加油失败：无法消耗燃料罐");
    }
    (void)game::VehicleAddFuel(&vehicle.motion,
                               m_vehicleTuning.fuelPerCan);
    vehicle.fuelWarned = vehicle.motion.fuel <= 0.0f;
    SyncServerVehicleToEcs(vehicle);
    session.stateDirty = true;
    const int after = percent(vehicle.motion.fuel);
    SetSessionStatus(session, "已加油：燃料 " + std::to_string(after) + "%",
                     1);
    ZL_INFO("vehicle",
            "服务器加油：载具 stableId={} 燃料 {}% → {}%，消耗 {} x1",
            vehicle.stableId, before, after, m_vehicleFuelItem);
    return true;
}

bool ServerHost::ModifyServerVehicleMod(SessionPlayer& session,
                                        const net::Command& command,
                                        bool install, std::string* error) {
    auto fail = [&](const std::string& message) {
        SetSessionStatus(session, message, 2);
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_items == nullptr ||
        m_vehicleMods == nullptr || !m_vehicleMods->enabled) {
        return fail("车辆改装未启用");
    }
    if (command.payload.flags != 0) {
        return fail("车辆改装命令字段非法");
    }
    const int slotIndex = command.payload.index;
    if (slotIndex < 0 ||
        slotIndex >= static_cast<int>(ecs::kVehicleModSlotCount)) {
        return fail("车辆改装槽位非法");
    }
    if (session.drivingVehicleId != 0) {
        return fail("驾驶或乘坐载具时无法改装");
    }
    ServerVehicleRuntime* target =
        FindServerVehicle(command.payload.entityId);
    if (target == nullptr || target->entity == entt::null ||
        !m_simulation->Ecs().Alive(target->entity)) {
        ++m_networkStats.rangeRejected;
        return fail("改装目标不是服务器载具");
    }
    if (target->driverStableId != 0 || target->passengerStableId != 0 ||
        target->secondPassengerStableId != 0) {
        return fail("载具正在使用，无法改装");
    }
    if (game::VehicleSpeed(target->motion) > 0.1f) {
        return fail("载具未停稳，无法改装");
    }

    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    const auto* playerPosition =
        player != entt::null ? registry.try_get<ecs::Position>(player)
                             : nullptr;
    auto* bag = player != entt::null
                    ? registry.try_get<ecs::Container>(player)
                    : nullptr;
    if (player == entt::null || !m_simulation->Ecs().Alive(player) ||
        playerPosition == nullptr || bag == nullptr) {
        return fail("玩家背包或位置不可用");
    }
    if (playerPosition->z != target->motion.z) {
        return fail("玩家与载具不在同一层");
    }
    if (!game::VehicleCanEnter(target->motion, playerPosition->x,
                               playerPosition->y,
                               m_vehicleTuning.enterRange + 0.75f)) {
        ++m_networkStats.rangeRejected;
        return fail("距离载具太远");
    }

    auto* mods = registry.try_get<ecs::VehicleMods>(target->entity);
    const game::VehicleModSlot slot =
        static_cast<game::VehicleModSlot>(slotIndex);
    if (install) {
        if (command.payload.targetIndex < 0) {
            return fail("改装候选索引非法");
        }
        const auto candidates =
            game::VehicleModCandidatesForSlot(*m_vehicleMods, slot);
        if (command.payload.targetIndex >=
            static_cast<std::int32_t>(candidates.size())) {
            return fail("改装候选索引非法");
        }
        const game::VehicleModDef* def =
            candidates[static_cast<std::size_t>(
                command.payload.targetIndex)];
        if (def == nullptr) {
            return fail("改装定义不可用");
        }
        ecs::VehicleMods empty;
        const ecs::VehicleMods& current =
            mods != nullptr ? *mods : empty;
        std::string validationError;
        if (!game::CanInstallVehicleMod(*m_vehicleMods, current, def->id,
                                        &validationError)) {
            return fail(validationError);
        }
        std::string materialSummary;
        if (!game::VehicleModMaterialCountsMet(*bag, *m_items, *def,
                                               &materialSummary)) {
            return fail("材料不足：" + materialSummary);
        }
        if (!game::ConsumeVehicleModMaterials(*bag, *m_items, *def,
                                              &validationError)) {
            return fail("材料扣除失败：" + validationError);
        }
        if (mods == nullptr) {
            mods = &registry.emplace<ecs::VehicleMods>(target->entity);
        }
        if (!game::InstallVehicleMod(*m_vehicleMods, mods, def->id,
                                     &validationError)) {
            for (const game::VehicleModMaterial& material :
                 def->materials) {
                (void)ReturnServerItem(
                    player,
                    ecs::ItemInstance{material.item, material.count, 1.0f},
                    playerPosition->x, playerPosition->y,
                    playerPosition->z);
            }
            return fail("安装失败：" + validationError);
        }
        SyncServerVehicleModStorage(*target);
        session.stateDirty = true;
        SetSessionStatus(session, "已安装车辆改装：" + def->name, 1);
        ZL_INFO("vehicle",
                "服务器安装车辆改装：playerStableId={}，载具 stableId={}，"
                "槽位 {}，定义 {}",
                session.playerStableId, target->stableId,
                game::VehicleModSlotName(slot), def->id);
        return true;
    }

    if (command.payload.targetIndex != -1) {
        return fail("拆除改装命令字段非法");
    }
    if (mods == nullptr) {
        return fail("槽位没有可拆除的改装");
    }
    const std::string currentId =
        game::InstalledVehicleModId(*mods, slot);
    if (currentId.empty()) {
        return fail("槽位没有可拆除的改装");
    }
    const game::VehicleModDef* def =
        m_vehicleMods->Find(currentId);
    const std::string removedId = game::RemoveVehicleMod(mods, slot);
    if (removedId.empty()) {
        return fail("槽位没有可拆除的改装");
    }

    std::string refundText;
    bool refundDropped = false;
    if (def != nullptr) {
        for (const game::VehicleModMaterial& material :
             game::VehicleModRefundMaterials(
                 *def, m_vehicleMods->removeRefundRatio)) {
            if (!refundText.empty()) {
                refundText += "、";
            }
            const items::ItemDef* itemDef = m_items->Find(material.item);
            refundText +=
                (itemDef != nullptr ? itemDef->name : material.item) +
                " x" + std::to_string(material.count);
            if (ReturnServerItem(
                    player,
                    ecs::ItemInstance{material.item, material.count, 1.0f},
                    playerPosition->x, playerPosition->y,
                    playerPosition->z)) {
                refundDropped = true;
            }
        }
    }
    SyncServerVehicleModStorage(*target);
    session.stateDirty = true;
    std::string message =
        "已拆除车辆改装：" +
        (def != nullptr ? def->name : removedId);
    if (!refundText.empty()) {
        message += "（返还 " + refundText;
        if (refundDropped) {
            message += "，部分掉地";
        }
        message += "）";
    }
    SetSessionStatus(session, message, 1);
    ZL_INFO("vehicle",
            "服务器拆除车辆改装：playerStableId={}，载具 stableId={}，"
            "槽位 {}，定义 {}，返还 {}",
            session.playerStableId, target->stableId,
            game::VehicleModSlotName(slot), removedId,
            refundText.empty() ? "无" : refundText);
    return true;
}

void ServerHost::ReleaseServerVehicleSeat(SessionPlayer& session,
                                          bool placeAtExit,
                                          std::string_view reason) {
    if (m_simulation == nullptr || session.playerStableId == 0) {
        session.drivingVehicleId = 0;
        return;
    }
    ServerVehicleRuntime* target =
        FindServerVehicle(session.drivingVehicleId);
    if (target == nullptr) {
        for (ServerVehicleRuntime& candidate : m_serverVehicles) {
            if (candidate.driverStableId == session.playerStableId ||
                IsServerVehiclePassengerStableId(candidate,
                                                 session.playerStableId)) {
                target = &candidate;
                break;
            }
        }
    }
    session.drivingVehicleId = 0;
    const ecs::Entity rider =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (rider != entt::null && m_simulation->Ecs().Alive(rider)) {
        m_simulation->Ecs().Registry().remove<ecs::VehicleOccupant>(rider);
    }
    session.stateDirty = true;
    if (target == nullptr || target->entity == entt::null ||
        !m_simulation->Ecs().Alive(target->entity)) {
        return;
    }
    ServerVehicleRuntime& vehicle = *target;
    const bool wasDriver =
        vehicle.driverStableId == session.playerStableId;
    const bool wasPassenger =
        IsServerVehiclePassengerStableId(vehicle, session.playerStableId);
    if (!wasDriver && !wasPassenger) {
        return;
    }
    if (wasDriver) {
        vehicle.driverStableId = 0;
        vehicle.motion.vx = 0.0f;
        vehicle.motion.vy = 0.0f;
        vehicle.inputX = 0.0f;
        vehicle.inputY = 0.0f;
        SyncServerVehicleToEcs(vehicle);
        for (const std::uint64_t passengerId :
             {vehicle.passengerStableId, vehicle.secondPassengerStableId}) {
            if (passengerId == 0) {
                continue;
            }
            const ecs::Entity passenger =
                m_simulation->Ecs().FindById(passengerId);
            if (passenger != entt::null &&
                m_simulation->Ecs().Alive(passenger)) {
                SyncVehiclePassenger(vehicle, passenger);
            }
            if (SessionPlayer* passengerSession =
                    FindSessionByPlayerStableId(passengerId);
                passengerSession != nullptr) {
                SetSessionStatus(*passengerSession, "驾驶员已离开，车辆已停", 1);
            }
        }
        if (placeAtExit) {
            const ecs::Entity player =
                m_simulation->Ecs().FindById(session.playerStableId);
            float exitX = 0.0f;
            float exitY = 0.0f;
            if (player != entt::null &&
                FindServerVehicleExitCell(vehicle, &exitX, &exitY)) {
                PlaceServerPlayer(player, exitX, exitY, vehicle.motion.z);
            }
        }
        ZL_INFO("vehicle",
                "服务器座位释放：playerStableId={} 载具 stableId={}（{}，驾驶位）",
                session.playerStableId, vehicle.stableId, reason);
        return;
    }
    RemoveServerVehiclePassengerStableId(vehicle, session.playerStableId);
    SyncServerVehicleToEcs(vehicle);
    if (placeAtExit) {
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        float exitX = 0.0f;
        float exitY = 0.0f;
        if (player != entt::null &&
            FindServerVehicleExitCell(vehicle, &exitX, &exitY)) {
            PlaceServerPlayer(player, exitX, exitY, vehicle.motion.z);
        }
    }
    ZL_INFO("vehicle",
            "服务器座位释放：playerStableId={} 载具 stableId={}（{}，乘客位）",
            session.playerStableId, vehicle.stableId, reason);
}

void ServerHost::StandbyOwnedCompanion(std::uint64_t ownerStableId,
                                       const char* reason) {
    if (m_simulation == nullptr || ownerStableId == 0) {
        return;
    }
    ecs::EcsWorld& world = m_simulation->Ecs();
    const ecs::Entity companion =
        game::FindOwnedCompanion(world, ownerStableId);
    if (companion == entt::null || !world.Alive(companion)) {
        return;
    }
    entt::registry& registry = world.Registry();
    const auto* identity = registry.try_get<game::Companion>(companion);
    auto* state = registry.try_get<game::CompanionState>(companion);
    if (identity == nullptr || state == nullptr || state->dead ||
        state->standby) {
        return;
    }
    const game::SettlementDef* home =
        m_settlements != nullptr
            ? m_settlements->Find(identity->homeSettlementId)
            : nullptr;
    bool teleported = false;
    if (!state->downed && home != nullptr) {
        teleported = m_simulation->TeleportCompanion(
            companion, home->centerX, home->centerY);
    }
    state->standby = true;
    state->command = game::CompanionCommand::Stay;
    if (!teleported) {
        const auto* position = registry.try_get<ecs::Position>(companion);
        if (auto* intent = registry.try_get<ecs::MoveIntent>(companion);
            intent != nullptr) {
            intent->x = 0.0f;
            intent->y = 0.0f;
        }
        if (auto* request = registry.try_get<ecs::PathRequest>(companion);
            request != nullptr) {
            request->newRequest = false;
            request->queryId = 0;
            request->state = ecs::PathStatus::None;
        }
        if (auto* follow = registry.try_get<ecs::PathFollow>(companion);
            follow != nullptr) {
            follow->waypoints.clear();
            follow->index = 0;
            follow->status = ecs::PathStatus::None;
            if (position != nullptr) {
                follow->anchorX = position->x;
                follow->anchorY = position->y;
            }
        }
        if (auto* ai = registry.try_get<game::CompanionAiState>(companion);
            ai != nullptr) {
            ai->targetId = 0;
            ai->engaged = false;
            ai->autoRetreat = false;
            ai->frozen = false;
            ai->hasAnchor = true;
            ai->repathTimer = 0.0f;
            if (position != nullptr) {
                ai->anchorX = position->x;
                ai->anchorY = position->y;
            }
        }
    }
    ZL_INFO("companions", "同伴断线待命：{}，owner={}，{}，位置{}",
            identity->defId, ownerStableId,
            reason != nullptr ? reason : "manual",
            teleported ? "已回聚落" : "原地");
}

bool ServerHost::SaveWorld(const char* reason) {
    if (m_simulation == nullptr || m_saveManager == nullptr ||
        m_saveComponents == nullptr || m_activeSaveId.empty()) {
        return false;
    }
    SettleServerThrows(reason != nullptr ? reason : "save");
    if (m_activeSaveUseBackup) {
        ZL_WARN("save", "服务器当前从备份读档，跳过世界回写（{}）",
                reason != nullptr ? reason : "manual");
        return false;
    }
    for (ServerVehicleRuntime& vehicle : m_serverVehicles) {
        SyncServerVehicleToEcs(vehicle);
    }
    StoreServerForageState();
    StoreServerSettlementState();
    if (m_simulation->Animals() != nullptr) {
        m_simulation->Animals()->StoreState(m_simulation->Ecs());
    }
    m_simulation->Flush();

    save::EntitySnapshot entities;
    save::EntityBucketSet entityBuckets;
    std::string error;
    world::RegionStreaming* regions = m_simulation->Regions();
    const world::WorldDefinition* definition =
        regions != nullptr ? &regions->Definition() : nullptr;
    const auto withoutPlayers = [this](ecs::Entity entity) {
        const entt::registry& registry = m_simulation->Ecs().Registry();
        const bool temporaryNpc =
            registry.all_of<game::SettlementNpc>(entity) &&
            !registry.all_of<ecs::MerchantStock>(entity);
        const auto* health = registry.try_get<ecs::Health>(entity);
        const bool invalidHealth =
            health != nullptr &&
            (!std::isfinite(health->current) ||
             !std::isfinite(health->max) || health->max <= 0.0f ||
             health->current < 0.0f || health->current > health->max);
        return !registry.all_of<ecs::PlayerTag>(entity) &&
               !registry.any_of<ecs::Projectile, ecs::TransientEffect>(entity) &&
               !temporaryNpc && !invalidHealth &&
               !registry.all_of<game::SettlementRaidZombie>(entity);
    };
    const bool captured =
        definition != nullptr
            ? save::CaptureRegionEntityBuckets(
                  m_simulation->Ecs(), *m_saveComponents, *definition,
                  &entityBuckets, withoutPlayers, &error)
            : save::CaptureEntities(m_simulation->Ecs(), *m_saveComponents,
                                    &entities, withoutPlayers, &error);
    if (!captured) {
        ZL_WARN("save", "服务器世界实体快照失败（{}）：{}",
                reason != nullptr ? reason : "manual", error);
        return false;
    }
    if (definition != nullptr) {
        entities = std::move(entityBuckets.global);
    }

    std::vector<save::ChunkDiff> chunks;
    bool chunksOk = true;
    m_simulation->Chunks().ForEachLoadedChunk(
        [&](const world::Chunk& chunk) {
            if (!chunksOk) {
                return;
            }
            save::ChunkDiff diff;
            if (!save::CaptureChunkDiff(chunk, &diff, &error)) {
                chunksOk = false;
                return;
            }
            chunks.push_back(std::move(diff));
        });
    if (!chunksOk) {
        ZL_WARN("save", "服务器世界 chunk 快照失败（{}）：{}",
                reason != nullptr ? reason : "manual", error);
        return false;
    }

    save::SaveManager::SaveOptions options;
    options.entities = &entities;
    options.chunks = &chunks;
    save::WorldManifest manifest;
    if (definition != nullptr && regions != nullptr) {
        manifest.version = save::kWorldManifestVersion;
        manifest.identity = world::MakeWorldIdentity(*definition);
        manifest.worldSaveVersion = save::kWorldSaveVersion;
        manifest.regions.reserve(regions->Regions().size());
        for (const world::RegionRuntime& region : regions->Regions()) {
            manifest.regions.push_back(
                save::ManifestRegion{region.id,
                                     static_cast<std::uint8_t>(region.phase)});
        }
        if (game::RegionEventDirector* events = m_simulation->RegionEvents();
            events != nullptr) {
            manifest.events.reserve(events->Events().size());
            for (const game::RegionEvent& event : events->Events()) {
                manifest.events.push_back(save::ManifestRegionEvent{
                    event.id,
                    event.regionId,
                    static_cast<std::uint8_t>(event.kind),
                    static_cast<std::uint8_t>(event.status),
                    event.day,
                    event.x,
                    event.y,
                    event.remainingSeconds,
                    event.targetCount,
                    event.spawnedCount,
                    event.contentSpawned});
            }
        }
        if (game::StrategicState* strategic = m_simulation->Strategic();
            strategic != nullptr) {
            strategic->contributions.clear();
            for (const auto& [stableId, regions] : m_strategicContributions) {
                for (const auto& [regionId, points] : regions) {
                    if (points > 0) {
                        strategic->contributions.push_back(
                            game::StrategicContributionRecord{
                                stableId, regionId, points});
                    }
                }
            }
        }
        if (!m_simulation->CaptureStrategicToManifest(&manifest, &error)) {
            ZL_WARN("save", "服务器战略状态快照失败（{}）：{}",
                    reason != nullptr ? reason : "manual", error);
            return false;
        }
        if (!m_simulation->CaptureWorldEventsToManifest(&manifest, &error)) {
            ZL_WARN("save", "服务器世界事件快照失败（{}）：{}",
                    reason != nullptr ? reason : "manual", error);
            return false;
        }
        options.manifest = &manifest;
        options.buckets = &entityBuckets.buckets;
    }
    if (m_gameTime != nullptr) {
        options.gameMinutes = m_gameTime->TotalGameMinutes();
    }
    if (!m_saveManager->Save(m_activeSaveId, options, &error)) {
        ZL_WARN("save", "服务器世界保存失败（{}，{}）：{}",
                reason != nullptr ? reason : "manual", m_activeSaveId, error);
        return false;
    }
    ZL_INFO("save",
            "服务器世界已保存（{}）：{}，实体 {}，chunk {}，桶 {}，事件 {}",
            reason != nullptr ? reason : "manual", m_activeSaveId,
            entities.entities.size(), chunks.size(), entityBuckets.buckets.size(),
            manifest.events.size());
    return true;
}

void ServerHost::UpdatePlayerLifecycle() {
    if (!m_profilesEnabled || m_simulation == nullptr) {
        return;
    }
    bool changed = false;
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.playerStableId == 0 || session.dead || !session.spawned) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        bool died = false;
        if (player == entt::null) {
            died = true;
        } else if (const auto* health =
                       m_simulation->Ecs().Registry().try_get<ecs::Health>(player);
                   health != nullptr && health->current <= 0.0f) {
            died = true;
        }
        if (!died) {
            continue;
        }

        ReleaseServerVehicleSeat(session, true, "玩家死亡");
        ReleaseServerForageReservation(session);
        session.survival = {};
        session.dead = true;
        session.respawnAtTick = m_ticks + m_respawnDelayTicks;
        session.stateDirty = true;
        ++m_networkStats.playerDeaths;
        CaptureSessionProfile(session, true);
        StandbyOwnedCompanion(session.playerStableId, "玩家死亡");
        const double seconds = static_cast<double>(m_respawnDelayTicks) /
                               std::max(1.0f, m_options.tickRate);
        SetSessionStatus(session,
                         "你已死亡，" + std::to_string(static_cast<int>(
                                             std::ceil(seconds))) +
                             " 秒后复活",
                         2);
        changed = true;
        ZL_INFO("server", "玩家死亡：identity={}，stableId={}，{} 秒后复活",
                session.identity, session.playerStableId, seconds);
    }
    if (changed) {
        FailServerPoiOnWipe();
        SavePlayerProfiles("death");
    }
}

bool ServerHost::RespawnSessionPlayer(SessionPlayer& session) {
    PlayerProfile* profile = FindSessionProfile(session);
    if (profile == nullptr || m_simulation == nullptr) {
        return false;
    }
    std::string respawnError;
    ecs::Entity player = entt::null;
    if (profile->hasSnapshot) {
        player = m_simulation->RespawnNetworkPlayer(
            profile->entity, m_options.playerTargetX, m_options.playerTargetY, 24, true,
            &respawnError);
    } else {
        player = m_simulation->SpawnNetworkPlayer(
            m_options.playerTargetX, m_options.playerTargetY, 24, profile->stableId);
    }
    if (player == entt::null) {
        ZL_WARN("server", "玩家复活暂不可用：identity={}，原因={}", session.identity,
                respawnError);
        return false;
    }

    session.playerStableId = m_simulation->Ecs().IdOf(player).id;
    session.spawned = true;
    session.dead = false;
    session.respawnAtTick = 0;
    session.drivingVehicleId = 0;
    ResetServerStealth(session, player, "复活");
    session.needsFullSnapshot = true;
    session.stateDirty = true;
    session.lastStateTick = 0;
    session.attackUsed = false;
    session.projectileUsed = false;
    session.throwUsed = false;
    session.lastThrowTick = 0;
    session.rangedWeapon = {};
    session.rangedShotSerial = 0;
    ReleaseServerForageReservation(session);
    session.survival = {};
    session.nearbyForage.clear();
    session.nearbyFishing.clear();
    session.survivalScanned = false;
    session.lastProgressionMinute =
        m_gameTime != nullptr ? m_gameTime->TotalGameMinutes() : 0.0;
    MergeSettlementReputationSummary(
        m_simulation->Ecs(), player, profile->state.settlementReputation);
    ApplyProgressionRuntime(player, &session);
    EnsureServerVehicles();
    PlacePlayerOutsideServerVehicle(player);
    if (session.producer != nullptr) {
        session.producer->Reset();
    } else if (m_replicationRegistry != nullptr) {
        game::EntityReplicationProducer::Options producerOptions;
        producerOptions.aoiRadius = static_cast<float>(
            m_config->Get<double>("net.aoiRadius", 48.0));
        producerOptions.maxEntities = static_cast<std::size_t>(
            std::max(1, m_config->Get<int>("net.maxEntities", 256)));
        producerOptions.viewerStableId = session.playerStableId;
        session.producer = std::make_unique<game::EntityReplicationProducer>(
            m_simulation->Ecs(), *m_replicationRegistry, producerOptions);
    }
    profile->alive = true;
    profile->respawnSeconds = 0.0f;
    ++m_networkStats.playerRespawns;
    SetSessionStatus(session, "已复活，生命已恢复", 1);
    CaptureSessionProfile(session, false);
    SavePlayerProfiles("respawn");
    ZL_INFO("server", "玩家复活：identity={}，stableId={}", session.identity,
            session.playerStableId);
    return true;
}

void ServerHost::EmitGunshot(std::uint64_t playerStableId, float noiseRadius) {
    if (m_events == nullptr || m_simulation == nullptr) {
        return;
    }
    const ecs::Entity player = m_simulation->Ecs().FindById(playerStableId);
    const auto* position =
        player != entt::null
            ? m_simulation->Ecs().Registry().try_get<ecs::Position>(player)
            : nullptr;
    if (position == nullptr) {
        return;
    }
    core::SoundEvent shot;
    shot.x = position->x;
    shot.y = position->y;
    shot.radius = noiseRadius >= 0.0f ? noiseRadius : m_gunshotRadius;
    shot.type = core::SoundType::Gunshot;

    net::WorldSoundEvent remote;
    remote.sourcePlayerStableId = playerStableId;
    remote.x = shot.x;
    remote.y = shot.y;
    remote.z = position->z;
    remote.radius = shot.radius;
    remote.type = static_cast<std::uint8_t>(core::SoundType::Gunshot);
    remote.clip = "gunshot";
    BroadcastWorldSound(remote);

    m_events->EmitQueued(core::kOnSound, std::move(shot));
    ++m_networkStats.gunshots;
}

void ServerHost::BroadcastWorldSound(const net::WorldSoundEvent& sound) {
    if (!m_networkEnabled || m_network == nullptr || m_sessions.empty()) {
        return;
    }
    std::string error;
    const std::vector<std::uint8_t> bytes =
        net::EncodeWorldSound(sound, &error);
    if (bytes.empty()) {
        ZL_WARN("net", "世界声音编码失败：{}", error);
        return;
    }
    for (const auto& [sessionId, session] : m_sessions) {
        if (session.playerStableId == 0 || !session.assignmentSent) {
            continue;
        }
        if (!m_network->Send(sessionId, bytes, &error)) {
            ZL_WARN("net", "会话 {} 发送世界声音失败：{}", sessionId, error);
            continue;
        }
        ++m_networkStats.worldSoundsSent;
    }
}

void ServerHost::MarkStructureSyncDirty() {
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        session.lastStructureSyncTick = 0;
    }
}

world::Chunk* ServerHost::FindServerLoadedChunk(int wx, int wy) const {
    if (m_simulation == nullptr) {
        return nullptr;
    }
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    world::Chunk* chunk =
        game::FindChunkInWorld(m_simulation->World(), coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return nullptr;
    }
    return chunk;
}

void ServerHost::RebuildServerChunk(int chunkX, int chunkY) {
    world::Chunk* chunk = FindServerLoadedChunk(
        chunkX * core::kChunkSize, chunkY * core::kChunkSize);
    if (chunk == nullptr || m_simulation == nullptr) {
        return;
    }
    m_simulation->Physics().BuildChunkStatics(chunkX, chunkY, *chunk);
    if (ai::PathfindingService* pathfinding = m_simulation->Pathfinding();
        pathfinding != nullptr) {
        pathfinding->OnChunkLoaded(*chunk);
    }
}

bool ServerHost::FindServerStructureAt(int wx, int wy, int* layer,
                                       std::size_t* index,
                                       const game::BuildableDef** def) const {
    if (layer == nullptr || index == nullptr || def == nullptr) {
        return false;
    }
    const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
    if (chunk == nullptr) {
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    const world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
    constexpr std::array<world::SquareLayer, 3> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture,
        world::SquareLayer::Floor};
    for (const world::SquareLayer candidateLayer : kLayers) {
        const auto& objects = square.Layer(candidateLayer);
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const world::SquareObject& object = objects[i];
            if (!object.playerBuilt) {
                continue;
            }
            const game::BuildableDef* candidate =
                FindBuildableForSprite(m_buildables, object.sprite);
            if (candidate == nullptr) {
                continue;
            }
            *layer = static_cast<int>(candidateLayer);
            *index = i;
            *def = candidate;
            return true;
        }
    }
    return false;
}

ecs::Entity ServerHost::FindServerBuildEntityAt(
    int wx, int wy, const game::BuildableDef** def) const {
    if (def == nullptr || m_simulation == nullptr) {
        return entt::null;
    }
    *def = nullptr;
    entt::registry& registry = m_simulation->Ecs().Registry();
    ecs::Entity best = entt::null;
    float bestDistanceSq = 0.0f;
    registry.view<ecs::Position, ecs::SpriteRef>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::SpriteRef& sprite) {
            if (!m_simulation->Ecs().Alive(entity) || position.z != 0 ||
                static_cast<int>(std::floor(position.x)) != wx ||
                static_cast<int>(std::floor(position.y)) != wy ||
                registry.all_of<ecs::PlayerTag>(entity) ||
                registry.all_of<ecs::AiState>(entity) ||
                registry.all_of<ecs::ItemInstance>(entity) ||
                registry.all_of<ecs::Projectile>(entity) ||
                registry.all_of<ecs::TransientEffect>(entity)) {
                return;
            }
            const auto* container = registry.try_get<ecs::Container>(entity);
            const auto* storage = registry.try_get<ecs::PlayerStorage>(entity);
            const game::BuildableDef* candidate = nullptr;
            if (const auto* placed = registry.try_get<ecs::PlacedTrap>(entity);
                placed != nullptr) {
                const game::TrapDef* trapDef =
                    m_traps != nullptr ? m_traps->Find(placed->defId) : nullptr;
                if (trapDef == nullptr) {
                    return;
                }
                for (const game::BuildableDef& buildable : m_buildables) {
                    if (buildable.id == trapDef->buildingId) {
                        candidate = &buildable;
                        break;
                    }
                }
                if (candidate == nullptr) {
                    return;
                }
            } else {
                for (const game::BuildableDef& buildable : m_buildables) {
                    if (ecs::HashName(buildable.sprite) != sprite.sprite &&
                        (buildable.spriteVertical.empty() ||
                         ecs::HashName(buildable.spriteVertical) !=
                             sprite.sprite) &&
                        (buildable.openSprite.empty() ||
                         ecs::HashName(buildable.openSprite) != sprite.sprite) &&
                        (buildable.openSpriteVertical.empty() ||
                         ecs::HashName(buildable.openSpriteVertical) !=
                             sprite.sprite)) {
                        continue;
                    }
                    if (game::IsStorageBuildable(buildable) &&
                        (container == nullptr || storage == nullptr ||
                         buildable.storageSlots != storage->maxStacks ||
                         std::abs(buildable.storageKg - container->capacityKg) >
                             0.01f)) {
                        continue;
                    }
                    candidate = &buildable;
                    break;
                }
            }
            if (candidate == nullptr) {
                return;
            }
            const float cx = static_cast<float>(wx) + 0.5f;
            const float cy = static_cast<float>(wy) + 0.5f;
            const float dx = position.x - cx;
            const float dy = position.y - cy;
            const float distanceSq = dx * dx + dy * dy;
            if (best == entt::null || distanceSq < bestDistanceSq) {
                best = entity;
                bestDistanceSq = distanceSq;
                *def = candidate;
            }
        });
    return best;
}

bool ServerHost::ServerCellOccupied(int wx, int wy) const {
    if (m_simulation == nullptr) {
        return true;
    }
    bool occupied = false;
    m_simulation->Ecs().Registry().view<ecs::Position>().each(
        [&](const ecs::Position& position) {
            if (!occupied && position.z == 0 &&
                static_cast<int>(std::floor(position.x)) == wx &&
                static_cast<int>(std::floor(position.y)) == wy) {
                occupied = true;
            }
        });
    return occupied;
}

bool ServerHost::ServerBuildCooldown(SessionPlayer& session,
                                     std::string* error) const {
    if (session.buildUsed && m_buildCooldownTicks > 0 &&
        m_ticks < session.lastBuildTick + m_buildCooldownTicks) {
        if (error != nullptr) {
            *error = "建造操作冷却中";
        }
        return false;
    }
    return true;
}

bool ServerHost::ServerCanEditStructure(const SessionPlayer& session,
                                        std::uint64_t owner,
                                        std::string* error) {
    if (game::CanEditStructure(owner, session.playerStableId)) {
        return true;
    }
    ++m_networkStats.ownershipRejected;
    if (error != nullptr) {
        *error = "该结构属于其他玩家";
    }
    return false;
}

ServerHost::DemolishConfirmDecision ServerHost::ServerConfirmDemolish(
    SessionPlayer& session, int wx, int wy, bool entityTarget, int layer,
    std::uint64_t token, std::uint16_t flags, const std::string& name,
    std::string* error) {
    const bool request =
        (flags & net::kDemolishConfirmRequestFlag) != 0;
    const bool accept =
        (flags & net::kDemolishConfirmAcceptFlag) != 0;
    const std::uint16_t knownFlags =
        net::kDemolishConfirmRequestFlag | net::kDemolishConfirmAcceptFlag;
    if ((flags & ~knownFlags) != 0 || (request && accept)) {
        if (error != nullptr) {
            *error = "拆除确认参数非法";
        }
        return DemolishConfirmDecision::Reject;
    }
    if (request) {
        session.demolishConfirmPending = true;
        session.demolishConfirmEntity = entityTarget;
        session.demolishConfirmX = wx;
        session.demolishConfirmY = wy;
        session.demolishConfirmLayer = layer;
        session.demolishConfirmToken = token;
        session.demolishConfirmExpiresTick = m_ticks + 180;
        SetSessionStatus(session,
                         "再次按 X 确认拆除 " + name + "（3 秒内）", 0);
        return DemolishConfirmDecision::Pending;
    }
    if (!accept) {
        return DemolishConfirmDecision::Proceed;
    }
    const bool expired =
        !session.demolishConfirmPending ||
        m_ticks > session.demolishConfirmExpiresTick;
    const bool mismatch =
        session.demolishConfirmEntity != entityTarget ||
        session.demolishConfirmX != wx || session.demolishConfirmY != wy ||
        session.demolishConfirmLayer != layer ||
        session.demolishConfirmToken != token;
    if (expired) {
        session.demolishConfirmPending = false;
        if (error != nullptr) {
            *error = "拆除确认已过期，请重新按 X";
        }
        return DemolishConfirmDecision::Reject;
    }
    if (mismatch) {
        session.demolishConfirmPending = false;
        if (error != nullptr) {
            *error = "拆除目标已变化，请重新确认";
        }
        return DemolishConfirmDecision::Reject;
    }
    session.demolishConfirmPending = false;
    return DemolishConfirmDecision::Proceed;
}

void ServerHost::MarkServerBuildAction(SessionPlayer& session) {
    session.buildUsed = true;
    session.lastBuildTick = m_ticks;
}

void ServerHost::MarkStructureTombstone(int wx, int wy, int z) {
    if (!m_networkEnabled) {
        return;
    }
    const std::uint64_t retention =
        std::max<std::uint64_t>(m_structureSyncIntervalTicks * 2U, 60U);
    const std::uint64_t expiresTick = m_ticks + retention;
    for (StructureTombstone& tombstone : m_structureTombstones) {
        if (tombstone.wx == wx && tombstone.wy == wy && tombstone.z == z) {
            tombstone.expiresTick = std::max(tombstone.expiresTick, expiresTick);
            return;
        }
    }
    constexpr std::size_t kMaxStructureTombstones = 4096;
    if (m_structureTombstones.size() >= kMaxStructureTombstones) {
        m_structureTombstones.erase(m_structureTombstones.begin());
    }
    m_structureTombstones.push_back(StructureTombstone{wx, wy, z, expiresTick});
}

void ServerHost::PruneStructureTombstones() {
    m_structureTombstones.erase(
        std::remove_if(m_structureTombstones.begin(), m_structureTombstones.end(),
                       [this](const StructureTombstone& tombstone) {
                           return tombstone.expiresTick < m_ticks;
                       }),
        m_structureTombstones.end());
}

bool ServerHost::RemoveServerWood(ecs::Entity player, int count) {
    if (m_simulation == nullptr || m_items == nullptr || count < 0) {
        return false;
    }
    auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    if (container == nullptr) {
        return false;
    }
    return items::RemoveItem(*container, m_buildWoodItem, count) == count;
}

bool ServerHost::ReturnServerItem(ecs::Entity player,
                                  const ecs::ItemInstance& item, float x,
                                  float y, int z) {
    if (!item.Valid() || m_simulation == nullptr || m_items == nullptr) {
        return false;
    }
    auto* container =
        m_simulation->Ecs().Registry().try_get<ecs::Container>(player);
    int added = 0;
    if (container != nullptr) {
        added = items::AddItem(*container, *m_items, item);
    }
    if (added >= item.count) {
        return false;
    }
    ecs::ItemInstance remainder = item;
    remainder.count -= added;
    (void)game::SpawnGroundItem(m_simulation->Ecs(), *m_items, remainder, x, y,
                                z);
    return true;
}

bool ServerHost::PlaceStructure(ecs::Entity player, SessionPlayer& session,
                                const net::Command& command,
                                std::string* error) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return false;
    }
    int wx = 0;
    int wy = 0;
    if (!DecodeBuildCell(command.payload.x, command.payload.y, &wx, &wy)) {
        return false;
    }
    if (command.payload.index < 0 ||
        static_cast<std::size_t>(command.payload.index) >= m_buildables.size()) {
        if (error != nullptr) {
            *error = "建造项索引非法";
        }
        return false;
    }
    if ((command.payload.flags & ~std::uint16_t{1}) != 0) {
        if (error != nullptr) {
            *error = "建造命令 flags 非法";
        }
        return false;
    }
    const game::BuildableDef& def =
        m_buildables[static_cast<std::size_t>(command.payload.index)];
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    if (playerPos == nullptr) {
        return false;
    }
    const float distance = std::hypot(
        playerPos->x - (static_cast<float>(wx) + 0.5f),
        playerPos->y - (static_cast<float>(wy) + 0.5f));
    if (distance > m_buildRange) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "建造目标超出范围";
        }
        return false;
    }
    if (!ServerBuildCooldown(session, error)) {
        ++m_networkStats.cooldownRejected;
        return false;
    }
    if (def.id == "farm_plot" && !m_homesteadConfig.farm.enabled) {
        if (error != nullptr) {
            *error = "农业系统已关闭";
        }
        return false;
    }
    if ((def.id == "generator" || def.id == "fridge" ||
         def.id == "floodlight" || def.id == "electric_fence") &&
        !m_homesteadConfig.power.enabled) {
        if (error != nullptr) {
            *error = "电力系统已关闭";
        }
        return false;
    }
    if (def.id == "spike_trap" && !m_homesteadConfig.trap.enabled) {
        if (error != nullptr) {
            *error = "陷阱系统已关闭";
        }
        return false;
    }
    const game::TrapDef* trapDef =
        m_traps != nullptr ? m_traps->FindByBuilding(def.id) : nullptr;
    const bool trapBuilding =
        trapDef != nullptr || def.id == "bear_trap" ||
        def.id == "tripwire_alarm" || def.id == "fuel_trap";
    if (trapBuilding &&
        (m_traps == nullptr || !m_traps->enabled || trapDef == nullptr)) {
        if (error != nullptr) {
            *error = "陷阱数据不可用";
        }
        return false;
    }
    world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
    if (chunk == nullptr) {
        if (error != nullptr) {
            *error = "建造目标区域未加载";
        }
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
    if (!square.HasFloor()) {
        if (error != nullptr) {
            *error = "建造目标格没有地面";
        }
        return false;
    }
    if (!game::IsBuildCellFreeFor(def, square)) {
        if (error != nullptr) {
            *error = "建造目标格被占用";
        }
        return false;
    }
    if (ServerCellOccupied(wx, wy)) {
        if (error != nullptr) {
            *error = "建造目标格被角色或物品占用";
        }
        return false;
    }
    auto* container = registry.try_get<ecs::Container>(player);
    if (container == nullptr ||
        items::CountOf(*container, m_buildWoodItem) < def.woodCost) {
        if (error != nullptr) {
            *error = "木材不足";
        }
        return false;
    }
    if (!RemoveServerWood(player, def.woodCost)) {
        if (error != nullptr) {
            *error = "建造扣木材失败";
        }
        return false;
    }

    bool placed = false;
    ecs::Entity placedEntity = entt::null;
    if (def.id == "farm_plot") {
        placedEntity = game::SpawnFarmPlot(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, m_homesteadConfig.farm);
    } else if (def.id == "generator") {
        placedEntity = game::SpawnGenerator(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, m_homesteadConfig.power);
    } else if (def.id == "fridge") {
        placedEntity = game::SpawnColdStorage(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, def.sprite, def.storageKg,
            def.storageSlots);
    } else if (def.id == "floodlight") {
        placedEntity = game::SpawnFloodlight(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, def.maxHp);
    } else if (def.id == "spike_trap") {
        placedEntity = game::SpawnSpikeTrap(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, m_homesteadConfig.trap);
    } else if (trapDef != nullptr) {
        placedEntity = game::SpawnPlacedTrap(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, *trapDef, def,
            session.playerStableId);
    } else if (game::IsStorageBuildable(def)) {
        placedEntity = game::SpawnPlayerStorage(
            m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, def.sprite, def.storageKg,
            def.storageSlots);
    } else {
        world::SquareObject built = game::MakeBuiltObject(
            def, (command.payload.flags & 1U) != 0);
        built.structureId = game::MakeStructureId(
            wx, wy, 0, static_cast<int>(def.layer));
        built.ownerStableId = session.playerStableId;
        if (def.layer == world::SquareLayer::Floor) {
            if (const world::SquareObject* floor = square.Floor();
                floor != nullptr) {
                built.underlay = floor->sprite;
            }
            square.SetFloor(std::move(built));
        } else {
            square.AddObject(def.layer, std::move(built));
        }
        placed = true;
        if (def.blocksMovement) {
            RebuildServerChunk(chunk->ChunkX(), chunk->ChunkY());
        }
    }
    if (placedEntity != entt::null) {
        placed = true;
        registry.emplace_or_replace<ecs::StructureOwner>(
            placedEntity, ecs::StructureOwner{session.playerStableId});
    }
    if (!placed) {
        ecs::ItemInstance wood{m_buildWoodItem, def.woodCost, 1.0f};
        ReturnServerItem(player, wood, playerPos->x, playerPos->y, playerPos->z);
        if (error != nullptr) {
            *error = "建造实体创建失败";
        }
        return false;
    }

    MarkServerBuildAction(session);
    MarkStructureSyncDirty();
    SetSessionStatus(session,
                     "建造了 " + def.name + "（-" +
                         std::to_string(def.woodCost) + " 木材）",
                     1);
    ZL_INFO("building", "服务器建造 {}：({}, {}) 玩家 {}",
            def.id, wx, wy, session.playerStableId);
    return true;
}

bool ServerHost::DemolishStructure(ecs::Entity player, SessionPlayer& session,
                                   const net::Command& command,
                                   std::string* error) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return false;
    }
    int wx = 0;
    int wy = 0;
    if (!DecodeBuildCell(command.payload.x, command.payload.y, &wx, &wy)) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    if (playerPos == nullptr) {
        return false;
    }
    const float distance = std::hypot(
        playerPos->x - (static_cast<float>(wx) + 0.5f),
        playerPos->y - (static_cast<float>(wy) + 0.5f));
    if (distance > m_buildActionRange) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "拆除目标超出范围";
        }
        return false;
    }
    if (!ServerBuildCooldown(session, error)) {
        ++m_networkStats.cooldownRejected;
        return false;
    }

    int layer = static_cast<int>(world::SquareLayer::Furniture);
    std::size_t objectIndex = 0;
    const game::BuildableDef* def = nullptr;
    if (FindServerStructureAt(wx, wy, &layer, &objectIndex, &def)) {
        world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
        if (chunk == nullptr) {
            return false;
        }
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
        auto& objects =
            square.Layer(static_cast<world::SquareLayer>(layer));
        if (objectIndex >= objects.size()) {
            return false;
        }
        const world::SquareObject& object = objects[objectIndex];
        if (!object.playerBuilt ||
            !game::BuildableMatchesSprite(*def, object.sprite)) {
            return false;
        }
        if (!ServerCanEditStructure(session, object.ownerStableId, error)) {
            return false;
        }
        const std::uint64_t token =
            object.structureId != 0 ? object.structureId
                                    : ecs::HashName(object.sprite);
        const DemolishConfirmDecision confirm = ServerConfirmDemolish(
            session, wx, wy, false, layer, token, command.payload.flags,
            def->name, error);
        if (confirm == DemolishConfirmDecision::Pending) {
            return true;
        }
        if (confirm == DemolishConfirmDecision::Reject) {
            return false;
        }
        const bool blocksMovement = object.blocksMovement;
        const std::string underlay = object.underlay;
        objects.erase(objects.begin() +
                      static_cast<std::ptrdiff_t>(objectIndex));
        MarkStructureTombstone(wx, wy, 0);
        if (def->layer == world::SquareLayer::Floor) {
            square.SetFloor(world::SquareObject{
                underlay.empty() ? std::string("tile.floor.grass") : underlay,
                0, 0, false});
        } else if (blocksMovement) {
            RebuildServerChunk(chunk->ChunkX(), chunk->ChunkY());
        }
        const int refund =
            game::BuildWoodRefund(def->woodCost, m_demolishRefundRatio);
        if (refund > 0) {
            ReturnServerItem(player,
                             ecs::ItemInstance{m_buildWoodItem, refund, 1.0f},
                             playerPos->x, playerPos->y, playerPos->z);
        }
        MarkServerBuildAction(session);
        MarkStructureSyncDirty();
        SetSessionStatus(session,
                         "拆除 " + def->name + "（返还 " +
                             std::to_string(refund) + " 木材）",
                         1);
        ZL_INFO("building", "服务器拆除 {}：({}, {}) 玩家 {}", def->id, wx,
                wy, session.playerStableId);
        return true;
    }

    const game::BuildableDef* entityDef = nullptr;
    const ecs::Entity entity =
        FindServerBuildEntityAt(wx, wy, &entityDef);
    if (entity == entt::null || entityDef == nullptr) {
        if (error != nullptr) {
            *error = "附近没有可拆除的己方结构";
        }
        return false;
    }
    if (const auto* owner = registry.try_get<ecs::StructureOwner>(entity);
        owner != nullptr &&
        !ServerCanEditStructure(session, owner->ownerStableId, error)) {
        return false;
    }
    const DemolishConfirmDecision confirm = ServerConfirmDemolish(
        session, wx, wy, true, 0, m_simulation->Ecs().IdOf(entity).id,
        command.payload.flags, entityDef->name, error);
    if (confirm == DemolishConfirmDecision::Pending) {
        return true;
    }
    if (confirm == DemolishConfirmDecision::Reject) {
        return false;
    }
    const auto* position = registry.try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return false;
    }
    const float dropX = position->x;
    const float dropY = position->y;
    const int dropZ = position->z;
    std::vector<ecs::ItemInstance> contents;
    if (game::IsPlayerStorage(m_simulation->Ecs(), entity)) {
        contents = game::DetachPlayerStorageContents(m_simulation->Ecs(),
                                                      entity);
    }
    m_simulation->Ecs().Destroy(entity);
    for (const ecs::ItemInstance& item : contents) {
        if (item.Valid()) {
            (void)game::SpawnGroundItem(m_simulation->Ecs(), *m_items, item,
                                        dropX, dropY, dropZ);
        }
    }
    const int refund =
        game::BuildWoodRefund(entityDef->woodCost, m_demolishRefundRatio);
    if (refund > 0) {
        ReturnServerItem(player,
                         ecs::ItemInstance{m_buildWoodItem, refund, 1.0f},
                         playerPos->x, playerPos->y, playerPos->z);
    }
    MarkServerBuildAction(session);
    MarkStructureSyncDirty();
    SetSessionStatus(session,
                     "拆除 " + entityDef->name + "（返还 " +
                         std::to_string(refund) + " 木材）",
                     1);
    ZL_INFO("building", "服务器拆除设施 {}：({}, {}) 玩家 {}",
            entityDef->id, wx, wy, session.playerStableId);
    return true;
}

bool ServerHost::RepairStructure(ecs::Entity player, SessionPlayer& session,
                                 const net::Command& command,
                                 std::string* error) {
    if (m_simulation == nullptr) {
        return false;
    }
    int wx = 0;
    int wy = 0;
    if (!DecodeBuildCell(command.payload.x, command.payload.y, &wx, &wy)) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    if (playerPos == nullptr) {
        return false;
    }
    const float distance = std::hypot(
        playerPos->x - (static_cast<float>(wx) + 0.5f),
        playerPos->y - (static_cast<float>(wy) + 0.5f));
    if (distance > m_buildActionRange) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "维修目标超出范围";
        }
        return false;
    }
    if (!ServerBuildCooldown(session, error)) {
        ++m_networkStats.cooldownRejected;
        return false;
    }
    int layer = 0;
    std::size_t index = 0;
    const game::BuildableDef* def = nullptr;
    const bool foundStructure =
        FindServerStructureAt(wx, wy, &layer, &index, &def) && def != nullptr &&
        def->maxHp > 0.0f;
    if (!foundStructure) {
        ecs::Entity light = entt::null;
        registry.view<ecs::Floodlight, ecs::Position>().each(
            [&](const ecs::Entity entity, const ecs::Floodlight&,
                const ecs::Position& position) {
                if (light == entt::null && position.z == 0 &&
                    static_cast<int>(std::floor(position.x)) == wx &&
                    static_cast<int>(std::floor(position.y)) == wy) {
                    light = entity;
                }
            });
        if (light == entt::null) {
            if (error != nullptr) {
                *error = "目标没有可维修的己方结构";
            }
            return false;
        }
        auto* health = registry.try_get<ecs::Health>(light);
        if (health == nullptr || health->max <= 0.0f) {
            if (error != nullptr) {
                *error = "该设施不可维修";
            }
            return false;
        }
        const auto* owner = registry.try_get<ecs::StructureOwner>(light);
        if (!ServerCanEditStructure(
                session, owner != nullptr ? owner->ownerStableId : 0, error)) {
            return false;
        }
        if (health->current >= health->max - 0.01f) {
            if (error != nullptr) {
                *error = "设施耐久已满";
            }
            return false;
        }
        if (!RemoveServerWood(player, m_repairWoodCost)) {
            if (error != nullptr) {
                *error = "维修木材不足";
            }
            return false;
        }
        health->current =
            std::min(health->max,
                     health->current + m_structureRepairAmount);
        MarkServerBuildAction(session);
        SetSessionStatus(session,
                         "维修 泛光灯：HP " +
                             std::to_string(static_cast<int>(
                                 std::lround(health->current))) +
                             "/" +
                             std::to_string(static_cast<int>(
                                 std::lround(health->max))),
                         1);
        ZL_INFO("homestead", "服务器维修泛光灯：({}, {}) 玩家 {}", wx, wy,
                session.playerStableId);
        return true;
    }
    world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
    if (chunk == nullptr) {
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    auto& objects = chunk->Square(local.lx, local.ly, 0)
                        .Layer(static_cast<world::SquareLayer>(layer));
    if (index >= objects.size()) {
        return false;
    }
    world::SquareObject& object = objects[index];
    if (!object.playerBuilt ||
        !game::BuildableMatchesSprite(*def, object.sprite) ||
        def->maxHp <= 0.0f) {
        if (error != nullptr) {
            *error = "该结构不可维修";
        }
        return false;
    }
    if (!ServerCanEditStructure(session, object.ownerStableId, error)) {
        return false;
    }
    const float oldHp = game::ResolveBarricadeHp(object, def->maxHp);
    if (oldHp >= def->maxHp) {
        if (error != nullptr) {
            *error = "结构耐久已满";
        }
        return false;
    }
    if (!RemoveServerWood(player, m_repairWoodCost)) {
        if (error != nullptr) {
            *error = "维修木材不足";
        }
        return false;
    }
    object.hp = game::RepairBarricadeHp(oldHp, def->maxHp,
                                        m_structureRepairAmount);
    MarkServerBuildAction(session);
    MarkStructureSyncDirty();
    SetSessionStatus(session,
                     "维修 " + def->name + "：HP " +
                         std::to_string(static_cast<int>(
                             std::lround(object.hp))) +
                         "/" +
                         std::to_string(static_cast<int>(
                             std::lround(def->maxHp))),
                     1);
    ZL_INFO("building", "服务器维修 {}：({}, {}) 玩家 {}", def->id, wx, wy,
            session.playerStableId);
    return true;
}

bool ServerHost::ToggleDoor(ecs::Entity player, SessionPlayer& session,
                            const net::Command& command, std::string* error) {
    if (m_simulation == nullptr) {
        return false;
    }
    int wx = 0;
    int wy = 0;
    if (!DecodeBuildCell(command.payload.x, command.payload.y, &wx, &wy)) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    if (playerPos == nullptr) {
        return false;
    }
    const float distance = std::hypot(
        playerPos->x - (static_cast<float>(wx) + 0.5f),
        playerPos->y - (static_cast<float>(wy) + 0.5f));
    if (distance > m_buildActionRange) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "门超出交互范围";
        }
        return false;
    }
    if (!ServerBuildCooldown(session, error)) {
        ++m_networkStats.cooldownRejected;
        return false;
    }
    int layer = 0;
    std::size_t index = 0;
    const game::BuildableDef* def = nullptr;
    if (!FindServerStructureAt(wx, wy, &layer, &index, &def) || def == nullptr ||
        !def->door) {
        if (error != nullptr) {
            *error = "附近没有可开关的己方门";
        }
        return false;
    }
    world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
    if (chunk == nullptr) {
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    auto& objects = chunk->Square(local.lx, local.ly, 0)
                        .Layer(static_cast<world::SquareLayer>(layer));
    if (index >= objects.size()) {
        return false;
    }
    world::SquareObject& object = objects[index];
    if (!object.playerBuilt || !game::BuildableMatchesSprite(*def, object.sprite)) {
        return false;
    }
    if (!ServerCanEditStructure(session, object.ownerStableId, error)) {
        return false;
    }
    const bool vertical = game::IsVerticalBuildSprite(*def, object.sprite);
    const bool open = game::IsOpenDoorSprite(*def, object.sprite);
    if (open) {
        bool blocked = false;
        registry.view<ecs::Position, ecs::Health>().each(
            [&](const ecs::Position& position, const ecs::Health& health) {
                if (!blocked && health.current > 0.0f && position.z == 0 &&
                    static_cast<int>(std::floor(position.x)) == wx &&
                    static_cast<int>(std::floor(position.y)) == wy) {
                    blocked = true;
                }
            });
        if (blocked) {
            if (error != nullptr) {
                *error = "门口被挡住，无法关门";
            }
            return false;
        }
    }
    object.sprite = std::string(game::BuildableSprite(*def, vertical, !open));
    object.blocksMovement = open;
    RebuildServerChunk(chunk->ChunkX(), chunk->ChunkY());
    MarkServerBuildAction(session);
    MarkStructureSyncDirty();
    SetSessionStatus(session,
                     std::string(open ? "关门：" : "开门：") + def->name, 1);
    ZL_INFO("building", "服务器{} {}：({}, {}) 玩家 {}", open ? "关门" : "开门",
            def->id, wx, wy, session.playerStableId);
    return true;
}

void ServerHost::EnsureServerMerchant() {
    if (m_simulation == nullptr || !m_homesteadConfig.trade.enabled ||
        m_simulation->Player() == entt::null) {
        return;
    }
    if (m_settlements != nullptr && m_settlements->enabled &&
        !m_settlements->defs.empty()) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    bool found = false;
    registry.view<ecs::MerchantStock>().each(
        [&](const ecs::Entity, const ecs::MerchantStock&) { found = true; });
    if (found) {
        return;
    }

    const int targetGridX =
        static_cast<int>(std::floor(m_homesteadConfig.trade.merchantX));
    const int targetGridY =
        static_cast<int>(std::floor(m_homesteadConfig.trade.merchantY));
    const auto walkable = [this](int wx, int wy) {
        const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
        if (chunk == nullptr) {
            return false;
        }
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        if (!chunk->Square(local.lx, local.ly, 0).HasFloor()) {
            return false;
        }
        return !game::IsSquareBlocked(m_simulation->World(), wx, wy, 0) &&
               !ServerCellOccupied(wx, wy);
    };
    int wx = 0;
    int wy = 0;
    if (!ai::FindNearestWalkable(targetGridX, targetGridY, 6, walkable, &wx,
                                 &wy)) {
        return;
    }
    const ecs::Entity merchant = game::SpawnMerchant(
        m_simulation->Ecs(), static_cast<float>(wx) + 0.5f,
        static_cast<float>(wy) + 0.5f, 0);
    if (merchant != entt::null) {
        ZL_INFO("homestead", "服务器商人已就位：({}, {})", wx, wy);
    }
}

void ServerHost::EnsureServerSettlementNpcs() {
    if (m_simulation == nullptr || m_settlements == nullptr ||
        !m_settlements->enabled || m_simulation->Player() == entt::null) {
        return;
    }
    game::SettlementNpcSystem* system = m_simulation->SettlementNpcs();
    if (system == nullptr) {
        return;
    }
    const auto walkable = [this](int wx, int wy) {
        const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
        if (chunk == nullptr) {
            return false;
        }
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        if (!chunk->Square(local.lx, local.ly, 0).HasFloor()) {
            return false;
        }
        return !game::IsSquareBlocked(m_simulation->World(), wx, wy, 0) &&
               !ServerCellOccupied(wx, wy);
    };
    system->EnsurePopulation(m_simulation->Ecs(),
                             m_homesteadConfig.trade.enabled, walkable);
}

bool ServerHost::ServerFindNearbyBed(ecs::Entity player,
                                    ecs::Entity* bed) const {
    if (bed != nullptr) {
        *bed = entt::null;
    }
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return false;
    }
    const auto* position =
        m_simulation->Ecs().Registry().try_get<ecs::Position>(player);
    if (position == nullptr) {
        return false;
    }
    const int radius =
        std::max(1, static_cast<int>(std::ceil(m_buildActionRange)));
    const float rangeSq = m_buildActionRange * m_buildActionRange;
    float bestDistanceSq = rangeSq;
    ecs::Entity best = entt::null;
    bool bedFound = false;
    for (int wy = static_cast<int>(std::floor(position->y)) - radius;
         wy <= static_cast<int>(std::floor(position->y)) + radius; ++wy) {
        for (int wx = static_cast<int>(std::floor(position->x)) - radius;
             wx <= static_cast<int>(std::floor(position->x)) + radius; ++wx) {
            if (const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
                chunk != nullptr) {
                const world::SquareLocal local =
                    world::SquareLocalOf(wx, wy);
                constexpr std::array<world::SquareLayer, 2> kBedLayers{
                    world::SquareLayer::Wall, world::SquareLayer::Furniture};
                for (const world::SquareLayer layer : kBedLayers) {
                    for (const world::SquareObject& object :
                         chunk->Square(local.lx, local.ly, 0).Layer(layer)) {
                        if (!object.playerBuilt) {
                            continue;
                        }
                        const game::BuildableDef* worldDef =
                            FindBuildableForSprite(m_buildables, object.sprite);
                        if (worldDef == nullptr || worldDef->id != "bed") {
                            continue;
                        }
                        const float dx =
                            (static_cast<float>(wx) + 0.5f) - position->x;
                        const float dy =
                            (static_cast<float>(wy) + 0.5f) - position->y;
                        const float distanceSq = dx * dx + dy * dy;
                        if (distanceSq <= bestDistanceSq) {
                            bestDistanceSq = distanceSq;
                            best = entt::null;
                            bedFound = true;
                        }
                    }
                }
            }
            const game::BuildableDef* def = nullptr;
            const ecs::Entity candidate =
                FindServerBuildEntityAt(wx, wy, &def);
            if (candidate == entt::null || def == nullptr ||
                def->id != "bed" ||
                !m_simulation->Ecs().Alive(candidate)) {
                continue;
            }
            const auto* bedPosition =
                m_simulation->Ecs().Registry().try_get<ecs::Position>(
                    candidate);
            if (bedPosition == nullptr || bedPosition->z != position->z) {
                continue;
            }
            const float dx = bedPosition->x - position->x;
            const float dy = bedPosition->y - position->y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq <= bestDistanceSq) {
                bestDistanceSq = distanceSq;
                best = candidate;
            }
        }
    }
    if (bed != nullptr) {
        *bed = best;
    }
    return bedFound || best != entt::null;
}

bool ServerHost::ServerZombieNear(float x, float y, int z,
                                  float radius) const {
    if (m_simulation == nullptr || radius <= 0.0f) {
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const float radiusSq = radius * radius;
    bool zombieNear = false;
    registry.view<ecs::Position, ecs::Health, ecs::AiState>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::Health& health, const ecs::AiState& ai) {
            if (zombieNear || health.current <= 0.0f ||
                ai.status == ecs::AiStatus::Dead || position.z != z ||
                registry.any_of<ecs::Dying>(entity)) {
                return;
            }
            const float dx = position.x - x;
            const float dy = position.y - y;
            zombieNear = dx * dx + dy * dy <= radiusSq;
        });
    return zombieNear;
}

std::string ServerHost::ServerSleepDurationLabel() const {
    if (m_serverSleep.untilDawn) {
        return "睡到天亮";
    }
    const double minutes = std::max(0.0, m_serverSleep.durationMinutes);
    const int hours = std::max(1, static_cast<int>(std::llround(minutes / 60.0)));
    return std::to_string(hours) + " 小时";
}

std::string ServerHost::ServerSleepStatus(const SessionPlayer& session) const {
    if (m_serverSleep.phase == ServerSleepPhase::None) {
        return {};
    }
    std::string initiatorName;
    for (const auto& [sessionId, other] : m_sessions) {
        static_cast<void>(sessionId);
        if (other.playerStableId == m_serverSleep.initiator) {
            initiatorName = other.playerName.empty() ? "玩家" : other.playerName;
            break;
        }
    }
    if (initiatorName.empty()) {
        initiatorName = "玩家";
    }
    if (m_serverSleep.phase == ServerSleepPhase::Vote) {
        std::string text = "睡眠投票：" + initiatorName + " 提议 " +
                           ServerSleepDurationLabel() + " · " +
                           std::to_string(m_serverSleep.yes.size()) + "/" +
                           std::to_string(m_serverSleep.eligible.size());
        if (session.playerStableId == m_serverSleep.initiator) {
            text += "（发起者）";
        }
        if (m_serverSleep.yes.contains(session.playerStableId)) {
            text += " 已同意";
        } else {
            text += " · Y 同意 / N 拒绝";
        }
        if (m_serverSleep.voteSeconds > 0.0f) {
            text += " · " +
                    std::to_string(static_cast<int>(
                        std::ceil(m_serverSleep.voteSeconds))) +
                    "s";
        }
        return text;
    }
    const double elapsedHours =
        m_serverSleepSession.ElapsedMinutes() / 60.0;
    const double totalHours =
        std::max(1.0, (m_serverSleep.targetMinutes -
                       m_serverSleep.startMinutes) /
                          60.0);
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  "睡眠中：%s · %.1f/%.1f 小时 · ESC 取消",
                  ServerSleepDurationLabel().c_str(), elapsedHours,
                  totalHours);
    return buffer;
}

void ServerHost::MarkServerSleepDirty() {
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        session.stateDirty = true;
    }
}

void ServerHost::HaltServerPlayerForSleep(ecs::Entity player) {
    if (m_simulation == nullptr || player == entt::null ||
        !m_simulation->Ecs().Alive(player)) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (auto* move = registry.try_get<ecs::MoveInput>(player); move != nullptr) {
        move->x = 0.0f;
        move->y = 0.0f;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(player);
        intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
    if (auto* velocity = registry.try_get<ecs::Velocity>(player);
        velocity != nullptr) {
        velocity->x = 0.0f;
        velocity->y = 0.0f;
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(player);
        request != nullptr) {
        request->newRequest = false;
        request->queryId = 0;
        request->state = ecs::PathStatus::None;
    }
    if (auto* follow = registry.try_get<ecs::PathFollow>(player);
        follow != nullptr) {
        follow->waypoints.clear();
        follow->index = 0;
        follow->status = ecs::PathStatus::None;
    }
    if (auto* attack = registry.try_get<ecs::AttackIntent>(player);
        attack != nullptr) {
        attack->pending = false;
    }
}

bool ServerHost::StartServerSleepVote(SessionPlayer& session, bool untilDawn,
                                     float durationMinutes,
                                     std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!m_serverSleepEnabled || m_simulation == nullptr ||
        m_gameTime == nullptr) {
        return fail("服务器未启用睡眠");
    }
    if (m_serverSleep.phase != ServerSleepPhase::None) {
        return fail("已有睡眠投票或睡眠进行中");
    }
    if (!session.spawned || session.dead) {
        return fail("玩家状态不可睡眠");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return fail("玩家实体不存在");
    }
    if (m_serverBaseSiege.Active()) {
        return fail("守夜事件进行中，无法入睡");
    }
    ecs::Entity bed = entt::null;
    if (!ServerFindNearbyBed(player, &bed)) {
        return fail("附近没有床");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (const auto* job = registry.try_get<ecs::CraftingJob>(player);
        job != nullptr && job->active) {
        return fail("制作进行中，先完成或取消制作");
    }
    const auto* position = registry.try_get<ecs::Position>(player);
    if (position == nullptr) {
        return fail("玩家缺少位置");
    }
    if (m_serverSleepConfig.interruptOnZombie &&
        ServerZombieNear(position->x, position->y, position->z,
                         m_serverSleepConfig.zombieRadius)) {
        return fail("附近有僵尸，无法入睡");
    }
    double duration = 0.0;
    if (untilDawn) {
        duration = m_gameTime->MinutesUntilHour(m_serverSleepConfig.dawnHour);
    } else if (std::isfinite(durationMinutes) && durationMinutes > 0.0f) {
        duration = std::clamp(
            static_cast<double>(durationMinutes),
            static_cast<double>(m_serverSleepConfig.minHours) * 60.0,
            static_cast<double>(m_serverSleepConfig.maxHours) * 60.0);
    }
    if (!(duration > 0.0) || !std::isfinite(duration)) {
        return fail("睡眠时长无效");
    }

    m_serverSleep = {};
    m_serverSleep.phase = ServerSleepPhase::Vote;
    m_serverSleep.initiator = session.playerStableId;
    m_serverSleep.untilDawn = untilDawn;
    m_serverSleep.startMinutes = m_gameTime->TotalGameMinutes();
    m_serverSleep.durationMinutes = duration;
    m_serverSleep.targetMinutes = m_serverSleep.startMinutes + duration;
    m_serverSleep.voteSeconds = m_serverSleepVoteSeconds;
    m_serverSleep.yes.insert(session.playerStableId);
    for (const auto& [sessionId, other] : m_sessions) {
        static_cast<void>(sessionId);
        if (!other.spawned || other.dead || other.playerStableId == 0) {
            continue;
        }
        const ecs::Entity otherPlayer =
            m_simulation->Ecs().FindById(other.playerStableId);
        if (otherPlayer == entt::null) {
            continue;
        }
        const auto* health =
            registry.try_get<ecs::Health>(otherPlayer);
        if (health == nullptr || health->current <= 0.0f) {
            continue;
        }
        m_serverSleep.eligible.insert(other.playerStableId);
        m_serverSleep.lastHealth[other.playerStableId] = health->current;
    }
    if (!m_serverSleep.eligible.contains(session.playerStableId)) {
        m_serverSleep = {};
        return fail("当前没有可参与投票的玩家");
    }
    ZL_INFO("sleep", "服务器睡眠投票：发起者 {}，{} 小时，{} 人",
            session.playerStableId, duration / 60.0,
            m_serverSleep.eligible.size());
    if (m_serverSleep.eligible.size() <= 1) {
        StartServerSleep();
    } else {
        MarkServerSleepDirty();
    }
    return true;
}

bool ServerHost::VoteServerSleep(SessionPlayer& session, bool agree,
                                std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_serverSleep.phase != ServerSleepPhase::Vote) {
        return fail("当前没有睡眠投票");
    }
    if (!m_serverSleep.eligible.contains(session.playerStableId)) {
        return fail("当前会话不在睡眠投票中");
    }
    if (!agree) {
        FinishServerSleep(game::SleepStopReason::Cancelled,
                          session.playerName + " 拒绝");
        return true;
    }
    m_serverSleep.yes.insert(session.playerStableId);
    if (m_serverSleep.eligible.size() <= m_serverSleep.yes.size()) {
        StartServerSleep();
    } else {
        MarkServerSleepDirty();
    }
    return true;
}

void ServerHost::StartServerSleep() {
    if (m_gameTime == nullptr || m_simulation == nullptr ||
        m_serverSleep.phase != ServerSleepPhase::Vote) {
        return;
    }
    if (m_serverBaseSiege.Active()) {
        FinishServerSleep(game::SleepStopReason::SiegeStarted,
                          "守夜事件开始");
        return;
    }
    const double now = m_gameTime->TotalGameMinutes();
    const double duration =
        m_serverSleep.untilDawn
            ? m_gameTime->MinutesUntilHour(m_serverSleepConfig.dawnHour)
            : m_serverSleep.durationMinutes;
    if (!(duration > 0.0) || !std::isfinite(duration)) {
        FinishServerSleep(game::SleepStopReason::Cancelled, "睡眠时长无效");
        return;
    }
    m_serverSleep.startMinutes = now;
    m_serverSleep.durationMinutes = duration;
    m_serverSleep.targetMinutes = now + duration;
    m_serverSleepSession.Start(now, duration);
    if (!m_serverSleepSession.Active()) {
        FinishServerSleep(game::SleepStopReason::Cancelled, "睡眠启动失败");
        return;
    }
    m_serverSleep.phase = ServerSleepPhase::Sleeping;
    m_serverSleep.voteSeconds = 0.0f;
    m_serverSleep.yes = m_serverSleep.eligible;
    entt::registry& registry = m_simulation->Ecs().Registry();
    for (const std::uint64_t playerId : m_serverSleep.eligible) {
        const ecs::Entity player = m_simulation->Ecs().FindById(playerId);
        if (player == entt::null) {
            continue;
        }
        if (const auto* health = registry.try_get<ecs::Health>(player);
            health != nullptr) {
            m_serverSleep.lastHealth[playerId] = health->current;
        }
        HaltServerPlayerForSleep(player);
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (m_serverSleep.eligible.contains(session.playerStableId)) {
            SetSessionStatus(session,
                             "全队入睡：" + ServerSleepDurationLabel(), 0);
        }
    }
    MarkServerSleepDirty();
    ZL_INFO("sleep", "服务器全队入睡：{}（{} 分钟）",
            ServerSleepDurationLabel(), duration);
}

void ServerHost::TickServerSleep(float dt) {
    if (m_serverSleep.phase == ServerSleepPhase::None || m_simulation == nullptr ||
        m_gameTime == nullptr) {
        return;
    }
    if (dt <= 0.0f || !std::isfinite(dt)) {
        dt = 1.0f / 60.0f;
    }
    auto sessionFor = [this](std::uint64_t playerId) -> SessionPlayer* {
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (session.playerStableId == playerId) {
                return &session;
            }
        }
        return nullptr;
    };
    if (m_serverSleep.phase == ServerSleepPhase::Vote) {
        m_serverSleep.voteSeconds -= dt;
        for (auto it = m_serverSleep.eligible.begin();
             it != m_serverSleep.eligible.end();) {
            SessionPlayer* session = sessionFor(*it);
            if (session == nullptr || !session->spawned || session->dead) {
                if (*it == m_serverSleep.initiator) {
                    FinishServerSleep(game::SleepStopReason::Cancelled,
                                      "发起者已离开");
                    return;
                }
                m_serverSleep.yes.erase(*it);
                m_serverSleep.lastHealth.erase(*it);
                it = m_serverSleep.eligible.erase(it);
            } else {
                ++it;
            }
        }
        if (m_serverSleep.phase == ServerSleepPhase::None) {
            return;
        }
        if (m_serverSleep.eligible.empty()) {
            FinishServerSleep(game::SleepStopReason::Cancelled,
                              "没有可参与投票的玩家");
            return;
        }
        if (m_serverSleep.yes.size() >= m_serverSleep.eligible.size()) {
            StartServerSleep();
            return;
        }
        if (m_serverSleep.voteSeconds <= 0.0f) {
            FinishServerSleep(game::SleepStopReason::Cancelled, "投票超时");
        }
        return;
    }

    const double current = m_gameTime->TotalGameMinutes();
    const double remaining = m_serverSleep.targetMinutes - current;
    if (remaining <= 1e-6) {
        FinishServerSleep(game::SleepStopReason::Completed, "睡醒了");
        return;
    }
    const double step = std::min(
        static_cast<double>(m_serverSleepConfig.gameMinutesPerSecond) *
            static_cast<double>(dt),
        remaining);
    if (!(step > 0.0)) {
        return;
    }
    m_gameTime->AdvanceGameMinutes(step);
    entt::registry& registry = m_simulation->Ecs().Registry();
    for (const std::uint64_t playerId : m_serverSleep.eligible) {
        SessionPlayer* session = sessionFor(playerId);
        if (session == nullptr || !session->spawned || session->dead) {
            FinishServerSleep(game::SleepStopReason::Died, "玩家离开");
            return;
        }
        const ecs::Entity player = m_simulation->Ecs().FindById(playerId);
        if (player == entt::null || !m_simulation->Ecs().Alive(player)) {
            FinishServerSleep(game::SleepStopReason::Died, "玩家死亡");
            return;
        }
        const auto* health = registry.try_get<ecs::Health>(player);
        if (health == nullptr || health->current <= 0.0f) {
            FinishServerSleep(game::SleepStopReason::Died, "玩家死亡");
            return;
        }
        auto last = m_serverSleep.lastHealth.find(playerId);
        if (last != m_serverSleep.lastHealth.end() &&
            m_serverSleepConfig.interruptOnDamage &&
            health->current + 0.01f < last->second) {
            FinishServerSleep(game::SleepStopReason::Damaged,
                              "玩家受到伤害");
            return;
        }
        m_serverSleep.lastHealth[playerId] = health->current;
        if (auto* needs = registry.try_get<ecs::Needs>(player); needs != nullptr) {
            game::ApplySleepNeeds(*needs, m_serverSleepHungerPerDay,
                                  m_serverSleepThirstPerDay,
                                  static_cast<float>(step),
                                  m_serverSleepConfig.fatigueRecoveryPerHour);
            if (needs->hunger >= 0.95f || needs->thirst >= 0.95f) {
                FinishServerSleep(game::SleepStopReason::NeedsCritical,
                                  "饥饿或脱水");
                return;
            }
        }
        const auto* position = registry.try_get<ecs::Position>(player);
        if (m_serverSleepConfig.interruptOnZombie && position != nullptr &&
            ServerZombieNear(position->x, position->y, position->z,
                             m_serverSleepConfig.zombieRadius)) {
            FinishServerSleep(game::SleepStopReason::ZombieNear,
                              "附近有僵尸");
            return;
        }
    }
    m_serverSleepSession.Advance(step);
    if (!m_serverSleepSession.Active()) {
        FinishServerSleep(game::SleepStopReason::Completed, "睡醒了");
    }
}

void ServerHost::FinishServerSleep(game::SleepStopReason reason,
                                  const std::string& detail) {
    if (m_serverSleep.phase == ServerSleepPhase::None) {
        return;
    }
    const bool wasSleeping =
        m_serverSleep.phase == ServerSleepPhase::Sleeping;
    const std::uint8_t tone =
        reason == game::SleepStopReason::Completed
            ? 1
            : reason == game::SleepStopReason::Cancelled ? 0 : 2;
    std::string message;
    if (!wasSleeping) {
        message = "睡眠投票取消：" + detail;
    } else if (reason == game::SleepStopReason::Completed) {
        message = "睡眠结束：" + detail;
    } else {
        message = "睡眠中断：" + std::string(game::SleepStopText(reason));
        if (!detail.empty()) {
            message += "（" + detail + "）";
        }
    }
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!m_serverSleep.eligible.contains(session.playerStableId)) {
            continue;
        }
        if (m_simulation != nullptr) {
            const ecs::Entity player =
                m_simulation->Ecs().FindById(session.playerStableId);
            if (player != entt::null) {
                HaltServerPlayerForSleep(player);
            }
        }
        SetSessionStatus(session, message, tone);
    }
    m_serverSleepSession.Stop();
    m_serverSleep = {};
    MarkServerSleepDirty();
    ZL_INFO("sleep", "服务器睡眠结束：{}（{}），{}", game::SleepStopText(reason),
            detail, wasSleeping ? "睡眠态" : "投票态");
}

void ServerHost::OnServerSessionRemovedForSleep(
    std::uint64_t playerStableId) {
    if (m_serverSleep.phase == ServerSleepPhase::None ||
        playerStableId == 0) {
        return;
    }
    if (playerStableId == m_serverSleep.initiator) {
        FinishServerSleep(game::SleepStopReason::Cancelled,
                          "发起者已离开");
        return;
    }
    if (m_serverSleep.eligible.erase(playerStableId) > 0) {
        m_serverSleep.yes.erase(playerStableId);
        m_serverSleep.lastHealth.erase(playerStableId);
        if (m_serverSleep.eligible.empty()) {
            FinishServerSleep(game::SleepStopReason::Cancelled,
                              "没有可继续睡眠的玩家");
        } else if (m_serverSleep.phase == ServerSleepPhase::Vote &&
                   m_serverSleep.yes.size() >= m_serverSleep.eligible.size()) {
            StartServerSleep();
        } else {
            MarkServerSleepDirty();
        }
    }
}

void ServerHost::InitializeServerBaseSiege() {
    if (m_config == nullptr || m_simulation == nullptr || m_gameTime == nullptr) {
        return;
    }
    game::BaseSiegeCallbacks callbacks;
    callbacks.now = [this]() {
        game::BaseSiegeTime time;
        if (m_gameTime == nullptr) {
            time.paused = true;
            return time;
        }
        const core::DateTime& now = m_gameTime->Now();
        time.day = 1 + static_cast<int>(
                           m_gameTime->TotalGameMinutes() / (24.0 * 60.0));
        time.hour = now.hour;
        time.minute = now.minute;
        time.paused =
            m_gameTime->GetSpeed() == core::GameTime::Speed::Paused;
        return time;
    };
    callbacks.playerPos = [this](game::BaseSiegePoint* out) {
        if (out == nullptr || m_simulation == nullptr) {
            return false;
        }
        const ecs::Entity anchor = ServerSiegeAnchorPlayer();
        if (anchor == entt::null) {
            return false;
        }
        entt::registry& registry = m_simulation->Ecs().Registry();
        const auto* position = registry.try_get<ecs::Position>(anchor);
        if (position == nullptr) {
            return false;
        }
        const auto* health = registry.try_get<ecs::Health>(anchor);
        if (health != nullptr && health->current <= 0.0f) {
            return false;
        }
        out->x = position->x;
        out->y = position->y;
        return true;
    };
    callbacks.structureCount = [this](const game::BaseSiegePoint& point,
                                      float radius) {
        return CountServerPlayerStructuresNear(point, radius, false);
    };
    callbacks.barricadeCount = [this](const game::BaseSiegePoint& point,
                                      float radius) {
        return CountServerPlayerStructuresNear(point, radius, true);
    };
    callbacks.structureList = [this](const game::BaseSiegePoint& point,
                                     float radius) {
        return ListServerPlayerStructuresNear(point, radius);
    };
    callbacks.isWalkableCell = [this](int wx, int wy) {
        return IsServerSiegeSpawnCell(wx, wy);
    };
    callbacks.spawnZombie =
        [this](int wx, int wy, ecs::ZombieSpecialKind special) {
            return SpawnServerSiegeZombie(wx, wy, special);
        };
    callbacks.isAlive = [this](std::uint64_t stableId) {
        if (m_simulation == nullptr) {
            return false;
        }
        const ecs::Entity entity = m_simulation->Ecs().FindById(stableId);
        return entity != entt::null && m_simulation->Ecs().Alive(entity);
    };
    callbacks.despawnZombie = [this](std::uint64_t stableId) {
        DespawnServerSiegeZombie(stableId);
    };
    callbacks.notice = [this](const std::string& text,
                              const std::string& tone) {
        BroadcastServerNotice(text, tone);
    };
    callbacks.onResult = [this](const game::BaseSiegeResult& result) {
        return GrantServerBaseSiegeRewards(result);
    };
    m_serverBaseSiege.Configure(LoadServerBaseSiegeConfig(*m_config),
                                std::move(callbacks));
    m_serverSiegeAnchorStableId = 0;
}

ecs::Entity ServerHost::ServerSiegeAnchorPlayer() {
    if (m_simulation == nullptr) {
        return entt::null;
    }
    const auto valid = [this](ecs::Entity entity) {
        if (entity == entt::null || !m_simulation->Ecs().Alive(entity)) {
            return false;
        }
        entt::registry& registry = m_simulation->Ecs().Registry();
        if (!registry.all_of<ecs::PlayerTag>(entity) ||
            registry.try_get<ecs::Position>(entity) == nullptr) {
            return false;
        }
        const auto* health = registry.try_get<ecs::Health>(entity);
        return health == nullptr || health->current > 0.0f;
    };
    if (m_serverSiegeAnchorStableId != 0) {
        const ecs::Entity anchor =
            m_simulation->Ecs().FindById(m_serverSiegeAnchorStableId);
        return valid(anchor) ? anchor : entt::null;
    }
    const ecs::Entity local = m_simulation->Player();
    if (valid(local)) {
        m_serverSiegeAnchorStableId = m_simulation->Ecs().IdOf(local).id;
        return local;
    }
    std::vector<std::uint64_t> candidates;
    candidates.reserve(m_sessions.size());
    for (const auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (!session.spawned || session.dead ||
            session.playerStableId == 0) {
            continue;
        }
        candidates.push_back(session.playerStableId);
    }
    std::sort(candidates.begin(), candidates.end());
    for (const std::uint64_t stableId : candidates) {
        const ecs::Entity entity = m_simulation->Ecs().FindById(stableId);
        if (valid(entity)) {
            m_serverSiegeAnchorStableId = stableId;
            return entity;
        }
    }
    return entt::null;
}

int ServerHost::CountServerPlayerStructuresNear(
    const game::BaseSiegePoint& point, float radius,
    bool barricadesOnly) const {
    if (m_simulation == nullptr || radius <= 0.0f) {
        return 0;
    }
    const int minX = static_cast<int>(std::floor(point.x - radius));
    const int maxX = static_cast<int>(std::ceil(point.x + radius));
    const int minY = static_cast<int>(std::floor(point.y - radius));
    const int maxY = static_cast<int>(std::ceil(point.y + radius));
    const float radiusSq = radius * radius;
    int count = 0;
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx = (static_cast<float>(wx) + 0.5f) - point.x;
            const float dy = (static_cast<float>(wy) + 0.5f) - point.y;
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
            if (chunk == nullptr) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            constexpr std::array<world::SquareLayer, 2> kLayers{
                world::SquareLayer::Wall, world::SquareLayer::Furniture};
            for (const world::SquareLayer layer : kLayers) {
                for (const world::SquareObject& object :
                     chunk->Square(local.lx, local.ly, 0).Layer(layer)) {
                    if (!object.playerBuilt) {
                        continue;
                    }
                    const game::BuildableDef* def =
                        FindBuildableForSprite(m_buildables, object.sprite);
                    if (def == nullptr) {
                        continue;
                    }
                    if (barricadesOnly && def->maxHp <= 0.0f) {
                        continue;
                    }
                    ++count;
                }
            }
        }
    }
    return count;
}

std::vector<game::BaseSiegeStructure> ServerHost::ListServerPlayerStructuresNear(
    const game::BaseSiegePoint& point, float radius) const {
    std::vector<game::BaseSiegeStructure> result;
    if (m_simulation == nullptr || radius <= 0.0f) {
        return result;
    }
    const int minX = static_cast<int>(std::floor(point.x - radius));
    const int maxX = static_cast<int>(std::ceil(point.x + radius));
    const int minY = static_cast<int>(std::floor(point.y - radius));
    const int maxY = static_cast<int>(std::ceil(point.y + radius));
    const float radiusSq = radius * radius;
    constexpr std::array<world::SquareLayer, 2> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture};
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx = (static_cast<float>(wx) + 0.5f) - point.x;
            const float dy = (static_cast<float>(wy) + 0.5f) - point.y;
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            for (const world::SquareLayer layer : kLayers) {
                for (const world::SquareObject& object :
                     chunk->Square(local.lx, local.ly, 0).Layer(layer)) {
                    if (!object.playerBuilt || !object.blocksMovement) {
                        continue;
                    }
                    const game::BuildableDef* def =
                        FindBuildableForSprite(m_buildables, object.sprite);
                    if (def == nullptr || def->maxHp <= 0.0f) {
                        continue;
                    }
                    const float hp =
                        game::ResolveBarricadeHp(object, def->maxHp);
                    if (hp <= 0.0f) {
                        continue;
                    }
                    game::BaseSiegeStructure structure;
                    structure.id =
                        object.structureId != 0
                            ? object.structureId
                            : game::MakeStructureId(
                                  wx, wy, 0, static_cast<int>(layer));
                    structure.x = static_cast<float>(wx) + 0.5f;
                    structure.y = static_cast<float>(wy) + 0.5f;
                    structure.hp = hp;
                    structure.maxHp = def->maxHp;
                    structure.door = def->door;
                    structure.name = def->name;
                    result.push_back(std::move(structure));
                }
            }
        }
    }
    return result;
}

bool ServerHost::IsServerSiegeSpawnCell(int wx, int wy) const {
    if (m_simulation == nullptr) {
        return false;
    }
    const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
    if (chunk == nullptr) {
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    if (!chunk->Square(local.lx, local.ly, 0).HasFloor()) {
        return false;
    }
    return !game::IsSquareBlocked(m_simulation->World(), wx, wy, 0);
}

std::uint64_t ServerHost::SpawnServerSiegeZombie(
    int wx, int wy, ecs::ZombieSpecialKind special) {
    if (m_simulation == nullptr) {
        return 0;
    }
    const float px = static_cast<float>(wx) + 0.5f;
    const float py = static_cast<float>(wy) + 0.5f;
    ecs::Entity zombie = entt::null;
    if (special == ecs::ZombieSpecialKind::Howler) {
        zombie = m_simulation->SpawnZombieAt(
            px, py, ecs::ZombieVariantKind::Fast);
    } else if (special == ecs::ZombieSpecialKind::Breaker) {
        zombie = m_simulation->SpawnZombieAt(
            px, py, ecs::ZombieVariantKind::Strong);
    } else {
        zombie = m_simulation->SpawnZombieAt(px, py);
    }
    if (zombie == entt::null) {
        return 0;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (!registry.valid(zombie)) {
        return 0;
    }
    registry.emplace_or_replace<ecs::BaseSiegeZombie>(zombie);
    if (special != ecs::ZombieSpecialKind::None) {
        ecs::ZombieSpecial value;
        value.kind = special;
        registry.emplace_or_replace<ecs::ZombieSpecial>(zombie, value);
    }
    const ecs::Entity anchor = ServerSiegeAnchorPlayer();
    const auto* anchorPosition =
        anchor != entt::null
            ? registry.try_get<ecs::Position>(anchor)
            : nullptr;
    const std::uint64_t anchorId =
        anchor != entt::null ? m_simulation->Ecs().IdOf(anchor).id : 0;
    if (auto* ai = registry.try_get<ecs::AiState>(zombie); ai != nullptr) {
        ai->status = ecs::AiStatus::Chase;
        ai->stateTime = 0.0f;
        ai->actionTimer = 0.0f;
        ai->targetId = anchorId;
    }
    if (auto* perception =
            registry.try_get<ecs::PerceptionState>(zombie);
        perception != nullptr) {
        if (anchorPosition != nullptr) {
            perception->lastKnownX = anchorPosition->x;
            perception->lastKnownY = anchorPosition->y;
        }
        perception->alertness = 1.0f;
        perception->sinceSeen = 0.0f;
        perception->sinceHeard = 0.0f;
    }
    const std::uint64_t stableId = m_simulation->Ecs().IdOf(zombie).id;
    ZL_INFO("siege", "服务器生成事件僵尸 stableId={} at ({}, {}) special={}",
            stableId, wx, wy, static_cast<int>(special));
    return stableId;
}

void ServerHost::DespawnServerSiegeZombie(std::uint64_t stableId) {
    if (m_simulation == nullptr) {
        return;
    }
    const ecs::Entity entity = m_simulation->Ecs().FindById(stableId);
    if (entity != entt::null) {
        m_simulation->DespawnEntity(entity);
    }
}

void ServerHost::DriveServerSiegeZombies() {
    if (m_simulation == nullptr || m_serverSiegeAnchorStableId == 0) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity anchor =
        m_simulation->Ecs().FindById(m_serverSiegeAnchorStableId);
    if (anchor == entt::null) {
        return;
    }
    const auto* anchorPosition = registry.try_get<ecs::Position>(anchor);
    if (anchorPosition == nullptr) {
        return;
    }
    const std::uint64_t anchorId = m_serverSiegeAnchorStableId;
    const game::BaseSiegeAttackPlan& plan =
        m_serverBaseSiege.AttackPlan();
    const bool director = m_serverBaseSiege.DirectorActive();
    float approachX = 0.0f;
    float approachY = 0.0f;
    const game::BaseSiegePoint& center = m_serverBaseSiege.Center();
    const bool hasApproach =
        director &&
        game::FindBaseSiegeApproachPoint(
            plan, center.x, center.y,
            [this](int wx, int wy) {
                return IsServerSiegeSpawnCell(wx, wy);
            },
            &approachX, &approachY);

    bool hasRear = false;
    float rearX = 0.0f;
    float rearY = 0.0f;
    if (director && plan.currentDirection >= 0) {
        const float angle =
            game::BaseSiegeDirectionAngle(plan.currentDirection);
        constexpr float kRearDistance = 11.0f;
        rearX = center.x + std::cos(angle) * kRearDistance;
        rearY = center.y - std::sin(angle) * kRearDistance;
        hasRear = true;
    }

    const auto findLure = [&](float x, float y, float* outX,
                              float* outY) {
        if (!m_homesteadConfig.power.enabled || outX == nullptr ||
            outY == nullptr) {
            return false;
        }
        const float radius = m_homesteadConfig.power.attractRadius;
        const float radiusSq = radius * radius;
        const bool night =
            m_gameTime != nullptr && m_gameTime->IsNight();
        bool found = false;
        float bestDistance = radiusSq;
        if (!m_homesteadConfig.power.deviceRequiresNight || night) {
            registry.view<ecs::Position, ecs::Floodlight>().each(
                [&](const ecs::Entity entity, const ecs::Position& position,
                    const ecs::Floodlight& floodlight) {
                    if (!floodlight.on || !floodlight.powered) {
                        return;
                    }
                    if (const auto* health =
                            registry.try_get<ecs::Health>(entity);
                        health != nullptr && health->current <= 0.0f) {
                        return;
                    }
                    const float dx = position.x - x;
                    const float dy = position.y - y;
                    const float distance = dx * dx + dy * dy;
                    if (distance > bestDistance) {
                        return;
                    }
                    found = true;
                    bestDistance = distance;
                    *outX = position.x;
                    *outY = position.y;
                });
        }
        if (found) {
            return true;
        }
        registry.view<ecs::Position, ecs::Generator>().each(
            [&](const ecs::Entity, const ecs::Position& position,
                const ecs::Generator& generator) {
                if (!generator.running || generator.fuelMinutes <= 0.0f) {
                    return;
                }
                const float dx = position.x - x;
                const float dy = position.y - y;
                const float distance = dx * dx + dy * dy;
                if (distance > bestDistance) {
                    return;
                }
                found = true;
                bestDistance = distance;
                *outX = position.x;
                *outY = position.y;
            });
        return found;
    };

    for (const std::uint64_t stableId :
         m_serverBaseSiege.EventZombies()) {
        const ecs::Entity entity = m_simulation->Ecs().FindById(stableId);
        if (entity == entt::null || !m_simulation->Ecs().Alive(entity)) {
            continue;
        }
        auto* ai = registry.try_get<ecs::AiState>(entity);
        if (ai == nullptr || ai->status == ecs::AiStatus::Dead) {
            continue;
        }
        auto* perception =
            registry.try_get<ecs::PerceptionState>(entity);
        const auto* special =
            registry.try_get<ecs::ZombieSpecial>(entity);
        const bool breaker =
            special != nullptr &&
            special->kind == ecs::ZombieSpecialKind::Breaker;
        const bool howler =
            special != nullptr &&
            special->kind == ecs::ZombieSpecialKind::Howler;

        if (breaker && hasApproach) {
            if (ai->status != ecs::AiStatus::SearchLastKnown ||
                ai->targetId != 0) {
                ai->status = ecs::AiStatus::SearchLastKnown;
                ai->stateTime = 0.0f;
                ai->actionTimer = 0.0f;
            }
            ai->targetId = 0;
            auto& assault =
                registry.get_or_emplace<ecs::SiegeAssaultTarget>(entity);
            assault.x = approachX;
            assault.y = approachY;
            assault.active = true;
            if (perception != nullptr) {
                perception->lastKnownX = approachX;
                perception->lastKnownY = approachY;
                perception->alertness = 1.0f;
                perception->sinceHeard = 0.0f;
            }
            continue;
        }

        if (howler && hasRear) {
            if (ai->status != ecs::AiStatus::SearchLastKnown ||
                ai->targetId != 0) {
                ai->status = ecs::AiStatus::SearchLastKnown;
                ai->stateTime = 0.0f;
                ai->actionTimer = 0.0f;
            }
            ai->targetId = 0;
            auto& assault =
                registry.get_or_emplace<ecs::SiegeAssaultTarget>(entity);
            assault.x = rearX;
            assault.y = rearY;
            assault.active = true;
            if (perception != nullptr) {
                perception->lastKnownX = rearX;
                perception->lastKnownY = rearY;
                perception->alertness = 1.0f;
                perception->sinceHeard = 0.0f;
            }
            continue;
        }

        const auto* position = registry.try_get<ecs::Position>(entity);
        const bool seenPlayer =
            perception != nullptr && perception->sinceSeen < 1.0f;
        float lureX = 0.0f;
        float lureY = 0.0f;
        if (!seenPlayer && position != nullptr &&
            findLure(position->x, position->y, &lureX, &lureY)) {
            if (ai->status != ecs::AiStatus::SearchLastKnown ||
                ai->targetId != 0) {
                ai->status = ecs::AiStatus::SearchLastKnown;
                ai->stateTime = 0.0f;
                ai->actionTimer = 0.0f;
            }
            ai->targetId = 0;
            auto& assault =
                registry.get_or_emplace<ecs::SiegeAssaultTarget>(entity);
            assault.x = lureX;
            assault.y = lureY;
            assault.active = true;
            if (perception != nullptr) {
                perception->lastKnownX = lureX;
                perception->lastKnownY = lureY;
                perception->alertness =
                    std::max(perception->alertness, 0.8f);
                perception->sinceHeard = 0.0f;
            }
            continue;
        }

        registry.remove<ecs::SiegeAssaultTarget>(entity);
        if (perception != nullptr) {
            perception->lastKnownX = anchorPosition->x;
            perception->lastKnownY = anchorPosition->y;
            perception->alertness = std::max(perception->alertness, 0.8f);
            perception->sinceHeard = 0.0f;
        }
        if (ai->status == ecs::AiStatus::Idle ||
            ai->status == ecs::AiStatus::Wander ||
            ai->status == ecs::AiStatus::SearchLastKnown ||
            (ai->status == ecs::AiStatus::Chase && ai->targetId == 0)) {
            ai->status = ecs::AiStatus::Chase;
            ai->targetId = anchorId;
            ai->stateTime = 0.0f;
            ai->actionTimer = 0.0f;
        }
    }
}

void ServerHost::BroadcastServerNotice(const std::string& text,
                                       const std::string& tone) {
    const std::uint8_t value =
        tone == "fail" ? 2 : (tone == "warn" ? 1 : 0);
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        SetSessionStatus(session, text, value);
    }
    ZL_INFO("siege", "{}", text);
}

void ServerHost::PushServerChat(const std::string& sender, std::string text) {
    if (text.size() > net::kMaxChatBytes) {
        text.resize(net::kMaxChatBytes);
    }
    m_chatLog.push_back(
        net::SocialChatLine{m_ticks, sender, std::move(text)});
    while (m_chatLog.size() > net::kMaxSocialChatMessages) {
        m_chatLog.pop_front();
    }
    m_socialDirty = true;
}

void ServerHost::PublishSiegeReport(const std::string& text) {
    BroadcastServerNotice(text, "ok");
    PushServerChat("系统", text);
}

void ServerHost::TickServerBaseSiege(float dt) {
    if (m_simulation == nullptr || m_gameTime == nullptr) {
        return;
    }
    m_serverBaseSiege.Update(dt);
    if (!m_serverBaseSiege.Active()) {
        m_serverSiegeAnchorStableId = 0;
        return;
    }
    DriveServerSiegeZombies();
}

void ServerHost::TickServerFloodlightAttacks(float dt) {
    if (m_simulation == nullptr) {
        return;
    }
    if (!m_serverBaseSiege.Active()) {
        m_serverFloodlightAttackCooldowns.clear();
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const std::vector<game::FloodlightAttack> attacks =
        game::TickZombieFloodlightAttacks(
            registry, m_serverFloodlightAttackCooldowns, dt,
            m_buildZombieDamage, m_buildZombieCooldown, m_breakerDamage,
            m_breakerCooldown, 1.4f, m_breakerRange);
    int destroyed = 0;
    for (const game::FloodlightAttack& attack : attacks) {
        if (!attack.destroyed) {
            continue;
        }
        ++destroyed;
        const auto* position = registry.try_get<ecs::Position>(attack.light);
        ZL_INFO("homestead", "服务器泛光灯被事件僵尸摧毁：({}, {})",
                position != nullptr ? position->x : 0.0f,
                position != nullptr ? position->y : 0.0f);
        m_simulation->Ecs().Destroy(attack.light);
    }
    if (destroyed > 0) {
        BroadcastServerNotice("泛光灯被摧毁 ×" + std::to_string(destroyed),
                              "fail");
    }
}

std::string ServerHost::GrantServerBaseSiegeRewards(
    const game::BaseSiegeResult& result) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return {};
    }
    const std::vector<game::BaseSiegeReward> rewards =
        game::BaseSiegeRewardsFor(result.rating, m_buildWoodItem);
    if (rewards.empty()) {
        return {};
    }

    std::vector<const items::ItemDef*> defs;
    defs.reserve(rewards.size());
    std::string text;
    for (const game::BaseSiegeReward& reward : rewards) {
        const items::ItemDef* def = m_items->Find(reward.itemId);
        defs.push_back(def);
        if (def == nullptr) {
            continue;
        }
        if (!text.empty()) {
            text += "、";
        }
        text += def->name + "×" + std::to_string(reward.count);
    }
    if (text.empty()) {
        return {};
    }

    int recipients = 0;
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.dead || !session.spawned || session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        const auto* position =
            player != entt::null
                ? m_simulation->Ecs().Registry().try_get<ecs::Position>(player)
                : nullptr;
        const auto* health =
            player != entt::null
                ? m_simulation->Ecs().Registry().try_get<ecs::Health>(player)
                : nullptr;
        if (position == nullptr ||
            (health != nullptr && health->current <= 0.0f)) {
            continue;
        }
        const float dropX = position->x;
        const float dropY = position->y;
        const int dropZ = position->z;
        for (std::size_t i = 0; i < rewards.size(); ++i) {
            if (defs[i] == nullptr) {
                continue;
            }
            ReturnServerItem(player,
                             ecs::ItemInstance{rewards[i].itemId,
                                               rewards[i].count, 1.0f},
                             dropX, dropY, dropZ);
        }
        session.stateDirty = true;
        ++recipients;
    }
    if (recipients == 0) {
        return {};
    }
    ZL_INFO("siege", "服务器守夜评级 {}，奖励 {} 名玩家：{}", result.rating,
            recipients, text);
    return text;
}

void ServerHost::TickHomestead(float dt) {
    if (m_simulation == nullptr || m_gameTime == nullptr) {
        return;
    }
    const double now = m_gameTime->TotalGameMinutes();
    float gameMinutes = 0.0f;
    if (m_lastHomesteadMinute >= 0.0 && now > m_lastHomesteadMinute) {
        gameMinutes = static_cast<float>(now - m_lastHomesteadMinute);
    }
    m_lastHomesteadMinute = now;

    entt::registry& registry = m_simulation->Ecs().Registry();
    if (m_homesteadConfig.farm.enabled && gameMinutes > 0.0f) {
        const game::FarmSeason season =
            game::FarmSeasonFromMonth(m_gameTime->Now().month);
        const game::WeatherState weather = m_simulation->CurrentWeather();
        const bool raining = weather.kind == game::WeatherKind::Rain &&
                             weather.rainIntensity > 0.0f;
        registry.view<ecs::FarmPlot>().each(
            [&](const ecs::Entity, ecs::FarmPlot& plot) {
                const game::CropDef* crop =
                    m_crops != nullptr ? m_crops->Find(plot.cropId) : nullptr;
                game::AdvanceFarmPlot(plot, gameMinutes, m_homesteadConfig.farm,
                                      crop, season, raining);
            });
    }
    TickServerPower(gameMinutes, dt);
    if (m_homesteadConfig.trap.enabled && dt > 0.0f) {
        registry.view<ecs::SpikeTrap>().each(
            [&](const ecs::Entity, ecs::SpikeTrap& trap) {
                game::AdvanceTrap(trap, dt);
            });
    }
    if (m_traps != nullptr && m_traps->enabled && dt > 0.0f) {
        registry.view<ecs::PlacedTrap>().each(
            [&](const ecs::Entity entity, ecs::PlacedTrap& trap) {
                (void)game::AdvancePlacedTrapCooldown(trap, dt);
                if (game::AdvancePlacedTrapArming(trap, dt)) {
                    ZL_INFO("traps", "服务器陷阱布防完成：{}（{}）", trap.defId,
                            m_simulation->Ecs().IdOf(entity).id);
                }
            });
    }

    if (m_homesteadConfig.trade.enabled) {
        const int day = 1 + static_cast<int>(now / (24.0 * 60.0));
        const auto& offers = game::MerchantOffers();
        registry.view<ecs::MerchantStock>().each(
            [&](const ecs::Entity entity, ecs::MerchantStock& stock) {
                int restockDays = m_homesteadConfig.trade.restockDays;
                if (const auto* npc =
                        registry.try_get<game::SettlementNpc>(entity);
                    npc != nullptr && m_settlements != nullptr) {
                    if (const game::SettlementDef* settlement =
                            m_settlements->Find(npc->settlementId);
                        settlement != nullptr) {
                        restockDays = settlement->restockDays;
                    }
                }
                game::ClampMerchantStock(stock);
                if (stock.restockDay <= 0 ||
                    day >= stock.restockDay + restockDays) {
                    for (std::size_t i = 0;
                         i < offers.size() && i < stock.stock.size(); ++i) {
                        stock.stock[i] = offers[i].maxStock;
                    }
                    stock.restockDay = day;
                }
            });
    }
}

void ServerHost::TickServerPower(float gameMinutes, float dt) {
    m_serverPowerSummary = {};
    if (m_simulation == nullptr || m_gameTime == nullptr) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const game::PowerConfig& config = m_homesteadConfig.power;
    for (auto& [key, remaining] : m_serverFenceCooldowns) {
        (void)key;
        remaining = std::max(0.0f, remaining - std::max(0.0f, dt));
    }
    m_serverFenceNoticeCooldown =
        std::max(0.0f, m_serverFenceNoticeCooldown - std::max(0.0f, dt));
    if (!config.enabled) {
        registry.view<ecs::Generator>().each(
            [](const ecs::Entity, ecs::Generator& generator) {
                generator.running = false;
                generator.load = 0.0f;
            });
        registry.view<ecs::ColdStorage>().each(
            [](const ecs::Entity, ecs::ColdStorage& storage) {
                storage.powered = false;
            });
        registry.view<ecs::Floodlight>().each(
            [](const ecs::Entity, ecs::Floodlight& floodlight) {
                floodlight.powered = false;
            });
        m_serverPoweredFenceCells.clear();
        m_serverElectricFenceCells.clear();
        m_serverPowerScanTimer = 0.0f;
        return;
    }

    const bool night = m_gameTime->IsNight();
    const bool devicesActive = !config.deviceRequiresNight || night;
    struct SourceRuntime {
        ecs::Entity entity = entt::null;
        game::PowerSource supply;
    };
    std::vector<SourceRuntime> sources;
    const auto collectSources = [&]() {
        sources.clear();
        registry.view<ecs::Generator, ecs::Position>().each(
            [&](const ecs::Entity entity, ecs::Generator& generator,
                const ecs::Position& position) {
                generator.load = 0.0f;
                if (sources.size() >= 8 || !generator.running ||
                    generator.fuelMinutes <= 0.0f ||
                    generator.radius <= 0.0f) {
                    return;
                }
                sources.push_back(SourceRuntime{
                    entity, game::PowerSource{position.x, position.y,
                                              generator.radius}});
            });
    };

    const auto collectFenceCells = [&]() {
        m_serverElectricFenceCells.clear();
        for (const SourceRuntime& source : sources) {
            const int scanRadius = std::clamp(
                static_cast<int>(std::ceil(source.supply.radius)), 1, 12);
            const int cx = static_cast<int>(std::floor(source.supply.x));
            const int cy = static_cast<int>(std::floor(source.supply.y));
            for (int wy = cy - scanRadius; wy <= cy + scanRadius; ++wy) {
                for (int wx = cx - scanRadius; wx <= cx + scanRadius; ++wx) {
                    if (m_serverElectricFenceCells.size() >= 256) {
                        return;
                    }
                    const int dx = wx - cx;
                    const int dy = wy - cy;
                    if (dx * dx + dy * dy > scanRadius * scanRadius) {
                        continue;
                    }
                    const std::uint64_t key = game::BuildCellKey(wx, wy);
                    if (m_serverElectricFenceCells.contains(key)) {
                        continue;
                    }
                    const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
                    if (chunk == nullptr) {
                        continue;
                    }
                    const world::SquareLocal local =
                        world::SquareLocalOf(wx, wy);
                    const world::GridSquare& square =
                        chunk->Square(local.lx, local.ly, 0);
                    constexpr std::array<world::SquareLayer, 2> kLayers{
                        world::SquareLayer::Wall,
                        world::SquareLayer::Furniture};
                    bool found = false;
                    for (const world::SquareLayer layer : kLayers) {
                        for (const world::SquareObject& object :
                             square.Layer(layer)) {
                            if (object.playerBuilt &&
                                object.sprite == game::kElectricFenceSprite &&
                                object.hp > 0.0f) {
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                            break;
                        }
                    }
                    if (found) {
                        m_serverElectricFenceCells.insert(key);
                    }
                }
            }
        }
    };

    const auto assignDevices = [&]() {
        std::vector<game::PowerSource> supplies;
        supplies.reserve(sources.size());
        for (SourceRuntime& source : sources) {
            if (auto* generator =
                    registry.try_get<ecs::Generator>(source.entity);
                generator != nullptr) {
                generator->load = 0.0f;
            }
            supplies.push_back(source.supply);
        }
        const auto powerAt = [&](float x, float y) {
            return game::FindPowerSourceFor(supplies, x, y);
        };
        const auto addLoad = [&](int source, float load) {
            if (source < 0 || !devicesActive) {
                return;
            }
            if (auto* generator = registry.try_get<ecs::Generator>(
                    sources[static_cast<std::size_t>(source)].entity);
                generator != nullptr) {
                generator->load += load;
            }
        };

        registry.view<ecs::ColdStorage, ecs::Position>().each(
            [&](const ecs::Entity, ecs::ColdStorage& storage,
                const ecs::Position& position) {
                const int source = powerAt(position.x, position.y);
                storage.powered = source >= 0;
                addLoad(source, config.coldStorageLoad);
            });
        registry.view<ecs::Floodlight, ecs::Position>().each(
            [&](const ecs::Entity entity, ecs::Floodlight& floodlight,
                const ecs::Position& position) {
                if (const auto* health = registry.try_get<ecs::Health>(entity);
                    health != nullptr && health->current <= 0.0f) {
                    floodlight.powered = false;
                    return;
                }
                if (!floodlight.on) {
                    floodlight.powered = false;
                    return;
                }
                const int source = powerAt(position.x, position.y);
                floodlight.powered = source >= 0;
                addLoad(source, config.floodlightLoad);
            });
        m_serverPoweredFenceCells.clear();
        for (const std::uint64_t key : m_serverElectricFenceCells) {
            const int source = powerAt(
                static_cast<float>(game::BuildCellX(key)) + 0.5f,
                static_cast<float>(game::BuildCellY(key)) + 0.5f);
            if (source < 0) {
                continue;
            }
            m_serverPoweredFenceCells.insert(key);
            addLoad(source, config.electricFenceLoad);
        }
        for (auto it = m_serverFenceCooldowns.begin();
             it != m_serverFenceCooldowns.end();) {
            if (!m_serverPoweredFenceCells.contains(it->first)) {
                it = m_serverFenceCooldowns.erase(it);
            } else {
                ++it;
            }
        }
    };

    collectSources();
    m_serverPowerScanTimer -= std::max(0.0f, dt);
    if (m_serverPowerScanTimer <= 0.0f) {
        m_serverPowerScanTimer = 0.5f;
        collectFenceCells();
    }
    assignDevices();
    if (gameMinutes > 0.0f) {
        bool stopped = false;
        for (const SourceRuntime& source : sources) {
            auto* generator = registry.try_get<ecs::Generator>(source.entity);
            if (generator == nullptr || !generator->running) {
                continue;
            }
            game::AdvanceGenerator(*generator, gameMinutes,
                                   config.baseLoad + generator->load);
            if (!generator->running) {
                stopped = true;
            }
        }
        if (stopped) {
            collectSources();
            assignDevices();
        }
    }

    int totalGenerators = 0;
    registry.view<ecs::Generator>().each(
        [&](const ecs::Entity, ecs::Generator&) { ++totalGenerators; });
    float maxEffectiveMinutes = 0.0f;
    for (const SourceRuntime& source : sources) {
        const auto* generator = registry.try_get<ecs::Generator>(source.entity);
        if (generator == nullptr || !generator->running) {
            continue;
        }
        m_serverPowerSummary.load += config.baseLoad + generator->load;
        const float effective =
            game::GeneratorEffectiveMinutes(*generator, config.baseLoad);
        maxEffectiveMinutes = std::max(maxEffectiveMinutes, effective);
        if (config.lowFuelWarningMinutes > 0.0f &&
            effective < config.lowFuelWarningMinutes) {
            m_serverPowerSummary.lowFuel = true;
        }
    }
    m_serverPowerSummary.fuelMinutes = maxEffectiveMinutes;
    m_serverPowerSummary.outage =
        totalGenerators > 0 && sources.empty();
}

void ServerHost::TickElectricFences() {
    if (m_simulation == nullptr || m_gameTime == nullptr ||
        !m_homesteadConfig.power.enabled ||
        !m_serverBaseSiege.Active() || m_serverPoweredFenceCells.empty()) {
        return;
    }
    const game::PowerConfig& config = m_homesteadConfig.power;
    if (config.deviceRequiresNight && !m_gameTime->IsNight()) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    characters::CombatSystem* combat = m_simulation->Combat();
    if (combat == nullptr) {
        return;
    }
    constexpr float kRange = 1.35f;
    std::vector<std::uint64_t> fences(m_serverPoweredFenceCells.begin(),
                                      m_serverPoweredFenceCells.end());
    for (const std::uint64_t key : fences) {
        const int wx = game::BuildCellX(key);
        const int wy = game::BuildCellY(key);
        const world::Chunk* chunk = FindServerLoadedChunk(wx, wy);
        if (chunk == nullptr) {
            continue;
        }
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        const world::GridSquare& square =
            chunk->Square(local.lx, local.ly, 0);
        bool intact = false;
        for (const world::SquareLayer layer :
             {world::SquareLayer::Wall, world::SquareLayer::Furniture}) {
            for (const world::SquareObject& object : square.Layer(layer)) {
                if (object.playerBuilt &&
                    object.sprite == game::kElectricFenceSprite &&
                    object.hp > 0.0f) {
                    intact = true;
                    break;
                }
            }
            if (intact) {
                break;
            }
        }
        if (!intact) {
            m_serverPoweredFenceCells.erase(key);
            m_serverElectricFenceCells.erase(key);
            m_serverFenceCooldowns.erase(key);
            continue;
        }
        if (auto cooldown = m_serverFenceCooldowns.find(key);
            cooldown != m_serverFenceCooldowns.end() &&
            cooldown->second > 0.0f) {
            continue;
        }
        const float fenceX = static_cast<float>(wx) + 0.5f;
        const float fenceY = static_cast<float>(wy) + 0.5f;
        ecs::Entity target = entt::null;
        float bestDistance = kRange * kRange;
        registry.view<ecs::BaseSiegeZombie, ecs::AiState, ecs::Health,
                      ecs::Position>()
            .each([&](const ecs::Entity entity, const ecs::AiState& ai,
                      const ecs::Health& health,
                      const ecs::Position& position) {
                if (health.current <= 0.0f ||
                    ai.status == ecs::AiStatus::Dead ||
                    registry.any_of<ecs::Dying>(entity) ||
                    registry.any_of<ecs::PlayerTag>(entity) ||
                    position.z != 0) {
                    return;
                }
                const float dx = position.x - fenceX;
                const float dy = position.y - fenceY;
                const float distance = dx * dx + dy * dy;
                if (distance > bestDistance) {
                    return;
                }
                bestDistance = distance;
                target = entity;
            });
        if (target == entt::null) {
            continue;
        }
        const auto* targetPosition = registry.try_get<ecs::Position>(target);
        ecs::DamageEvent event;
        event.type = ecs::DamageType::Blunt;
        event.amount = config.electricFenceDamage;
        event.staggerSeconds = config.electricFenceStaggerSeconds;
        if (targetPosition != nullptr) {
            const float dx = targetPosition->x - fenceX;
            const float dy = targetPosition->y - fenceY;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.0001f) {
                event.dirX = dx / length;
                event.dirY = dy / length;
            }
        }
        if (!combat->ApplyDamage(m_simulation->Ecs(), target, event)) {
            continue;
        }
        m_serverFenceCooldowns[key] =
            config.electricFenceCooldownSeconds;
        SpawnImpactEffect(fenceX, fenceY, 0);
        if (m_serverFenceNoticeCooldown <= 0.0f) {
            const std::string message =
                "电网电击：夜间事件僵尸受到 " +
                std::to_string(static_cast<int>(
                    std::lround(config.electricFenceDamage))) +
                " 伤害";
            constexpr float kNoticeRadiusSq = 12.0f * 12.0f;
            for (auto& [sessionId, session] : m_sessions) {
                static_cast<void>(sessionId);
                if (session.dead || session.playerStableId == 0) {
                    continue;
                }
                const ecs::Entity player =
                    m_simulation->Ecs().FindById(session.playerStableId);
                const auto* position = player != entt::null
                                           ? registry.try_get<ecs::Position>(
                                                 player)
                                           : nullptr;
                if (position == nullptr) {
                    continue;
                }
                const float dx = position->x - fenceX;
                const float dy = position->y - fenceY;
                if (dx * dx + dy * dy <= kNoticeRadiusSq) {
                    SetSessionStatus(session, message, 1);
                }
            }
            m_serverFenceNoticeCooldown = 1.0f;
        }
    }
}

void ServerHost::TickTrapTriggers() {
    if (m_simulation == nullptr) {
        return;
    }
    const bool legacyEnabled = m_homesteadConfig.trap.enabled;
    const bool placedEnabled = m_traps != nullptr && m_traps->enabled;
    if (!legacyEnabled && !placedEnabled) {
        return;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    if (legacyEnabled) {
        struct PendingHit {
            ecs::Entity trap = entt::null;
            ecs::Entity target = entt::null;
        };
        std::vector<PendingHit> pending;
        registry.view<ecs::SpikeTrap, ecs::Position>().each(
            [&](const ecs::Entity trapEntity, ecs::SpikeTrap& trap,
                const ecs::Position& trapPosition) {
                if (!game::TrapCanTrigger(trap)) {
                    return;
                }
                ecs::Entity target = entt::null;
                float bestDistance = trap.radius * trap.radius;
                registry.view<ecs::AiState, ecs::Health, ecs::Position>()
                    .each([&](const ecs::Entity entity, const ecs::AiState& ai,
                              const ecs::Health& health,
                              const ecs::Position& position) {
                        if (!registry.any_of<ecs::BaseSiegeZombie,
                                              game::SettlementRaidZombie>(
                                entity) ||
                            health.current <= 0.0f ||
                            ai.status == ecs::AiStatus::Dead ||
                            registry.any_of<ecs::Dying>(entity) ||
                            registry.any_of<ecs::PlayerTag>(entity) ||
                            position.z != trapPosition.z) {
                            return;
                        }
                        const float dx = position.x - trapPosition.x;
                        const float dy = position.y - trapPosition.y;
                        const float distance = dx * dx + dy * dy;
                        if (distance > bestDistance) {
                            return;
                        }
                        bestDistance = distance;
                        target = entity;
                    });
                if (target != entt::null) {
                    pending.push_back(PendingHit{trapEntity, target});
                }
            });

        for (const PendingHit& hit : pending) {
            auto* trap = registry.try_get<ecs::SpikeTrap>(hit.trap);
            const auto* trapPosition =
                registry.try_get<ecs::Position>(hit.trap);
            const auto* targetPosition =
                registry.try_get<ecs::Position>(hit.target);
            if (trap == nullptr || trapPosition == nullptr ||
                targetPosition == nullptr || !game::TrapCanTrigger(*trap)) {
                continue;
            }
            ecs::DamageEvent event;
            event.type = ecs::DamageType::Blunt;
            event.amount = trap->damage;
            event.knockback = 0.8f;
            event.staggerSeconds = trap->staggerSeconds;
            if (const auto* owner =
                    registry.try_get<ecs::StructureOwner>(hit.trap);
                owner != nullptr) {
                event.sourceId = owner->ownerStableId;
            }
            const float dx = targetPosition->x - trapPosition->x;
            const float dy = targetPosition->y - trapPosition->y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.0001f) {
                event.dirX = dx / length;
                event.dirY = dy / length;
            }
            characters::CombatSystem* combat = m_simulation->Combat();
            const auto* targetHealth =
                registry.try_get<ecs::Health>(hit.target);
            const float healthBefore =
                targetHealth != nullptr ? targetHealth->current : 0.0f;
            if (combat == nullptr ||
                !combat->ApplyDamage(m_simulation->Ecs(), hit.target, event)) {
                continue;
            }
            const auto* healthAfter =
                registry.try_get<ecs::Health>(hit.target);
            if (event.sourceId != 0 && healthAfter != nullptr) {
                RecordSiegeDefenseDamage(
                    event.sourceId, hit.target,
                    std::max(0.0f, healthBefore - healthAfter->current));
            }
            if (!game::ConsumeTrapUse(*trap)) {
                continue;
            }
            const float impactX = trapPosition->x;
            const float impactY = trapPosition->y;
            const int impactZ = trapPosition->z;
            SpawnImpactEffect(impactX, impactY, impactZ);
            NotifyTrapTrigger(impactX, impactY, impactZ, trap->uses);
            ZL_INFO("homestead", "服务器陷阱 {} 命中目标 {}，剩余 {} 次",
                    m_simulation->Ecs().IdOf(hit.trap).id,
                    m_simulation->Ecs().IdOf(hit.target).id, trap->uses);
        }
    }

    if (!placedEnabled) {
        return;
    }
    bool anyReady = false;
    registry.view<ecs::PlacedTrap>().each(
        [&](const ecs::Entity, const ecs::PlacedTrap& trap) {
            const game::TrapDef* def = m_traps->Find(trap.defId);
            if (def != nullptr && game::TrapCanActivate(trap, *def)) {
                anyReady = true;
            }
        });
    if (!anyReady) {
        return;
    }
    struct PendingPlacedHit {
        ecs::Entity trap = entt::null;
        ecs::Entity target = entt::null;
        game::TrapTargetKind kind = game::TrapTargetKind::None;
        const game::TrapDef* def = nullptr;
    };
    std::vector<PendingPlacedHit> pendingPlaced;
    const std::vector<game::TrapTargetCandidate> targets =
        game::CollectTrapTargets(m_simulation->Ecs());
    registry.view<ecs::PlacedTrap, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::PlacedTrap& trap,
            const ecs::Position& position) {
            const game::TrapDef* def = m_traps->Find(trap.defId);
            if (def == nullptr || !game::TrapCanActivate(trap, *def)) {
                return;
            }
            game::TrapTargetKind kind = game::TrapTargetKind::None;
            const ecs::Entity target =
                game::FindTrapTarget(targets, position, *def, &kind);
            if (target != entt::null) {
                pendingPlaced.push_back(
                    PendingPlacedHit{entity, target, kind, def});
            }
        });

    const auto notifyPlaced = [&](const game::TrapDef& def,
                                  game::TrapTargetKind kind,
                                  const ecs::Position& position,
                                  int usesLeft, std::size_t fireCells) {
        constexpr float kNoticeRadiusSq = 12.0f * 12.0f;
        std::string message = def.name + "触发";
        if (def.damage > 0.0f) {
            message += "：造成 " +
                       std::to_string(static_cast<int>(
                           std::lround(def.damage))) +
                       " 伤害";
        } else if (def.noiseRadius > 0.0f) {
            message += "：发出 " +
                       std::to_string(static_cast<int>(
                           std::lround(def.noiseRadius))) +
                       " 格噪声";
        } else if (def.fire.has_value()) {
            message += "：点燃 " + std::to_string(fireCells) + " 格火焰";
        }
        static_cast<void>(kind);
        message += "（剩余 " + std::to_string(usesLeft) + " 次）";
        for (auto& [sessionId, session] : m_sessions) {
            static_cast<void>(sessionId);
            if (session.dead || session.playerStableId == 0) {
                continue;
            }
            const ecs::Entity player =
                m_simulation->Ecs().FindById(session.playerStableId);
            const auto* playerPosition =
                player != entt::null
                    ? registry.try_get<ecs::Position>(player)
                    : nullptr;
            if (playerPosition == nullptr ||
                playerPosition->z != position.z) {
                continue;
            }
            const float dx = playerPosition->x - position.x;
            const float dy = playerPosition->y - position.y;
            if (dx * dx + dy * dy <= kNoticeRadiusSq) {
                SetSessionStatus(session, message, 1);
            }
        }
    };

    for (const PendingPlacedHit& hit : pendingPlaced) {
        auto* trap = registry.try_get<ecs::PlacedTrap>(hit.trap);
        const auto* trapPosition = registry.try_get<ecs::Position>(hit.trap);
        const auto* targetPosition =
            registry.try_get<ecs::Position>(hit.target);
        const auto* targetHealth =
            registry.try_get<ecs::Health>(hit.target);
        if (trap == nullptr || trapPosition == nullptr ||
            targetPosition == nullptr || targetHealth == nullptr ||
            targetHealth->current <= 0.0f || hit.def == nullptr ||
            !game::TrapCanActivate(*trap, *hit.def)) {
            continue;
        }
        bool applied = false;
        if (hit.def->damage > 0.0f) {
            characters::CombatSystem* combat = m_simulation->Combat();
            if (combat != nullptr) {
                ecs::DamageEvent event;
                event.type = ecs::DamageType::Blunt;
                event.amount = hit.def->damage;
                event.knockback = 0.0f;
                event.staggerSeconds = hit.def->staggerSeconds;
                event.sourceId = trap->owner;
                const float dx = targetPosition->x - trapPosition->x;
                const float dy = targetPosition->y - trapPosition->y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.0001f) {
                    event.dirX = dx / length;
                    event.dirY = dy / length;
                }
                const float healthBefore = targetHealth->current;
                if (combat->ApplyDamage(m_simulation->Ecs(), hit.target,
                                        event)) {
                    applied = true;
                    const auto* healthAfter =
                        registry.try_get<ecs::Health>(hit.target);
                    if (event.sourceId != 0 && healthAfter != nullptr) {
                        RecordSiegeDefenseDamage(
                            event.sourceId, hit.target,
                            std::max(0.0f,
                                     healthBefore - healthAfter->current));
                    }
                }
            }
        }
        if (hit.def->noiseRadius > 0.0f) {
            core::SoundEvent noise;
            noise.x = trapPosition->x;
            noise.y = trapPosition->y;
            noise.radius = hit.def->noiseRadius;
            noise.type = core::SoundType::Impact;
            noise.clip = audio::kImpact;
            if (m_events != nullptr) {
                m_events->EmitQueued(core::kOnSound, noise);
            }
            net::WorldSoundEvent remote;
            remote.sourcePlayerStableId = trap->owner;
            remote.x = trapPosition->x;
            remote.y = trapPosition->y;
            remote.z = trapPosition->z;
            remote.radius = hit.def->noiseRadius;
            remote.type =
                static_cast<std::uint8_t>(core::SoundType::Impact);
            remote.clip = audio::kImpact;
            BroadcastWorldSound(remote);
            applied = true;
        }
        std::size_t fireCells = 0;
        if (hit.def->fire.has_value()) {
            game::FireSystem* fire = m_simulation->Fire();
            if (fire != nullptr) {
                double dayLengthMinutes = 60.0;
                if (m_config != nullptr) {
                    dayLengthMinutes = m_config->Get<double>(
                        "game.dayLengthMinutes", dayLengthMinutes);
                }
                const float gameMinutesPerRealSecond =
                    std::isfinite(dayLengthMinutes) &&
                            dayLengthMinutes > 0.0
                        ? static_cast<float>(1440.0 /
                                             (dayLengthMinutes * 60.0))
                        : 0.4f;
                const float burnSeconds = game::TrapFireDurationSeconds(
                    hit.def->fire->minutes, gameMinutesPerRealSecond);
                const std::vector<game::TrapFireCell> cells =
                    game::TrapFireAreaCells(trapPosition->x,
                                            trapPosition->y,
                                            hit.def->fire->radius);
                for (const game::TrapFireCell& cell : cells) {
                    if (fire->IgniteForcedAt(
                            m_simulation->Ecs(), m_simulation->World(),
                            cell.cellX, cell.cellY, trapPosition->z,
                            burnSeconds, hit.def->fire->radius,
                            trap->owner)) {
                        ++fireCells;
                    }
                }
                if (fireCells > 0) {
                    applied = true;
                }
            }
        }
        if (!applied) {
            continue;
        }
        (void)game::ConsumePlacedTrapUse(*trap, *hit.def);
        if (hit.def->damage > 0.0f || hit.def->fire.has_value()) {
            SpawnImpactEffect(trapPosition->x, trapPosition->y,
                              trapPosition->z);
        }
        notifyPlaced(*hit.def, hit.kind, *trapPosition, trap->uses,
                     fireCells);
        ZL_INFO("traps",
                "服务器陷阱 {} 触发 {}（{}），剩余 {} 次，冷却 {:.1f}s，火焰 {}/{} 格",
                m_simulation->Ecs().IdOf(hit.trap).id,
                game::TrapTargetKindName(hit.kind),
                m_simulation->Ecs().IdOf(hit.target).id, trap->uses,
                static_cast<double>(trap->cooldown), fireCells,
                hit.def->fire.has_value()
                    ? game::TrapFireAreaCells(trapPosition->x,
                                              trapPosition->y,
                                              hit.def->fire->radius)
                          .size()
                    : 0u);
    }
}

void ServerHost::NotifyTrapTrigger(float x, float y, int z, int usesLeft) {
    if (m_simulation == nullptr) {
        return;
    }
    constexpr float kNoticeRadiusSq = 12.0f * 12.0f;
    const std::string message =
        "陷阱命中僵尸（剩余 " + std::to_string(usesLeft) + " 次）";
    entt::registry& registry = m_simulation->Ecs().Registry();
    for (auto& [sessionId, session] : m_sessions) {
        static_cast<void>(sessionId);
        if (session.dead || session.playerStableId == 0) {
            continue;
        }
        const ecs::Entity player =
            m_simulation->Ecs().FindById(session.playerStableId);
        const auto* position =
            player != entt::null ? registry.try_get<ecs::Position>(player)
                                 : nullptr;
        if (position == nullptr || position->z != z) {
            continue;
        }
        const float dx = position->x - x;
        const float dy = position->y - y;
        if (dx * dx + dy * dy > kNoticeRadiusSq) {
            continue;
        }
        SetSessionStatus(session, message, 1);
    }
}

bool ServerHost::InteractFacility(ecs::Entity player, SessionPlayer& session,
                                  const net::Command& command,
                                  std::string* error) {
    if (m_simulation == nullptr || m_items == nullptr) {
        return false;
    }
    if (command.payload.entityId == 0) {
        if (error != nullptr) {
            *error = "设施目标不存在";
        }
        return false;
    }
    ecs::Entity facility =
        m_simulation->Ecs().FindById(command.payload.entityId);
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    const auto* facilityPos =
        facility != entt::null ? registry.try_get<ecs::Position>(facility)
                               : nullptr;
    if (facility == entt::null || !m_simulation->Ecs().Alive(facility) ||
        playerPos == nullptr || facilityPos == nullptr ||
        playerPos->z != facilityPos->z) {
        if (error != nullptr) {
            *error = "设施目标不存在";
        }
        return false;
    }
    const float dx = facilityPos->x - playerPos->x;
    const float dy = facilityPos->y - playerPos->y;
    if (dx * dx + dy * dy > m_buildActionRange * m_buildActionRange) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "设施超出交互范围";
        }
        return false;
    }
    std::uint64_t ownerStableId = 0;
    if (const auto* owner = registry.try_get<ecs::StructureOwner>(facility);
        owner != nullptr) {
        ownerStableId = owner->ownerStableId;
    }
    if (!ServerCanEditStructure(session, ownerStableId, error)) {
        return false;
    }

    auto* container = registry.try_get<ecs::Container>(player);
    if (container == nullptr) {
        if (error != nullptr) {
            *error = "玩家背包不可用";
        }
        return false;
    }
    const auto itemName = [&](std::string_view defId) {
        const items::ItemDef* def = m_items->Find(std::string(defId));
        return def != nullptr ? def->name : std::string(defId);
    };

    if ((command.payload.flags & game::kFarmActionMask) != 0 &&
        registry.try_get<ecs::FarmPlot>(facility) == nullptr) {
        if (error != nullptr) {
            *error = "该设施不支持农田操作";
        }
        return false;
    }

    if (auto* plot = registry.try_get<ecs::FarmPlot>(facility);
        plot != nullptr && m_homesteadConfig.farm.enabled) {
        const game::FarmConfig& config = m_homesteadConfig.farm;
        const auto seasonName = [&]() -> const char* {
            return m_gameTime != nullptr
                       ? game::FarmSeasonName(game::FarmSeasonFromMonth(
                             m_gameTime->Now().month))
                       : "夏季";
        };
        const std::uint16_t farmAction =
            command.payload.flags & game::kFarmActionMask;
        if (farmAction != 0 && farmAction != game::kFarmActionPlant &&
            farmAction != game::kFarmActionCare) {
            if (error != nullptr) {
                *error = "农田操作非法";
            }
            return false;
        }
        const auto findCrop = [&](std::string_view id) -> const game::CropDef* {
            return m_crops != nullptr ? m_crops->Find(id) : nullptr;
        };
        if (farmAction == game::kFarmActionCare) {
            if (plot->stage != ecs::CropStage::Growing) {
                if (error != nullptr) {
                    *error = "只有生长中的作物需要施肥/杀菌";
                }
                return false;
            }
            if (plot->diseased) {
                if (items::CountOf(*container, config.fungicideItem) < 1) {
                    if (error != nullptr) {
                        *error = "杀菌需要 " + itemName(config.fungicideItem);
                    }
                    return false;
                }
                if (items::RemoveItem(*container, config.fungicideItem, 1) !=
                        1 ||
                    !game::CureFarmDisease(*plot)) {
                    if (error != nullptr) {
                        *error = "杀菌失败";
                    }
                    return false;
                }
                SetSessionStatus(
                    session,
                    "已杀菌：" + game::FarmStatusText(
                                      *plot, findCrop(plot->cropId),
                                      seasonName()),
                    1);
                return true;
            }
            if (plot->fertilized) {
                if (error != nullptr) {
                    *error = "该农田已经施过肥";
                }
                return false;
            }
            if (items::CountOf(*container, config.fertilizerItem) < 1) {
                if (error != nullptr) {
                    *error = "施肥需要 " + itemName(config.fertilizerItem);
                }
                return false;
            }
            if (items::RemoveItem(*container, config.fertilizerItem, 1) != 1 ||
                !game::FertilizeFarmPlot(*plot,
                                         config.fertilizerYieldBonus)) {
                if (error != nullptr) {
                    *error = "施肥失败";
                }
                return false;
            }
            SetSessionStatus(
                session,
                "施肥完成：" + game::FarmStatusText(
                                    *plot, findCrop(plot->cropId),
                                    seasonName()),
                1);
            return true;
        }
        if (farmAction == game::kFarmActionPlant &&
            plot->stage != ecs::CropStage::Empty) {
            if (error != nullptr) {
                *error = "农田已有作物";
            }
            return false;
        }
        if (plot->stage == ecs::CropStage::Empty) {
            const game::CropDef* selected = nullptr;
            if (farmAction == game::kFarmActionPlant) {
                if (m_crops == nullptr || command.payload.index < 0 ||
                    command.payload.index >=
                        static_cast<std::int32_t>(m_crops->All().size())) {
                    if (error != nullptr) {
                        *error = "作物选择非法";
                    }
                    return false;
                }
                selected = &m_crops->All()[
                    static_cast<std::size_t>(command.payload.index)];
            } else if (m_crops != nullptr && m_crops->Size() > 0) {
                for (const game::CropDef& crop : m_crops->All()) {
                    if (items::CountOf(*container, crop.seedItem) >= 1) {
                        selected = &crop;
                        break;
                    }
                }
                if (selected == nullptr) {
                    if (error != nullptr) {
                        *error = "农田需要可种植的种子";
                    }
                    return false;
                }
            }
            const std::string seedItem =
                selected != nullptr ? selected->seedItem : config.seedItem;
            if (items::CountOf(*container, seedItem) < 1 ||
                items::RemoveItem(*container, seedItem, 1) != 1) {
                if (error != nullptr) {
                    *error = "农田需要 " + itemName(seedItem);
                }
                return false;
            }
            const bool planted =
                selected != nullptr
                    ? game::PlantFarmPlot(*plot, *selected)
                    : game::PlantFarmPlot(*plot, "cabbage", config.growMinutes,
                                          config.harvestYield);
            if (!planted) {
                if (error != nullptr) {
                    *error = "种植失败";
                }
                return false;
            }
            const std::string cropName =
                selected != nullptr ? selected->name : "白菜";
            SetSessionStatus(session, "已种植" + cropName + "；下一步需要浇水",
                             1);
            return true;
        }
        if (plot->stage == ecs::CropStage::Growing) {
            if (plot->diseased) {
                plot->cropId.clear();
                plot->growthMinutes = 0.0f;
                plot->waterMinutes = 0.0f;
                plot->dryMinutes = 0.0f;
                plot->stage = ecs::CropStage::Empty;
                plot->diseased = false;
                plot->fertilized = false;
                SetSessionStatus(session, "清理了病害作物", 0);
                return true;
            }
            const game::CropDef* crop = findCrop(plot->cropId);
            const float waterMinutes =
                crop != nullptr ? crop->waterMinutes : config.waterMinutes;
            const float maxWaterMinutes =
                crop != nullptr ? crop->maxWaterMinutes
                                : config.maxWaterMinutes;
            if (items::CountOf(*container, config.waterItem) < 1) {
                if (error != nullptr) {
                    *error = "浇水需要 " + itemName(config.waterItem);
                }
                return false;
            }
            if (!game::WaterFarmPlot(*plot, waterMinutes, maxWaterMinutes) ||
                items::RemoveItem(*container, config.waterItem, 1) != 1) {
                if (error != nullptr) {
                    *error = "浇水失败";
                }
                return false;
            }
            std::string emptyBottle = "empty_bottle";
            if (const items::ItemDef* def =
                    m_items->Find(config.waterItem);
                def != nullptr && !def->use.replaceWith.empty()) {
                emptyBottle = def->use.replaceWith;
            }
            ReturnServerItem(player, ecs::ItemInstance{emptyBottle, 1, 1.0f},
                             playerPos->x, playerPos->y, playerPos->z);
            SetSessionStatus(
                session,
                "浇水完成：" + game::FarmStatusText(*plot, crop, seasonName()),
                1);
            return true;
        }
        if (plot->stage == ecs::CropStage::Mature) {
            const game::CropDef* crop = findCrop(plot->cropId);
            const std::string produceItem =
                crop != nullptr ? crop->produceItem : config.produceItem;
            const std::string seedItem =
                crop != nullptr ? crop->seedItem : config.seedItem;
            const int seedYield =
                crop != nullptr ? crop->seedYield : config.seedYield;
            ecs::Container probe = *container;
            ecs::ItemInstance produce{produceItem,
                                      std::max(1, plot->harvestYield), 1.0f};
            if (items::AddItem(probe, *m_items, produce) <= 0) {
                if (error != nullptr) {
                    *error = "背包空间不足，无法收获";
                }
                return false;
            }
            int harvested = 0;
            if (!game::HarvestFarmPlot(*plot, &harvested)) {
                return false;
            }
            ecs::ItemInstance output{produceItem, harvested, 1.0f};
            ReturnServerItem(player, output, playerPos->x, playerPos->y,
                             playerPos->z);
            if (seedYield > 0) {
                ReturnServerItem(player,
                                 ecs::ItemInstance{seedItem, seedYield, 1.0f},
                                 playerPos->x, playerPos->y, playerPos->z);
            }
            SetSessionStatus(session,
                             "收获 " + itemName(produceItem) + " ×" +
                                 std::to_string(harvested),
                             1);
            return true;
        }
        plot->cropId.clear();
        plot->growthMinutes = 0.0f;
        plot->waterMinutes = 0.0f;
        plot->dryMinutes = 0.0f;
        plot->stage = ecs::CropStage::Empty;
        plot->diseased = false;
        plot->fertilized = false;
        SetSessionStatus(session, "清理了枯萎作物", 0);
        return true;
    }

    if (auto* generator = registry.try_get<ecs::Generator>(facility);
        generator != nullptr && m_homesteadConfig.power.enabled) {
        if (generator->running) {
            generator->running = false;
            SetSessionStatus(session, game::GeneratorStatusText(*generator), 0);
            return true;
        }
        if (generator->fuelMinutes <= 0.0f) {
            if (items::CountOf(*container, m_homesteadConfig.power.fuelItem) <
                1) {
                if (error != nullptr) {
                    *error = "发电机需要 " +
                             itemName(m_homesteadConfig.power.fuelItem);
                }
                return false;
            }
            if (items::RemoveItem(
                    *container, m_homesteadConfig.power.fuelItem, 1) != 1) {
                return false;
            }
            generator->fuelMinutes +=
                m_homesteadConfig.power.fuelMinutesPerCan;
        }
        generator->running = true;
        SetSessionStatus(session, game::GeneratorStatusText(*generator), 1);
        return true;
    }

    if (auto* floodlight = registry.try_get<ecs::Floodlight>(facility);
        floodlight != nullptr && m_homesteadConfig.power.enabled) {
        floodlight->on = !floodlight->on;
        if (!floodlight->on) {
            floodlight->powered = false;
        }
        const bool night = m_gameTime != nullptr && m_gameTime->IsNight();
        SetSessionStatus(
            session,
            game::FloodlightStatusText(
                *floodlight, night,
                m_homesteadConfig.power.deviceRequiresNight),
            floodlight->on ? 1 : 0);
        return true;
    }

    if (auto* trap = registry.try_get<ecs::SpikeTrap>(facility);
        trap != nullptr && m_homesteadConfig.trap.enabled) {
        if (trap->uses <= 0) {
            if (error != nullptr) {
                *error = "陷阱已耗尽，需要拆除重建";
            }
            return false;
        }
        trap->armed = !trap->armed;
        SetSessionStatus(session, trap->armed ? "陷阱已布防" : "陷阱已解除",
                         trap->armed ? 1 : 0);
        return true;
    }

    if (auto* trap = registry.try_get<ecs::PlacedTrap>(facility);
        trap != nullptr && m_traps != nullptr && m_traps->enabled) {
        const game::TrapDef* trapDef = m_traps->Find(trap->defId);
        if (trapDef == nullptr) {
            if (error != nullptr) {
                *error = "陷阱数据不可用";
            }
            return false;
        }
        if (!game::TrapCanToggle(*trap)) {
            if (error != nullptr) {
                *error = "陷阱已耗尽，需要拆除重建";
            }
            return false;
        }
        const bool arm = !trap->armed;
        if (!game::SetPlacedTrapArmed(*trap, arm)) {
            if (error != nullptr) {
                *error = "陷阱无法布防";
            }
            return false;
        }
        SetSessionStatus(session, arm ? "陷阱已布防" : "陷阱已解除",
                         arm ? 1 : 0);
        ZL_INFO("traps", "服务器{}陷阱：{}（{}）玩家 {}", arm ? "布防" : "解除",
                trap->defId, command.payload.entityId, session.playerStableId);
        return true;
    }

    if (error != nullptr) {
        *error = "该设施当前不可交互";
    }
    return false;
}

bool ServerHost::PurchaseMerchantOffer(ecs::Entity player,
                                      ServerHost::SessionPlayer& session,
                                      const net::Command& command,
                                      std::string* error) {
    if (m_simulation == nullptr || m_items == nullptr) {
        if (error != nullptr) {
            *error = "服务器商人服务不可用";
        }
        return false;
    }
    if (!m_homesteadConfig.trade.enabled) {
        if (error != nullptr) {
            *error = "交易系统已关闭";
        }
        return false;
    }
    if (command.payload.entityId == 0 || command.payload.flags != 0) {
        if (error != nullptr) {
            *error = "商人交易命令字段非法";
        }
        return false;
    }
    if (command.payload.index < 0 ||
        command.payload.index >= static_cast<int>(game::kMerchantOfferCount)) {
        if (error != nullptr) {
            *error = "商人报价索引非法";
        }
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity merchant =
        m_simulation->Ecs().FindById(command.payload.entityId);
    const auto* merchantPosition =
        merchant != entt::null ? registry.try_get<ecs::Position>(merchant)
                               : nullptr;
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    auto* stock =
        merchant != entt::null
            ? registry.try_get<ecs::MerchantStock>(merchant)
            : nullptr;
    auto* container = registry.try_get<ecs::Container>(player);
    if (merchant == entt::null || !m_simulation->Ecs().Alive(merchant) ||
        merchantPosition == nullptr || playerPosition == nullptr ||
        playerPosition->z != merchantPosition->z || stock == nullptr ||
        container == nullptr ||
        static_cast<std::size_t>(command.payload.index) >= stock->stock.size()) {
        if (error != nullptr) {
            *error = "商人目标不存在";
        }
        return false;
    }
    const game::SettlementNpc* npc =
        registry.try_get<game::SettlementNpc>(merchant);
    if (npc != nullptr &&
        npc->kind != game::SettlementNpcKind::Merchant) {
        if (error != nullptr) {
            *error = "目标 NPC 不是商人";
        }
        return false;
    }
    const game::SettlementDef* settlement =
        npc != nullptr && m_settlements != nullptr
            ? m_settlements->Find(npc->settlementId)
            : nullptr;
    if (npc != nullptr && settlement == nullptr) {
        if (error != nullptr) {
            *error = "商人所属聚落数据不存在";
        }
        return false;
    }
    const float dx = merchantPosition->x - playerPosition->x;
    const float dy = merchantPosition->y - playerPosition->y;
    if (dx * dx + dy * dy >
        m_homesteadConfig.trade.range * m_homesteadConfig.trade.range) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "商人超出交易范围";
        }
        return false;
    }

    if (settlement != nullptr) {
        if (game::SettlementNpcSystem* system =
                m_simulation->SettlementNpcs();
            system != nullptr &&
            (system->IsFallen(settlement->id) ||
             system->RaidActive(settlement->id))) {
            if (error != nullptr) {
                *error = "聚落当前无法交易";
            }
            return false;
        }
    }

    const auto& offers = game::MerchantOffers();
    const game::TradeOffer& offer =
        offers[static_cast<std::size_t>(command.payload.index)];
    const std::size_t stockIndex =
        static_cast<std::size_t>(command.payload.index);
    int cost = offer.costCount;
    if (settlement != nullptr) {
        const int score = game::SettlementReputationScoreFor(
            m_simulation->Ecs(), player, settlement->id,
            settlement->startingReputation);
        if (!game::SettlementCanUseService(
                *settlement, m_settlements->reputation,
                game::SettlementService::Trade, score)) {
            if (error != nullptr) {
                *error =
                    game::HasSettlementService(*settlement,
                                               game::SettlementService::Trade)
                        ? "声望敌视：商人拒绝交易"
                        : "该聚落不提供交易";
            }
            return false;
        }
        cost = game::SettlementBuyPrice(
            offer.costCount, m_settlements->reputation, score);
    }
    if (stock->stock[stockIndex] <= 0) {
        if (error != nullptr) {
            *error = "商人库存不足";
        }
        return false;
    }

    ecs::KnownRecipes* known =
        registry.try_get<ecs::KnownRecipes>(player);
    if (!offer.blueprint.empty() && known != nullptr) {
        const auto it = std::find(known->recipeIds.begin(),
                                  known->recipeIds.end(),
                                  std::string(offer.blueprint));
        if (it != known->recipeIds.end()) {
            if (error != nullptr) {
                *error = "已经学会该图纸";
            }
            return false;
        }
    }

    ecs::Container next = *container;
    if (items::RemoveItem(next, offer.costDefId, cost) != cost) {
        if (error != nullptr) {
            *error = "物资不足：需要 " + std::string(offer.costDefId) +
                     " ×" + std::to_string(cost);
        }
        return false;
    }
    if (!offer.rewardDefId.empty()) {
        ecs::ItemInstance reward{std::string(offer.rewardDefId),
                                 offer.rewardCount, 1.0f};
        if (items::AddItem(next, *m_items, reward) < offer.rewardCount) {
            if (error != nullptr) {
                *error = "背包空间不足";
            }
            return false;
        }
    }

    *container = std::move(next);
    if (!offer.blueprint.empty()) {
        if (known == nullptr) {
            known = &registry.emplace<ecs::KnownRecipes>(player);
        }
        known->recipeIds.emplace_back(offer.blueprint);
        std::sort(known->recipeIds.begin(), known->recipeIds.end());
        known->recipeIds.erase(
            std::unique(known->recipeIds.begin(), known->recipeIds.end()),
            known->recipeIds.end());
    }
    --stock->stock[stockIndex];
    std::string message = "交易完成：" + std::string(offer.name);
    if (!offer.blueprint.empty()) {
        message += "（已学习图纸）";
    }
    if (settlement != nullptr) {
        const int day =
            m_gameTime != nullptr
                ? 1 + static_cast<int>(
                          m_gameTime->TotalGameMinutes() / (24.0 * 60.0))
                : 1;
        const game::SettlementReputationChange change =
            game::AwardSettlementReputation(
                m_simulation->Ecs(), player, m_settlements->reputation,
                settlement->id, game::SettlementReputationEvent::Trade, {},
                settlement->startingReputation, day);
        if (change.applied && change.delta != 0) {
            message += " · 声望 " + std::to_string(change.delta);
        }
    }
    SetSessionStatus(session, std::move(message), 1);
    ZL_INFO("homestead", "服务器商人交易：{}，库存 {}", offer.id,
            stock->stock[stockIndex]);
    return true;
}

bool ServerHost::AcceptSettlementMission(ecs::Entity player,
                                         ServerHost::SessionPlayer& session,
                                         const net::Command& command,
                                         std::string* error) {
    if (m_simulation == nullptr || m_settlements == nullptr ||
        m_missionConfig == nullptr || !m_missionConfig->enabled) {
        if (error != nullptr) {
            *error = "服务器聚落委托不可用";
        }
        return false;
    }
    if (command.payload.entityId == 0 || command.payload.flags != 0) {
        if (error != nullptr) {
            *error = "聚落委托命令字段非法";
        }
        return false;
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity npc =
        m_simulation->Ecs().FindById(command.payload.entityId);
    const auto* component =
        npc != entt::null ? registry.try_get<game::SettlementNpc>(npc)
                          : nullptr;
    const auto* npcPosition =
        npc != entt::null ? registry.try_get<ecs::Position>(npc) : nullptr;
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    if (npc == entt::null || !m_simulation->Ecs().Alive(npc) ||
        component == nullptr ||
        component->kind != game::SettlementNpcKind::Requester ||
        npcPosition == nullptr || playerPosition == nullptr ||
        playerPosition->z != npcPosition->z) {
        if (error != nullptr) {
            *error = "求助者不存在";
        }
        return false;
    }
    const float dx = npcPosition->x - playerPosition->x;
    const float dy = npcPosition->y - playerPosition->y;
    const float range = m_buildActionRange + 1.0f;
    if (dx * dx + dy * dy > range * range) {
        ++m_networkStats.rangeRejected;
        if (error != nullptr) {
            *error = "求助者超出委托范围";
        }
        return false;
    }
    const game::SettlementDef* settlement =
        m_settlements->Find(component->settlementId);
    if (settlement == nullptr) {
        if (error != nullptr) {
            *error = "聚落数据不存在";
        }
        return false;
    }
    if (game::SettlementNpcSystem* system = m_simulation->SettlementNpcs();
        system != nullptr &&
        (system->IsFallen(settlement->id) ||
         system->RaidActive(settlement->id))) {
        if (error != nullptr) {
            *error = "聚落当前无法接取委托";
        }
        return false;
    }

    const int score = game::SettlementReputationScoreFor(
        m_simulation->Ecs(), player, settlement->id,
        settlement->startingReputation);
    if (!game::SettlementCanUseService(
            *settlement, m_settlements->reputation,
            game::SettlementService::Missions, score)) {
        if (error != nullptr) {
            *error =
                game::HasSettlementService(*settlement,
                                           game::SettlementService::Missions)
                    ? "声望敌视：拒绝委托"
                    : "该聚落不提供委托";
        }
        return false;
    }
    auto* log = registry.try_get<ecs::MissionLog>(player);
    if (log == nullptr) {
        log = &registry.emplace<ecs::MissionLog>(player);
    }
    const int day =
        m_gameTime != nullptr
            ? 1 + static_cast<int>(
                      m_gameTime->TotalGameMinutes() / (24.0 * 60.0))
            : 1;
    const game::MissionDef* def = game::FindAvailableSettlementMission(
        *settlement, *m_missionConfig, *log, day, score);
    if (def == nullptr) {
        if (error != nullptr) {
            *error = "当前没有可接取的委托";
        }
        return false;
    }
    ActivateServerMission(session, *def, day, settlement->id);
    SetSessionStatus(session, "已接取委托：" + def->name, 1);
    ZL_INFO("missions", "服务器聚落委托接取：{}（{}），玩家 {}", def->name,
            def->id, session.playerStableId);
    return true;
}

bool ServerHost::RecruitCompanion(ecs::Entity player, SessionPlayer& session,
                                  const net::Command& command,
                                  std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_companions == nullptr ||
        !m_companions->enabled || m_settlements == nullptr) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.entityId == 0 || command.payload.flags != 0) {
        return fail("同伴招募命令字段非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity npc =
        m_simulation->Ecs().FindById(command.payload.entityId);
    const auto* component =
        npc != entt::null ? registry.try_get<game::SettlementNpc>(npc)
                          : nullptr;
    const auto* npcPosition =
        npc != entt::null ? registry.try_get<ecs::Position>(npc) : nullptr;
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    if (npc == entt::null || !m_simulation->Ecs().Alive(npc) ||
        component == nullptr ||
        component->kind != game::SettlementNpcKind::Requester ||
        npcPosition == nullptr || playerPosition == nullptr ||
        playerPosition->z != npcPosition->z) {
        return fail("求助者不存在");
    }
    const float dx = npcPosition->x - playerPosition->x;
    const float dy = npcPosition->y - playerPosition->y;
    const float range = m_buildActionRange + 1.0f;
    if (dx * dx + dy * dy > range * range) {
        ++m_networkStats.rangeRejected;
        return fail("求助者超出招募范围");
    }
    const game::SettlementDef* settlement =
        m_settlements->Find(component->settlementId);
    if (settlement == nullptr) {
        return fail("聚落数据不存在");
    }
    if (game::SettlementNpcSystem* system = m_simulation->SettlementNpcs();
        system != nullptr &&
        (system->IsFallen(settlement->id) ||
         system->RaidActive(settlement->id))) {
        return fail("聚落当前无法招募");
    }

    const std::uint64_t ownerStableId = session.playerStableId;
    const ecs::Entity existing =
        game::FindOwnedCompanion(m_simulation->Ecs(), ownerStableId);
    if (existing != entt::null) {
        auto* companion = registry.try_get<game::Companion>(existing);
        auto* state = registry.try_get<game::CompanionState>(existing);
        if (companion == nullptr || state == nullptr ||
            !m_simulation->Ecs().Alive(existing)) {
            return fail("同伴状态无效");
        }
        if (state->dead) {
            return fail("同伴已死亡，无法再次招募");
        }
        if (!state->standby) {
            return fail("你已经有一名活动同伴");
        }
        if (companion->homeSettlementId != settlement->id) {
            return fail("同伴在其他聚落待命");
        }
        if (game::SettlementNpcSystem* system =
                m_simulation->SettlementNpcs();
            system != nullptr &&
            (system->IsFallen(settlement->id) ||
             system->RaidActive(settlement->id))) {
            return fail("聚落当前无法召回同伴");
        }
        const int score = game::SettlementReputationScoreFor(
            m_simulation->Ecs(), player, settlement->id,
            settlement->startingReputation);
        if (!game::SettlementCanUseService(
                *settlement, m_settlements->reputation,
                game::SettlementService::Recruit, score)) {
            return fail("当前声望不允许召回同伴");
        }
        if (!m_simulation->TeleportCompanion(existing,
                                             playerPosition->x + 0.8f,
                                             playerPosition->y)) {
            return fail("同伴归队位置不可用");
        }
        state->standby = false;
        state->command = game::CompanionCommand::Follow;
        const game::CompanionDef* def =
            m_companions->Find(companion->defId);
        const std::string name = def != nullptr ? def->name : companion->defId;
        SetSessionStatus(session, "同伴已归队：" + name, 1);
        ZL_INFO("companions", "服务器同伴归队：{}，玩家 {}", companion->defId,
                ownerStableId);
        return true;
    }

    const int score = game::SettlementReputationScoreFor(
        m_simulation->Ecs(), player, settlement->id,
        settlement->startingReputation);
    const auto* log = registry.try_get<ecs::MissionLog>(player);
    const auto* container = registry.try_get<ecs::Container>(player);
    const game::CompanionRecruitCheck check =
        game::EvaluateCompanionRecruitment(
            *m_companions, *settlement, m_settlements->reputation, score, log,
            container, false);
    if (check.status != game::CompanionRecruitStatus::Ready ||
        check.def == nullptr) {
        std::string message = game::CompanionRecruitStatusText(check.status);
        if (check.status == game::CompanionRecruitStatus::MissionRequired &&
            !check.def->recruit.requiredMission.empty()) {
            message += "：" + check.def->recruit.requiredMission;
        } else if (check.status ==
                       game::CompanionRecruitStatus::ItemRequired &&
                   !check.def->recruit.requiredItem.empty()) {
            message += "：" + check.def->recruit.requiredItem + " ×" +
                       std::to_string(check.def->recruit.requiredItemCount) +
                       "（当前 " + std::to_string(check.itemCount) + "）";
        }
        return fail(message);
    }
    if (container == nullptr) {
        return fail("同伴招募需要玩家背包");
    }
    ecs::Container next = *container;
    if (!game::ConsumeCompanionRecruitItems(&next, *check.def)) {
        return fail("招募物资扣除失败");
    }
    const ecs::Entity companion = m_simulation->SpawnCompanionAt(
        *check.def, ownerStableId, settlement->id, npcPosition->x + 1.0f,
        npcPosition->y);
    if (companion == entt::null) {
        return fail("同伴生成位置不可用");
    }
    auto* inventory = registry.try_get<ecs::Container>(player);
    if (inventory == nullptr) {
        return fail("同伴招募需要玩家背包");
    }
    *inventory = std::move(next);
    SetSessionStatus(session, "已招募同伴：" + check.def->name, 1);
    ZL_INFO("companions", "服务器同伴招募：{}，玩家 {}", check.def->id,
            ownerStableId);
    return true;
}

bool ServerHost::DismissCompanion(ecs::Entity player, SessionPlayer& session,
                                  const net::Command& command,
                                  std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_companions == nullptr ||
        !m_companions->enabled || m_settlements == nullptr) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.entityId == 0 || command.payload.flags != 0) {
        return fail("同伴解散命令字段非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity npc =
        m_simulation->Ecs().FindById(command.payload.entityId);
    const auto* component =
        npc != entt::null ? registry.try_get<game::SettlementNpc>(npc)
                          : nullptr;
    const auto* npcPosition =
        npc != entt::null ? registry.try_get<ecs::Position>(npc) : nullptr;
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    if (npc == entt::null || !m_simulation->Ecs().Alive(npc) ||
        component == nullptr ||
        component->kind != game::SettlementNpcKind::Requester ||
        npcPosition == nullptr || playerPosition == nullptr ||
        playerPosition->z != npcPosition->z) {
        return fail("求助者不存在");
    }
    const float dx = npcPosition->x - playerPosition->x;
    const float dy = npcPosition->y - playerPosition->y;
    const float range = m_buildActionRange + 1.0f;
    if (dx * dx + dy * dy > range * range) {
        ++m_networkStats.rangeRejected;
        return fail("求助者超出解散范围");
    }
    const game::SettlementDef* settlement =
        m_settlements->Find(component->settlementId);
    if (settlement == nullptr) {
        return fail("聚落数据不存在");
    }
    const ecs::Entity companion =
        game::FindOwnedCompanion(m_simulation->Ecs(),
                                 session.playerStableId);
    auto* identity =
        companion != entt::null
            ? registry.try_get<game::Companion>(companion)
            : nullptr;
    auto* state =
        companion != entt::null
            ? registry.try_get<game::CompanionState>(companion)
            : nullptr;
    if (companion == entt::null || identity == nullptr || state == nullptr) {
        return fail("当前没有可解散的同伴");
    }
    if (identity->homeSettlementId != settlement->id) {
        return fail("只能让同伴回到所属聚落待命");
    }
    if (state->dead) {
        return fail("同伴已死亡");
    }
    if (state->downed) {
        return fail("同伴已倒地，需先救援");
    }
    if (state->standby) {
        return fail("同伴已经在聚落待命");
    }
    if (!m_simulation->TeleportCompanion(companion, settlement->centerX,
                                           settlement->centerY)) {
        return fail("聚落待命位置不可用");
    }
    state->standby = true;
    state->command = game::CompanionCommand::Stay;
    const game::CompanionDef* def = m_companions->Find(identity->defId);
    const std::string name = def != nullptr ? def->name : identity->defId;
    SetSessionStatus(session, "同伴已回聚落待命：" + name, 1);
    ZL_INFO("companions", "服务器同伴解散：{}，玩家 {}", identity->defId,
            session.playerStableId);
    return true;
}

bool ServerHost::SetCompanionCommandAction(ecs::Entity player,
                                           SessionPlayer& session,
                                           const net::Command& command,
                                           std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_companions == nullptr ||
        !m_companions->enabled) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.flags > static_cast<std::uint16_t>(
                                      game::CompanionCommand::Retreat) ||
        command.payload.entityId != 0 || command.payload.index != -1 ||
        command.payload.targetEntityId != 0) {
        return fail("同伴指令字段非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity companion = game::FindOwnedCompanion(
        m_simulation->Ecs(), session.playerStableId);
    auto* identity = companion != entt::null
                         ? registry.try_get<game::Companion>(companion)
                         : nullptr;
    auto* state = companion != entt::null
                      ? registry.try_get<game::CompanionState>(companion)
                      : nullptr;
    if (companion == entt::null || identity == nullptr || state == nullptr ||
        !m_simulation->Ecs().Alive(companion)) {
        return fail("当前没有同伴");
    }
    if (state->dead) {
        return fail("同伴已死亡");
    }
    if (state->downed) {
        return fail("同伴已倒地，需先救援");
    }
    if (state->standby) {
        return fail("同伴在聚落待命，需先召回");
    }
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    const auto* companionPosition = registry.try_get<ecs::Position>(companion);
    if (playerPosition == nullptr || companionPosition == nullptr ||
        playerPosition->z != companionPosition->z) {
        return fail("同伴不在同一层");
    }
    const float dx = playerPosition->x - companionPosition->x;
    const float dy = playerPosition->y - companionPosition->y;
    constexpr float kCommandRange = 40.0f;
    if (dx * dx + dy * dy > kCommandRange * kCommandRange) {
        return fail("同伴超出指令范围");
    }
    const auto next = static_cast<game::CompanionCommand>(command.payload.flags);
    state->command = next;
    const game::CompanionDef* def = m_companions->Find(identity->defId);
    const std::string name = def != nullptr ? def->name : identity->defId;
    SetSessionStatus(session,
                     "同伴指令：" + name + " · " +
                         game::CompanionCommandName(next),
                     1);
    ZL_INFO("companions", "服务器同伴指令：{} {}，玩家 {}", identity->defId,
            game::CompanionCommandName(next), session.playerStableId);
    return true;
}

bool ServerHost::GiftToCompanionAction(ecs::Entity player,
                                       SessionPlayer& session,
                                       const net::Command& command,
                                       std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_companions == nullptr ||
        !m_companions->enabled || m_items == nullptr) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.flags != 0 || command.payload.entityId != 0 ||
        command.payload.index < 0 || command.payload.targetEntityId != 0) {
        return fail("同伴赠礼字段非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity companion = game::FindOwnedCompanion(
        m_simulation->Ecs(), session.playerStableId);
    auto* identity = companion != entt::null
                         ? registry.try_get<game::Companion>(companion)
                         : nullptr;
    auto* state = companion != entt::null
                      ? registry.try_get<game::CompanionState>(companion)
                      : nullptr;
    auto* donor = registry.try_get<ecs::Container>(player);
    if (companion == entt::null || identity == nullptr || state == nullptr ||
        donor == nullptr || !m_simulation->Ecs().Alive(companion)) {
        return fail("当前没有同伴");
    }
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    const auto* companionPosition = registry.try_get<ecs::Position>(companion);
    if (playerPosition == nullptr || companionPosition == nullptr ||
        playerPosition->z != companionPosition->z) {
        return fail("同伴不在同一层");
    }
    const float dx = playerPosition->x - companionPosition->x;
    const float dy = playerPosition->y - companionPosition->y;
    constexpr float kGiftRange = 3.0f;
    if (dx * dx + dy * dy > kGiftRange * kGiftRange) {
        ++m_networkStats.rangeRejected;
        return fail("需要靠近同伴才能赠礼");
    }
    if (static_cast<std::size_t>(command.payload.index) >= donor->items.size()) {
        return fail("快捷栏物品不存在");
    }
    const game::CompanionDef* def = m_companions->Find(identity->defId);
    if (def == nullptr) {
        return fail("同伴定义不存在");
    }
    std::string giftError;
    if (!game::GiftToCompanion(
            m_simulation->Ecs(), companion, *def, *m_items, donor,
            static_cast<std::size_t>(command.payload.index), 1, &giftError)) {
        return fail(giftError.empty() ? "赠礼失败" : giftError);
    }
    SetSessionStatus(session, "已赠礼：" + def->name, 1);
    ZL_INFO("companions", "服务器同伴赠礼：{}，玩家 {}", identity->defId,
            session.playerStableId);
    return true;
}

bool ServerHost::AcceptCompanionQuestAction(ecs::Entity player,
                                            SessionPlayer& session,
                                            const net::Command& command,
                                            std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_companions == nullptr ||
        !m_companions->enabled) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.flags != 0 || command.payload.entityId != 0 ||
        command.payload.index != -1 || command.payload.targetEntityId != 0) {
        return fail("同伴委托字段非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity companion = game::FindOwnedCompanion(
        m_simulation->Ecs(), session.playerStableId);
    auto* identity = companion != entt::null
                         ? registry.try_get<game::Companion>(companion)
                         : nullptr;
    auto* state = companion != entt::null
                      ? registry.try_get<game::CompanionState>(companion)
                      : nullptr;
    if (companion == entt::null || identity == nullptr || state == nullptr ||
        !m_simulation->Ecs().Alive(companion)) {
        return fail("当前没有同伴");
    }
    if (state->dead) {
        return fail("同伴已死亡");
    }
    if (state->downed) {
        return fail("同伴已倒地，需先救援");
    }
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    const auto* companionPosition = registry.try_get<ecs::Position>(companion);
    if (playerPosition == nullptr || companionPosition == nullptr ||
        playerPosition->z != companionPosition->z) {
        return fail("同伴不在同一层");
    }
    const float dx = playerPosition->x - companionPosition->x;
    const float dy = playerPosition->y - companionPosition->y;
    constexpr float kQuestRange = 4.0f;
    if (dx * dx + dy * dy > kQuestRange * kQuestRange) {
        return fail("需要靠近同伴才能接取个人委托");
    }
    const game::CompanionDef* def = m_companions->Find(identity->defId);
    if (def == nullptr) {
        return fail("同伴定义不存在");
    }
    std::string questError;
    if (!game::StartCompanionQuest(*def, state, &questError)) {
        return fail(questError.empty() ? "当前无法接取个人委托" : questError);
    }
    SetSessionStatus(session, "已接取同伴个人委托", 1);
    ZL_INFO("companions", "服务器同伴委托接取：{}，玩家 {}", identity->defId,
            session.playerStableId);
    return true;
}

bool ServerHost::AbandonCompanionQuestAction(ecs::Entity player,
                                             SessionPlayer& session,
                                             const net::Command& command,
                                             std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr || m_companions == nullptr ||
        !m_companions->enabled) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.flags != 0 || command.payload.entityId != 0 ||
        command.payload.index != -1 || command.payload.targetEntityId != 0) {
        return fail("同伴委托字段非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const ecs::Entity companion = game::FindOwnedCompanion(
        m_simulation->Ecs(), session.playerStableId);
    auto* identity = companion != entt::null
                         ? registry.try_get<game::Companion>(companion)
                         : nullptr;
    auto* state = companion != entt::null
                      ? registry.try_get<game::CompanionState>(companion)
                      : nullptr;
    if (companion == entt::null || identity == nullptr || state == nullptr ||
        !m_simulation->Ecs().Alive(companion)) {
        return fail("当前没有同伴");
    }
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    const auto* companionPosition = registry.try_get<ecs::Position>(companion);
    if (playerPosition == nullptr || companionPosition == nullptr ||
        playerPosition->z != companionPosition->z) {
        return fail("同伴不在同一层");
    }
    const float dx = playerPosition->x - companionPosition->x;
    const float dy = playerPosition->y - companionPosition->y;
    constexpr float kQuestRange = 4.0f;
    if (dx * dx + dy * dy > kQuestRange * kQuestRange) {
        return fail("需要靠近同伴才能放弃个人委托");
    }
    const game::CompanionDef* def = m_companions->Find(identity->defId);
    if (def == nullptr) {
        return fail("同伴定义不存在");
    }
    std::string questError;
    if (!game::FailCompanionQuest(*def, state, &questError)) {
        return fail(questError.empty() ? "当前没有可放弃的个人委托"
                                       : questError);
    }
    SetSessionStatus(session, "已放一放同伴个人委托", 0);
    ZL_INFO("companions", "服务器同伴委托放弃：{}，玩家 {}", identity->defId,
            session.playerStableId);
    return true;
}

bool ServerHost::RescueCompanionAction(ecs::Entity player,
                                       SessionPlayer& session,
                                       const net::Command& command,
                                       std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (m_simulation == nullptr) {
        return fail("服务器同伴系统不可用");
    }
    if (command.payload.flags != 0 || command.payload.entityId != 0 ||
        command.payload.index != -1 || command.payload.targetEntityId != 0) {
        return fail("同伴救援字段非法");
    }
    const ecs::Entity companion = game::FindOwnedCompanion(
        m_simulation->Ecs(), session.playerStableId);
    if (companion == entt::null || !m_simulation->Ecs().Alive(companion)) {
        return fail("当前没有同伴");
    }
    std::string rescueError;
    if (!m_simulation->RescueCompanion(companion, player, &rescueError)) {
        return fail(rescueError.empty() ? "救援失败" : rescueError);
    }
    SetSessionStatus(session, "已救起同伴", 1);
    ZL_INFO("companions", "服务器同伴救援：玩家 {}", session.playerStableId);
    return true;
}

bool ServerHost::ExecuteSessionCommand(std::uint64_t sessionId, SessionPlayer& session,
                                       const net::Command& command,
                                       std::string* error) {
    static_cast<void>(sessionId);  // 仅 ZL_DEBUG 诊断日志使用
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    auto applyInventory = [&](const game::InventoryResult& result) {
        SetSessionStatus(session, result.message,
                         static_cast<std::uint8_t>(result.tone));
        return result.ok ? true : fail(result.message);
    };
    auto applyBuild = [&](bool ok) {
        if (!ok) {
            SetSessionStatus(session,
                             error != nullptr && !error->empty()
                                 ? *error
                                 : "建造命令被拒绝",
                             2);
        }
        return ok;
    };
    if (m_simulation == nullptr || session.playerStableId == 0) {
        return fail("会话玩家尚未生成");
    }
    if (session.dead) {
        return fail("玩家已死亡，等待复活");
    }
    if (m_inventory == nullptr) {
        return fail("服务器背包服务不可用");
    }
    if (command.playerStableId != session.playerStableId) {
        ++m_networkStats.ownershipRejected;
        return fail("命令 playerStableId 与会话不匹配");
    }
    const ecs::Entity player =
        m_simulation->Ecs().FindById(session.playerStableId);
    if (player == entt::null) {
        return fail("权威玩家实体不存在");
    }
    if (!std::isfinite(command.payload.x) || !std::isfinite(command.payload.y)) {
        return fail("命令 payload 浮点非法");
    }
    entt::registry& registry = m_simulation->Ecs().Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(player);
    if (playerPos == nullptr) {
        return fail("玩家缺少位置");
    }
    const bool sleepCommand =
        command.type == net::CommandType::SleepRequest ||
        command.type == net::CommandType::SleepVote ||
        command.type == net::CommandType::SleepCancel;
    if (m_serverSleep.phase != ServerSleepPhase::None && !sleepCommand &&
        m_serverSleep.eligible.contains(session.playerStableId)) {
        SetSessionStatus(session, "睡眠投票或睡眠中无法行动", 2);
        return fail("睡眠投票或睡眠中无法行动");
    }

    if (session.drivingVehicleId != 0) {
        ServerVehicleRuntime* vehicle =
            FindServerVehicleForSession(session);
        const bool isDriver =
            vehicle != nullptr &&
            vehicle->driverStableId == session.playerStableId;
        const bool isPassenger =
            vehicle != nullptr &&
            IsServerVehiclePassengerStableId(*vehicle,
                                             session.playerStableId);
        const bool vehicleValid =
            vehicle != nullptr && vehicle->entity != entt::null &&
            (isDriver || isPassenger) &&
            m_simulation->Ecs().Alive(vehicle->entity);
        if (!vehicleValid) {
            session.drivingVehicleId = 0;
            session.stateDirty = true;
            SetSessionStatus(session, "载具状态已失效，已恢复步行", 2);
            return fail("载具不存在或座位已释放");
        }
        const bool allowed =
            command.type == net::CommandType::ExitVehicle ||
            command.type == net::CommandType::SelectHotbar ||
            (isPassenger &&
             (command.type == net::CommandType::TakeDriverSeat ||
              command.type == net::CommandType::InventoryAction ||
              command.type == net::CommandType::UseBag ||
              command.type == net::CommandType::EquipBag)) ||
            (isDriver &&
             (command.type == net::CommandType::Move ||
              command.type == net::CommandType::ToggleHeadlights ||
              command.type == net::CommandType::HonkHorn ||
              command.type == net::CommandType::GiveDriverSeat));
        if (!allowed) {
            ++m_networkStats.unsupportedCommands;
            SetSessionStatus(session,
                             isDriver ? "驾驶中无法执行该操作"
                                      : "乘客中无法执行该操作",
                             2);
            return fail(isDriver
                            ? "驾驶中只允许移动、下车或聊天/菜单操作"
                            : "乘客中只允许下车、背包/快捷使用或聊天/菜单操作");
        }
    }

    switch (command.type) {
        case net::CommandType::Move: {
            float x = command.payload.x;
            float y = command.payload.y;
            const float length = std::sqrt(x * x + y * y);
            if (length > 1.0f) {
                x /= length;
                y /= length;
            }
            ServerVehicleRuntime* driven =
                FindServerVehicleForSession(session);
            if (driven != nullptr &&
                driven->driverStableId == session.playerStableId) {
                // Move payload 与步行一致是屏幕方向；车辆权威输入必须转世界格空间。
                const glm::vec2 grid =
                    characters::InputSystem::ScreenDirToGridDir(
                        glm::vec2(x, y));
                const float gridLength =
                    std::sqrt(grid.x * grid.x + grid.y * grid.y);
                if (gridLength > 1.0e-4f) {
                    driven->inputX = grid.x / gridLength;
                    driven->inputY = grid.y / gridLength;
                } else {
                    driven->inputX = 0.0f;
                    driven->inputY = 0.0f;
                }
                return true;
            }
            if (session.animalHarvest.active &&
                (std::abs(x) > 0.01f || std::abs(y) > 0.01f)) {
                CancelServerAnimalHarvest(session, "移动中断", 0);
            }
            auto* move = registry.try_get<ecs::MoveInput>(player);
            if (move == nullptr) {
                move = &registry.emplace<ecs::MoveInput>(player);
            }
            move->x = x;
            move->y = y;
            return true;
        }
        case net::CommandType::EnterVehicle: {
            if (!EnterServerVehicle(session, command, error)) {
                SetSessionStatus(
                    session,
                    error != nullptr && !error->empty() ? *error : "上车被拒绝",
                    2);
                return false;
            }
            if (auto* stance = registry.try_get<ecs::CombatStance>(player);
                stance != nullptr) {
                stance->blocking = false;
                stance->blockElapsed = 0.0f;
                stance->dodgeLeft = 0.0f;
                stance->dodgeInvulnLeft = 0.0f;
                stance->dodgeDirX = 0.0f;
                stance->dodgeDirY = 0.0f;
            }
            session.stateDirty = true;
            return true;
        }
        case net::CommandType::EnterVehiclePassenger: {
            if (!EnterServerVehiclePassenger(session, command, error)) {
                SetSessionStatus(
                    session,
                    error != nullptr && !error->empty() ? *error
                                                       : "上乘客位被拒绝",
                    2);
                return false;
            }
            if (auto* stance = registry.try_get<ecs::CombatStance>(player);
                stance != nullptr) {
                stance->blocking = false;
                stance->blockElapsed = 0.0f;
                stance->dodgeLeft = 0.0f;
                stance->dodgeInvulnLeft = 0.0f;
                stance->dodgeDirX = 0.0f;
                stance->dodgeDirY = 0.0f;
            }
            session.stateDirty = true;
            return true;
        }
        case net::CommandType::ExitVehicle: {
            if (!ExitServerVehicle(session, error)) {
                SetSessionStatus(
                    session,
                    error != nullptr && !error->empty() ? *error : "下车被拒绝",
                    2);
                return false;
            }
            return true;
        }
        case net::CommandType::TakeDriverSeat: {
            if (!TakeServerVehicleDriverSeat(session, error)) {
                SetSessionStatus(
                    session,
                    error != nullptr && !error->empty() ? *error
                                                       : "接管驾驶位被拒绝",
                    2);
                return false;
            }
            return true;
        }
        case net::CommandType::GiveDriverSeat: {
            if (!GiveServerVehicleDriverSeat(session, error)) {
                SetSessionStatus(
                    session,
                    error != nullptr && !error->empty() ? *error : "让座被拒绝",
                    2);
                return false;
            }
            return true;
        }
        case net::CommandType::ServiceVehicle:
            return ServiceServerVehicle(session, command, error);
        case net::CommandType::InstallVehicleMod:
            return ModifyServerVehicleMod(session, command, true, error);
        case net::CommandType::RemoveVehicleMod:
            return ModifyServerVehicleMod(session, command, false, error);
        case net::CommandType::ToggleHeadlights:
            return ToggleServerVehicleHeadlights(session, error);
        case net::CommandType::HonkHorn:
            return HonkServerVehicleHorn(session, error);
        case net::CommandType::MoveTo: {
            if (session.animalHarvest.active) {
                CancelServerAnimalHarvest(session, "移动中断", 0);
            }
            const float dx = command.payload.x - playerPos->x;
            const float dy = command.payload.y - playerPos->y;
            if (dx * dx + dy * dy >
                m_maxMoveToDistance * m_maxMoveToDistance) {
                ++m_networkStats.rangeRejected;
                return fail("MoveTo 目标超出服务器允许距离");
            }
            auto* request = registry.try_get<ecs::PathRequest>(player);
            if (request == nullptr) {
                request = &registry.emplace<ecs::PathRequest>(player);
            }
            request->goalX = static_cast<int>(std::lround(command.payload.x));
            request->goalY = static_cast<int>(std::lround(command.payload.y));
            request->newRequest = true;
            return true;
        }
        case net::CommandType::Attack: {
            if (session.survival.mode != net::kPlayerSurvivalModeNone) {
                ClearServerSurvival(session, "采集/钓鱼中断", 0);
                return true;
            }
            if (session.animalHarvest.active) {
                CancelServerAnimalHarvest(session, "攻击中断", 0);
                return true;
            }
            auto* attack = registry.try_get<ecs::AttackIntent>(player);
            if (attack == nullptr) {
                return fail("玩家缺少攻击组件");
            }
            if (attack->pending) {
                ++m_networkStats.cooldownRejected;
                return fail("攻击动作尚未结束");
            }
            if (const auto* hit = registry.try_get<ecs::HitReaction>(player);
                hit != nullptr && std::isfinite(hit->staggerLeft) &&
                hit->staggerLeft > 0.0f) {
                return fail("硬直中无法攻击");
            }
            if (const auto* stance = registry.try_get<ecs::CombatStance>(player);
                stance != nullptr &&
                (stance->blocking ||
                 (std::isfinite(stance->guardBreakLeft) &&
                  stance->guardBreakLeft > 0.0f) ||
                 (std::isfinite(stance->dodgeLeft) &&
                  stance->dodgeLeft > 0.0f))) {
                return fail("姿态锁定中无法攻击");
            }
            const bool heavy =
                (command.payload.flags & net::kAttackHeavyFlag) != 0;
            // M15-T1：服务器同样按主手武器解析攻击参数与耐久；空手走 config 默认值。
            items::WeaponAttackParams base;
            base.damage = m_attackDamage;
            base.range = m_attackRange;
            base.arcDegrees = m_attackArc;
            base.cooldownSeconds = m_attackCooldownSeconds;
            base.knockback = m_attackKnockback;
            base.staggerSeconds = m_attackStagger;
            base.wearPerHit = m_attackWear;
            const items::ItemDef* weaponDef = nullptr;
            if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
                equipment != nullptr) {
                const auto& slot = equipment->slots[static_cast<std::size_t>(
                    ecs::EquipSlot::PrimaryHand)];
                if (slot.has_value()) {
                    if (!(slot->condition > 0.0f)) {
                        return fail("武器已损坏，请卸下或更换");
                    }
                    if (m_items != nullptr) {
                        weaponDef = m_items->Find(slot->defId);
                    }
                }
            }
            characters::CombatSystem* combat = m_simulation->Combat();
            if (combat == nullptr) {
                return fail("服务器战斗系统不可用");
            }
            items::WeaponAttackParams params =
                game::ResolvePlayerMeleeAttack(
                    weaponDef, base, heavy, combat->GetCombatRules())
                    .params;
            auto* stamina = registry.try_get<ecs::CombatStamina>(player);
            if (stamina == nullptr) {
                return fail("玩家缺少体力组件");
            }
            if ((std::isfinite(stamina->exhausted) && stamina->exhausted > 0.0f) ||
                characters::StaminaExhausted(stamina->current,
                                             combat->GetCombatRules())) {
                return fail("体力耗尽");
            }
            if (!std::isfinite(stamina->current) ||
                !(stamina->current >= params.staminaCost)) {
                return fail("体力不足");
            }
            float cooldownSeconds = std::max(0.0f, params.cooldownSeconds);
            if (const auto* pain = registry.try_get<ecs::Pain>(player); pain != nullptr) {
                cooldownSeconds *= std::clamp(pain->attackCooldownScale, 1.0f, 3.0f);
            }
            if (const auto* progression =
                    registry.try_get<ecs::Progression>(player);
                progression != nullptr) {
                params.damage *=
                    game::MeleeDamageMultiplier(*progression, m_progressionConfig);
                cooldownSeconds *=
                    game::MeleeCooldownMultiplier(*progression, m_progressionConfig);
            }
            ecs::Entity best = entt::null;
            float bestDistSq = m_maxAttackDistance * m_maxAttackDistance;
            if (command.payload.entityId != 0) {
                best = m_simulation->Ecs().FindById(command.payload.entityId);
                const auto* targetPos =
                    best != entt::null ? registry.try_get<ecs::Position>(best) : nullptr;
                const auto* health =
                    best != entt::null ? registry.try_get<ecs::Health>(best) : nullptr;
                const bool attackable =
                    best != entt::null &&
                    (registry.all_of<ecs::AiState>(best) ||
                     registry.all_of<ecs::Animal>(best) ||
                     (m_pvpEnabled && registry.all_of<ecs::PlayerTag>(best)));
                if (best == player || !attackable || targetPos == nullptr ||
                    health == nullptr || health->current <= 0.0f) {
                    return fail("攻击目标不可用");
                }
                const float dx = targetPos->x - playerPos->x;
                const float dy = targetPos->y - playerPos->y;
                bestDistSq = dx * dx + dy * dy;
                if (bestDistSq > m_maxAttackDistance * m_maxAttackDistance) {
                    ++m_networkStats.rangeRejected;
                    return fail("攻击目标超出范围");
                }
            } else {
                registry.view<ecs::Health, ecs::Position>().each(
                    [&](const ecs::Entity entity, const ecs::Health& health,
                        const ecs::Position& pos) {
                        if (!m_simulation->Ecs().Alive(entity) ||
                            health.current <= 0.0f || entity == player ||
                            registry.any_of<ecs::NoCombatTarget>(entity) ||
                            !(registry.all_of<ecs::AiState>(entity) ||
                              registry.all_of<ecs::Animal>(entity) ||
                              (m_pvpEnabled &&
                               registry.all_of<ecs::PlayerTag>(entity)))) {
                            return;
                        }
                        const float dx = pos.x - playerPos->x;
                        const float dy = pos.y - playerPos->y;
                        const float distSq = dx * dx + dy * dy;
                        if (distSq < bestDistSq) {
                            bestDistSq = distSq;
                            best = entity;
                        }
                    });
            }
            if (session.attackUsed &&
                m_ticks < session.lastAttackTick +
                              (session.lastAttackCooldownTicks > 0
                                   ? session.lastAttackCooldownTicks
                                   : m_attackCooldownTicks)) {
                ++m_networkStats.cooldownRejected;
                return fail("攻击冷却中");
            }
            float dx = 1.0f;
            float dy = 0.0f;
            if (best != entt::null) {
                const auto* targetPos = registry.try_get<ecs::Position>(best);
                if (targetPos == nullptr) {
                    return fail("攻击目标缺少位置");
                }
                dx = targetPos->x - playerPos->x;
                dy = targetPos->y - playerPos->y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.0001f) {
                    dx /= length;
                    dy /= length;
                } else {
                    dx = 1.0f;
                    dy = 0.0f;
                }
            } else if (const auto* facing = registry.try_get<ecs::Facing>(player);
                       facing != nullptr) {
                game::FacingAttackDirection(facing->dir, &dx, &dy);
            }
            if (!combat->TrySpendStamina(m_simulation->Ecs(), player,
                                         params.staminaCost)) {
                return fail("体力不足");
            }
            attack->targetId =
                best != entt::null ? m_simulation->Ecs().IdOf(best).id : 0;
            attack->dirX = dx;
            attack->dirY = dy;
            attack->damage = params.damage;
            attack->range = params.range;
            attack->knockback = params.knockback;
            attack->staggerSeconds = params.staggerSeconds;
            attack->arcDegrees = params.arcDegrees;
            attack->weaponWearPerHit = params.wearPerHit;
            attack->type = ecs::DamageType::Blunt;
            attack->kind =
                heavy ? ecs::AttackKind::Heavy : ecs::AttackKind::Light;
            attack->windupSeconds = std::max(0.0f, params.windupSeconds);
            attack->windupLeft = attack->windupSeconds;
            attack->poiseDamage = std::max(0.0f, params.poiseDamage);
            attack->staminaCost = std::max(0.0f, params.staminaCost);
            const characters::StealthRuleConfig stealthRules =
                characters::SanitizeStealthRuleConfig(m_stealthRules);
            attack->backstabMultiplier =
                stealthRules.enabled ? stealthRules.backstabMultiplier : 1.0f;
            attack->backstabHalfAngleDegrees =
                stealthRules.backstabHalfAngleDegrees;
            attack->pending = true;
            ResetServerStealth(session, player, "攻击");
            session.attackUsed = true;
            session.lastAttackTick = m_ticks;
            session.lastAttackCooldownTicks = std::max<std::uint64_t>(
                1, static_cast<std::uint64_t>(std::llround(
                       cooldownSeconds * static_cast<double>(m_options.tickRate))));
            ZL_DEBUG("net", "session={} 攻击目标 stableId={}（距离 {:.2f}）", sessionId,
                     attack->targetId, static_cast<double>(std::sqrt(bestDistSq)));
            return true;
        }
        case net::CommandType::Block: {
            const bool down =
                (command.payload.flags & net::kBlockDownFlag) != 0;
            auto* stance = registry.try_get<ecs::CombatStance>(player);
            if (!down) {
                if (stance != nullptr && stance->blocking) {
                    stance->blocking = false;
                    stance->blockElapsed = 0.0f;
                    session.stateDirty = true;
                }
                return true;
            }
            if (session.survival.mode != net::kPlayerSurvivalModeNone ||
                session.animalHarvest.active) {
                return fail("当前动作中无法格挡");
            }
            if (registry.all_of<ecs::VehicleOccupant>(player)) {
                return fail("驾驶中无法格挡");
            }
            if (const auto* hit = registry.try_get<ecs::HitReaction>(player);
                hit != nullptr && std::isfinite(hit->staggerLeft) &&
                hit->staggerLeft > 0.0f) {
                return fail("硬直中无法格挡");
            }
            if (stance != nullptr && stance->blocking) {
                return true;
            }
            if (stance != nullptr &&
                ((std::isfinite(stance->guardBreakLeft) &&
                  stance->guardBreakLeft > 0.0f) ||
                 (std::isfinite(stance->dodgeLeft) &&
                  stance->dodgeLeft > 0.0f))) {
                return fail("姿态锁定中无法格挡");
            }
            if (const auto* attack = registry.try_get<ecs::AttackIntent>(player);
                attack != nullptr && attack->pending) {
                return fail("攻击中无法格挡");
            }
            auto* stamina = registry.try_get<ecs::CombatStamina>(player);
            characters::CombatSystem* combat = m_simulation->Combat();
            if (stamina == nullptr || combat == nullptr) {
                return fail("玩家体力状态未就绪");
            }
            if ((std::isfinite(stamina->exhausted) && stamina->exhausted > 0.0f) ||
                characters::StaminaExhausted(stamina->current,
                                             combat->GetCombatRules())) {
                return fail("体力不足");
            }
            const items::ItemDef* weaponDef = nullptr;
            if (const auto* equipment = registry.try_get<ecs::Equipment>(player);
                equipment != nullptr) {
                const auto& slot = equipment->slots[static_cast<std::size_t>(
                    ecs::EquipSlot::PrimaryHand)];
                if (slot.has_value() && slot->condition > 0.0f &&
                    m_items != nullptr) {
                    weaponDef = m_items->Find(slot->defId);
                }
            }
            const game::PlayerBlockPlan plan =
                game::ResolvePlayerBlock(weaponDef);
            if (stance == nullptr) {
                stance = &registry.emplace<ecs::CombatStance>(player);
            }
            stance->blocking = true;
            stance->blockElapsed = 0.0f;
            stance->blockReduction = plan.blockReduction;
            stance->blockStaminaCost = plan.blockStaminaCost;
            session.stateDirty = true;
            SetSessionStatus(session, "格挡", 1);
            return true;
        }
        case net::CommandType::Dodge: {
            if (session.survival.mode != net::kPlayerSurvivalModeNone ||
                session.animalHarvest.active) {
                return fail("当前动作中无法闪避");
            }
            if (registry.all_of<ecs::VehicleOccupant>(player)) {
                return fail("驾驶中无法闪避");
            }
            auto* stance = registry.try_get<ecs::CombatStance>(player);
            if (stance == nullptr) {
                stance = &registry.emplace<ecs::CombatStance>(player);
            }
            if (const auto* hit = registry.try_get<ecs::HitReaction>(player);
                hit != nullptr && std::isfinite(hit->staggerLeft) &&
                hit->staggerLeft > 0.0f) {
                return fail("硬直中无法闪避");
            }
            if (std::isfinite(stance->guardBreakLeft) &&
                stance->guardBreakLeft > 0.0f) {
                return fail("破防恢复中无法闪避");
            }
            if (stance->blocking) {
                return fail("先松开格挡再闪避");
            }
            if (std::isfinite(stance->dodgeLeft) && stance->dodgeLeft > 0.0f) {
                return true;
            }
            if (std::isfinite(stance->dodgeCooldown) &&
                stance->dodgeCooldown > 0.0f) {
                return fail("闪避冷却中");
            }
            if (const auto* attack = registry.try_get<ecs::AttackIntent>(player);
                attack != nullptr && attack->pending) {
                return fail("攻击中无法闪避");
            }
            auto* stamina = registry.try_get<ecs::CombatStamina>(player);
            characters::CombatSystem* combat = m_simulation->Combat();
            if (stamina == nullptr || combat == nullptr) {
                return fail("玩家体力状态未就绪");
            }
            const characters::CombatRuleConfig& rules =
                combat->GetCombatRules();
            if ((std::isfinite(stamina->exhausted) && stamina->exhausted > 0.0f) ||
                characters::StaminaExhausted(stamina->current, rules)) {
                return fail("体力不足");
            }
            const float cost = characters::DodgeStaminaCost(rules);
            if (!std::isfinite(stamina->current) ||
                !(stamina->current >= cost)) {
                return fail("体力不足");
            }
            ecs::Direction facing = ecs::Direction::Down;
            if (const auto* facingComponent =
                    registry.try_get<ecs::Facing>(player);
                facingComponent != nullptr) {
                facing = facingComponent->dir;
            }
            const game::PlayerDodgePlan plan = game::ResolvePlayerDodge(
                command.payload.x, command.payload.y, facing, rules);
            if (!(plan.speed > 0.0f)) {
                return fail("闪避方向或速度配置无效");
            }
            if (!combat->TrySpendStamina(m_simulation->Ecs(), player, cost)) {
                return fail("体力不足");
            }
            const float duration = characters::DodgeDurationSeconds(rules);
            const float invuln =
                std::min(characters::DodgeInvulnSeconds(rules), duration);
            stance->blocking = false;
            stance->blockElapsed = 0.0f;
            stance->dodgeLeft = duration;
            stance->dodgeInvulnLeft = invuln;
            stance->dodgeDirX = plan.dirX;
            stance->dodgeDirY = plan.dirY;
            stance->dodgeCooldown = characters::DodgeCooldownSeconds(rules);
            session.stateDirty = true;
            SetSessionStatus(session, "闪避", 1);
            return true;
        }
        case net::CommandType::PickupNearest:
            return applyInventory(m_inventory->PickupNearest(player));
        case net::CommandType::PickupEntity:
            return applyInventory(
                m_inventory->Pickup(player, command.payload.entityId));
        case net::CommandType::UseBag:
            return applyInventory(m_inventory->UseBag(player, command.payload.index));
        case net::CommandType::EquipBag: {
            const int slot = command.payload.index < 0
                                 ? -1
                                 : (command.payload.targetIndex >= 0 &&
                                            command.payload.targetIndex <
                                                static_cast<int>(
                                                    ecs::kEquipSlotCount)
                                        ? command.payload.targetIndex
                                        : static_cast<int>(
                                              ecs::EquipSlot::PrimaryHand));
            return applyInventory(
                m_inventory->EquipBag(player, command.payload.index, slot));
        }
        case net::CommandType::Unequip:
            return applyInventory(
                m_inventory->Unequip(player, command.payload.index));
        case net::CommandType::DropBag:
            return applyInventory(
                m_inventory->DropBag(player, command.payload.index));
        case net::CommandType::DropEquipment:
            return applyInventory(
                m_inventory->DropEquipment(player, command.payload.index));
        case net::CommandType::InventoryAction: {
            const auto actionType =
                static_cast<ui::InventoryActionType>(command.payload.flags & 0xFFu);
            const int count = static_cast<int>(command.payload.flags >> 8);
            if (count > 99 ||
                command.payload.area >
                    static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                command.payload.targetArea >
                    static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                static_cast<std::uint8_t>(actionType) >
                    static_cast<std::uint8_t>(ui::InventoryActionType::SortStorage)) {
                return fail("背包命令字段非法");
            }
            ui::InventoryAction action;
            action.type = actionType;
            action.from = static_cast<ui::InventoryArea>(command.payload.area);
            action.to =
                static_cast<ui::InventoryArea>(command.payload.targetArea);
            action.fromIndex = command.payload.index;
            action.toIndex = command.payload.targetIndex;
            action.fromEntityId = command.payload.entityId;
            action.count = count > 0 ? count : 1;
            return applyInventory(m_inventory->Dispatch(player, action));
        }
        case net::CommandType::Gunshot:
            EmitGunshot(session.playerStableId);
            SetSessionStatus(session, "开了一枪", 0);
            return true;
        case net::CommandType::FireProjectile: {
            // M23-F1-T7：旧客户端兼容入口只转换方向，仍经过 FireRanged 的
            // 装备/弹药/冷却校验，不能绕过服务器权威。
            net::Command converted = command;
            converted.type = net::CommandType::FireRanged;
            if (converted.payload.entityId != net::kNoEntity) {
                const ecs::Entity target =
                    m_simulation->Ecs().FindById(command.payload.entityId);
                const auto* targetPos =
                    target != entt::null
                        ? registry.try_get<ecs::Position>(target)
                        : nullptr;
                if (targetPos != nullptr) {
                    converted.payload.x = targetPos->x - playerPos->x;
                    converted.payload.y = targetPos->y - playerPos->y;
                }
            }
            if (!FireRanged(session, converted)) {
                return fail("远程开火失败（装备/弹药/冷却/方向无效）");
            }
            return true;
        }
        case net::CommandType::FireRanged:
            if (!FireRanged(session, command)) {
                return fail("远程开火失败（装备/弹药/冷却/方向无效）");
            }
            return true;
        case net::CommandType::ReloadRanged:
            if (!ReloadRanged(session)) {
                return fail("换弹失败（无远程武器/弹匣已满/无弹药/正在换弹）");
            }
            return true;
        case net::CommandType::IgniteFire:
            return IgniteFireAction(session, command, error);
        case net::CommandType::ExtinguishFire:
            return ExtinguishFireAction(session, command, error);
        case net::CommandType::SelectHotbar:
            if (command.payload.index < 0 || command.payload.index >= 5) {
                return fail("快捷栏索引越界");
            }
            return true;  // 纯客户端 UI 状态；服务器无对应世界状态
        case net::CommandType::ChoosePerk: {
            auto* progression = registry.try_get<ecs::Progression>(player);
            if (progression == nullptr || progression->perkPoints <= 0) {
                return fail("当前没有可分配的 Perk");
            }
            if (command.payload.index < 0 || command.payload.index >= 3 ||
                !game::ChoosePerkByIndex(*progression, command.payload.index,
                                          m_progressionConfig)) {
                return fail("Perk 选项无效");
            }
            ApplyProgressionRuntime(player, &session);
            session.stateDirty = true;
            SetSessionStatus(session,
                             "已选择 Perk（剩余 " +
                                 std::to_string(progression->perkPoints) + " 点）",
                             1);
            return true;
        }
        case net::CommandType::PlaceStructure:
            return applyBuild(PlaceStructure(player, session, command, error));
        case net::CommandType::DemolishStructure:
            return applyBuild(
                DemolishStructure(player, session, command, error));
        case net::CommandType::RepairStructure:
            return applyBuild(RepairStructure(player, session, command, error));
        case net::CommandType::ToggleDoor:
            return applyBuild(ToggleDoor(player, session, command, error));
        case net::CommandType::InteractFacility:
            return applyBuild(
                InteractFacility(player, session, command, error));
        case net::CommandType::PoiInteract:
            return applyBuild(
                InteractPoi(player, session, command, error));
        case net::CommandType::TradeOffer:
            return applyBuild(
                PurchaseMerchantOffer(player, session, command, error));
        case net::CommandType::AcceptSettlementMission:
            return applyBuild(
                AcceptSettlementMission(player, session, command, error));
        case net::CommandType::RecruitCompanion:
            return applyBuild(
                RecruitCompanion(player, session, command, error));
        case net::CommandType::DismissCompanion:
            return applyBuild(
                DismissCompanion(player, session, command, error));
        case net::CommandType::SetCompanionCommand:
            return applyBuild(SetCompanionCommandAction(player, session,
                                                        command, error));
        case net::CommandType::GiftToCompanion:
            return applyBuild(GiftToCompanionAction(player, session, command,
                                                    error));
        case net::CommandType::AcceptCompanionQuest:
            return applyBuild(AcceptCompanionQuestAction(player, session,
                                                         command, error));
        case net::CommandType::AbandonCompanionQuest:
            return applyBuild(AbandonCompanionQuestAction(player, session,
                                                          command, error));
        case net::CommandType::RescueCompanion:
            return applyBuild(RescueCompanionAction(player, session, command,
                                                    error));
        case net::CommandType::ChooseWorldEvent:
            return ChooseServerWorldEvent(session, command, error);
        case net::CommandType::SearchLootContainer: {
            if (command.payload.entityId == 0) {
                return fail("容器目标不存在");
            }
            return applyInventory(m_inventory->SearchLoot(
                player, command.payload.entityId));
        }
        case net::CommandType::AbandonMission: {
            if (m_missionConfig == nullptr || command.payload.index < 0 ||
                static_cast<std::size_t>(command.payload.index) >=
                    m_missionConfig->defs.size()) {
                return fail("任务不存在");
            }
            const game::MissionDef& def =
                m_missionConfig->defs[static_cast<std::size_t>(
                    command.payload.index)];
            auto* log = registry.try_get<ecs::MissionLog>(player);
            const int day =
                m_gameTime != nullptr
                    ? 1 + static_cast<int>(
                              m_gameTime->TotalGameMinutes() / (24.0 * 60.0))
                    : 1;
            const ecs::MissionRunState* before =
                log != nullptr ? game::FindMissionState(*log, def.id)
                               : nullptr;
            const std::string settlementId =
                before != nullptr ? before->settlementId : std::string{};
            if (log == nullptr ||
                !game::AbandonMission(log, def.id, day)) {
                return fail("当前没有可放弃的任务");
            }
            int reputationDelta = 0;
            if (!settlementId.empty() && m_settlements != nullptr) {
                if (const game::SettlementDef* settlement =
                        m_settlements->Find(settlementId);
                    settlement != nullptr) {
                    const std::string key =
                        "mission:" + settlementId + ":" + def.id + ":" +
                        std::to_string(day) + ":abandon";
                    const game::SettlementReputationChange change =
                        game::AwardSettlementReputation(
                            m_simulation->Ecs(), player,
                            m_settlements->reputation, settlementId,
                            game::SettlementReputationEvent::MissionAbandon,
                            key, settlement->startingReputation, day);
                    reputationDelta = change.delta;
                }
            }
            session.stateDirty = true;
            std::string text = "任务已放弃：" + def.name;
            if (def.kind == game::MissionKind::Once) {
                text += "（次日可重新接取）";
            }
            if (reputationDelta != 0) {
                text += " · 声望 " + std::to_string(reputationDelta);
            }
            SetSessionStatus(session, text, 2);
            return true;
        }
        case net::CommandType::InteractRadio: {
            if (m_config == nullptr || m_items == nullptr ||
                m_simulation == nullptr) {
                return fail("电台交互不可用");
            }
            const float radioX = static_cast<float>(
                m_config->Get<double>("objective.radioX", 72.5));
            const float radioY = static_cast<float>(
                m_config->Get<double>("objective.radioY", 20.5));
            const float dx = playerPos->x - radioX;
            const float dy = playerPos->y - radioY;
            if (dx * dx + dy * dy > m_buildActionRange * m_buildActionRange) {
                ++m_networkStats.rangeRejected;
                return fail("电台距离太远");
            }
            const std::string partId =
                m_config->Get<std::string>("objective.radioPart",
                                           "radio_part");
            auto* container = registry.try_get<ecs::Container>(player);
            if (container == nullptr ||
                items::CountOf(*container, partId) < 1) {
                return fail("需要无线电零件");
            }
            const int day =
                m_gameTime != nullptr
                    ? 1 + static_cast<int>(
                              m_gameTime->TotalGameMinutes() / (24.0 * 60.0))
                    : 1;
            UpdateServerMissionAvailability(session, day);
            if (m_missionConfig != nullptr &&
                m_missionConfig->Find("radio_repair") != nullptr) {
                const auto* log = registry.try_get<ecs::MissionLog>(player);
                const ecs::MissionRunState* repair =
                    log != nullptr ? game::FindMissionState(*log, "radio_repair")
                                   : nullptr;
                if (repair == nullptr ||
                    repair->status != ecs::MissionRunStatus::Active) {
                    return fail("电台任务尚未开始");
                }
            }
            if (items::RemoveItem(*container, partId, 1) != 1) {
                return fail("无线电零件消耗失败");
            }
            auto* objective = registry.try_get<ecs::Objective>(player);
            if (objective == nullptr) {
                objective = &registry.emplace<ecs::Objective>(player);
            }
            objective->step = ecs::ObjectiveStep::ReachExtraction;
            RecordServerMissionEvent(
                session, game::MissionObjectiveKind::Interact, "radio", 1);
            session.stateDirty = true;
            SetSessionStatus(session, "电台已修复，前往撤离点", 1);
            return true;
        }
        case net::CommandType::SleepRequest: {
            const bool untilDawn =
                (command.payload.flags & net::kSleepUntilDawnFlag) != 0;
            const bool ok = StartServerSleepVote(
                session, untilDawn, command.payload.x, error);
            if (!ok) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "睡眠请求被拒绝",
                                 2);
            }
            return ok;
        }
        case net::CommandType::SleepVote: {
            const bool ok = VoteServerSleep(
                session,
                (command.payload.flags & net::kSleepVoteYesFlag) != 0, error);
            if (!ok) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "睡眠投票被拒绝",
                                 2);
            }
            return ok;
        }
        case net::CommandType::SleepCancel:
            if (m_serverSleep.phase == ServerSleepPhase::None) {
                return fail("当前没有睡眠投票或睡眠");
            }
            if (!m_serverSleep.eligible.contains(session.playerStableId)) {
                return fail("当前会话不在睡眠中");
            }
            FinishServerSleep(game::SleepStopReason::Cancelled, "玩家取消");
            return true;
        case net::CommandType::ForageStart:
            if (!StartServerForage(session, command, error)) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "觅食请求被拒绝",
                                 2);
                return false;
            }
            return true;
        case net::CommandType::ForageCancel:
            if (session.survival.mode != net::kPlayerSurvivalModeForage) {
                return fail("当前没有觅食动作");
            }
            ClearServerSurvival(session, "已取消觅食", 0);
            return true;
        case net::CommandType::FishingStart:
            if (!StartServerFishing(session, command, error)) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "钓鱼请求被拒绝",
                                 2);
                return false;
            }
            return true;
        case net::CommandType::FishingReel:
            if (!ReelServerFishing(session, error)) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "收杆被拒绝",
                                 2);
                return false;
            }
            return true;
        case net::CommandType::FishingCancel:
            if (session.survival.mode != net::kPlayerSurvivalModeFishing) {
                return fail("当前没有钓鱼动作");
            }
            ClearServerSurvival(session, "已取消钓鱼", 0);
            return true;
        case net::CommandType::AnimalHarvestStart:
            if (!StartServerAnimalHarvest(session, command, error)) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "剥取请求被拒绝",
                                 2);
                return false;
            }
            return true;
        case net::CommandType::AnimalHarvestCancel:
            if (!session.animalHarvest.active) {
                return fail("当前没有动物剥取动作");
            }
            CancelServerAnimalHarvest(session, "已取消剥取", 0);
            return true;
        case net::CommandType::ThrowItem: {
            if (!ThrowItemAction(session, command, error)) {
                SetSessionStatus(session,
                                 error != nullptr && !error->empty()
                                     ? *error
                                     : "投掷命令被拒绝",
                                 2);
                return false;
            }
            return true;
        }
        case net::CommandType::SetStealth: {
            if ((command.payload.flags & ~net::kStealthCrouchFlag) != 0 ||
                command.payload.x != 0.0f || command.payload.y != 0.0f ||
                command.payload.index != -1 ||
                command.payload.targetIndex != -1 ||
                command.payload.area != 0 || command.payload.targetArea != 0 ||
                command.payload.entityId != 0 ||
                command.payload.targetEntityId != 0) {
                return fail("潜行命令字段非法");
            }
            const bool crouched =
                (command.payload.flags & net::kStealthCrouchFlag) != 0;
            std::string stealthError;
            if (!SetServerStealth(session, player, crouched, &stealthError)) {
                SetSessionStatus(session,
                                 stealthError.empty() ? "潜行切换被拒绝"
                                                      : stealthError,
                                 2);
                return fail(stealthError.empty() ? "潜行切换被拒绝"
                                                  : stealthError);
            }
            return true;
        }
        default:
            ++m_networkStats.unsupportedCommands;
            return fail("服务器暂未实现该命令");
    }
}

void ServerHost::RemoveSession(std::uint64_t sessionId, std::uint8_t reason,
                               const std::string& message) {
    const auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) {
        return;
    }
    SettleServerThrows("disconnect", it->second.playerStableId);
    ReleaseServerVehicleSeat(it->second, true, "断线");
    ReleaseServerForageReservation(it->second);
    ReleaseServerAnimalHarvest(it->second);
    it->second.survival = {};
    it->second.animalHarvest = {};
    OnServerSessionRemovedForSleep(it->second.playerStableId);
    if (m_simulation != nullptr && it->second.playerStableId != 0) {
        CaptureSessionProfile(it->second, it->second.dead);
        StandbyOwnedCompanion(it->second.playerStableId, "断线");
        const ecs::Entity player =
            m_simulation->Ecs().FindById(it->second.playerStableId);
        if (player != entt::null) {
            m_simulation->DespawnEntity(player);
        }
    }
    SavePlayerProfiles("disconnect");
    m_socialDirty = true;
    ZL_INFO("net", "客户端离开：session={}，原因={}{}（剩余 {} 人）", sessionId,
            net::SessionDisconnectReasonName(
                static_cast<net::SessionDisconnectReason>(reason)),
            message.empty() ? "" : "：“" + message + "”", m_sessions.size() - 1);
    m_sessions.erase(it);
}

std::uint16_t ServerHost::NetworkPort() const {
    return m_network != nullptr ? m_network->Port() : 0;
}

void ServerHost::LogStatus() {
    if (m_simulation == nullptr) {
        return;
    }
    const auto& stats = m_simulation->Chunks().GetStats();
    const ecs::Entity player = m_simulation->Player();
    glm::vec2 playerPosition{0.0f, 0.0f};
    if (player != entt::null) {
        const auto& position = m_simulation->Ecs().Registry().get<ecs::Position>(player);
        playerPosition = glm::vec2(position.x, position.y);
    }
    ZL_INFO("server",
            "tick={} 实体={} chunk={} loading={} 兴趣窗={} 玩家={:.1f},{:.1f} 会话={}",
            m_ticks, m_simulation->Ecs().AliveCount(), stats.loadedChunks,
            stats.loadingChunks, stats.focusCount, playerPosition.x,
            playerPosition.y, m_networkEnabled ? m_sessions.size() : 0);
    if (m_networkEnabled && m_network != nullptr) {
        const net::KcpSessionServerStats sessionStats = m_network->GetStats();
        ZL_INFO("net",
                "服务器网络: 收包={} 坏包={} 超大={} 过期={} | 命令 收/受/拒={}/{}/{} "
                "限频={} 冷却={} 越权={} 距离={} 未实现={} | 分配={} 快照={} 失败={} "
                "建筑={}/{} 过滤={} 状态 发/失败={}/{} 社交={} 聊天 收/拒={}/{} 枪声={} 投射物 发/中={}/{} | 档案 恢复/保存/死亡/复活={}/{}/{}/{} | KCP 数据报 收/发={}/{} 拒绝={}",
                m_networkStats.dataPacketsReceived, m_networkStats.malformedPackets,
                m_networkStats.oversizedPackets, m_networkStats.stalePackets,
                m_networkStats.commandsReceived, m_networkStats.commandsAccepted,
                m_networkStats.commandsRejected, m_networkStats.rateLimitedCommands,
                m_networkStats.cooldownRejected, m_networkStats.ownershipRejected,
                m_networkStats.rangeRejected, m_networkStats.unsupportedCommands,
                m_networkStats.assignmentsSent, m_networkStats.snapshotsSent,
                m_networkStats.snapshotFailures, m_networkStats.structureDiffsSent,
                m_networkStats.structureDiffFailures,
                m_networkStats.structureChunksFiltered,
                m_networkStats.playerStatesSent,
                m_networkStats.playerStateFailures, m_networkStats.socialStatesSent,
                m_networkStats.chatMessagesReceived, m_networkStats.chatMessagesRejected,
                m_networkStats.gunshots, m_networkStats.projectilesFired,
                m_networkStats.projectilesHit,
                m_networkStats.profileRestores, m_networkStats.profileSaves,
                m_networkStats.playerDeaths, m_networkStats.playerRespawns,
                sessionStats.transport.datagramsReceived,
                sessionStats.transport.datagramsSent,
                sessionStats.transport.datagramsRejected);
    }
}

}  // namespace zl::server
