#include "game/states/WorldDemoState.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

#include "core/Config.h"
#include "core/Constants.h"
#include "game/Crops.h"
#include "core/EventBus.h"
#include "core/GameLoop.h"
#include "core/GameTime.h"
#include "core/Input.h"
#include "core/Log.h"
#include "ai/PathfindingService.h"
#include "ai/Passability.h"
#include "ai/VirtualHorde.h"
#include "ai/components/AiComponents.h"
#include "ai/systems/AiSystem.h"
#include "ai/systems/LodSystem.h"
#include "ai/systems/PerceptionSystem.h"
#include "ai/systems/SpawnLodSystem.h"
#include "audio/SoundIds.h"
#include "audio/SoundManager.h"
#include "characters/components/AnimalComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "characters/WeaponSocket.h"
#include "characters/systems/AnimationSystem.h"
#include "characters/systems/CombatRules.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/InfectionSystem.h"
#include "characters/systems/InputSystem.h"
#include "characters/systems/PainSystem.h"
#include "characters/systems/WoundSystem.h"
#include "ecs/EcsWorld.h"
#include "ecs/SpatialIndex.h"
#include "ecs/components/CommonComponents.h"
#include "ecs/components/PhysicsComponents.h"
#include "game/EventProbe.h"
#include "game/ActionHints.h"
#include "game/AnimalHarvest.h"
#include "game/AnimalSystem.h"
#include "game/Animals.h"
#include "game/Armor.h"
#include "game/BarricadeDefense.h"
#include "game/CameraFollow.h"
#include "game/CombatAudio.h"
#include "game/CombatPresentation.h"
#include "game/EntityReplication.h"
#include "game/FireSystem.h"
#include "game/FishingRuntime.h"
#include "game/FoodSafety.h"
#include "game/InfectionStatus.h"
#include "game/NetworkClient.h"
#include "game/PainStatus.h"
#include "game/Poi.h"
#include "game/PoiComponents.h"
#include "game/PlayerCombat.h"
#include "game/ProgressionRules.h"
#include "game/RandomEvents.h"
#include "game/RangedSound.h"
#include "game/RunStats.h"
#include "game/SettlementNpc.h"
#include "game/WeatherStatus.h"
#include "game/WorldQueries.h"
#include "game/WorldSimulation.h"
#include "game/states/MainMenuState.h"
#include "game/ZombieFactory.h"
#include "game/GroundItems.h"
#include "game/LootContainers.h"
#include "game/PlayerStorage.h"
#include "game/VehicleComponents.h"
#include "game/StructureComponents.h"
#include "items/Crafting.h"
#include "items/LootTable.h"
#include "items/Repair.h"
#include "items/InventoryOps.h"
#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"
#include "items/RecipeRegistry.h"
#include "items/Spoilage.h"
#include "net/Command.h"
#include "net/CommandPipeline.h"
#include "physics/PhysicsWorld.h"
#include "platform/Window.h"
#include "render/Camera.h"
#include "render/IsoMath.h"
#include "render/Renderer.h"
#include "render/SpriteBatch.h"
#include "save/ChunkDiff.h"
#include "save/SaveManager.h"
#include "ui/UiSystem.h"
#include "world/Chunk.h"
#include "world/ChunkManager.h"
#include "world/World.h"
#include "world/WorldCoords.h"

namespace zl::game {
namespace {

constexpr float kPanSpeedPxPerSecond = 900.0f;
constexpr float kGridLineThickness = 1.2f;
constexpr float kHoverLineThickness = 2.5f;
constexpr float kMarkerLineThickness = 3.0f;
constexpr float kStatsLogInterval = 3.0f;
constexpr float kPerfLogInterval = 5.0f;
constexpr uint32_t kGridColor = 0x30ffffff;   // 白色低透明（ABGR: A=0x30）
constexpr uint32_t kHoverColor = 0xff30e0ff;  // 黄色高亮
constexpr uint32_t kAxisXColor = 0xc04040ff;  // ??
constexpr uint32_t kAxisYColor = 0xc040ff40;  // ??
// 上层（z≥1）透明度策略（M3-T6 临时方案）：楼上整体 35% 不透明，看得到室内
constexpr uint32_t kUpperZColor = 0x59ffffff;
// M4-T7：寻路目标标记（SpriteBatch 色，格式同 kHoverColor）
constexpr uint32_t kMarkerActiveColor = 0xff40e0ff;  // 黄：寻路??跟随中（常显??
constexpr uint32_t kMarkerArriveColor = 0xff50ff60;  // 绿：到达（限时）
constexpr uint32_t kMarkerFailColor = 0xff3030ff;    // 红：不可达（限时??
constexpr uint32_t kPoiMarkerCompletedColor = 0xff808080;  // 灰：POI 已完??
constexpr uint32_t kForageReadyColor = 0xff60e070;
constexpr uint32_t kForageEmptyColor = 0xff606060;
constexpr uint32_t kForageTargetColor = 0xff40d0ff;
constexpr uint32_t kFishingReadyColor = 0xff50c8ff;
constexpr uint32_t kFishingTargetColor = 0xffffd040;
// M21-F1c：守夜破口地图标记（ABGR；黄=预计破口，红=已破口）。
constexpr uint32_t kSiegeTargetMarkerColor = 0xff30b0ff;  // 橙黄
constexpr uint32_t kSiegeBreachMarkerColor = 0xff3030ff;  // ??
// M4-T7：提示色调（映射到 RmlUi HUD 的 notice 颜色类）。
// bgfx debug 字体只支持 ASCII；中文提示统一走 RmlUi HUD/背包状态行。
constexpr uint32_t kNoticeInfoColor = 0x0E;  // ??
constexpr uint32_t kNoticeOkColor = 0x0A;    // ??
constexpr uint32_t kNoticeFailColor = 0x0C;  // ??
constexpr float kThreatFeedbackSeconds = 1.0f;
constexpr float kBaseHudRadius = 16.0f;
constexpr float kBaseHudRefreshSeconds = 0.25f;
constexpr const char* kHotbarActions[] = {"Hotbar1", "Hotbar2", "Hotbar3", "Hotbar4",
                                          "Hotbar5"};

VehicleVisualDirection VehicleVisualDirectionFromSpriteHash(
    std::uint32_t spriteHash, VehicleVisualDirection fallback) {
    for (int i = 0; i < 8; ++i) {
        const auto direction = static_cast<VehicleVisualDirection>(i);
        if (spriteHash == ecs::HashName(VehicleVisualSprite(direction))) {
            return direction;
        }
    }
    return fallback;
}

// M15-T3：世界方向 → 屏幕等距方向（px 比例 2:1），返回 HUD 边缘 id 与中文方位。
void ThreatDirectionFor(float dx, float dy, std::string* direction, std::string* label) {
    const float screenX = dx - dy;
    const float screenY = (dx + dy) * 0.5f;
    if (std::abs(screenX) >= std::abs(screenY)) {
        if (screenX < 0.0f) {
            *direction = "left";
            *label = "左侧";
        } else {
            *direction = "right";
            *label = "右侧";
        }
    } else if (screenY < 0.0f) {
        *direction = "top";
        *label = "上方";
    } else {
        *direction = "bottom";
        *label = "下方";
    }
}

const char* DamageTypeLabel(ecs::DamageType type) {
    switch (type) {
        case ecs::DamageType::Claw:
            return "爪击";
        case ecs::DamageType::Bite:
            return "撕咬";
        case ecs::DamageType::Blunt:
            return "钝击";
        case ecs::DamageType::Debug:
            return "调试";
        case ecs::DamageType::Fire:
            return "火焰";
    }
    return "未知";
}

template <typename T>
ecs::Entity FindNearestComponentEntity(ecs::EcsWorld& world,
                                       ecs::Entity player, float range) {
    if (player == entt::null || range <= 0.0f) {
        return entt::null;
    }
    entt::registry& registry = world.Registry();
    const auto* playerPosition = registry.try_get<ecs::Position>(player);
    if (playerPosition == nullptr) {
        return entt::null;
    }
    float bestDistance = range * range;
    ecs::Entity best = entt::null;
    registry.view<ecs::Position, T>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const T&) {
            const float dx = position.x - playerPosition->x;
            const float dy = position.y - playerPosition->y;
            const float distance = dx * dx + dy * dy;
            if (distance > bestDistance) {
                return;
            }
            bestDistance = distance;
            best = entity;
        });
    return best;
}

// 工作集内存（压测记录用；非 Windows 返回 0）
uint64_t WorkingSetBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.WorkingSetSize;
    }
#endif
    return 0;
}

int CountMissionItem(entt::registry& registry, ecs::Entity player,
                     std::string_view defId) {
    int count = 0;
    if (const auto* container = registry.try_get<ecs::Container>(player);
        container != nullptr) {
        for (const ecs::ItemInstance& item : container->items) {
            if (item.defId == defId) {
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

ecs::SkillKind MissionSkillKind(std::string_view id) {
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

}  // namespace

void WorldDemoState::OnEnter() {
    core::Config& config = *m_ctx.config;
    m_networkMode = m_ctx.networkClient != nullptr;
    m_networkSleep = {};
    StopVehicleAudio();
    m_vehicle = entt::null;
    m_vehicleMotion = {};
    m_localParkedVehicleMotions.clear();
    m_vehicleHeadingX = 0.0f;
    m_vehicleHeadingY = 1.0f;
    m_vehicleVisualDirection = VehicleVisualDirection::South;
    m_vehicleSurface = VehicleSurface::Paved;
    m_lastVehicleSurface = VehicleSurface::Paved;
    m_driving = false;
    m_riding = false;
    m_vehicleFuelWarned = false;
    m_vehicleLowConditionWarned = false;
    m_vehicleNoiseCooldown = 0.0f;
    m_vehicleRamWearCooldown = 0.0f;
    m_vehicleBodyCollisionCooldown = 0.0f;
    m_vehicleHornCooldown = 0.0f;
    m_vehicleZombieAttackCooldown = 0.0f;
    m_vehicleEngineVoices.clear();
    m_vehicleRamCooldowns.clear();
    m_vehicleHitSparks.clear();
    m_vehicleHitSparkSerial = 0;
    m_vehicleMetalDebris.clear();
    m_vehicleMetalDebrisSerial = 0;
    m_vehicleShakeStrength = 0.0f;
    m_vehicleShakeAge = 0.0f;
    m_vehicleShakeDuration = 0.0f;
    m_vehicleSmokePuffs.clear();
    m_vehicleSmokeRuntimes.clear();
    m_vehicleVisualDirections.clear();
    m_vehicleDamageVisuals.clear();
    m_networkVehicle = {};
    m_networkCombat = {};
    m_vehiclePredictionActive = false;
    m_vehiclePredictionTick = 0;
    m_networkHasConnected = false;
    m_networkWasDisconnected = false;
    m_networkDisconnectWasDriving = false;
    m_playerSpriteBackup.reset();
    m_rangedWeapon = {};
    m_rangedReloadHitSerial = 0;
    m_rangedShotSerial = 0;
    ZL_INFO("render", "进入 WorldDemo（{}??",
            m_networkMode ? "M13 网络客户端" : "M3 单机世界");
    m_autoSaveOnExit =
        !m_networkMode && config.Get<bool>("save.autoSaveOnExit", true);
    m_ctx.activeSaveId.clear();
    m_ctx.activeSaveUseBackup = false;
    m_ctx.saveWarning.clear();

    const std::string manifest =
        config.Get<std::string>("assets.manifest", "assets/asset_manifest.json");
    const std::string provider = config.Get<std::string>("assets.provider", "own");
    if (!m_ctx.assets->Init(manifest, provider, *m_ctx.atlases,
                            m_ctx.modAssetManifests)) {
        ZL_ERROR("render", "资源初始化失败，世界将不可见");
    }

    // 相机：对准 village_demo 北侧村庄（房群在 chunk (6..7, 1..2)）；边界=地图（M4-T5）
    render::Camera& camera = *m_ctx.camera;
    camera.SetViewport(m_ctx.renderer->FramebufferWidth(), m_ctx.renderer->FramebufferHeight());
    camera.SetCenter(render::ToScreen(70.0f, 24.0f));
    m_followCamera = config.Get<bool>("camera.follow", true);
    m_followRate = static_cast<float>(config.Get<double>("camera.followRate", 8.0));
    // M14-T1：玩家被墙/门遮住时降到该 alpha（1=不透明）
    m_wallOcclusionAlpha = std::clamp(
        static_cast<float>(config.Get<double>("render.wallOcclusionAlpha", 0.35)), 0.05f, 1.0f);
    // M14-T10：进入房间后切换为剖切视图（隐藏房间上层与前方边界墙）
    m_roomCutaway = config.Get<bool>("render.roomCutaway", true);
    m_showDebugText = config.Get<bool>("render.debugText", true);

    // 自动巡航压测（M3-T7；F8 手动切换）
    m_cruiseEnabled = config.Get<bool>("world.autoCruise", false);
    m_cruiseSpeed = static_cast<float>(config.Get<double>("world.cruiseSpeed", 30.0));
    m_cruiseRadius = static_cast<float>(config.Get<double>("world.cruiseRadius", 40.0));
    m_cruiseCenterX = static_cast<float>(config.Get<double>("world.cruiseCenterX", 500.0));
    m_cruiseCenterY = static_cast<float>(config.Get<double>("world.cruiseCenterY", 500.0));
    m_cruiseMinutes = config.Get<double>("world.cruiseMinutes", 5.0);
    // M5-T4：演示枪声基础可闻半径（隔墙衰减由感知系统的传播模型按听者计算）
    m_gunshotRadius = static_cast<float>(config.Get<double>("sounds.gunshotRadius", 30.0));
    // M5-T6：玩家挥击参数（伤害管线入口，CombatSystem 消费 AttackIntent；M15-T1 由武器覆盖）
    m_playerAttackDamage = static_cast<float>(config.Get<double>("combat.playerDamage", 40.0));
    m_playerAttackRange = static_cast<float>(config.Get<double>("combat.playerRange", 1.7));
    m_playerAttackArc =
        static_cast<float>(config.Get<double>("combat.playerArcDegrees", 100.0));
    m_playerAttackCooldown =
        static_cast<float>(config.Get<double>("combat.playerCooldownSeconds", 0.5));
    m_playerAttackKnockback =
        static_cast<float>(config.Get<double>("combat.playerKnockback", 3.0));
    m_playerAttackStagger =
        static_cast<float>(config.Get<double>("combat.playerStaggerSeconds", 0.35));
    m_playerAttackWear =
        static_cast<float>(config.Get<double>("combat.playerWearPerHit", 0.0));
    m_progressionConfig = game::LoadProgressionConfig(config);
    m_baseWalkSpeed = std::clamp(
        static_cast<float>(config.Get<double>("player.walkSpeed", 4.0)), 0.5f,
        20.0f);
    ZL_INFO("progression", "成长配置：{}，等级上限 {}，技能 {}，Perk {}",
            m_progressionConfig.enabled ? "启用" : "关闭",
            m_progressionConfig.maxLevel, m_progressionConfig.skills.size(),
            m_progressionConfig.perks.size());
    m_networkProgression = {};
    m_networkPerkChoices = {};
    m_networkProgressionValid = false;
    m_networkFood = {};
    m_lastSurvivalXpMinute = -1.0;
    m_progressionPlayer = entt::null;
    m_characterOpen = false;
    m_siegeReportOpen = false;
    m_lastSettlementNoticeSerial = 0;
    m_siegeReportTitle.clear();
    m_siegeReportRating.clear();
    m_siegeReportTone = "info";
    m_siegeReportHint.clear();
    m_siegeReportLines.fill({});
    m_poiConfig = {};
    if (m_ctx.poiConfig != nullptr) {
        m_poiConfig = *m_ctx.poiConfig;
    } else {
        std::string poiError;
        if (!game::LoadPoiConfig(config, &m_poiConfig, &poiError)) {
            ZL_ERROR("poi", "POI 配置加载失败：{}", poiError);
        } else if (m_poiConfig.enabled) {
            ZL_INFO("poi", "POI 配置：{} 个区域（指引范围 {:.0f} 格）",
                    m_poiConfig.defs.size(),
                    static_cast<double>(m_poiConfig.guideRange));
        }
    }
    InitializePoiRuntime();
    m_settlementConfig = {};
    if (m_ctx.settlements != nullptr) {
        m_settlementConfig = *m_ctx.settlements;
    } else {
        std::string settlementError;
        if (!game::LoadSettlementConfig(config, &m_settlementConfig,
                                        &settlementError)) {
            ZL_WARN("settlement", "聚落配置加载失败：{}", settlementError);
        }
    }
    if (const SettlementDef* settlement =
            PrimarySettlement(m_settlementConfig);
        settlement != nullptr) {
        ZL_INFO("settlement", "聚落就位：{}（{}）安全半径 {:.1f} 格，NPC {} 个",
                settlement->name, settlement->id,
                static_cast<double>(settlement->safeRadius),
                settlement->npcPoints.size());
    }
    m_missionConfig = {};
    m_missionFocusId.clear();
    m_lastMissionKills = 0;
    m_missionKillsInitialized = false;
    if (m_ctx.missionConfig != nullptr) {
        m_missionConfig = *m_ctx.missionConfig;
    } else {
        std::string missionError;
        if (!game::LoadMissionConfig(config, &m_missionConfig, &missionError)) {
            ZL_ERROR("missions", "任务配置加载失败：{}", missionError);
        } else if (m_missionConfig.enabled) {
            ZL_INFO("missions", "任务配置：{} 条（随机种子 {}）",
                    m_missionConfig.defs.size(), m_missionConfig.randomSeed);
        }
    }
    InitializeMissionRuntime();
    // M6-T2：玩家背包容量（kg）
    m_playerCapacityKg =
        static_cast<float>(config.Get<double>("inventory.playerCapacityKg", 20.0));
    // M6-T3：地面物品拾取半径（格）+ 每次进入状态重新散落
    m_pickupRadius = static_cast<float>(config.Get<double>("inventory.pickupRadius", 1.6));
    m_inventoryRange =
        static_cast<float>(config.Get<double>("inventory.lootRange", 3.0));
    m_forageInteractRange = std::clamp(
        static_cast<float>(config.Get<double>("forage.interactRange", 2.2)),
        0.5f, 6.0f);
    m_fishingInteractRange = std::clamp(
        static_cast<float>(config.Get<double>("fishing.interactRange", 2.2)),
        0.5f, 6.0f);
    m_foragePlanRadius = std::clamp(
        static_cast<float>(config.Get<double>("forage.planRadius", 10.0)),
        2.0f, 16.0f);
    m_fishingPlanRadius = std::clamp(
        static_cast<float>(config.Get<double>("fishing.planRadius", 12.0)),
        2.0f, 20.0f);
    m_forageScanSeconds = std::clamp(
        static_cast<float>(config.Get<double>("forage.scanSeconds", 1.0)),
        0.25f, 10.0f);
    m_forageAction = {};
    m_fishingAction = {};
    m_animalHarvestAction = {};
    m_forageNodes.clear();
    m_fishingSpots.clear();
    m_foragePlanTimer = 0.0f;
    m_foragePlanValid = false;
    m_radioPartDefId =
        config.Get<std::string>("objective.radioPart", "radio_part");
    m_radioX = static_cast<float>(config.Get<double>("objective.radioX", 72.5));
    m_radioY = static_cast<float>(config.Get<double>("objective.radioY", 20.5));
    m_extractionX =
        static_cast<float>(config.Get<double>("objective.extractionX", 66.5));
    m_extractionY =
        static_cast<float>(config.Get<double>("objective.extractionY", 28.5));
    m_extractionRadius = std::clamp(
        static_cast<float>(config.Get<double>("objective.extractionRadius", 1.5)),
        0.25f, 8.0f);
    m_objectiveInteractRange = std::clamp(
        static_cast<float>(config.Get<double>("objective.interactRange", 2.2)),
        0.25f, 8.0f);
    m_repairKitDefId =
        config.Get<std::string>("inventory.repairKit", "repair_kit");
    m_repairKitCount = std::max(1, config.Get<int>("inventory.repairKitCount", 1));
    m_repairAmount = std::clamp(
        static_cast<float>(config.Get<double>("inventory.repairAmount", 0.5)),
        0.01f, 1.0f);
    m_vehicleTuning = kVehicleDefaultTuning;
    m_vehicleTuning.maxSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.maxSpeed", kVehicleMaxSpeed)),
        1.0f, 30.0f);
    m_vehicleTuning.acceleration = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.acceleration",
                                               kVehicleAcceleration)),
        1.0f, 50.0f);
    m_vehicleTuning.braking = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.braking", kVehicleBraking)),
        1.0f, 50.0f);
    m_vehicleTuning.radius = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.radius", kVehicleRadius)),
        0.2f, 2.0f);
    m_vehicleTuning.enterRange = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.enterRange",
                                               kVehicleEnterRange)),
        0.5f, 6.0f);
    m_vehicleTuning.fuelPerSecond = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.fuelPerSecond",
                                               kVehicleFuelPerSecond)),
        0.0f, 1.0f);
    m_vehicleTuning.spawnOffsetX = std::clamp(
        config.Get<int>("vehicle.spawnOffsetX", kVehicleSpawnOffsetX), -16, 16);
    m_vehicleTuning.spawnOffsetY = std::clamp(
        config.Get<int>("vehicle.spawnOffsetY", kVehicleSpawnOffsetY), -16, 16);
    m_vehicleTuning.spawnSearchRadius = std::clamp(
        config.Get<int>("vehicle.spawnSearchRadius", kVehicleSpawnSearchRadius), 0,
        8);
    m_localVehicleCount = std::clamp(
        config.Get<int>("vehicle.count", 3), 1, 16);
    m_vehicleTuning.crashMinSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.crashMinSpeed",
                                               kVehicleCrashMinSpeed)),
        0.0f, 30.0f);
    m_vehicleTuning.crashDamagePerSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.crashDamagePerSpeed",
                                               kVehicleCrashDamagePerSpeed)),
        0.0f, 1.0f);
    m_vehicleTuning.maxCrashDamage = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.maxCrashDamage",
                                               kVehicleMaxCrashDamage)),
        0.0f, 1.0f);
    m_vehicleTuning.ramMinSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramMinSpeed",
                                               kVehicleRamMinSpeed)),
        0.0f, 40.0f);
    m_vehicleTuning.ramBaseDamage = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramBaseDamage",
                                               kVehicleRamBaseDamage)),
        0.0f, 500.0f);
    m_vehicleTuning.ramDamagePerSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramDamagePerSpeed",
                                               kVehicleRamDamagePerSpeed)),
        0.0f, 200.0f);
    m_vehicleTuning.ramKnockbackPerSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramKnockbackPerSpeed",
                                               kVehicleRamKnockbackPerSpeed)),
        0.0f, 50.0f);
    m_vehicleTuning.ramKnockbackBase = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramKnockbackBase",
                                               kVehicleRamKnockbackBase)),
        0.0f, 50.0f);
    m_vehicleTuning.ramStaggerBase = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramStaggerBase",
                                               kVehicleRamStaggerBase)),
        0.0f, 5.0f);
    m_vehicleTuning.ramStaggerPerSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramStaggerPerSpeed",
                                               kVehicleRamStaggerPerSpeed)),
        0.0f, 1.0f);
    m_vehicleTuning.ramCooldownSeconds = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramCooldownSeconds",
                                               kVehicleRamCooldownSeconds)),
        0.05f, 10.0f);
    m_vehicleTuning.ramConditionDamageBase = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.ramConditionDamageBase",
            kVehicleRamConditionDamageBase)),
        0.0f, 1.0f);
    m_vehicleTuning.ramConditionDamagePerSpeed = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.ramConditionDamagePerSpeed",
            kVehicleRamConditionDamagePerSpeed)),
        0.0f, 1.0f);
    m_vehicleTuning.ramConditionDamageMax = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.ramConditionDamageMax",
            kVehicleRamConditionDamageMax)),
        0.0f, 1.0f);
    m_vehicleTuning.vehicleCollisionMinSpeed = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionMinSpeed",
            kVehicleCollisionMinSpeed)),
        0.0f, m_vehicleTuning.maxSpeed);
    m_vehicleTuning.vehicleCollisionConditionDamageBase = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionConditionDamageBase",
            kVehicleCollisionConditionDamageBase)),
        0.0f, 1.0f);
    m_vehicleTuning.vehicleCollisionConditionDamagePerSpeed = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionConditionDamagePerSpeed",
            kVehicleCollisionConditionDamagePerSpeed)),
        0.0f, 1.0f);
    m_vehicleTuning.vehicleCollisionConditionDamageMax = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionConditionDamageMax",
            kVehicleCollisionConditionDamageMax)),
        0.0f, 1.0f);
    m_vehicleTuning.vehicleCollisionCooldownSeconds = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionCooldownSeconds",
            kVehicleCollisionCooldownSeconds)),
        0.05f, 10.0f);
    m_vehicleTuning.vehicleCollisionPushPerSpeed = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionPushPerSpeed",
            kVehicleCollisionPushPerSpeed)),
        0.0f, 2.0f);
    m_vehicleTuning.vehicleCollisionPushMax = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionPushMax",
            kVehicleCollisionPushMax)),
        0.0f, 4.0f);
    m_vehicleTuning.vehicleCollisionRestitution = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionRestitution",
            kVehicleCollisionRestitution)),
        0.0f, 1.0f);
    m_vehicleTuning.vehicleCoastBraking = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCoastBraking", kVehicleCoastBraking)),
        0.5f, 20.0f);
    m_vehicleTuning.vehicleCollisionFriction = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionFriction",
            kVehicleCollisionFriction)),
        0.0f, 1.0f);
    m_vehicleTuning.vehicleAngularDamping = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleAngularDamping", kVehicleAngularDamping)),
        0.0f, 20.0f);
    m_vehicleTuning.vehicleCollisionOccupantDamageBase = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionOccupantDamageBase",
            kVehicleCollisionOccupantDamageBase)),
        0.0f, 50.0f);
    m_vehicleTuning.vehicleCollisionOccupantDamagePerSpeed = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionOccupantDamagePerSpeed",
            kVehicleCollisionOccupantDamagePerSpeed)),
        0.0f, 20.0f);
    m_vehicleTuning.vehicleCollisionOccupantDamageMax = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.vehicleCollisionOccupantDamageMax",
            kVehicleCollisionOccupantDamageMax)),
        0.0f, 100.0f);
    m_vehicleTuning.driverDamageReduction = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.driverDamageReduction",
            kVehicleDriverDamageReduction)),
        0.0f, 0.95f);
    m_vehicleTuning.seatHandoffMaxSpeed = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.seatHandoffMaxSpeed", kVehicleSeatHandoffMaxSpeed)),
        0.0f, 2.0f);
    m_vehicleTuning.zombieAttackConditionDamage = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.zombieAttackConditionDamage",
            kVehicleZombieAttackConditionDamage)),
        0.0f, 1.0f);
    m_vehicleTuning.zombieAttackCooldownSeconds = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.zombieAttackCooldownSeconds",
            kVehicleZombieAttackCooldownSeconds)),
        0.05f, 10.0f);
    m_vehicleTuning.noiseRadius = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.noiseRadius",
                                               kVehicleNoiseRadius)),
        0.0f, 64.0f);
    m_vehicleTuning.noiseIntervalSeconds = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.noiseIntervalSeconds",
                                               kVehicleNoiseIntervalSeconds)),
        0.05f, 10.0f);
    m_vehicleTuning.noiseMinSpeed = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.noiseMinSpeed",
                                               kVehicleNoiseMinSpeed)),
        0.0f, 40.0f);
    m_vehicleTuning.hornNoiseRadius = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.hornNoiseRadius",
                                               kVehicleHornNoiseRadius)),
        0.0f, 64.0f);
    m_vehicleTuning.hornCooldownSeconds = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.hornCooldownSeconds",
                                               kVehicleHornCooldownSeconds)),
        0.1f, 10.0f);
    m_vehicleTuning.dirtMaxSpeedScale = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.dirtMaxSpeedScale",
                                               kVehicleDirtMaxSpeedScale)),
        0.0f, 1.0f);
    m_vehicleTuning.dirtAccelerationScale = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.dirtAccelerationScale",
            kVehicleDirtAccelerationScale)),
        0.0f, 1.0f);
    m_vehicleTuning.dirtBrakingScale = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.dirtBrakingScale",
                                               kVehicleDirtBrakingScale)),
        0.0f, 1.0f);
    m_vehicleTuning.grassMaxSpeedScale = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.grassMaxSpeedScale",
                                               kVehicleGrassMaxSpeedScale)),
        0.0f, 1.0f);
    m_vehicleTuning.grassAccelerationScale = std::clamp(
        static_cast<float>(config.Get<double>(
            "vehicle.grassAccelerationScale",
            kVehicleGrassAccelerationScale)),
        0.0f, 1.0f);
    m_vehicleTuning.grassBrakingScale = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.grassBrakingScale",
                                               kVehicleGrassBrakingScale)),
        0.0f, 1.0f);
    m_vehicleTuning.engineIdleVolume = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.engineIdleVolume",
                                               kVehicleEngineIdleVolume)),
        0.0f, 1.0f);
    m_vehicleTuning.engineMaxVolume = std::max(
        m_vehicleTuning.engineIdleVolume,
        std::clamp(static_cast<float>(config.Get<double>(
                       "vehicle.engineMaxVolume", kVehicleEngineMaxVolume)),
                   0.0f, 1.0f));
    m_vehicleTuning.engineMinPitch = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.engineMinPitch",
                                               kVehicleEngineMinPitch)),
        0.25f, 4.0f);
    m_vehicleTuning.engineMaxPitch = std::max(
        m_vehicleTuning.engineMinPitch,
        std::clamp(static_cast<float>(config.Get<double>(
                       "vehicle.engineMaxPitch", kVehicleEngineMaxPitch)),
                   0.25f, 4.0f));
    m_vehicleTuning.ramSoundVolume = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.ramSoundVolume",
                                               kVehicleRamSoundVolume)),
        0.0f, 1.0f);
    m_vehicleTuning.repairAmount = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.repairAmount",
                                               kVehicleRepairAmount)),
        0.0f, 1.0f);
    m_vehicleRepairItem =
        config.Get<std::string>("vehicle.repairItem", "repair_kit");
    if (m_vehicleRepairItem.empty()) {
        m_vehicleRepairItem = "repair_kit";
    }
    m_vehicleTuning.fuelPerCan = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.fuelPerCan",
                                               kVehicleFuelPerCan)),
        0.0f, 1.0f);
    m_vehicleFuelItem =
        config.Get<std::string>("vehicle.fuelItem", "fuel_can");
    if (m_vehicleFuelItem.empty()) {
        m_vehicleFuelItem = "fuel_can";
    }
    m_vehicleTuning.trunkCapacityKg = std::clamp(
        static_cast<float>(config.Get<double>("vehicle.trunkCapacityKg", 40.0)),
        1.0f, 200.0f);
    m_vehicleTuning.trunkStacks =
        std::clamp(config.Get<int>("vehicle.trunkStacks", 6), 1, 6);
    m_stationRange = std::clamp(
        static_cast<float>(config.Get<double>("crafting.stationRange", 2.5)),
        0.0f, 10.0f);
    m_spoiledFoodDamage = std::clamp(
        static_cast<float>(config.Get<double>("food.spoiledDamage", 4.0)), 0.0f,
        100.0f);
    m_foodWarningThreshold = std::clamp(
        static_cast<float>(config.Get<double>("food.warningThreshold", 0.2)), 0.01f,
        1.0f);
    m_foodSafety.spoiledPoisonChance = std::clamp(
        static_cast<float>(config.Get<double>("food.spoiledPoisonChance", 0.85)),
        0.0f, 1.0f);
    m_foodSafety.poisonDurationSeconds = std::max(
        0.0f, static_cast<float>(
                  config.Get<double>("food.poisonDurationSeconds", 45.0)));
    m_foodSafety.poisonDamagePerSecond = std::max(
        0.0f, static_cast<float>(
                  config.Get<double>("food.poisonDamagePerSecond", 0.12)));
    m_foodSafety.nutritionDecayPerDay = std::max(
        0.0f, static_cast<float>(
                  config.Get<double>("food.nutritionDecayPerDay", 0.5)));
    m_foodSafety.nutritionPoisonReduction = std::clamp(
        static_cast<float>(
            config.Get<double>("food.nutritionPoisonReduction", 0.5)),
        0.0f, 1.0f);
    m_sleepConfig.minHours = std::clamp(config.Get<int>("sleep.minHours", 1), 1, 8);
    m_sleepConfig.maxHours =
        std::clamp(config.Get<int>("sleep.maxHours", 8), m_sleepConfig.minHours, 8);
    m_sleepConfig.dawnHour =
        std::clamp(config.Get<int>("sleep.dawnHour", 7), 0, 23);
    m_sleepConfig.gameMinutesPerSecond = std::clamp(
        static_cast<float>(config.Get<double>("sleep.gameMinutesPerSecond", 120.0)),
        1.0f, 600.0f);
    m_sleepConfig.fatigueRecoveryPerHour = std::clamp(
        static_cast<float>(config.Get<double>("sleep.fatigueRecoveryPerHour", 0.15)),
        0.0f, 1.0f);
    m_sleepConfig.zombieRadius = std::clamp(
        static_cast<float>(config.Get<double>("sleep.zombieRadius", 8.0)), 0.0f,
        30.0f);
    m_sleepConfig.interruptOnDamage =
        config.Get<bool>("sleep.interruptOnDamage", true);
    m_sleepConfig.interruptOnZombie =
        config.Get<bool>("sleep.interruptOnZombie", true);
    m_sleepEnabled = config.Get<bool>("sleep.enabled", true);
    m_sleepHungerPerDay =
        std::max(0.0f, static_cast<float>(config.Get<double>("needs.hungerPerDay", 1.4)));
    m_sleepThirstPerDay =
        std::max(0.0f, static_cast<float>(config.Get<double>("needs.thirstPerDay", 1.8)));
    m_sleepSelectedHours =
        std::clamp(6, m_sleepConfig.minHours, m_sleepConfig.maxHours);
    m_sleepSelecting = false;
    m_sleepSelectUntilDawn = false;
    m_sleep.Stop();
    m_sleepBedName.clear();
    m_stationLabels.clear();
    m_stationLabels.emplace("workbench", "工作台");
    m_stationLabels.emplace("campfire", "篝火");
    m_stationLabels.emplace("locker", "雨水收集器");
    const auto& root = config.Raw();
    const auto crafting = root.find("crafting");
    if (crafting != root.end() && crafting->is_object()) {
        const auto labels = crafting->find("stationLabels");
        if (labels != crafting->end() && labels->is_object()) {
            for (const auto& [id, label] : labels->items()) {
                if (id.empty() || !label.is_string() || label.get<std::string>().empty()) {
                    ZL_WARN("crafting", "忽略非法制作站标签：{}", id);
                    continue;
                }
                m_stationLabels[id] = label.get<std::string>();
            }
        }
    }
    m_buildWoodItem = config.Get<std::string>("building.woodItem", "wood");
    m_buildRange = std::clamp(
        static_cast<float>(config.Get<double>("building.range", 3.5)), 0.5f, 8.0f);
    m_buildZombieDamage = std::clamp(
        static_cast<float>(config.Get<double>("building.zombieDamage", 12.0)),
        0.0f, 1000.0f);
    m_buildZombieCooldown = std::clamp(
        static_cast<float>(
            config.Get<double>("building.zombieAttackCooldownSeconds", 1.0)),
        0.05f, 60.0f);
    m_breakerDamage = std::clamp(
        static_cast<float>(config.Get<double>("baseEvent.breakerDamage", 28.0)),
        0.0f, 1000.0f);
    m_breakerCooldown = std::clamp(
        static_cast<float>(
            config.Get<double>("baseEvent.breakerCooldownSeconds", 1.2)),
        0.05f, 60.0f);
    m_breakerRange = std::clamp(
        static_cast<float>(config.Get<double>("baseEvent.breakerRange", 2.2)),
        0.5f, 8.0f);
    m_breakerWindup = std::clamp(
        static_cast<float>(
            config.Get<double>("baseEvent.breakerWindupSeconds", 0.9)),
        0.0f, 5.0f);
    m_buildActionRange = std::clamp(
        static_cast<float>(config.Get<double>("building.actionRange", 2.5)),
        0.5f, 8.0f);
    m_buildActionCooldownSeconds = std::clamp(
        static_cast<float>(
            config.Get<double>("building.actionCooldownSeconds", 0.5)),
        0.0f, 10.0f);
    m_repairWoodCost = std::clamp(
        config.Get<int>("building.repairWoodCost", 1), 0, 99);
    m_structureRepairAmount = std::clamp(
        static_cast<float>(config.Get<double>("building.repairAmount", 30.0)),
        0.0f, 100000.0f);
    m_demolishRefundRatio = std::clamp(
        static_cast<float>(
            config.Get<double>("building.demolishRefundRatio", 0.5)),
        0.0f, 1.0f);
    m_buildActionCooldown = 0.0f;
    RebuildBuildables();
    m_dynamicContentApplied = m_ctx.dynamicContentCount;
    m_contentReloadApplied = m_ctx.contentReloadCount;
    m_buildIndex = 0;
    m_buildMode = false;
    m_buildOrientationVertical = false;
    m_barricadeScanTimer = 0.0f;
    m_barricades.clear();
    m_barricadeAttackCooldowns.clear();
    m_baseHudScanTimer = 0.0f;
    m_baseHud = {};
    InitializeBaseSiege();
    InitializeHomestead();
    m_blueprintRecipes.clear();
    if (m_ctx.items != nullptr && m_ctx.recipes != nullptr) {
        for (const items::ItemDef& def : m_ctx.items->All()) {
            for (const std::string& recipeId : def.unlocksRecipes) {
                if (m_ctx.recipes->Find(recipeId) != nullptr) {
                    m_blueprintRecipes.insert(recipeId);
                }
            }
        }
    }
    m_demoItemsScattered = false;
    m_demoLootContainersSpawned = false;
    m_houseLootContainersSpawned = false;
    m_runOutcome = RunOutcome::None;
    m_runStatsPlayer = entt::null;
    m_runSurvivalSeconds = 0.0;
    m_runInjuries = 0;
    m_runDamageTaken = 0.0f;
    m_runLastHealth = -1.0f;
    m_runHitSerial = 0;
    m_runDeathCause = ecs::DamageCause::Unknown;
    if (m_cruiseEnabled) {
        camera.SetCenter(render::ToScreen(m_cruiseCenterX + m_cruiseRadius, m_cruiseCenterY));
        ZL_INFO("perf", "自动巡航开启：圆心=({:.0f},{:.0f}) 半径={:.0f} 速度={:.0f}格/s 时长={:.1f}min",
                m_cruiseCenterX, m_cruiseCenterY, m_cruiseRadius, m_cruiseSpeed, m_cruiseMinutes);
    }
    ApplyCameraMode();

    m_ctx.onSaveFinished =
        [this](bool ok, bool created, const std::string& id,
               const std::string& error) {
            HandleSaveFinished(ok, created, id, error);
        };
    m_ctx.quickSave = [this]() -> bool {
        if (m_networkMode || m_ctx.saveManager == nullptr ||
            m_ctx.saveService == nullptr || m_ctx.saveComponents == nullptr ||
            m_ctx.ecs == nullptr || m_savePending || m_saveRequestPending) {
            return false;
        }
        RequestQuickSave(true);
        return true;
    };
    m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
    m_moveCommandValid = false;
    m_ctx.executePlayerCommand =
        [this](const net::Command& command, std::string& error) {
            return ExecutePlayerCommand(command, error);
        };

    m_loadedFromSave = !m_networkMode && !m_loadSaveId.empty() && LoadFromSave();
    if (!m_loadedFromSave && !m_networkMode && m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetTotalGameMinutes(0.0);
    }
    if (!m_loadedFromSave && !m_networkMode) {
        // M4-T2/T3：ECS 演示实体（村庄附近漂移；物理体由 Bullet 驱动）
        SpawnDemoEntities();
    }
    // 单机：玩家实体由 OnFrame 延迟生成（等目标 chunk 加载完成）。
    // 网络：等待服务器分配 StableId + 首个全量快照，再生成纯渲染副本。
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->HideMenu();
        m_ctx.ui->SetInventoryActionHandler(
            [this](const ui::InventoryAction& action) { SubmitInventoryAction(action); });
        m_ctx.ui->SetMenuActionHandler(
            [this](ui::MenuActionType action) { HandleMenuAction(action); });
        if (m_networkMode) {
            m_ctx.ui->SetSocialChatHandler(
                [this](const std::string& text, std::string& error) {
                    SendChat(text, error);
                    return error.empty();
                });
            RefreshSocialState();
        } else {
            m_ctx.ui->SetSocialChatHandler({});
            m_ctx.ui->SetSocialState({});
        }
    }
    ConsumeSaveWarning();

    ZL_INFO("render", "按键：WASD 移动玩家 | 左键 点击移动 | SPACE 轻击 | LSHIFT 重击 | LCTRL 格挡 | Z 闪避 | F 拾取 | E 使用/修理电台/床边睡觉/觅食 | R 装备/换弹（建造模式旋转优先） | G 丢弃 | C 角色 | I 背包（或右下角按钮） | Enter 上下??| Y 聊天（联机） | F6 演示击杀 | F7 调试枪声（声音引怪） | V 远程开??联机投射??| F4 自由相机 | 方向??右键拖拽 平移相机 | 滚轮 缩放 | F8 自动巡航 | F9 网格??| F10 物理线框 | F1 暂停 | F2 倍??| F5 热重??| F12 快速存??| ESC 暂停菜单");
}

bool WorldDemoState::LoadFromSave() {
    if (m_ctx.saveManager == nullptr || m_ctx.ecs == nullptr ||
        m_ctx.saveComponents == nullptr) {
        m_ctx.saveWarning = "读取存档失败：存档服务未初始化";
        return false;
    }
    m_baseSiege.Abort({}, false);
    m_crafting.Cancel();
    m_craftBatchTotal = 0;
    m_craftBatchCompleted = 0;
    m_craftQueue.clear();

    save::SaveMetadata metadata;
    save::EntitySnapshot snapshot;
    save::SaveManager::LoadReport report;
    std::string error;
    const save::SaveManager::LoadResult result = m_ctx.saveManager->Load(
        m_loadSaveId, *m_ctx.saveComponents, &snapshot, &metadata, &error, &report);
    if (result != save::SaveManager::LoadResult::Ok) {
        m_ctx.saveWarning = "读取存档失败（" + m_loadSaveId + "）：" + error +
                            "；建议删除存档或回退备份";
        ZL_ERROR("save", "读取存档失败 '{}'：{}", m_loadSaveId, error);
        m_loadSaveId.clear();
        return false;
    }
    const std::string currentMap =
        m_ctx.chunkManager != nullptr ? m_ctx.chunkManager->Map().name : std::string();
    if (!metadata.mapId.empty() && !currentMap.empty() &&
        metadata.mapId != currentMap) {
        m_ctx.saveWarning = "存档地图不匹配：" + metadata.mapId + "（当前 " +
                            currentMap + "??";
        ZL_ERROR("save", "{}", m_ctx.saveWarning);
        m_loadSaveId.clear();
        return false;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    std::vector<ecs::Entity> oldEntities;
    registry.view<ecs::StableId>().each(
        [&](const ecs::Entity entity, const ecs::StableId&) {
            if (m_ctx.ecs->Alive(entity)) {
                oldEntities.push_back(entity);
            }
        });
    for (const ecs::Entity entity : oldEntities) {
        if (m_ctx.physics != nullptr) {
            if (const auto* rigid = registry.try_get<ecs::RigidBody>(entity);
                rigid != nullptr && rigid->body != nullptr) {
                m_ctx.physics->DestroyBody(rigid->body);
            }
        }
        m_ctx.ecs->Destroy(entity);
    }
    m_ctx.ecs->Flush();
    m_player = entt::null;
    StopVehicleAudio();
    m_vehicle = entt::null;
    m_driving = false;
    m_riding = false;
    m_vehicleNoiseCooldown = 0.0f;
    m_vehicleRamWearCooldown = 0.0f;
    m_vehicleBodyCollisionCooldown = 0.0f;
    m_vehicleHornCooldown = 0.0f;
    m_vehicleZombieAttackCooldown = 0.0f;
    m_networkVehicle = {};
    m_networkCombat = {};
    m_vehiclePredictionActive = false;
    m_vehiclePredictionTick = 0;
    m_playerSpriteBackup.reset();
    m_vehicleMotion = {};
    m_localParkedVehicleMotions.clear();

    std::vector<ecs::Entity> loaded;
    if (!save::LoadEntities(snapshot, *m_ctx.ecs, *m_ctx.saveComponents, &loaded,
                            &error)) {
        m_ctx.saveWarning = "读取实体失败：" + error;
        ZL_ERROR("save", "{}", m_ctx.saveWarning);
        m_loadSaveId.clear();
        return false;
    }

    if (m_ctx.simulation != nullptr) {
        if (SettlementNpcSystem* settlementSystem =
                m_ctx.simulation->SettlementNpcs();
            settlementSystem != nullptr) {
            settlementSystem->Reset();
        }
    }
    ApplySettlementState();
    RestoreRuntimeComponents(loaded);
    {
        int corpseCount = 0;
        int harvestedCount = 0;
        int corpseItems = 0;
        std::string corpseItemIds;
        registry.view<ecs::AnimalCorpse>().each(
            [&](const ecs::Entity entity, const ecs::AnimalCorpse& corpse) {
                ++corpseCount;
                if (corpse.harvested) {
                    ++harvestedCount;
                }
                const auto* container =
                    registry.try_get<ecs::Container>(entity);
                if (container == nullptr) {
                    return;
                }
                corpseItems += static_cast<int>(container->items.size());
                for (const ecs::ItemInstance& item : container->items) {
                    if (corpseItemIds.size() >= 160) {
                        break;
                    }
                    if (!corpseItemIds.empty()) {
                        corpseItemIds += ",";
                    }
                    corpseItemIds += item.defId;
                }
            });
        ZL_INFO("animals", "读档动物尸体：{} 具（已剥取 {}），战利品 {} 件{}",
                corpseCount, harvestedCount, corpseItems,
                corpseItemIds.empty() ? std::string()
                                      : "??" + corpseItemIds + "）；");
        if (m_player != entt::null) {
            const auto* bag = registry.try_get<ecs::Container>(m_player);
            ZL_INFO("save", "读档玩家背包：{} 件",
                    bag != nullptr ? bag->items.size() : 0);
        }
    }
    if (m_ctx.animalSystem != nullptr) {
        m_ctx.animalSystem->Reset();
        m_ctx.animalSystem->LoadState(*m_ctx.ecs);
    }
    if (const BuildableDef* floodlight = BuildableById("floodlight");
        floodlight != nullptr) {
        EnsureFloodlightHealth(*m_ctx.ecs, floodlight->maxHp);
    }
    RestoreCraftingJobFromEcs();
    if (m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetTotalGameMinutes(metadata.gameMinutes);
    }
    m_lastSurvivalXpMinute = metadata.gameMinutes;
    m_progressionPlayer = entt::null;
    if (m_player != entt::null) {
        InitializeProgressionRuntime();
    }
    m_saveId = report.usedBackup ? std::string() : m_loadSaveId;
    m_ctx.activeSaveId = m_loadSaveId;
    m_ctx.activeSaveUseBackup = report.usedBackup;
    m_demoItemsScattered = true;
    m_demoLootContainersSpawned = true;
    m_houseLootContainersSpawned = false;
    InitializePoiRuntime();
    InitializeMissionRuntime();
    m_followSnapped = false;
    if (m_ctx.virtualHordes != nullptr) {
        m_ctx.virtualHordes->Clear();
    }
    if (report.usedBackup) {
        m_ctx.saveWarning = report.warning + "；本次会话保存将新建存档";
    }
    m_baseSiege.Reset();
    ZL_INFO("save", "已读取存档 '{}'：实体 {}，玩家 {}，缩略图 {}", m_saveId,
            loaded.size(), m_player != entt::null ? 1 : 0,
            metadata.thumbnail.empty() ? "??" : metadata.thumbnail);
    return true;
}

void WorldDemoState::RestoreRuntimeComponents(
    const std::vector<ecs::Entity>& loaded) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    int players = 0;
    int characters = 0;
    for (const ecs::Entity entity : loaded) {
        if (!m_ctx.ecs->Alive(entity)) {
            continue;
        }
        const auto* position = registry.try_get<ecs::Position>(entity);
        if (position == nullptr) {
            continue;
        }
        if (registry.all_of<ecs::Animal>(entity)) {
            if (!registry.all_of<ecs::AnimalWander>(entity)) {
                registry.emplace<ecs::AnimalWander>(entity);
            }
            if (!registry.all_of<ecs::Flee>(entity)) {
                registry.emplace<ecs::Flee>(entity);
            }
            if (!registry.all_of<ecs::LodState>(entity)) {
                registry.emplace<ecs::LodState>(entity);
            }
            continue;
        }
        const bool isPlayer = registry.all_of<ecs::PlayerTag>(entity);
        const bool isGroundItem = registry.all_of<ecs::ItemInstance>(entity);
        const bool isCorpse = registry.all_of<ecs::Corpse>(entity);
        const bool isFacility = registry.any_of<
            ecs::FarmPlot, ecs::Generator, ecs::ColdStorage, ecs::Floodlight,
            ecs::SpikeTrap, ecs::PlayerStorage, ecs::MerchantStock>(entity);
        const bool isCharacter =
            isPlayer || (registry.all_of<ecs::Health>(entity) &&
                         !isGroundItem && !isCorpse && !isFacility);
        if (!isCharacter) {
            continue;
        }

        float visualScale = 1.0f;
        if (!isPlayer) {
            if (const auto* variant = registry.try_get<ecs::ZombieVariant>(entity);
                variant != nullptr) {
                visualScale = std::clamp(variant->visualScale, 0.5f, 2.0f);
            }
        }
        if (!registry.all_of<ecs::Collider>(entity)) {
            const ecs::Collider defaults;
            registry.emplace<ecs::Collider>(
                entity, ecs::Collider{defaults.radius * visualScale,
                                      defaults.height * visualScale, defaults.category,
                                      defaults.mask});
        }
        if (m_ctx.physics != nullptr &&
            !registry.all_of<ecs::RigidBody>(entity)) {
            const auto* collider = registry.try_get<ecs::Collider>(entity);
            const ecs::Collider defaults;
            const float radius = collider != nullptr ? collider->radius : defaults.radius;
            const float height = collider != nullptr ? collider->height : defaults.height;
            ecs::RigidBody rigid;
            rigid.body = m_ctx.physics->CreateCharacterBody(
                radius, height, position->x, position->y, position->z);
            registry.emplace<ecs::RigidBody>(entity, rigid);
        }
        if (!registry.all_of<ecs::AnimState>(entity)) {
            registry.emplace<ecs::AnimState>(entity);
        }
        if (!registry.all_of<ecs::ActionState>(entity)) {
            registry.emplace<ecs::ActionState>(entity);
        }
        if (!registry.all_of<ecs::MoveInput>(entity)) {
            registry.emplace<ecs::MoveInput>(entity);
        }
        if (!registry.all_of<ecs::MoveIntent>(entity)) {
            registry.emplace<ecs::MoveIntent>(entity);
        }
        if (!registry.all_of<ecs::PathRequest>(entity)) {
            registry.emplace<ecs::PathRequest>(entity);
        }
        if (!registry.all_of<ecs::PathFollow>(entity)) {
            registry.emplace<ecs::PathFollow>(entity);
        }
        if (!registry.all_of<ecs::AttackIntent>(entity)) {
            registry.emplace<ecs::AttackIntent>(entity);
        }

        if (isPlayer) {
            if (!registry.all_of<ecs::LocalControl>(entity)) {
                registry.emplace<ecs::LocalControl>(entity);
            }
            if (!registry.all_of<ecs::Container>(entity)) {
                registry.emplace<ecs::Container>(
                    entity, ecs::Container{{}, m_playerCapacityKg});
            }
            if (!registry.all_of<ecs::Equipment>(entity)) {
                registry.emplace<ecs::Equipment>(entity);
            }
            if (!registry.all_of<ecs::KnownRecipes>(entity)) {
                registry.emplace<ecs::KnownRecipes>(entity);
            }
            if (!registry.all_of<ecs::CraftingJob>(entity)) {
                registry.emplace<ecs::CraftingJob>(entity);
            }
            if (!registry.all_of<ecs::Progression>(entity)) {
                registry.emplace<ecs::Progression>(entity);
            }
            if (!registry.all_of<ecs::Pain>(entity)) {
                registry.emplace<ecs::Pain>(entity);
            }
            if (!registry.all_of<ecs::Infection>(entity)) {
                registry.emplace<ecs::Infection>(entity);
            }
            if (!registry.all_of<ecs::Climate>(entity)) {
                registry.emplace<ecs::Climate>(entity);
            }
            if (!registry.all_of<ecs::Objective>(entity)) {
                registry.emplace<ecs::Objective>(entity);
            }
            if (!registry.all_of<ecs::PoiTasks>(entity)) {
                registry.emplace<ecs::PoiTasks>(entity);
            }
            if (!registry.all_of<ecs::MissionLog>(entity)) {
                registry.emplace<ecs::MissionLog>(entity);
            }
            if (!registry.all_of<ecs::ForageState>(entity)) {
                registry.emplace<ecs::ForageState>(entity);
            }
            if (m_player == entt::null) {
                m_player = entity;
            }
            ++players;
        } else {
            if (!registry.all_of<ecs::AiState>(entity)) {
                registry.emplace<ecs::AiState>(entity);
            }
            if (!registry.all_of<ecs::PerceptionState>(entity)) {
                ecs::PerceptionState perception;
                perception.lastKnownX = position->x;
                perception.lastKnownY = position->y;
                registry.emplace<ecs::PerceptionState>(entity, perception);
            }
            if (!registry.all_of<ecs::LodState>(entity)) {
                registry.emplace<ecs::LodState>(entity);
            }
            ++characters;
        }
    }
    ZL_INFO("save", "运行时组件补建：玩家 {}，僵尸 {}", players, characters);
}

bool WorldDemoState::CaptureSaveChunks(std::vector<save::ChunkDiff>* out,
                                       std::string* error) {
    if (out == nullptr || m_ctx.chunkManager == nullptr) {
        if (error != nullptr) {
            *error = "chunk 快照输出为空";
        }
        return false;
    }
    bool ok = true;
    m_ctx.chunkManager->ForEachLoadedChunk(
        [&](const world::Chunk& chunk) {
            if (!ok) {
                return;
            }
            save::ChunkDiff diff;
            if (!save::CaptureChunkDiff(chunk, &diff, error)) {
                ok = false;
                return;
            }
            out->push_back(std::move(diff));
        });
    return ok;
}

void WorldDemoState::RequestQuickSave(bool withThumbnail) {
    if (m_networkMode) {
        SetNotice("multiplayer: server owns the world", kNoticeInfoColor, 2.0f);
        return;
    }
    if (m_savePending || m_saveRequestPending) {
        SetNotice("存档正在进行中", kNoticeInfoColor, 1.5f);
        return;
    }
    if (m_ctx.saveManager == nullptr || m_ctx.saveService == nullptr ||
        m_ctx.saveComponents == nullptr || m_ctx.ecs == nullptr) {
        SetNotice("存档服务未就绪", kNoticeFailColor, 2.5f);
        return;
    }
    if (m_baseSiege.Active()) {
        m_baseSiege.Abort({}, false);
        ZL_INFO("siege", "保存前中止守夜并清理事件僵尸");
    }

    UpdateMissions();
    SyncCraftingJobToEcs();
    if (m_ctx.animalSystem != nullptr) {
        m_ctx.animalSystem->StoreState(*m_ctx.ecs);
    }
    StoreSettlementState();
    SyncVehicleStateToEcs();
    save::EntitySnapshot entities;
    std::string error;
    const auto withoutEphemeral = [this](ecs::Entity entity) {
        const entt::registry& registry = m_ctx.ecs->Registry();
        const bool transient =
            registry.any_of<ecs::Projectile, ecs::TransientEffect>(entity);
        const bool temporaryNpc =
            registry.all_of<SettlementNpc>(entity) &&
            !registry.all_of<ecs::MerchantStock>(entity);
        const bool raidZombie =
            registry.all_of<SettlementRaidZombie>(entity);
        return !transient && !temporaryNpc && !raidZombie;
    };
    bool restoreDrivingSprite = false;
    if (m_driving && m_ctx.ecs->Alive(m_player) &&
        m_playerSpriteBackup.has_value()) {
        entt::registry& registry = m_ctx.ecs->Registry();
        if (!registry.all_of<ecs::SpriteRef>(m_player)) {
            registry.emplace<ecs::SpriteRef>(m_player, *m_playerSpriteBackup);
            restoreDrivingSprite = true;
        }
    }
    const bool captured = save::CaptureEntities(
        *m_ctx.ecs, *m_ctx.saveComponents, &entities, withoutEphemeral, &error);
    if (restoreDrivingSprite && m_ctx.ecs->Alive(m_player) && m_driving) {
        m_ctx.ecs->Registry().remove<ecs::SpriteRef>(m_player);
    }
    if (!captured) {
        SetNotice("保存快照失败：" + error, kNoticeFailColor, 3.5f);
        ZL_ERROR("save", "保存快照失败：{}", error);
        return;
    }
    std::vector<save::ChunkDiff> chunks;
    if (!CaptureSaveChunks(&chunks, &error)) {
        SetNotice("保存 chunk 失败??" + error, kNoticeFailColor, 3.5f);
        ZL_ERROR("save", "保存 chunk 快照失败：{}", error);
        return;
    }

    m_pendingSave = save::SaveRequest{};
    m_pendingSave.id = m_saveId;
    m_pendingSave.name = "快速存??";
    m_pendingSave.mapId =
        m_ctx.chunkManager != nullptr ? m_ctx.chunkManager->Map().name : std::string();
    m_pendingSave.gameVersion =
        m_ctx.config->Get<std::string>("save.gameVersion", save::kDefaultGameVersion);
    m_pendingSave.gameMinutes =
        m_ctx.gameTime != nullptr ? m_ctx.gameTime->TotalGameMinutes() : 0.0;
    m_pendingSave.entities = std::move(entities);
    m_pendingSave.chunks = std::move(chunks);

    if (m_player != entt::null && m_ctx.ecs->Alive(m_player)) {
        const std::uint64_t playerId = m_ctx.ecs->IdOf(m_player).id;
        if (playerId != 0) {
            m_pendingSave.players.push_back(
                save::SavePlayer{playerId, "Player", true});
        }
    }
    m_saveRequestPending = true;

    if (!withThumbnail) {
        SubmitPendingSave();
        return;
    }

    std::string rootError;
    const bool rootReady = m_ctx.saveManager->EnsureRoot(&rootError);
    const std::filesystem::path shotPath =
        m_ctx.saveManager->Root() / ".screenshot-readback";
    if (!rootReady || m_ctx.renderer == nullptr ||
        !m_ctx.renderer->RequestScreenshot(shotPath.string())) {
        ZL_WARN("save", "缩略图请求失败，改为无缩略图保存：{}",
                rootReady ? "renderer 不可??" : rootError);
        SubmitPendingSave();
        return;
    }
    m_screenshotPending = true;
    m_screenshotTimer = 0.0;
    SetNotice("正在生成存档缩略图…", kNoticeInfoColor, 2.0f);
}

void WorldDemoState::SubmitPendingSave() {
    if (!m_saveRequestPending) {
        return;
    }
    m_screenshotPending = false;
    if (m_ctx.saveService == nullptr) {
        m_saveRequestPending = false;
        m_pendingSave = save::SaveRequest{};
        SetNotice("存档服务未就绪", kNoticeFailColor, 2.5f);
        return;
    }

    const std::string saveId = m_pendingSave.id;
    std::string error;
    if (!m_ctx.saveService->Request(std::move(m_pendingSave), &error)) {
        m_saveRequestPending = false;
        m_pendingSave = save::SaveRequest{};
        SetNotice("提交后台存档失败：" + error, kNoticeFailColor, 3.5f);
        return;
    }
    m_pendingSave = save::SaveRequest{};
    m_saveRequestPending = false;
    m_savePending = true;
    if (m_ctx.events != nullptr) {
        m_ctx.events->Emit(core::kOnSaveBegin);
    }
    SetNotice("正在后台保存…", kNoticeInfoColor, 2.0f);
    ZL_INFO("save", "已提交后台存档请求（{}）",
            saveId.empty() ? "新建" : saveId);
}

void WorldDemoState::PollSaveScreenshot(double frameDt) {
    if (!m_screenshotPending) {
        return;
    }
    m_screenshotTimer += frameDt;
    render::ScreenshotImage image;
    if (m_ctx.renderer != nullptr && m_ctx.renderer->PollScreenshot(&image)) {
        if (image.Valid() && image.width <= 16384 && image.height <= 16384) {
            m_pendingSave.thumbnail.width = static_cast<int>(image.width);
            m_pendingSave.thumbnail.height = static_cast<int>(image.height);
            m_pendingSave.thumbnail.rgba = std::move(image.rgba);
        } else {
            ZL_WARN("save", "缩略图回读数据非法，改为无缩略图保存");
        }
        m_screenshotPending = false;
        SubmitPendingSave();
        return;
    }
    if (m_screenshotTimer >= 2.0) {
        ZL_WARN("save", "缩略图回读超时，改为无缩略图保存");
        m_screenshotPending = false;
        SubmitPendingSave();
    }
}

void WorldDemoState::HandleSaveFinished(bool ok, bool created,
                                        const std::string& id,
                                        const std::string& error) {
    m_savePending = false;
    if (ok) {
        m_saveId = id;
        m_ctx.activeSaveId = id;
        m_ctx.activeSaveUseBackup = false;
        SetNotice(created ? "存档已创建" : "存档已保存", kNoticeOkColor, 2.5f);
        ZL_INFO("save", "{}：{}", created ? "存档已创建" : "存档已保存", id);
    } else {
        SetNotice("保存失败：" + error, kNoticeFailColor, 4.0f);
        ZL_ERROR("save", "保存失败：{}", error);
    }
    if (m_ctx.events != nullptr) {
        m_ctx.events->Emit(core::kOnSaveEnd);
    }
}

void WorldDemoState::ConsumeSaveWarning() {
    if (m_ctx.saveWarning.empty()) {
        return;
    }
    const std::string warning = std::move(m_ctx.saveWarning);
    m_ctx.saveWarning.clear();
    SetNotice("存档警告：" + warning, kNoticeFailColor, 5.0f);
    ZL_WARN("save", "{}", warning);
}

void WorldDemoState::ApplyCameraMode() {
    render::Camera& camera = *m_ctx.camera;
    const bool cruiseActive = m_cruiseEnabled && !m_cruiseFinished;
    if (m_followCamera && !cruiseActive) {
        // 跟随模式：相机中心夹取到地图格范围的投影 AABB 内（边缘不移出地图）
        const world::MapLoader::Meta& meta = m_ctx.chunkManager->Map();
        const float gx0 = static_cast<float>(meta.chunkX0 * core::kChunkSize);
        const float gy0 = static_cast<float>(meta.chunkY0 * core::kChunkSize);
        const float gx1 = static_cast<float>((meta.chunkX0 + meta.chunksX) * core::kChunkSize);
        const float gy1 = static_cast<float>((meta.chunkY0 + meta.chunksY) * core::kChunkSize);
        glm::vec2 minPx;
        glm::vec2 maxPx;
        MapPixelBounds(gx0, gy0, gx1, gy1, &minPx, &maxPx);
        camera.SetBounds(minPx, maxPx);
        m_followSnapped = false;  // 下次跟随直接落位（不做缓动）
    } else {
        camera.ClearBounds();  // 自由相机/巡航：无边界（开发漫??压测??
    }
}

void WorldDemoState::UpdateCameraFollow(double frameDt) {
    if (!m_followCamera || (m_cruiseEnabled && !m_cruiseFinished)) {
        return;  // 自由模式或巡航接管
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    const auto* pos = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (pos == nullptr) {
        return;
    }
    render::Camera& camera = *m_ctx.camera;
    const glm::vec2 target = render::ToScreen(pos->x, pos->y, 0);
    if (!m_followSnapped) {
        camera.SetCenter(target);  // 首次/切回：直接落位，避免长距离缓动
        m_followSnapped = true;
        ZL_INFO("render", "相机跟随就位：玩家格 ({:.1f}, {:.1f})", pos->x, pos->y);
        return;
    }
    camera.SetCenter(CameraFollowStep(camera.Center(), target, m_followRate,
                                      static_cast<float>(frameDt)));
}

bool WorldDemoState::IsSolidSquare(int wx, int wy) const {
    return IsSquareBlocked(*m_ctx.world, wx, wy, 0);
}

void WorldDemoState::SyncNetworkReplicas(float alpha) {
    if (!m_networkMode || m_ctx.networkClient == nullptr || m_ctx.ecs == nullptr) {
        return;
    }
    const auto& actors = m_ctx.networkClient->Replication().Actors();
    entt::registry& registry = m_ctx.ecs->Registry();
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(actors.size());

    for (const auto& [stableId, actor] : actors) {
        if (stableId == 0 || !actor.hasPosition) {
            continue;
        }
        seen.insert(stableId);
        ecs::Entity entity = entt::null;
        const auto mapped = m_networkEntities.find(stableId);
        if (mapped != m_networkEntities.end() && m_ctx.ecs->Alive(mapped->second)) {
            entity = mapped->second;
        } else {
            if (mapped != m_networkEntities.end()) {
                m_networkEntities.erase(mapped);
            }
            entity = m_ctx.ecs->CreateWithStableId(stableId);
            if (entity == entt::null) {
                // 仅当本地已有同 ID 实体（非网络状态残留）时复用；否则跳过该副本。
                entity = m_ctx.ecs->FindById(stableId);
                if (entity == entt::null) {
                    ZL_WARN("net", "无法创建网络副本实体 stableId={}（ID 冲突）", stableId);
                    continue;
                }
            }
            m_networkEntities.emplace(stableId, entity);
        }

        ecs::Position position = actor.position;
        (void)m_ctx.networkClient->Replication().SamplePosition(stableId, alpha, &position);
        if (auto* current = registry.try_get<ecs::Position>(entity); current != nullptr) {
            *current = position;
        } else {
            registry.emplace<ecs::Position>(entity, position);
        }
        if (auto* velocity = registry.try_get<ecs::Velocity>(entity); velocity != nullptr) {
            *velocity = actor.velocity;
        } else {
            registry.emplace<ecs::Velocity>(entity, actor.velocity);
        }
        if (actor.hasHealth) {
            if (auto* health = registry.try_get<ecs::Health>(entity); health != nullptr) {
                *health = actor.health;
            } else {
                registry.emplace<ecs::Health>(entity, actor.health);
            }
        }
        if (actor.hasSprite) {
            if (auto* sprite = registry.try_get<ecs::SpriteRef>(entity); sprite != nullptr) {
                *sprite = actor.sprite;
            } else {
                registry.emplace<ecs::SpriteRef>(entity, actor.sprite);
            }
        }
        if (actor.hasFacing) {
            if (auto* facing = registry.try_get<ecs::Facing>(entity); facing != nullptr) {
                *facing = actor.facing;
            } else {
                registry.emplace<ecs::Facing>(entity, actor.facing);
            }
        }
        if (actor.hasAnim) {
            if (auto* anim = registry.try_get<ecs::AnimState>(entity); anim != nullptr) {
                // 网络负载只含旧 4 字段；clip/事件序号是本地运行时状态，保留不被快照清空，
                // 否则本地脚步/命中事件会在每次快照后重复或丢失。
                const std::uint32_t eventSerial = anim->eventSerial;
                const bool sameClip = anim->clip == actor.anim.clip;
                const std::uint8_t eventMaskDone = sameClip ? anim->eventMaskDone : 0;
                const ecs::AnimEvent lastEvent = anim->lastEvent;
                *anim = actor.anim;
                anim->eventSerial = eventSerial;
                anim->eventMaskDone = eventMaskDone;
                anim->lastEvent = lastEvent;
            } else {
                registry.emplace<ecs::AnimState>(entity, actor.anim);
            }
        }
        if (actor.hasItem) {
            if (auto* item = registry.try_get<ecs::ItemInstance>(entity);
                item != nullptr) {
                *item = actor.item;
            } else {
                registry.emplace<ecs::ItemInstance>(entity, actor.item);
            }
        }
        if (actor.hasEquipment) {
            registry.emplace_or_replace<ecs::Equipment>(entity, actor.equipment);
        }
        if (actor.hasAction) {
            registry.emplace_or_replace<ecs::ActionState>(entity, actor.action);
        }
        if (actor.hasVariant) {
            registry.emplace_or_replace<ecs::ZombieVariant>(entity, actor.variant);
        }
        if (actor.hasSpecial) {
            registry.emplace_or_replace<ecs::ZombieSpecial>(entity,
                                                            actor.special);
        }
        if (actor.hasWounds) {
            registry.emplace_or_replace<ecs::Wounds>(entity, actor.wounds);
        }
        if (actor.hasPain) {
            registry.emplace_or_replace<ecs::Pain>(entity, actor.pain);
        }
        if (actor.hasStorage) {
            registry.emplace_or_replace<ecs::PlayerStorage>(entity, actor.storage);
            registry.emplace_or_replace<ecs::Container>(entity,
                                                        actor.storageContainer);
        }
        if (actor.hasVehicle) {
            registry.emplace_or_replace<ecs::Vehicle>(entity, actor.vehicle);
            if (actor.vehicleDriverStableId != 0) {
                registry.emplace_or_replace<ecs::VehicleDriver>(
                    entity,
                    ecs::VehicleDriver{actor.vehicleDriverStableId});
            } else if (registry.all_of<ecs::VehicleDriver>(entity)) {
                registry.remove<ecs::VehicleDriver>(entity);
            }
        } else if (registry.all_of<ecs::Vehicle>(entity)) {
            registry.remove<ecs::Vehicle>(entity);
            if (registry.all_of<ecs::VehicleDriver>(entity)) {
                registry.remove<ecs::VehicleDriver>(entity);
            }
        }
        if (actor.hasLoot) {
            registry.emplace_or_replace<ecs::LootContainer>(
                entity, actor.lootContainer);
            registry.emplace_or_replace<ecs::Container>(
                entity, actor.lootContainerItems);

        }
        if (actor.hasFarmPlot) {
            registry.emplace_or_replace<ecs::FarmPlot>(entity, actor.farmPlot);
        }
        if (actor.hasGenerator) {
            registry.emplace_or_replace<ecs::Generator>(entity, actor.generator);
        }
        if (actor.hasColdStorage) {
            registry.emplace_or_replace<ecs::ColdStorage>(entity,
                                                          actor.coldStorage);
        }
        if (actor.hasFloodlight) {
            registry.emplace_or_replace<ecs::Floodlight>(entity,
                                                         actor.floodlight);
            registry.emplace_or_replace<ecs::NoCombatTarget>(entity);
        }
        if (actor.hasTrap) {
            registry.emplace_or_replace<ecs::SpikeTrap>(entity, actor.trap);
        }
        if (actor.hasMerchantStock) {
            registry.emplace_or_replace<ecs::MerchantStock>(
                entity, actor.merchantStock);
        }
        if (actor.hasSettlementNpc) {
            registry.emplace_or_replace<SettlementNpc>(
                entity, actor.settlementNpc);
        }
        if (actor.hasFire) {
            registry.emplace_or_replace<ecs::FireAnchor>(entity,
                                                         actor.fireAnchor);
            registry.emplace_or_replace<ecs::FireSource>(entity,
                                                         actor.fireSource);
        } else {
            if (registry.all_of<ecs::FireAnchor>(entity)) {
                registry.remove<ecs::FireAnchor>(entity);
            }
            if (registry.all_of<ecs::FireSource>(entity)) {
                registry.remove<ecs::FireSource>(entity);
            }
        }
        if (actor.hasBurning) {
            registry.emplace_or_replace<ecs::BurningZombie>(entity,
                                                             actor.burning);
        } else if (registry.all_of<ecs::BurningZombie>(entity)) {
            registry.remove<ecs::BurningZombie>(entity);
        }
        if (actor.hasAnimal) {
            registry.emplace_or_replace<ecs::Animal>(entity, actor.animal);
        } else if (registry.all_of<ecs::Animal>(entity)) {
            registry.remove<ecs::Animal>(entity);
        }
        if (actor.hasAnimalCorpse) {
            registry.emplace_or_replace<ecs::AnimalCorpse>(
                entity, actor.animalCorpse);
        } else if (registry.all_of<ecs::AnimalCorpse>(entity)) {
            registry.remove<ecs::AnimalCorpse>(entity);
        }
        if (actor.archetype == kReplicationArchetypeProp &&
            actor.ownerStableId != 0) {
            registry.emplace_or_replace<ecs::StructureOwner>(
                entity, ecs::StructureOwner{actor.ownerStableId});
        } else if (auto* owner = registry.try_get<ecs::StructureOwner>(entity);
                   owner != nullptr) {
            registry.remove<ecs::StructureOwner>(entity);
        }
    }

    for (auto it = m_networkEntities.begin(); it != m_networkEntities.end();) {
        if (seen.find(it->first) == seen.end()) {
            if (m_ctx.ecs->Alive(it->second)) {
                m_ctx.ecs->Destroy(it->second);
            }
            it = m_networkEntities.erase(it);
        } else {
            ++it;
        }
    }
    m_ctx.ecs->Flush();
    if (m_ctx.spatialIndex != nullptr) {
        m_ctx.spatialIndex->Update(*m_ctx.ecs);
    }

    const std::uint64_t localId = m_ctx.networkClient->PlayerStableId();
    const ecs::Entity nextPlayer =
        localId != 0 ? m_ctx.ecs->FindById(localId) : entt::null;
    if (nextPlayer != m_player) {
        m_player = nextPlayer;
        m_followSnapped = false;
        m_lastPlayerAnimEventSerial = 0;
        m_lastPlayerPathState = ecs::PathStatus::None;
        m_markerPathActive = false;
        m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
        m_moveCommandValid = false;
        m_lastNetworkStatusSequence = 0;
        m_pendingDemolish = {};
    }
    ApplyNetworkPlayerState();
    SyncNetworkVehicleBinding();
}

void WorldDemoState::SyncNetworkVehicleBinding() {
    if (!m_networkMode || m_ctx.ecs == nullptr) {
        return;
    }
    if (m_ctx.networkClient == nullptr ||
        !m_ctx.networkClient->IsConnected()) {
        if (m_driving || m_riding) {
            m_networkDisconnectWasDriving = true;
        }
        m_vehiclePredictionActive = false;
        m_vehiclePredictionTick = 0;
        if (m_vehicle != entt::null || m_driving || m_riding) {
            StopVehicleAudio();
            m_vehicle = entt::null;
            m_vehicleMotion = {};
            m_driving = false;
            m_riding = false;
            m_networkVehicle = {};
            m_vehicleFuelWarned = false;
        }
        ApplyNetworkDriverVisibility();
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    ecs::Entity target = entt::null;
    if (m_networkVehicle.valid && m_networkVehicle.vehicleStableId != 0) {
        target = m_ctx.ecs->FindById(m_networkVehicle.vehicleStableId);
        if (target != entt::null &&
            (!m_ctx.ecs->Alive(target) ||
             !registry.all_of<ecs::Vehicle>(target))) {
            target = entt::null;
        }
    }
    const bool serverDriving =
        m_networkVehicle.valid && m_networkVehicle.driving;
    const bool serverPassenger =
        m_networkVehicle.valid && m_networkVehicle.passenger;
    const bool serverRiding = serverDriving || serverPassenger;
    if (!serverRiding) {
        target = FindNearestVehicle(m_vehicleTuning.enterRange);
    }
    if (m_vehicle != target) {
        m_vehiclePredictionActive = false;
        m_vehiclePredictionTick = 0;
        m_vehicleHitSparks.clear();
    }
    if (target != entt::null && m_ctx.ecs->Alive(target) &&
        registry.all_of<ecs::Position, ecs::Vehicle>(target)) {
        if (m_vehicle != target) {
            ZL_INFO("vehicle", "联机载具绑定：stableId={}",
                    m_ctx.ecs->IdOf(target).id);
        }
        m_vehicle = target;
        SyncVehicleVisualDirectionFromSprite(target);
        const auto* position = registry.try_get<ecs::Position>(target);
        if (position != nullptr) {
            if (!(serverDriving && m_vehiclePredictionActive)) {
                m_vehicleMotion.x = position->x;
                m_vehicleMotion.y = position->y;
            }
            m_vehicleMotion.z = position->z;
        }
        if (!(serverDriving && m_vehiclePredictionActive)) {
            const auto* velocity = registry.try_get<ecs::Velocity>(target);
            m_vehicleMotion.vx = velocity != nullptr ? velocity->x : 0.0f;
            m_vehicleMotion.vy = velocity != nullptr ? velocity->y : 0.0f;
            const float speed = VehicleSpeed(m_vehicleMotion);
            if (speed > 0.05f) {
                m_vehicleHeadingX = m_vehicleMotion.vx / speed;
                m_vehicleHeadingY = m_vehicleMotion.vy / speed;
            }
        }
        const auto* vehicle = registry.try_get<ecs::Vehicle>(target);
        if (vehicle != nullptr) {
            m_vehicleMotion.fuel = std::clamp(vehicle->fuel, 0.0f, 1.0f);
            m_vehicleMotion.condition =
                std::clamp(vehicle->condition, 0.0f, 1.0f);
            m_vehicleMotion.headlights = vehicle->headlights;
        }
        m_vehicleSurface = VehicleSurfaceAt(
            m_vehicleMotion.x, m_vehicleMotion.y, m_vehicleMotion.z);
        if (!registry.all_of<ecs::SpriteScale>(target)) {
            registry.emplace<ecs::SpriteScale>(target, ecs::SpriteScale{1.0f});
        }
        if (!registry.all_of<ecs::Collider>(target)) {
            registry.emplace<ecs::Collider>(
                target,
                ecs::Collider{VehicleRadiusForType(m_vehicleMotion.type),
                              1.2f, ecs::kColliderCharacter,
                              ecs::kColliderAll});
        }
    } else if (m_vehicle != entt::null) {
        ZL_INFO("vehicle", "联机载具离开视野：清除绑??");
        m_vehicle = entt::null;
        m_vehicleMotion = {};
        m_vehicleFuelWarned = false;
        m_vehicleHitSparks.clear();
    }
    m_driving = serverDriving && m_vehicle != entt::null;
    m_riding = serverRiding && m_vehicle != entt::null;
    if (!m_driving || m_vehicle == entt::null) {
        m_vehiclePredictionActive = false;
        m_vehiclePredictionTick = 0;
    }
    ApplyNetworkDriverVisibility();
}

void WorldDemoState::UpdateNetworkVehiclePrediction(float frameDt) {
    if (!m_networkMode || m_ctx.networkClient == nullptr || m_ctx.ecs == nullptr) {
        return;
    }
    const auto stopPrediction = [this] {
        m_vehiclePredictionActive = false;
        m_vehiclePredictionTick = 0;
    };
    if (!m_driving || m_vehicle == entt::null ||
        !m_ctx.networkClient->IsConnected() || !m_ctx.ecs->Alive(m_vehicle)) {
        stopPrediction();
        return;
    }
    m_vehicleHornCooldown =
        std::max(0.0f, m_vehicleHornCooldown - frameDt);
    const std::uint64_t vehicleId = m_ctx.ecs->IdOf(m_vehicle).id;
    const EntityReplicationClient& replication =
        m_ctx.networkClient->Replication();
    const ReplicatedActor* actor = replication.Find(vehicleId);
    if (actor == nullptr || !actor->hasPosition) {
        stopPrediction();
        return;
    }
    const std::uint64_t tick = replication.LastAppliedTick();
    if (!m_vehiclePredictionActive) {
        m_vehicleMotion.x = actor->position.x;
        m_vehicleMotion.y = actor->position.y;
        m_vehicleMotion.z = actor->position.z;
        m_vehicleMotion.vx = actor->velocity.x;
        m_vehicleMotion.vy = actor->velocity.y;
        m_vehicleMotion.crashLatched = false;
        if (actor->hasVehicle) {
            m_vehicleMotion.type = actor->vehicle.type;
            m_vehicleMotion.heading = actor->vehicle.heading;
        }
        m_vehiclePredictionTick = tick;
        m_vehiclePredictionActive = true;
    } else if (tick != m_vehiclePredictionTick) {
        const float dx = actor->position.x - m_vehicleMotion.x;
        const float dy = actor->position.y - m_vehicleMotion.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const bool snapped = VehicleCorrectPosition(
            &m_vehicleMotion, actor->position.x, actor->position.y,
            actor->velocity.x, actor->velocity.y);
        if (actor->hasVehicle) {
            m_vehicleMotion.type = actor->vehicle.type;
            (void)VehicleCorrectHeading(&m_vehicleMotion,
                                        actor->vehicle.heading);
        }
        m_vehiclePredictionTick = tick;
        if (snapped) {
            ZL_DEBUG("vehicle",
                     "驾驶预测吸附：serverTick={} 距离={:.2f} ??", tick,
                     distance);
        }
    }

    float inputX = 0.0f;
    float inputY = 0.0f;
    if (m_moveCommandValid) {
        const glm::vec2 screen(m_lastMoveCommand.x, m_lastMoveCommand.y);
        if (glm::length(screen) > 0.0001f) {
            const glm::vec2 grid = glm::normalize(
                characters::InputSystem::ScreenDirToGridDir(
                    glm::normalize(screen)));
            inputX = grid.x;
            inputY = grid.y;
        }
    }
    const auto blocked = [this](int x, int y, int z) {
        return m_ctx.world == nullptr ||
               IsSquareBlocked(*m_ctx.world, x, y, z);
    };
    const float dt = std::clamp(frameDt, 0.0f, 0.05f);
    m_vehicleSurface =
        VehicleSurfaceAt(m_vehicleMotion.x, m_vehicleMotion.y, m_vehicleMotion.z);
    const VehicleTuning networkTuning =
        VehicleTuningForType(m_vehicleMotion.type, m_vehicleTuning);
    const auto bodyBlocked = [this](float x, float y, int z, float radius) {
        bool hit = false;
        m_ctx.ecs->Registry()
            .view<ecs::Position, ecs::Vehicle>()
            .each([&](const ecs::Entity entity, const ecs::Position& position,
                      const ecs::Vehicle& otherVehicle) {
                if (entity == m_vehicle || position.z != z) {
                    return;
                }
                if (game::VehicleBodiesOverlap(
                        x, y, radius, position.x, position.y,
                        VehicleRadiusForType(otherVehicle.type))) {
                    hit = true;
                }
            });
        return hit;
    };
    (void)VehiclePredictStep(&m_vehicleMotion, inputX, inputY, dt, blocked,
                             networkTuning, m_vehicleSurface, bodyBlocked);
    m_vehicleMotion.heading =
        VehicleNormalizeAngle(m_vehicleMotion.heading);
    VehicleHeadingVector(m_vehicleMotion, &m_vehicleHeadingX,
                         &m_vehicleHeadingY);

    entt::registry& registry = m_ctx.ecs->Registry();
    if (auto* position = registry.try_get<ecs::Position>(m_vehicle);
        position != nullptr) {
        position->x = m_vehicleMotion.x;
        position->y = m_vehicleMotion.y;
        position->z = m_vehicleMotion.z;
    }
    if (auto* velocity = registry.try_get<ecs::Velocity>(m_vehicle);
        velocity != nullptr) {
        velocity->x = m_vehicleMotion.vx;
        velocity->y = m_vehicleMotion.vy;
    }
    SyncPlayerToVehicle();
}

void WorldDemoState::ApplyNetworkDriverVisibility() {
    if (!m_networkMode || m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    std::unordered_set<std::uint64_t> keepHidden;
    const auto hideOccupant = [&](std::uint64_t stableId) {
        if (stableId == 0) {
            return;
        }
        keepHidden.insert(stableId);
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        if (entity != entt::null && m_ctx.ecs->Alive(entity) &&
            registry.all_of<ecs::SpriteRef>(entity)) {
            m_hiddenNetworkSprites.try_emplace(
                stableId, registry.get<ecs::SpriteRef>(entity));
            registry.remove<ecs::SpriteRef>(entity);
        }
    };
    if (m_ctx.networkClient != nullptr) {
        for (const auto& [stableId, actor] :
             m_ctx.networkClient->Replication().Actors()) {
            static_cast<void>(stableId);
            if (!actor.hasVehicle) {
                continue;
            }
            hideOccupant(actor.vehicleDriverStableId);
            hideOccupant(actor.vehiclePassengerStableId);
            hideOccupant(actor.vehicleSecondPassengerStableId);
        }
    }
    if (m_riding && m_ctx.networkClient != nullptr) {
        hideOccupant(m_ctx.networkClient->PlayerStableId());
    }
    for (auto it = m_hiddenNetworkSprites.begin();
         it != m_hiddenNetworkSprites.end();) {
        if (keepHidden.contains(it->first)) {
            ++it;
            continue;
        }
        const ecs::Entity entity = m_ctx.ecs->FindById(it->first);
        if (entity != entt::null && m_ctx.ecs->Alive(entity) &&
            !registry.all_of<ecs::SpriteRef>(entity)) {
            registry.emplace_or_replace<ecs::SpriteRef>(entity, it->second);
        }
        it = m_hiddenNetworkSprites.erase(it);
    }
}

void WorldDemoState::RefreshNetworkPowerDisplay(float frameDt) {
    if (!m_networkMode || m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const PowerConfig& config = m_homesteadConfig.power;
    m_poweredFloodlights.clear();
    if (!config.enabled) {
        m_poweredFenceCells.clear();
        return;
    }
    const bool night = m_ctx.gameTime != nullptr && m_ctx.gameTime->IsNight();
    const bool devicesActive = !config.deviceRequiresNight || night;
    std::vector<PowerSource> sources;
    registry.view<ecs::Generator, ecs::Position>().each(
        [&](const ecs::Entity, const ecs::Generator& generator,
            const ecs::Position& position) {
            if (sources.size() >= 8 || !generator.running ||
                generator.fuelMinutes <= 0.0f || generator.radius <= 0.0f) {
                return;
            }
            sources.push_back(PowerSource{position.x, position.y,
                                          generator.radius});
        });

    if (devicesActive) {
        registry.view<ecs::Floodlight, ecs::Position>().each(
            [&](const ecs::Entity, const ecs::Floodlight& floodlight,
                const ecs::Position& position) {
                if (!floodlight.on || !floodlight.powered) {
                    return;
                }
                m_poweredFloodlights.push_back(
                    PowerGlow{position.x, position.y,
                              config.floodlightRadius});
            });
    }

    m_powerScanTimer -= std::max(0.0f, frameDt);
    if (m_powerScanTimer > 0.0f) {
        return;
    }
    m_powerScanTimer = 0.5f;
    m_poweredFenceCells.clear();
    if (sources.empty()) {
        return;
    }
    for (const PowerSource& source : sources) {
        const int scanRadius = std::clamp(
            static_cast<int>(std::ceil(source.radius)), 1, 12);
        const int cx = static_cast<int>(std::floor(source.x));
        const int cy = static_cast<int>(std::floor(source.y));
        for (int wy = cy - scanRadius; wy <= cy + scanRadius; ++wy) {
            for (int wx = cx - scanRadius; wx <= cx + scanRadius; ++wx) {
                if (m_poweredFenceCells.size() >= 256) {
                    return;
                }
                const int dx = wx - cx;
                const int dy = wy - cy;
                if (dx * dx + dy * dy > scanRadius * scanRadius) {
                    continue;
                }
                const std::uint64_t key = BuildCellKey(wx, wy);
                if (m_poweredFenceCells.contains(key)) {
                    continue;
                }
                const world::ChunkCoord coord = world::ChunkOf(wx, wy);
                const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
                if (chunk == nullptr ||
                    chunk->State() != world::ChunkState::Loaded) {
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
                            object.sprite == kElectricFenceSprite &&
                            object.hp > 0.0f) {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        break;
                    }
                }
                if (!found) {
                    continue;
                }
                if (FindPowerSourceFor(sources, static_cast<float>(wx) + 0.5f,
                                       static_cast<float>(wy) + 0.5f) >= 0) {
                    m_poweredFenceCells.insert(key);
                }
            }
        }
    }
}

void WorldDemoState::ApplyNetworkChunkDiffs() {
    if (!m_networkMode || m_ctx.networkClient == nullptr ||
        m_ctx.chunkManager == nullptr) {
        return;
    }
    std::vector<save::ChunkDiff> incoming =
        m_ctx.networkClient->TakeChunkDiffs();
    for (save::ChunkDiff& diff : incoming) {
        for (save::ChunkDiff& one : save::SplitChunkDiffBySquare(diff)) {
            if (one.squares.empty()) {
                continue;
            }
            const save::ChunkSquareDiff& square = one.squares.front();
            const auto key = std::make_tuple(one.chunkX, one.chunkY,
                                             static_cast<int>(square.z),
                                             static_cast<int>(square.lx),
                                             static_cast<int>(square.ly));
            m_pendingNetworkChunkDiffs[key] = std::move(one);
        }
    }
    if (m_pendingNetworkChunkDiffs.empty()) {
        return;
    }

    std::set<std::pair<int, int>> rebuiltChunks;
    for (auto it = m_pendingNetworkChunkDiffs.begin();
         it != m_pendingNetworkChunkDiffs.end();) {
        const save::ChunkDiff& diff = it->second;
        world::Chunk* chunk = FindChunk(diff.chunkX, diff.chunkY);
        if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
            ++it;
            continue;
        }
        std::string error;
        if (!save::ApplyChunkDiff(diff, *chunk, &error)) {
            ZL_WARN("net", "应用服务器 chunk diff 失败：chunk=({}, {})：{}",
                    diff.chunkX, diff.chunkY, error);
            it = m_pendingNetworkChunkDiffs.erase(it);
            continue;
        }
        rebuiltChunks.emplace(diff.chunkX, diff.chunkY);
        it = m_pendingNetworkChunkDiffs.erase(it);
    }
    for (const auto& [chunkX, chunkY] : rebuiltChunks) {
        RebuildChunkCollision(chunkX, chunkY);
    }
}

void WorldDemoState::ApplyNetworkWorldSounds() {
    if (!m_networkMode || m_ctx.networkClient == nullptr) {
        return;
    }
    std::vector<net::WorldSoundEvent> sounds =
        m_ctx.networkClient->TakeWorldSounds();
    constexpr float kNetworkImpactFeedbackRange = 8.0f;
    for (const net::WorldSoundEvent& remote : sounds) {
        core::SoundEvent local;
        local.x = remote.x;
        local.y = remote.y;
        local.radius = remote.radius;
        local.type = static_cast<core::SoundType>(remote.type);
        local.clip = remote.clip;
        if (m_ctx.audio != nullptr) {
            m_ctx.audio->EmitWorldSound(local, local.clip);
        } else if (m_ctx.events != nullptr) {
            m_ctx.events->EmitQueued(core::kOnSound, std::move(local));
        }
        const float impactDx = remote.x - m_vehicleMotion.x;
        const float impactDy = remote.y - m_vehicleMotion.y;
        const bool localShake =
            m_driving && std::isfinite(impactDx) && std::isfinite(impactDy) &&
            impactDx * impactDx + impactDy * impactDy <=
                kNetworkImpactFeedbackRange * kNetworkImpactFeedbackRange;
        if (remote.type ==
            static_cast<std::uint8_t>(core::SoundType::Gunshot)) {
            SpawnRangedEffect(remote.x, remote.y, remote.z, 0.07f, 2,
                              0xFFFFC040u);
            ZL_INFO("net", "网络枪声：来自 {} 位置 ({:.1f}, {:.1f}, {})",
                    remote.sourcePlayerStableId, remote.x, remote.y, remote.z);
        } else if (remote.clip == audio::kVehicleHit) {
            SpawnVehicleHitFeedback(0.03f, remote.x, remote.y, remote.z,
                                    localShake);
            ZL_INFO("net", "网络载具受击：来??{} 位置 ({:.1f}, {:.1f}, {})",
                    remote.sourcePlayerStableId, remote.x, remote.y, remote.z);
        } else if (remote.clip == audio::kVehicleRam) {
            SpawnVehicleHitFeedback(0.04f, remote.x, remote.y, remote.z,
                                    localShake);
            ZL_INFO("net", "网络载具撞击：来??{} 位置 ({:.1f}, {:.1f}, {})",
                    remote.sourcePlayerStableId, remote.x, remote.y, remote.z);
        }
    }
}

void WorldDemoState::ApplyNetworkPlayerState() {
    if (!m_networkMode || m_ctx.networkClient == nullptr || m_ctx.ecs == nullptr ||
        !m_ctx.networkClient->HasPlayerState()) {
        return;
    }
    const net::PlayerState& state = m_ctx.networkClient->PlayerState();
    if (state.playerStableId != m_ctx.networkClient->PlayerStableId()) {
        return;
    }
    m_networkVehicle = state.vehicle;
    if (state.vehicle.valid) {
        m_vehicleMotion.fuel = std::clamp(state.vehicle.fuel, 0.0f, 1.0f);
        m_vehicleMotion.condition =
            std::clamp(state.vehicle.condition, 0.0f, 1.0f);
    }
    m_networkPlayerAlive = state.alive;
    m_networkHealth = state.health;
    m_networkHealthMax = state.healthMax;
    m_networkRespawnSeconds = state.respawnSeconds;
    m_networkRanged = state.ranged;
    m_networkCombat = state.combat;
    m_networkSurvival = state.survival;
    m_networkAnimalHarvest = state.animalHarvest;
    m_networkFood = state.food;
    m_networkInfection = {};
    m_networkInfection.stage =
        static_cast<ecs::InfectionStage>(state.infectionStage);
    m_networkInfection.progress = state.infectionProgress;
    m_networkInfection.timeToNextStage = state.infectionTimeToNextStage;
    m_networkInfection.slowStrength = state.infectionSlowStrength;
    m_networkInfection.slowRemaining = state.infectionSlowRemaining;
    m_networkInfection.slowDuration = state.infectionSlowDuration;
    m_networkClimate = {};
    m_networkClimate.bodyC = state.climateBodyC;
    m_networkClimate.wetness = state.climateWetness;
    m_networkSiegeStatus = state.siege.status;
    m_networkSiegeTone = state.siege.tone == 2 ? "fail"
                        : state.siege.tone == 1 ? "warn"
                                                : "info";
    m_networkSiegePhase = state.siege.phase;
    if (m_siegeReportOpen &&
        m_networkSiegePhase ==
            static_cast<std::uint8_t>(BaseSiegePhase::Warning)) {
        m_siegeReportOpen = false;
    }
    m_networkSiegeTargetValid = state.siege.targetValid;
    m_networkSiegeTargetX = state.siege.targetX;
    m_networkSiegeTargetY = state.siege.targetY;
    m_networkSiegeBreachValid = state.siege.breachValid;
    m_networkSiegeBreachX = state.siege.breachX;
    m_networkSiegeBreachY = state.siege.breachY;
    m_networkPower = state.power;
    m_networkSleep = state.sleep;
    m_networkSettlements.clear();
    for (const net::PlayerSettlementProgress& settlement :
         state.settlements) {
        NetworkSettlementStatus status;
        status.fallen = settlement.fallen;
        status.raidActive = settlement.raidActive;
        status.guardHealth = settlement.guardHealth;
        status.guardHealthMax = settlement.guardHealthMax;
        m_networkSettlements[settlement.id] = status;
    }
    m_networkSettlementNoticeSerial = state.settlementNoticeSerial;
    m_networkSettlementNotice = state.settlementNotice;
    m_networkSettlementNoticeTone =
        state.settlementNoticeTone == 2 ? "fail"
        : state.settlementNoticeTone == 1 ? "ok"
                                          : "info";

    if (state.statusSequence != m_lastNetworkStatusSequence) {
        m_lastNetworkStatusSequence = state.statusSequence;
        if (!state.status.empty()) {
            const std::uint32_t color =
                state.statusTone == 1 ? kNoticeOkColor
                : state.statusTone == 2 ? kNoticeFailColor
                                        : kNoticeInfoColor;
            SetNotice(state.status, color, 2.2f);
            if (state.status.rfind("守夜结束", 0) == 0) {
                const std::string tone = state.statusTone == 1 ? "ok"
                                         : state.statusTone == 2 ? "fail"
                                                                 : "info";
                OpenSiegeReport(state.status, tone);
            }
        }
    }

    m_networkProgression = {};
    m_networkProgression.level = state.progressionLevel;
    m_networkProgression.xp = state.progressionXp;
    m_networkProgression.perkPoints = state.perkPoints;
    m_networkProgression.perks = state.perks;
    for (std::size_t i = 0;
         i < m_networkProgression.skills.size() && i < state.skills.size(); ++i) {
        m_networkProgression.skills[i].level = state.skills[i].level;
        m_networkProgression.skills[i].xp = state.skills[i].xp;
    }
    m_networkPerkChoices = {};
    for (std::size_t i = 0;
         i < m_networkPerkChoices.size() && i < state.perkChoices.size(); ++i) {
        m_networkPerkChoices[i] = state.perkChoices[i];
    }
    m_networkProgressionValid = true;

    if (m_player == entt::null) {
        return;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    m_forageNodes.clear();
    m_fishingSpots.clear();
    if (!registry.all_of<ecs::ForageState>(m_player)) {
        registry.emplace<ecs::ForageState>(m_player);
    }
    auto& forageState = registry.get<ecs::ForageState>(m_player);
    forageState.harvested.clear();
    if (m_ctx.forage != nullptr) {
        const double now = m_ctx.gameTime != nullptr
                               ? m_ctx.gameTime->TotalGameMinutes()
                               : 0.0;
        for (const net::PlayerForageNodeState& node : state.forageNodes) {
            const ForageZoneDef* zone = m_ctx.forage->FindZone(node.zoneId);
            if (zone == nullptr) {
                continue;
            }
            m_forageNodes.push_back(
                ForageNode{node.zoneId, zone->tableId, node.x, node.y, node.z});
            if (!node.ready && node.respawnMinutes > 0.0f) {
                forageState.harvested.push_back(ecs::ForageHarvest{
                    node.zoneId, node.x, node.y, node.z,
                    now + static_cast<double>(node.respawnMinutes)});
            }
        }
        for (const net::PlayerFishingSpotState& spot : state.fishingSpots) {
            const FishingSpotDef* def = m_ctx.forage->FindSpot(spot.spotId);
            if (def == nullptr) {
                continue;
            }
            m_fishingSpots.push_back(
                FishingSpot{spot.spotId, def->tableId, spot.x, spot.y, spot.z,
                            spot.waterX, spot.waterY, spot.waterZ});
        }
    }
    m_forageAction = {};
    m_fishingAction = {};
    m_animalHarvestAction = {};
    if (state.animalHarvest.active) {
        m_animalHarvestAction.active = true;
        m_animalHarvestAction.corpseId =
            state.animalHarvest.corpseStableId;
        m_animalHarvestAction.speciesId = state.animalHarvest.speciesId;
        m_animalHarvestAction.corpseName = state.animalHarvest.name;
        m_animalHarvestAction.x = static_cast<float>(state.animalHarvest.x);
        m_animalHarvestAction.y = static_cast<float>(state.animalHarvest.y);
        m_animalHarvestAction.z = state.animalHarvest.z;
        m_animalHarvestAction.duration = state.animalHarvest.duration;
        m_animalHarvestAction.elapsed =
            state.animalHarvest.progress * state.animalHarvest.duration;
    }
    if (state.survival.mode == net::kPlayerSurvivalModeForage) {
        m_forageAction.active = true;
        m_forageAction.zoneId = state.survival.zoneId;
        m_forageAction.zoneName = state.survival.name;
        m_forageAction.tableId = state.survival.tableId;
        m_forageAction.x = state.survival.x;
        m_forageAction.y = state.survival.y;
        m_forageAction.z = state.survival.z;
        m_forageAction.duration = state.survival.duration;
        m_forageAction.elapsed =
            state.survival.progress * state.survival.duration;
    } else if (state.survival.mode == net::kPlayerSurvivalModeFishing) {
        m_fishingAction.active = true;
        m_fishingAction.spotId = state.survival.spotId;
        m_fishingAction.spotName = state.survival.name;
        m_fishingAction.tableId = state.survival.tableId;
        m_fishingAction.x = state.survival.x;
        m_fishingAction.y = state.survival.y;
        m_fishingAction.z = state.survival.z;
        m_fishingAction.waterX = state.survival.waterX;
        m_fishingAction.waterY = state.survival.waterY;
        m_fishingAction.waterZ = state.survival.waterZ;
        m_fishingAction.waitDuration = state.survival.duration;
        m_fishingAction.elapsed =
            state.survival.progress * state.survival.duration;
        m_fishingAction.biting =
            state.survival.phase == net::kPlayerSurvivalPhaseBiting;
        m_fishingAction.biteDuration = state.survival.biteRemaining;
        m_fishingAction.biteElapsed = 0.0f;
        if (m_ctx.forage != nullptr) {
            if (const FishingSpotDef* def =
                    m_ctx.forage->FindSpot(state.survival.spotId);
                def != nullptr) {
                m_fishingAction.trashTableId = def->trashTableId;
                m_fishingAction.rodDefId = def->rodDefId;
                m_fishingAction.baitDefId = def->baitDefId;
            }
        }
    }
    if (auto* health = registry.try_get<ecs::Health>(m_player); health != nullptr) {
        health->current = state.health;
        health->max = state.healthMax;
    } else {
        registry.emplace<ecs::Health>(
            m_player, ecs::Health{state.health, state.healthMax});
    }
    registry.emplace_or_replace<ecs::Infection>(m_player, m_networkInfection);
    if (auto* climate = registry.try_get<ecs::Climate>(m_player);
        climate != nullptr) {
        climate->bodyC = state.climateBodyC;
        climate->wetness = state.climateWetness;
    } else {
        registry.emplace<ecs::Climate>(m_player, m_networkClimate);
    }
    if (state.needs.valid) {
        if (auto* needs = registry.try_get<ecs::Needs>(m_player);
            needs != nullptr) {
            needs->hunger = state.needs.hunger;
            needs->thirst = state.needs.thirst;
            needs->fatigue = state.needs.fatigue;
            needs->warning = state.needs.warning;
        } else {
            ecs::Needs nextNeeds;
            nextNeeds.hunger = state.needs.hunger;
            nextNeeds.thirst = state.needs.thirst;
            nextNeeds.fatigue = state.needs.fatigue;
            nextNeeds.warning = state.needs.warning;
            registry.emplace<ecs::Needs>(m_player, nextNeeds);
        }
    }
    ecs::Container container;
    container.capacityKg = state.capacityKg;
    container.items.reserve(state.bag.size());
    for (const net::PlayerStateItem& item : state.bag) {
        container.items.push_back(
            ecs::ItemInstance{item.defId, item.count, item.condition});
    }
    registry.emplace_or_replace<ecs::Container>(m_player, std::move(container));

    ecs::Equipment equipment;
    for (std::size_t i = 0; i < state.equipment.size() &&
                            i < equipment.slots.size();
         ++i) {
        if (!state.equipment[i].present) {
            continue;
        }
        const net::PlayerStateItem& item = state.equipment[i].item;
        equipment.slots[i] = ecs::ItemInstance{
            item.defId, item.count, item.condition,
            state.equipment[i].loadedAmmo};
    }
    registry.emplace_or_replace<ecs::Equipment>(m_player, std::move(equipment));

    ecs::KnownRecipes known;
    known.recipeIds = state.knownRecipes;
    registry.emplace_or_replace<ecs::KnownRecipes>(m_player, std::move(known));

    ecs::CraftingJob job;
    job.active = state.craftingActive;
    job.recipeId = state.craftingRecipeId;
    job.elapsed = state.craftingElapsed;
    job.duration = state.craftingDuration;
    job.toolEfficiency = state.craftingToolEfficiency;
    job.batchTotal = state.batchTotal;
    job.batchCompleted = state.batchCompleted;
    job.queue.reserve(state.craftQueue.size());
    for (const net::PlayerStateCraftQueue& entry : state.craftQueue) {
        job.queue.push_back(ecs::CraftQueueItem{entry.recipeId, entry.count});
    }
    registry.emplace_or_replace<ecs::CraftingJob>(m_player, std::move(job));

    registry.emplace_or_replace<ecs::Progression>(m_player, m_networkProgression);
    if (state.objective.valid) {
        registry.emplace_or_replace<ecs::Objective>(
            m_player, ecs::Objective{
                          static_cast<ecs::ObjectiveStep>(state.objective.step)});
    } else {
        registry.remove<ecs::Objective>(m_player);
    }
    ApplyNetworkPoiAndMissions(state);
    if (state.objective.valid &&
        state.objective.step ==
            static_cast<std::uint8_t>(ecs::ObjectiveStep::Complete) &&
        m_runOutcome == RunOutcome::None) {
        FinishRun(true);
    }
}

void WorldDemoState::ApplyNetworkPoiAndMissions(const net::PlayerState& state) {
    m_networkPoiProgress.clear();
    for (const net::PlayerPoiProgress& poi : state.poiProgress) {
        NetworkPoiProgress progress;
        progress.completed = poi.completed;
        progress.targetSearched = poi.targetSearched;
        progress.liveGuards = poi.liveGuards;
        m_networkPoiProgress[poi.id] = progress;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    ecs::PoiTasks tasks;
    for (const net::PlayerPoiProgress& poi : state.poiProgress) {
        if (poi.completed &&
            std::find(tasks.completed.begin(), tasks.completed.end(),
                      poi.id) == tasks.completed.end()) {
            tasks.completed.push_back(poi.id);
        }
    }
    registry.emplace_or_replace<ecs::PoiTasks>(m_player, std::move(tasks));

    ecs::MissionLog log;
    log.lastDay = state.missionLastDay;
    log.randomDay = state.missionRandomDay;
    log.missions.reserve(state.missions.size());
    for (const net::PlayerMissionProgress& mission : state.missions) {
        ecs::MissionRunState run;
        run.id = mission.id;
        run.progress = mission.progress;
        run.status = static_cast<ecs::MissionRunStatus>(mission.status);
        run.day = mission.day;
        run.rewardClaimed = mission.rewardClaimed;
        log.missions.push_back(std::move(run));
    }
    registry.emplace_or_replace<ecs::MissionLog>(m_player, std::move(log));
}

void WorldDemoState::RefreshSocialState() {
    if (!m_networkMode || m_ctx.networkClient == nullptr) {
        return;
    }
    if (m_ctx.gameTime != nullptr && m_ctx.networkClient->HasSocialState()) {
        m_ctx.gameTime->SetTotalGameMinutes(
            m_ctx.networkClient->SocialState().gameMinutes);
    }
    if (m_ctx.ui == nullptr) {
        return;
    }
    ui::SocialUiState state;
    state.visible = m_ctx.networkClient->IsConnected();
    if (state.visible && m_ctx.networkClient->HasSocialState()) {
        const net::SocialState& social = m_ctx.networkClient->SocialState();
        const std::uint64_t localId = m_ctx.networkClient->PlayerStableId();
        state.players.reserve(social.players.size());
        for (const net::SocialPlayer& player : social.players) {
            state.players.push_back(
                ui::SocialPlayerUi{player.stableId, player.name,
                                   player.stableId == localId});
        }
        state.chat.reserve(social.chat.size());
        for (const net::SocialChatLine& line : social.chat) {
            state.chat.push_back(ui::SocialChatUi{line.sender, line.text});
        }
        if (state.players.empty()) {
            state.status = "等待其他玩家加入";
            state.statusTone = "info";
        }
    } else if (state.visible) {
        state.status = "正在同步玩家列表...";
        state.statusTone = "info";
    }
    m_ctx.ui->SetSocialState(state);
}

void WorldDemoState::SendChat(const std::string& text, std::string& error) {
    if (!m_networkMode || m_ctx.networkClient == nullptr) {
        error = "当前不在联机会话中";
        return;
    }
    if (!m_ctx.networkClient->SendChat(text, &error)) {
        return;
    }
    error.clear();
}
void WorldDemoState::EnsurePlayerSpawned() {
    if (m_ctx.ecs == nullptr || m_ctx.simulation == nullptr) {
        return;
    }
    if (m_networkMode) {
        const std::uint64_t networkId =
            m_ctx.networkClient != nullptr ? m_ctx.networkClient->PlayerStableId() : 0;
        const ecs::Entity next =
            networkId != 0 ? m_ctx.ecs->FindById(networkId) : entt::null;
        if (next != m_player) {
            m_player = next;
            if (m_player != entt::null) {
                m_followSnapped = false;
                m_lastPlayerAnimEventSerial = 0;
                m_lastPlayerPathState = ecs::PathStatus::None;
                m_markerPathActive = false;
                m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
                m_moveCommandValid = false;
            }
        }
        return;
    }
    if (m_runOutcome != RunOutcome::None) {
        return;
    }
    // M14-T8：单机玩家死亡（实体已回收）→ 结束本局并显示结算，不再原地自动重生。
    if (m_player != entt::null && !m_ctx.ecs->Alive(m_player)) {
        ZL_INFO("game", "玩家实体已死亡，结束本局（旧实体 ID {}）",
                m_ctx.ecs->IdOf(m_player).id);
        m_player = entt::null;
        m_lastPlayerAnimEventSerial = 0;
        m_lastPlayerPathState = ecs::PathStatus::None;
        m_markerPathActive = false;
        m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
        m_moveCommandValid = false;
        if (m_ctx.ui != nullptr) {
            m_ctx.ui->CloseInventory();
        }
        FinishRun(false);
        return;
    }
    if (m_player != entt::null) {
        EnsureForageState();
        return;
    }
    // 目标点 = 村庄南侧开阔地；WorldSimulation 会等目标 chunk 加载完成后再找可走格。
    m_player = m_ctx.simulation->EnsureLocalPlayer(70.0f, 24.0f, 24);
    if (m_player == entt::null) {
        return;
    }
    EnsureForageState();
    InitializeProgressionRuntime();
    InitializeMissionRuntime();
    m_lastPlayerAnimEventSerial = 0;
    m_lastPlayerPathState = ecs::PathStatus::None;
    m_markerPathActive = false;
    m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
    m_moveCommandValid = false;
}

void WorldDemoState::EnsureVehicleSpawned() {
    if (m_networkMode || m_ctx.ecs == nullptr || m_ctx.world == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_player) ||
        m_runOutcome != RunOutcome::None) {
        return;
    }
    if (m_vehicle != entt::null && !m_ctx.ecs->Alive(m_vehicle)) {
        m_vehicle = entt::null;
        m_driving = false;
        if (m_player != entt::null && m_ctx.ecs->Alive(m_player)) {
            m_ctx.ecs->Registry().remove<ecs::VehicleOccupant>(m_player);
        }
        StopVehicleAudio();
        m_playerSpriteBackup.reset();
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    std::size_t vehicleCount = 0;
    registry.view<ecs::Position, ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Position&, const ecs::Vehicle&) {
            if (m_ctx.ecs->Alive(entity)) {
                ++vehicleCount;
            }
        });

    const std::size_t targetCount =
        static_cast<std::size_t>(std::clamp(m_localVehicleCount, 1, 16));
    const auto* playerPosition = registry.try_get<ecs::Position>(m_player);
    if (vehicleCount < targetCount && playerPosition != nullptr) {
        const int anchorX = static_cast<int>(std::floor(playerPosition->x));
        const int anchorY = static_cast<int>(std::floor(playerPosition->y));
        const auto walkable = [this](int x, int y) {
            return m_ctx.world != nullptr &&
                   !IsSquareBlocked(*m_ctx.world, x, y, 0);
        };
        const auto overlapsVehicle = [&](float x, float y) {
            bool overlap = false;
            registry.view<ecs::Position, ecs::Vehicle>().each(
                [&](const ecs::Entity entity, const ecs::Position& position,
                    const ecs::Vehicle&) {
                    if (!m_ctx.ecs->Alive(entity) || position.z != 0) {
                        return;
                    }
                    const float dx = position.x - x;
                    const float dy = position.y - y;
                    if (dx * dx + dy * dy < 9.0f) {
                        overlap = true;
                    }
                });
            return overlap;
        };
        static constexpr int kExtraOffsets[][2] = {
            {6, -6},   {-6, 6},   {-6, -6}, {10, 0},  {0, 10},
            {-10, 0},  {0, -10},  {10, 10}, {10, -10}, {-10, 10},
            {-10, -10}, {14, 0},  {0, 14},  {-14, 0}, {0, -14}};
        int offsets[16][2] = {};
        offsets[0][0] = m_vehicleTuning.spawnOffsetX;
        offsets[0][1] = m_vehicleTuning.spawnOffsetY;
        for (std::size_t i = 1; i < 16; ++i) {
            offsets[i][0] = kExtraOffsets[i - 1][0];
            offsets[i][1] = kExtraOffsets[i - 1][1];
        }
        for (std::size_t i = 0; i < 16 && vehicleCount < targetCount; ++i) {
            int cellX = 0;
            int cellY = 0;
            if (!FindVehicleSpawnCell(anchorX + offsets[i][0],
                                      anchorY + offsets[i][1],
                                      m_vehicleTuning.spawnSearchRadius,
                                      walkable, &cellX, &cellY)) {
                continue;
            }
            const float spawnX = static_cast<float>(cellX) + 0.5f;
            const float spawnY = static_cast<float>(cellY) + 0.5f;
            const float dx = spawnX - playerPosition->x;
            const float dy = spawnY - playerPosition->y;
            if (dx * dx + dy * dy < 4.0f ||
                overlapsVehicle(spawnX, spawnY)) {
                continue;
            }
            const ecs::Entity entity = SpawnLocalVehicle(cellX, cellY);
            if (entity == entt::null) {
                continue;
            }
            ++vehicleCount;
            if (m_vehicle == entt::null) {
                AdoptVehicleEntity(entity);
            }
        }
    }

    if (!m_driving) {
        BindNearestLocalVehicle();
    }
}

ecs::Entity WorldDemoState::SpawnLocalVehicle(int cellX, int cellY) {
    if (m_ctx.ecs == nullptr) {
        return entt::null;
    }
    const float spawnX = static_cast<float>(cellX) + 0.5f;
    const float spawnY = static_cast<float>(cellY) + 0.5f;
    const ecs::Entity entity = m_ctx.ecs->Create();
    if (entity == entt::null) {
        return entt::null;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
    const ecs::VehicleType type = VehicleTypeForStableId(stableId);
    const VehicleProfile& profile = VehicleProfileFor(type);
    ecs::Vehicle vehicleState;
    vehicleState.type = type;
    vehicleState.heading = 1.5707963267948966f;
    registry.emplace<ecs::Position>(entity, ecs::Position{spawnX, spawnY, 0});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName("object.vehicle_south"), 0,
                               0xFFFFFFFFu});
    registry.emplace<ecs::SpriteScale>(entity, ecs::SpriteScale{1.0f});
    registry.emplace<ecs::Collider>(
        entity, ecs::Collider{profile.radius, 1.2f,
                              ecs::kColliderCharacter, ecs::kColliderAll});
    registry.emplace<ecs::Vehicle>(entity, vehicleState);
    registry.emplace<ecs::Container>(
        entity, ecs::Container{{}, m_vehicleTuning.trunkCapacityKg});
    registry.emplace<ecs::PlayerStorage>(
        entity, ecs::PlayerStorage{m_vehicleTuning.trunkStacks});
    ZL_INFO("vehicle", "载具生成：stableId={}，{}（{:.0f}kg），??({}, {})，燃??100%",
            stableId, VehicleTypeName(type), profile.curbMassKg, cellX, cellY);
    return entity;
}

void WorldDemoState::BindNearestLocalVehicle() {
    if (m_networkMode || m_driving || m_ctx.ecs == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_player) ||
        (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen())) {
        return;
    }
    ecs::Entity target = FindNearestVehicle(m_vehicleTuning.enterRange);
    if (target == entt::null && m_vehicle == entt::null) {
        target = FindNearestVehicle(1024.0f);
    }
    if (target == entt::null || target == m_vehicle ||
        !m_ctx.ecs->Alive(target)) {
        return;
    }
    if (m_vehicle != entt::null && m_ctx.ecs->Alive(m_vehicle)) {
        SyncVehicleStateToEcs();
    }
    AdoptVehicleEntity(target);
    ZL_INFO("vehicle",
            "单机载具绑定：stableId={}，载??({:.1f}, {:.1f})",
            m_ctx.ecs->IdOf(target).id, m_vehicleMotion.x, m_vehicleMotion.y);
}

void WorldDemoState::AdoptVehicleEntity(ecs::Entity entity) {
    if (m_ctx.ecs == nullptr || entity == entt::null ||
        !m_ctx.ecs->Alive(entity)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(entity);
    if (position == nullptr) {
        return;
    }
    m_vehicle = entity;
    m_vehicleMotion = VehicleMotion{};
    m_vehicleHeadingX = 0.0f;
    m_vehicleHeadingY = 1.0f;
    m_vehicleVisualDirection = VehicleVisualDirection::South;
    m_vehicleSurface = VehicleSurface::Paved;
    m_lastVehicleSurface = VehicleSurface::Paved;
    m_vehicleMotion.x = position->x;
    m_vehicleMotion.y = position->y;
    m_vehicleMotion.z = position->z;
    m_vehicleBodyCollisionCooldown = 0.0f;
    if (const auto* state = registry.try_get<ecs::Vehicle>(entity);
        state != nullptr) {
        m_vehicleMotion.fuel = std::clamp(state->fuel, 0.0f, 1.0f);
        m_vehicleMotion.condition = std::clamp(state->condition, 0.0f, 1.0f);
        m_vehicleMotion.headlights = state->headlights;
        m_vehicleMotion.type = VehicleTypeIsValid(state->type)
                                   ? state->type
                                   : ecs::VehicleType::Pickup;
        if (std::isfinite(state->heading)) {
            m_vehicleMotion.heading =
                VehicleNormalizeAngle(state->heading);
        }
    }
    if (!registry.all_of<ecs::SpriteRef>(entity)) {
        registry.emplace<ecs::SpriteRef>(
            entity, ecs::SpriteRef{ecs::HashName("object.vehicle_south"), 0,
                                   0xFFFFFFFFu});
    }
    SyncVehicleVisualDirectionFromSprite(entity);
    if (!registry.all_of<ecs::SpriteScale>(entity)) {
        registry.emplace<ecs::SpriteScale>(entity, ecs::SpriteScale{1.0f});
    }
    if (!registry.all_of<ecs::Collider>(entity)) {
        registry.emplace<ecs::Collider>(
            entity,
            ecs::Collider{VehicleRadiusForType(m_vehicleMotion.type), 1.2f,
                          ecs::kColliderCharacter, ecs::kColliderAll});
    }
    if (!registry.all_of<ecs::Container>(entity)) {
        registry.emplace<ecs::Container>(
            entity, ecs::Container{{}, m_vehicleTuning.trunkCapacityKg});
    }
    if (!registry.all_of<ecs::PlayerStorage>(entity)) {
        registry.emplace<ecs::PlayerStorage>(
            entity, ecs::PlayerStorage{m_vehicleTuning.trunkStacks});
    }
    if (!registry.all_of<ecs::Vehicle>(entity)) {
        registry.emplace<ecs::Vehicle>(entity, ecs::Vehicle{});
    }
    SyncVehicleStateToEcs();
    m_vehicleFuelWarned = m_vehicleMotion.fuel <= 0.0f;
    const auto* container = registry.try_get<ecs::Container>(entity);
    ZL_INFO("vehicle",
            "载具恢复：stableId={}，格 ({:.1f}, {:.1f})，燃??{:.0f}%??"
            "车况 {:.0f}%，后备箱 {} ??",
            m_ctx.ecs->IdOf(entity).id, m_vehicleMotion.x, m_vehicleMotion.y,
            m_vehicleMotion.fuel * 100.0f, m_vehicleMotion.condition * 100.0f,
            container != nullptr ? container->items.size() : 0);
}

void WorldDemoState::SyncVehicleStateToEcs() {
    if (m_ctx.ecs == nullptr || m_vehicle == entt::null ||
        !m_ctx.ecs->Alive(m_vehicle)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* state = registry.try_get<ecs::Vehicle>(m_vehicle);
    if (state == nullptr) {
        state = &registry.emplace<ecs::Vehicle>(m_vehicle);
    }
    state->fuel = std::clamp(m_vehicleMotion.fuel, 0.0f, 1.0f);
    state->condition = std::clamp(m_vehicleMotion.condition, 0.0f, 1.0f);
    state->headlights = m_vehicleMotion.headlights;
    state->type = m_vehicleMotion.type;
    state->heading = VehicleNormalizeAngle(m_vehicleMotion.heading);
}

void WorldDemoState::ApplyLocalVehicleVehicleCollision(
    float impactSpeed, ecs::Entity target,
    const VehicleMotion& previousMotion) {
    if (m_ctx.ecs == nullptr || m_vehicle == entt::null ||
        target == entt::null || !m_ctx.ecs->Alive(m_vehicle) ||
        !m_ctx.ecs->Alive(target) || m_vehicleBodyCollisionCooldown > 0.0f) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* targetState = registry.try_get<ecs::Vehicle>(target);
    const ecs::VehicleType targetType =
        targetState != nullptr && VehicleTypeIsValid(targetState->type)
            ? targetState->type
            : ecs::VehicleType::Pickup;
    auto* targetPosition = registry.try_get<ecs::Position>(target);
    if (targetPosition == nullptr) {
        return;
    }
    const std::uint64_t targetId = m_ctx.ecs->IdOf(target).id;
    const float targetDamage = VehicleCollisionConditionDamageForType(
        impactSpeed, m_vehicleTuning, targetType);
    const float activeDamagePreview =
        VehicleCollisionConditionDamageForType(impactSpeed, m_vehicleTuning,
                                               m_vehicleMotion.type);
    float otherDamage = 0.0f;
    if (targetState != nullptr && targetDamage > 0.0f) {
        const float before = std::clamp(targetState->condition, 0.0f, 1.0f);
        targetState->condition = std::max(0.0f, before - targetDamage);
        otherDamage = before - targetState->condition;
    }
    float activeDamage = 0.0f;
    if (activeDamagePreview > 0.0f) {
        activeDamage = VehicleApplyCollisionConditionDamageForType(
            &m_vehicleMotion, impactSpeed, m_vehicleTuning);
    }
    const bool hasDamage = activeDamage > 0.0f || otherDamage > 0.0f;
    const bool collisionSpeed =
        std::isfinite(impactSpeed) &&
        std::isfinite(m_vehicleTuning.vehicleCollisionMinSpeed) &&
        impactSpeed >= m_vehicleTuning.vehicleCollisionMinSpeed;
    const VehicleTuning& tuning = m_vehicleTuning;
    if (!hasDamage && !collisionSpeed &&
        (!std::isfinite(impactSpeed) ||
         impactSpeed < kVehicleContactMinSpeed)) {
        return;
    }
    float pushedDistance = 0.0f;
    if (hasDamage) {
        m_vehicleBodyCollisionCooldown =
            m_vehicleTuning.vehicleCollisionCooldownSeconds;
        SyncVehicleStateToEcs();
        pushedDistance = PushLocalVehicleOnCollision(target, impactSpeed);
    }
    if (targetId != 0) {
        VehicleMotion targetMotion;
        if (const auto found =
                m_localParkedVehicleMotions.find(targetId);
            found != m_localParkedVehicleMotions.end()) {
            targetMotion = found->second;
        } else {
            targetMotion.x = targetPosition->x;
            targetMotion.y = targetPosition->y;
            targetMotion.z = targetPosition->z;
            if (targetState != nullptr) {
                targetMotion.fuel =
                    std::clamp(targetState->fuel, 0.0f, 1.0f);
                targetMotion.condition =
                    std::clamp(targetState->condition, 0.0f, 1.0f);
                targetMotion.type = targetType;
                if (std::isfinite(targetState->heading)) {
                    targetMotion.heading =
                        VehicleNormalizeAngle(targetState->heading);
                }
            }
        }
        targetMotion.x = targetPosition->x;
        targetMotion.y = targetPosition->y;
        targetMotion.z = targetPosition->z;
        targetMotion.type = targetType;
        if (targetState != nullptr &&
            std::isfinite(targetState->heading)) {
            targetMotion.heading =
                VehicleNormalizeAngle(targetState->heading);
        }
        VehicleMotion impactMotion = previousMotion;
        impactMotion.x = m_vehicleMotion.x;
        impactMotion.y = m_vehicleMotion.y;
        impactMotion.z = m_vehicleMotion.z;
        impactMotion.type = m_vehicleMotion.type;
        const float activeMass = VehicleMassForType(m_vehicleMotion.type);
        const float targetMass = VehicleMassForType(targetType);
        const float activeRadius =
            VehicleRadiusForType(m_vehicleMotion.type);
        const float targetRadius = VehicleRadiusForType(targetType);
        const VehicleCollisionImpulseResult impulse =
            collisionSpeed
                ? VehicleCollisionImpulseFor(
                      impactMotion, targetMotion, tuning, activeMass,
                      targetMass, activeRadius, targetRadius)
                : VehicleContactImpulseFor(
                      impactMotion, targetMotion, tuning, activeMass,
                      targetMass, activeRadius, targetRadius);
        if (impulse.applied) {
            m_vehicleMotion.vx = impulse.firstVx;
            m_vehicleMotion.vy = impulse.firstVy;
            m_vehicleMotion.angularVelocity =
                impulse.firstAngularVelocity;
            targetMotion.vx = impulse.secondVx;
            targetMotion.vy = impulse.secondVy;
            targetMotion.angularVelocity =
                impulse.secondAngularVelocity;
            m_localParkedVehicleMotions[targetId] = targetMotion;
            if (hasDamage) {
                ZL_INFO("vehicle",
                        "单机载具冲量：目??stableId={}，质??{:.2f}/{:.2f}??"
                        "接近速度 {:.1f}，速度 {:.2f}/{:.2f}，角速度 "
                        "{:.2f}/{:.2f}，目标位??({:.2f}, {:.2f})",
                        targetId, activeMass, targetMass,
                        impulse.closingSpeed,
                        VehicleSpeed(m_vehicleMotion),
                        VehicleSpeed(targetMotion),
                        m_vehicleMotion.angularVelocity,
                        targetMotion.angularVelocity, targetPosition->x,
                        targetPosition->y);
            } else {
                ZL_DEBUG("vehicle",
                         "单机载具接触：目??stableId={}，接近速度 {:.1f}??"
                         "速度 {:.2f}/{:.2f}，目标位??({:.2f}, {:.2f})",
                         targetId, impulse.closingSpeed,
                         VehicleSpeed(m_vehicleMotion),
                         VehicleSpeed(targetMotion), targetPosition->x,
                         targetPosition->y);
            }
        }
    }
    if (!hasDamage) {
        return;
    }
    SetNotice("车辆碰撞：车况 " +
                  std::to_string(static_cast<int>(std::floor(
                      m_vehicleMotion.condition * 100.0f))) +
                  "%",
              kNoticeFailColor, 2.0f);
    const float occupantDamage = VehicleCollisionOccupantDamageForType(
        impactSpeed, m_vehicleTuning, m_vehicleMotion.type);
    if (occupantDamage > 0.0f && m_ctx.combat != nullptr &&
        m_player != entt::null && m_ctx.ecs->Alive(m_player)) {
        auto* health = registry.try_get<ecs::Health>(m_player);
        if (health != nullptr && health->current > 0.0f) {
            const float healthBefore = health->current;
            float dirX = m_vehicleMotion.x - targetPosition->x;
            float dirY = m_vehicleMotion.y - targetPosition->y;
            const float length = std::sqrt(dirX * dirX + dirY * dirY);
            if (length > 1.0e-4f) {
                dirX /= length;
                dirY /= length;
            } else {
                dirX = 1.0f;
                dirY = 0.0f;
            }
            ecs::DamageEvent event;
            event.sourceId = targetId;
            event.type = ecs::DamageType::Blunt;
            event.amount = occupantDamage;
            event.dirX = dirX;
            event.dirY = dirY;
            if (m_ctx.combat->ApplyDamage(*m_ctx.ecs, m_player, event)) {
                const float applied =
                    std::max(0.0f, healthBefore - health->current);
                SetNotice("碰撞受伤??" +
                              std::to_string(static_cast<int>(
                                  std::lround(applied))) +
                              " HP",
                          kNoticeFailColor, 2.0f);
                ZL_INFO("vehicle",
                        "单机载具碰撞乘员伤害：伤??{:.1f}，剩??{:.0f} HP",
                        applied, health->current);
            }
        }
    }
    ZL_INFO("vehicle",
            "载具间碰撞：速度 {:.1f}，本车扣车况 {:.1f}%，目??"
            "stableId={} 扣车??{:.1f}%，推??{:.2f} ??",
            impactSpeed, activeDamage * 100.0f, targetId,
            otherDamage * 100.0f, pushedDistance);
    SpawnVehicleHitFeedback(std::max(activeDamage, otherDamage),
                            m_vehicleMotion.x, m_vehicleMotion.y,
                            m_vehicleMotion.z);
    PlayLocalVehicleRamSound();
}

void WorldDemoState::UpdateLocalParkedVehicleMotions(float tickDt) {
    if (m_localParkedVehicleMotions.empty()) {
        return;
    }
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr ||
        m_vehicle == entt::null || !m_ctx.ecs->Alive(m_vehicle)) {
        m_localParkedVehicleMotions.clear();
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    for (auto it = m_localParkedVehicleMotions.begin();
         it != m_localParkedVehicleMotions.end();) {
        const std::uint64_t stableId = it->first;
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        if (entity == entt::null || entity == m_vehicle ||
            !m_ctx.ecs->Alive(entity)) {
            it = m_localParkedVehicleMotions.erase(it);
            continue;
        }
        auto* position = registry.try_get<ecs::Position>(entity);
        auto* state = registry.try_get<ecs::Vehicle>(entity);
        if (position == nullptr || state == nullptr) {
            it = m_localParkedVehicleMotions.erase(it);
            continue;
        }
        VehicleMotion motion = it->second;
        motion.x = position->x;
        motion.y = position->y;
        motion.z = position->z;
        motion.fuel = std::clamp(state->fuel, 0.0f, 1.0f);
        motion.condition = std::clamp(state->condition, 0.0f, 1.0f);
        motion.type = VehicleTypeIsValid(state->type)
                          ? state->type
                          : ecs::VehicleType::Pickup;
        if (std::isfinite(state->heading)) {
            motion.heading = VehicleNormalizeAngle(state->heading);
        }
        VehicleTuning coastTuning =
            VehicleTuningForType(motion.type, m_vehicleTuning);
        coastTuning.braking =
            std::isfinite(m_vehicleTuning.vehicleCoastBraking)
                ? m_vehicleTuning.vehicleCoastBraking
                : kVehicleCoastBraking;
        if (!std::isfinite(motion.vx) || !std::isfinite(motion.vy) ||
            VehicleSpeed(motion) <= kVehicleCoastStopSpeed) {
            motion.vx = 0.0f;
            motion.vy = 0.0f;
            VehicleIntegrateAngular(&motion, tickDt, coastTuning);
            state->heading = VehicleNormalizeAngle(motion.heading);
            if (std::fabs(motion.angularVelocity) > 0.01f) {
                it->second = motion;
                ++it;
                continue;
            }
            if (auto* velocity = registry.try_get<ecs::Velocity>(entity);
                velocity != nullptr) {
                velocity->x = 0.0f;
                velocity->y = 0.0f;
            }
            it = m_localParkedVehicleMotions.erase(it);
            continue;
        }
        const auto blocked = [this](int x, int y, int z) {
            return m_ctx.world == nullptr ||
                   IsSquareBlocked(*m_ctx.world, x, y, z);
        };
        const auto bodyBlocked = [this, entity](float x, float y, int z,
                                                float radius) {
            bool hit = false;
            m_ctx.ecs->Registry()
                .view<ecs::Position, ecs::Vehicle>()
                .each([&](const ecs::Entity other,
                          const ecs::Position& otherPosition,
                          const ecs::Vehicle& otherVehicle) {
                    if (other == entity || otherPosition.z != z) {
                        return;
                    }
                    if (VehicleBodiesOverlap(
                            x, y, radius, otherPosition.x, otherPosition.y,
                            VehicleRadiusForType(otherVehicle.type))) {
                        hit = true;
                    }
                });
            return hit;
        };
        (void)VehiclePredictStep(
            &motion, 0.0f, 0.0f, tickDt, blocked, coastTuning,
            VehicleSurfaceAt(motion.x, motion.y, motion.z), bodyBlocked);
        position->x = motion.x;
        position->y = motion.y;
        position->z = motion.z;
        state->heading = VehicleNormalizeAngle(motion.heading);
        auto* velocity = registry.try_get<ecs::Velocity>(entity);
        if (velocity == nullptr) {
            velocity = &registry.emplace<ecs::Velocity>(entity);
        }
        velocity->x = motion.vx;
        velocity->y = motion.vy;
        if (VehicleSpeed(motion) <= kVehicleCoastStopSpeed &&
            std::fabs(motion.angularVelocity) <= 0.01f) {
            velocity->x = 0.0f;
            velocity->y = 0.0f;
            ZL_INFO("vehicle",
                    "单机停放车滑行停止：stableId={}，位??({:.2f}, {:.2f})",
                    stableId, motion.x, motion.y);
            it = m_localParkedVehicleMotions.erase(it);
        } else {
            it->second = motion;
            ++it;
        }
    }
}

float WorldDemoState::PushLocalVehicleOnCollision(ecs::Entity target,
                                                  float impactSpeed) {
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr ||
        target == entt::null || !m_ctx.ecs->Alive(target)) {
        return 0.0f;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* targetPosition = registry.try_get<ecs::Position>(target);
    if (targetPosition == nullptr) {
        return 0.0f;
    }
    const auto* targetState = registry.try_get<ecs::Vehicle>(target);
    const float targetRadius =
        targetState != nullptr
            ? VehicleRadiusForType(targetState->type)
            : m_vehicleTuning.radius;
    const float pushDistance = VehicleCollisionPushDistanceForMasses(
        impactSpeed, m_vehicleTuning, VehicleMassForType(m_vehicleMotion.type),
        VehicleMassForType(targetState != nullptr
                               ? targetState->type
                               : ecs::VehicleType::Pickup));
    if (pushDistance <= 0.0f) {
        return 0.0f;
    }
    float dirX = targetPosition->x - m_vehicleMotion.x;
    float dirY = targetPosition->y - m_vehicleMotion.y;
    float directionLength = std::sqrt(dirX * dirX + dirY * dirY);
    if (directionLength <= 1.0e-4f) {
        dirX = m_vehicleHeadingX;
        dirY = m_vehicleHeadingY;
        directionLength = std::sqrt(dirX * dirX + dirY * dirY);
        if (directionLength <= 1.0e-4f) {
            dirX = 1.0f;
            dirY = 0.0f;
        }
    }
    const auto blocked = [this](int x, int y, int z) {
        return m_ctx.world == nullptr ||
               IsSquareBlocked(*m_ctx.world, x, y, z);
    };
    const auto bodyBlocked = [this, target](float x, float y, int z,
                                            float radius) {
        bool hit = false;
        m_ctx.ecs->Registry()
            .view<ecs::Position, ecs::Vehicle>()
            .each([&](const ecs::Entity entity, const ecs::Position& position,
                      const ecs::Vehicle& otherVehicle) {
                if (entity == m_vehicle || entity == target ||
                    position.z != z) {
                    return;
                }
                if (VehicleBodiesOverlap(
                        x, y, radius, position.x, position.y,
                        VehicleRadiusForType(otherVehicle.type))) {
                    hit = true;
                }
            });
        return hit;
    };
    const VehiclePushResult push = VehiclePushBody(
        targetPosition->x, targetPosition->y, targetPosition->z, dirX,
        dirY, pushDistance, targetRadius, blocked, bodyBlocked);
    targetPosition->x = push.x;
    targetPosition->y = push.y;
    if (push.distance > 0.0f) {
        ZL_INFO("vehicle",
                "单机载具推挤：目??stableId={}，推??{:.2f} 格，"
                "位置 ({:.2f}, {:.2f})",
                m_ctx.ecs->IdOf(target).id, push.distance,
                targetPosition->x, targetPosition->y);
    }
    return push.distance;
}

void WorldDemoState::ApplyLocalVehicleRam(
    const VehicleMotion* previousMotion) {
    if (m_ctx.ecs == nullptr || m_ctx.combat == nullptr ||
        m_vehicle == entt::null) {
        return;
    }
    const VehicleMotion& ramMotion =
        previousMotion != nullptr &&
                VehicleSpeed(*previousMotion) > VehicleSpeed(m_vehicleMotion)
            ? *previousMotion
            : m_vehicleMotion;
    const float speed = VehicleSpeed(ramMotion);
    if (VehicleRamDamage(speed, m_vehicleTuning) <= 0.0f) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const std::uint64_t vehicleId = m_ctx.ecs->IdOf(m_vehicle).id;
    const float dirX = speed > 0.0001f ? ramMotion.vx / speed : 1.0f;
    const float dirY = speed > 0.0001f ? ramMotion.vy / speed : 0.0f;
    bool rammed = false;
    const auto applyRam = [&](ecs::Entity entity,
                              const ecs::Position& position,
                              const ecs::Health& health,
                              VehicleRamTargetKind kind) {
        if (kind == VehicleRamTargetKind::None || health.current <= 0.0f) {
            return;
        }
        const std::uint64_t targetId = m_ctx.ecs->IdOf(entity).id;
        if (targetId == 0 ||
            m_vehicleRamCooldowns.find(targetId) !=
                m_vehicleRamCooldowns.end()) {
            return;
        }
        float targetRadius = 0.35f;
        if (const auto* collider = registry.try_get<ecs::Collider>(entity);
            collider != nullptr) {
            targetRadius = collider->radius;
        }
        if (!VehicleRamOverlap(
                ramMotion, position.x, position.y, targetRadius,
                VehicleRadiusForType(ramMotion.type))) {
            return;
        }
        const ecs::Animal* animal =
            kind == VehicleRamTargetKind::Animal
                ? registry.try_get<ecs::Animal>(entity)
                : nullptr;
        if (kind == VehicleRamTargetKind::Animal && animal == nullptr) {
            return;
        }
        ecs::DamageEvent event;
        event.sourceId = vehicleId;
        event.type = ecs::DamageType::Blunt;
        event.amount = VehicleRamDamage(speed, m_vehicleTuning);
        event.dirX = dirX;
        event.dirY = dirY;
        event.knockback = VehicleRamKnockback(speed, m_vehicleTuning);
        event.staggerSeconds = VehicleRamStagger(speed, m_vehicleTuning);
        if (!m_ctx.combat->ApplyDamage(*m_ctx.ecs, entity, event)) {
            return;
        }
        m_vehicleRamCooldowns[targetId] =
            m_vehicleTuning.ramCooldownSeconds;
        rammed = true;
        const bool lethal =
            registry.get<ecs::Health>(entity).current <= 0.0f;
        if (kind == VehicleRamTargetKind::Animal) {
            const AnimalDef* def =
                m_ctx.animals != nullptr ? m_ctx.animals->Find(animal->speciesId)
                                         : nullptr;
            const std::string species =
                def != nullptr ? def->name : animal->speciesId;
            ZL_INFO("vehicle",
                    "载具撞动物：载具 stableId={}，目??{}，物??{}，伤??"
                    "{:.0f}，速度 {:.1f}",
                    vehicleId, static_cast<unsigned long long>(targetId),
                    species, event.amount, speed);
            if (lethal) {
                ZL_INFO("vehicle",
                        "载具撞杀动物：载??stableId={}，目??{}，物??{}",
                        vehicleId, static_cast<unsigned long long>(targetId),
                        species);
            }
            return;
        }
        ZL_INFO("vehicle",
                "载具撞僵尸：载具 stableId={}，目??{}，伤??{:.0f}，速度 {:.1f}",
                vehicleId, static_cast<unsigned long long>(targetId),
                event.amount, speed);
        if (lethal && !m_networkMode && m_progressionConfig.enabled &&
            m_progressionConfig.meleeXpPerKill > 0.0f) {
            GrantProgressionXp(ecs::SkillKind::Melee,
                               m_progressionConfig.meleeXpPerKill,
                               "载具撞击");
            ZL_INFO("vehicle",
                    "载具撞杀：载??stableId={}，目??{}??{:.0f} 近战经验",
                    vehicleId, static_cast<unsigned long long>(targetId),
                    m_progressionConfig.meleeXpPerKill);
        }
    };
    registry.view<ecs::AiState, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::AiState&,
            const ecs::Position& position, const ecs::Health& health) {
            const VehicleRamTargetKind kind = VehicleRamTargetKindFor(
                true, registry.any_of<ecs::Animal>(entity),
                registry.any_of<ecs::PlayerTag>(entity),
                registry.any_of<ecs::FriendlyNpc>(entity), health.current,
                position.z, m_vehicleMotion.z);
            if (kind == VehicleRamTargetKind::Zombie) {
                applyRam(entity, position, health, kind);
            }
        });
    registry.view<ecs::Animal, ecs::Position, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::Animal&,
            const ecs::Position& position, const ecs::Health& health) {
            const VehicleRamTargetKind kind = VehicleRamTargetKindFor(
                registry.any_of<ecs::AiState>(entity), true,
                registry.any_of<ecs::PlayerTag>(entity),
                registry.any_of<ecs::FriendlyNpc>(entity), health.current,
                position.z, m_vehicleMotion.z);
            if (kind == VehicleRamTargetKind::Animal) {
                applyRam(entity, position, health, kind);
            }
        });
    if (rammed) {
        if (m_vehicleRamWearCooldown <= 0.0f) {
            const float wear = VehicleApplyRamConditionDamage(
                &m_vehicleMotion, speed, m_vehicleTuning);
            if (wear > 0.0f) {
                m_vehicleRamWearCooldown =
                    m_vehicleTuning.ramCooldownSeconds;
                SetNotice("载具撞击：车??" +
                              std::to_string(static_cast<int>(std::floor(
                                  m_vehicleMotion.condition * 100.0f))) +
                              "%",
                          kNoticeInfoColor, 2.0f);
                ZL_INFO("vehicle",
                        "撞击车况：速度 {:.1f}，扣车况 {:.1f}%，剩??{:.0f}%",
                        speed, wear * 100.0f,
                        m_vehicleMotion.condition * 100.0f);
                SpawnVehicleHitFeedback(wear, m_vehicleMotion.x,
                                        m_vehicleMotion.y,
                                        m_vehicleMotion.z);
            }
        }
        PlayLocalVehicleRamSound();
    }
}

void WorldDemoState::ApplyLocalVehicleAttacks() {
    if (!m_driving || m_ctx.ecs == nullptr || m_ctx.combat == nullptr ||
        m_player == entt::null || m_vehicle == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    if (m_vehicleZombieAttackCooldown > 0.0f) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (!registry.all_of<ecs::VehicleOccupant>(m_player)) {
        return;
    }
    VehicleAttackerKind attackerKind = VehicleAttackerKind::None;
    std::string attackerSpecies;
    if (m_ctx.animalSystem != nullptr) {
        for (const AnimalSystem::AttackInfo& attack :
             m_ctx.animalSystem->GetAttacks()) {
            if (attack.target != m_player || !(attack.damage > 0.0f) ||
                attack.attacker == entt::null ||
                !m_ctx.ecs->Alive(attack.attacker)) {
                continue;
            }
            const ecs::Animal* animal =
                registry.try_get<ecs::Animal>(attack.attacker);
            const VehicleAttackerKind kind = VehicleAttackerKindFor(
                registry.try_get<ecs::AiState>(attack.attacker) != nullptr,
                animal != nullptr,
                animal != nullptr &&
                    animal->kind == ecs::AnimalKind::Dangerous,
                registry.any_of<ecs::PlayerTag>(attack.attacker),
                registry.any_of<ecs::FriendlyNpc>(attack.attacker));
            if (kind == VehicleAttackerKind::None) {
                continue;
            }
            attackerKind = kind;
            attackerSpecies = attack.speciesId;
            break;
        }
    }
    for (const characters::CombatSystem::HitInfo& hit :
         m_ctx.combat->GetHits()) {
        if (attackerKind != VehicleAttackerKind::None) {
            break;
        }
        if (hit.target != m_player || !(hit.damage > 0.0f) ||
            hit.attacker == entt::null || !m_ctx.ecs->Alive(hit.attacker)) {
            continue;
        }
        const ecs::Animal* animal =
            registry.try_get<ecs::Animal>(hit.attacker);
        const VehicleAttackerKind kind = VehicleAttackerKindFor(
            registry.try_get<ecs::AiState>(hit.attacker) != nullptr,
            animal != nullptr,
            animal != nullptr &&
                animal->kind == ecs::AnimalKind::Dangerous,
            registry.any_of<ecs::PlayerTag>(hit.attacker),
            registry.any_of<ecs::FriendlyNpc>(hit.attacker));
        if (kind == VehicleAttackerKind::None) {
            continue;
        }
        attackerKind = kind;
        if (animal != nullptr) {
            attackerSpecies = animal->speciesId;
        }
        break;
    }
    if (attackerKind == VehicleAttackerKind::None) {
        return;
    }
    const float wear = VehicleApplyZombieAttackConditionDamage(
        &m_vehicleMotion, m_vehicleTuning);
    if (wear <= 0.0f) {
        return;
    }
    m_vehicleZombieAttackCooldown =
        m_vehicleTuning.zombieAttackCooldownSeconds;
    SpawnVehicleHitFeedback(wear, m_vehicleMotion.x, m_vehicleMotion.y,
                            m_vehicleMotion.z);
    PlayVehicleHitSound(m_vehicleMotion.x, m_vehicleMotion.y);
    SetNotice("载具受击：车??" +
                  std::to_string(static_cast<int>(std::floor(
                      m_vehicleMotion.condition * 100.0f))) +
                  "%",
              kNoticeFailColor, 1.5f);
    if (attackerKind == VehicleAttackerKind::Animal) {
        const AnimalDef* def =
            m_ctx.animals != nullptr
                ? m_ctx.animals->Find(attackerSpecies)
                : nullptr;
        const std::string species =
            def != nullptr ? def->name : attackerSpecies;
        ZL_INFO("vehicle",
                "载具受动物攻击：stableId={}，物??{}，扣车况 {:.1f}%??"
                "剩余 {:.0f}%",
                m_ctx.ecs->IdOf(m_vehicle).id, species, wear * 100.0f,
                m_vehicleMotion.condition * 100.0f);
        return;
    }
    ZL_INFO("vehicle",
            "载具受僵尸攻击：stableId={}，扣车况 {:.1f}%，剩??{:.0f}%",
            m_ctx.ecs->IdOf(m_vehicle).id, wear * 100.0f,
            m_vehicleMotion.condition * 100.0f);
}

void WorldDemoState::SpawnVehicleHitFeedback(float wear, float x, float y,
                                             int z, bool shake) {
    const int count = VehicleHitSparkCount(wear);
    if (count <= 0 || !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    const float lifetime = VehicleHitSparkLifetime(wear);
    const float strength = VehicleHitFeedbackStrength(wear);
    ++m_vehicleHitSparkSerial;
    for (int i = 0; i < count; ++i) {
        const float angle =
            static_cast<float>(m_vehicleHitSparkSerial + i) * 1.37f +
            6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        const float distance =
            0.08f + 0.28f * static_cast<float>((m_vehicleHitSparkSerial +
                                                i * 3u) % 7u) /
                        6.0f;
        VehicleHitSpark spark;
        spark.x = x + std::cos(angle) * distance;
        spark.y = y + std::sin(angle) * distance;
        spark.z = z;
        spark.lifetime = lifetime;
        spark.size = 5.0f + 4.5f * strength;
        spark.vx = std::cos(angle) * (0.45f + 0.55f * strength);
        spark.vy = std::sin(angle) * (0.45f + 0.55f * strength);
        spark.tint = (i % 3 == 0) ? 0xFF8AE0FFu : 0xFF40A0FFu;
        if (m_vehicleHitSparks.size() >= 64) {
            m_vehicleHitSparks.erase(m_vehicleHitSparks.begin());
        }
        m_vehicleHitSparks.push_back(spark);
    }
    SpawnVehicleMetalDebris(strength, x, y, z);
    if (shake && m_driving) {
        TriggerVehicleScreenShake(strength);
    }
    ZL_INFO("vehicle",
            "载具受击火花：磨??{:.1f}%，sparks={}，寿??{:.2f}s",
            wear * 100.0f, count, lifetime);
}

void WorldDemoState::UpdateVehicleHitFeedback(float frameDt) {
    const float dt = std::clamp(frameDt, 0.0f, 0.10f);
    if (dt <= 0.0f) {
        return;
    }
    for (auto it = m_vehicleHitSparks.begin();
         it != m_vehicleHitSparks.end();) {
        it->age += dt;
        if (it->age >= it->lifetime) {
            it = m_vehicleHitSparks.erase(it);
        } else {
            ++it;
        }
    }
    if (m_vehicleShakeDuration > 0.0f) {
        m_vehicleShakeAge += dt;
        if (m_vehicleShakeAge >= m_vehicleShakeDuration) {
            m_vehicleShakeAge = 0.0f;
            m_vehicleShakeDuration = 0.0f;
            m_vehicleShakeStrength = 0.0f;
        }
    }
}

void WorldDemoState::DrawVehicleHitFeedback(render::SpriteBatch& batch) {
    if (m_vehicleHitSparks.empty()) {
        return;
    }
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    const bgfx::TextureHandle softTexture = batch.SoftParticleTexture();
    for (const VehicleHitSpark& spark : m_vehicleHitSparks) {
        const float t = std::clamp(
            spark.age / std::max(spark.lifetime, 0.01f), 0.0f, 1.0f);
        const glm::vec2 center =
            render::ToScreen(spark.x + spark.vx * spark.age,
                             spark.y + spark.vy * spark.age, spark.z) -
            glm::vec2(0.0f, 6.0f + spark.age * 18.0f);
        const float size = spark.size * (1.0f - 0.35f * t);
        const float alpha = (1.0f - t) * 0.95f;
        if (bgfx::isValid(softTexture)) {
            batch.DrawSprite(softTexture,
                             {center.x - size * 0.5f,
                              center.y - size * 0.5f, size, size},
                             0.0f, 0.0f, 1.0f, 1.0f,
                             withAlpha(spark.tint, alpha));
        } else {
            batch.DrawRectFill(center.x - size * 0.5f,
                               center.y - size * 0.5f, size, size,
                               withAlpha(spark.tint, alpha));
        }
    }
}

void WorldDemoState::SpawnVehicleMetalDebris(float strength, float x,
                                             float y, int z) {
    const int count = VehicleMetalDebrisCount(strength);
    const float lifetime = VehicleMetalDebrisLifetime(strength);
    if (count <= 0 || lifetime <= 0.0f || !std::isfinite(x) ||
        !std::isfinite(y)) {
        return;
    }
    ++m_vehicleMetalDebrisSerial;
    for (int i = 0; i < count; ++i) {
        const float angle =
            static_cast<float>(m_vehicleMetalDebrisSerial) * 1.91f +
            6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        const float distance =
            0.12f + 0.30f * static_cast<float>(
                               (m_vehicleMetalDebrisSerial + i * 5u) % 7u) /
                        6.0f;
        VehicleMetalDebris debris;
        debris.x = x + std::cos(angle) * distance;
        debris.y = y + std::sin(angle) * distance;
        debris.z = z;
        debris.lifetime = lifetime;
        debris.size = 3.5f + 5.0f * std::clamp(strength, 0.0f, 1.0f);
        debris.vx = std::cos(angle) * (0.7f + 1.1f * strength);
        debris.vy = std::sin(angle) * (0.7f + 1.1f * strength) - 0.35f;
        debris.tint = (i % 3 == 0)
                          ? 0xFFE0E0E0u
                          : ((i % 4 == 0) ? 0xFF50C0FFu : 0xFF707070u);
        if (m_vehicleMetalDebris.size() >= 48) {
            m_vehicleMetalDebris.erase(m_vehicleMetalDebris.begin());
        }
        m_vehicleMetalDebris.push_back(debris);
    }
    ZL_INFO("vehicle",
            "载具金属碎片：强??{:.2f}，pieces={}，寿??{:.2f}s",
            strength, count, lifetime);
}

void WorldDemoState::UpdateVehicleMetalDebris(float frameDt) {
    const float dt = std::clamp(frameDt, 0.0f, 0.10f);
    if (dt <= 0.0f) {
        return;
    }
    for (auto it = m_vehicleMetalDebris.begin();
         it != m_vehicleMetalDebris.end();) {
        it->age += dt;
        if (it->age >= it->lifetime) {
            it = m_vehicleMetalDebris.erase(it);
        } else {
            ++it;
        }
    }
}

void WorldDemoState::DrawVehicleMetalDebris(render::SpriteBatch& batch) {
    if (m_vehicleMetalDebris.empty()) {
        return;
    }
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    for (const VehicleMetalDebris& debris : m_vehicleMetalDebris) {
        const float t = std::clamp(
            debris.age / std::max(debris.lifetime, 0.01f), 0.0f, 1.0f);
        const glm::vec2 center =
            render::ToScreen(debris.x + debris.vx * debris.age,
                             debris.y + debris.vy * debris.age, debris.z) -
            glm::vec2(0.0f, 4.0f + debris.age * 24.0f);
        const float size = debris.size * (1.0f - 0.25f * t);
        const float alpha = (1.0f - t) * 0.90f;
        batch.DrawRectFill(center.x - size * 0.55f,
                           center.y - size * 0.42f, size * 1.10f,
                           size * 0.84f,
                           withAlpha(0xFF303030u, alpha * 0.35f));
        batch.DrawRectFill(center.x - size * 0.22f,
                           center.y - size * 0.72f, size * 0.44f,
                           size * 1.08f,
                           withAlpha(debris.tint, alpha));
    }
}

void WorldDemoState::TriggerVehicleScreenShake(float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return;
    }
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (m_vehicleShakeDuration > 0.0f &&
        m_vehicleShakeAge < m_vehicleShakeDuration &&
        clamped < m_vehicleShakeStrength) {
        return;
    }
    m_vehicleShakeStrength = clamped;
    m_vehicleShakeAge = 0.0f;
    m_vehicleShakeDuration = VehicleShakeDuration(clamped);
    ZL_INFO("vehicle", "载具镜头震动：强??{:.2f}，时??{:.2f}s",
            m_vehicleShakeStrength, m_vehicleShakeDuration);
}

VehicleScreenShake WorldDemoState::CurrentVehicleScreenShake() const {
    if (m_vehicleShakeDuration <= 0.0f) {
        return {};
    }
    return VehicleScreenShakeOffset(m_vehicleShakeStrength, m_vehicleShakeAge,
                                    m_vehicleShakeDuration);
}

void WorldDemoState::PlayVehicleHitSound(float x, float y) {
    if (m_ctx.audio == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    if (m_ctx.audio->Engine()
            .Play3D(audio::kVehicleHit, {x, y}, audio::AudioGroup::SFX,
                    0.78f, false, 18.0f)
            .Valid()) {
        ZL_INFO("vehicle", "载具受击音效：vehicle_hit");
    }
}

void WorldDemoState::EmitLocalVehicleNoise() {
    if (VehicleSpeed(m_vehicleMotion) < m_vehicleTuning.noiseMinSpeed) {
        m_vehicleNoiseCooldown = 0.0f;
        return;
    }
    if (m_vehicleNoiseCooldown > 0.0f || m_ctx.events == nullptr) {
        return;
    }
    core::SoundEvent noise;
    noise.x = m_vehicleMotion.x;
    noise.y = m_vehicleMotion.y;
    noise.radius = m_vehicleTuning.noiseRadius;
    noise.type = core::SoundType::Vehicle;
    m_ctx.events->EmitQueued(core::kOnSound, std::move(noise));
    m_vehicleNoiseCooldown = m_vehicleTuning.noiseIntervalSeconds;
}

bool WorldDemoState::NetworkVehicleHasPassenger() const {
    if (!m_networkMode || m_ctx.ecs == nullptr ||
        m_ctx.networkClient == nullptr || m_vehicle == entt::null ||
        !m_ctx.ecs->Alive(m_vehicle)) {
        return false;
    }
    const std::uint64_t vehicleId = m_ctx.ecs->IdOf(m_vehicle).id;
    if (vehicleId == 0) {
        return false;
    }
    if (const ReplicatedActor* actor =
            m_ctx.networkClient->Replication().Find(vehicleId);
        actor != nullptr && actor->hasVehicle) {
        return actor->vehiclePassengerStableId != 0 ||
               actor->vehicleSecondPassengerStableId != 0;
    }
    const auto* passenger = m_ctx.ecs->Registry().try_get<ecs::VehiclePassenger>(
        m_vehicle);
    return passenger != nullptr &&
           (passenger->stableId != 0 || passenger->secondStableId != 0);
}

bool WorldDemoState::NetworkVehicleDriverSeatOpen() const {
    if (!m_networkMode || !m_riding || m_driving || m_vehicle == entt::null ||
        m_ctx.ecs == nullptr || !m_ctx.ecs->Alive(m_vehicle) ||
        m_ctx.networkClient == nullptr) {
        return false;
    }
    const std::uint64_t vehicleId = m_ctx.ecs->IdOf(m_vehicle).id;
    if (vehicleId == 0) {
        return false;
    }
    const ReplicatedActor* actor =
        m_ctx.networkClient->Replication().Find(vehicleId);
    return actor != nullptr && actor->hasVehicle &&
           game::VehicleCanTakeDriverSeat(
               actor->vehicleDriverStableId,
               actor->vehiclePassengerStableId,
               actor->vehicleSecondPassengerStableId,
               m_ctx.networkClient->PlayerStableId());
}

void WorldDemoState::TakeDriverSeat() {
    if (!NetworkVehicleDriverSeatOpen()) {
        return;
    }
    net::Command command =
        MakePlayerCommand(net::CommandType::TakeDriverSeat);
    std::string error;
    if (!SubmitPlayerCommand(command)) {
        SetNotice(error.empty() ? "接管驾驶位请求发送失??" : error,
                  kNoticeFailColor, 1.8f);
    }
}

void WorldDemoState::GiveDriverSeat() {
    if (!m_driving || !NetworkVehicleHasPassenger() ||
        VehicleSpeed(m_vehicleMotion) > m_vehicleTuning.seatHandoffMaxSpeed) {
        return;
    }
    net::Command command =
        MakePlayerCommand(net::CommandType::GiveDriverSeat);
    std::string error;
    if (!SubmitPlayerCommand(command)) {
        SetNotice(error.empty() ? "让座请求发送失??" : error,
                  kNoticeFailColor, 1.8f);
    }
}

void WorldDemoState::UpdateVehicleAudio() {
    if (m_ctx.audio == nullptr || !m_ctx.audio->IsReady()) {
        StopVehicleAudio();
        return;
    }
    audio::AudioEngine& engine = m_ctx.audio->Engine();
    std::uint64_t localId = 0;
    if (m_driving && m_vehicle != entt::null && m_ctx.ecs != nullptr &&
        m_ctx.ecs->Alive(m_vehicle)) {
        localId = m_ctx.ecs->IdOf(m_vehicle).id;
    }
    struct DesiredEngineVoice {
        std::uint64_t stableId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float speed = 0.0f;
        float condition = 1.0f;
        bool local = false;
    };
    std::vector<DesiredEngineVoice> desired;
    if (localId != 0 &&
        VehicleEngineAudioSourceFor(true, false, m_vehicleMotion.condition) ==
            VehicleEngineAudioSource::Local) {
        desired.push_back(DesiredEngineVoice{
            localId, m_vehicleMotion.x, m_vehicleMotion.y,
            VehicleSpeed(m_vehicleMotion), m_vehicleMotion.condition, true});
    }
    std::vector<VehicleEngineAudioCandidate> candidates;
    if (m_networkMode && m_ctx.ecs != nullptr &&
        m_ctx.networkClient != nullptr) {
        const std::uint64_t playerId =
            m_ctx.networkClient->PlayerStableId();
        entt::registry& registry = m_ctx.ecs->Registry();
        registry.view<ecs::Position, ecs::Vehicle, ecs::VehicleDriver>().each(
            [&](const ecs::Entity entity, const ecs::Position& position,
                const ecs::Vehicle& vehicle,
                const ecs::VehicleDriver& driver) {
                if (!m_ctx.ecs->Alive(entity)) {
                    return;
                }
                const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
                if (stableId == 0 || stableId == localId ||
                    driver.stableId == 0 || driver.stableId == playerId) {
                    return;
                }
                const auto* velocity =
                    registry.try_get<ecs::Velocity>(entity);
                candidates.push_back(VehicleEngineAudioCandidate{
                    stableId, position.x, position.y,
                    velocity != nullptr
                        ? std::sqrt(velocity->x * velocity->x +
                                    velocity->y * velocity->y)
                        : 0.0f,
                    std::clamp(vehicle.condition, 0.0f, 1.0f), true});
            });
    }
    glm::vec2 listener = engine.Listener();
    if (m_ctx.ecs != nullptr && m_player != entt::null &&
        m_ctx.ecs->Alive(m_player)) {
        if (const auto* position =
                m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
            position != nullptr) {
            listener = glm::vec2(position->x, position->y);
        }
    }
    for (const std::uint64_t stableId : SelectRemoteVehicleEngineAudio(
             candidates, listener.x, listener.y)) {
        const auto found = std::find_if(
            candidates.begin(), candidates.end(),
            [stableId](const VehicleEngineAudioCandidate& candidate) {
                return candidate.stableId == stableId;
            });
        if (found == candidates.end() ||
            VehicleEngineAudioSourceFor(false, true, found->condition) !=
                VehicleEngineAudioSource::Remote) {
            continue;
        }
        desired.push_back(DesiredEngineVoice{
            found->stableId, found->x, found->y, found->speed,
            found->condition, false});
    }
    std::unordered_set<std::uint64_t> desiredIds;
    desiredIds.reserve(desired.size());
    for (const DesiredEngineVoice& voice : desired) {
        desiredIds.insert(voice.stableId);
        VehicleEngineAudioVoice& existing =
            m_vehicleEngineVoices[voice.stableId];
        const VehicleEngineTone tone = VehicleEngineToneForSpeed(
            voice.speed, m_vehicleTuning.maxSpeed,
            m_vehicleTuning.engineIdleVolume,
            m_vehicleTuning.engineMaxVolume,
            m_vehicleTuning.engineMinPitch,
            m_vehicleTuning.engineMaxPitch);
        if (existing.soundId == 0) {
            const audio::AudioHandle handle = engine.Play3D(
                audio::kVehicleEngine, glm::vec2(voice.x, voice.y),
                audio::AudioGroup::SFX, tone.volume, true,
                kVehicleEngineAudioMaxDistance, true);
            if (!handle.Valid()) {
                continue;
            }
            existing.soundId = handle.id;
            engine.SetPitch(handle, tone.pitch);
            ZL_INFO("vehicle",
                    "引擎音效启动（{}，占位循环，载具 stableId={}??"
                    "音量/音高随速度??",
                    voice.local ? "本地" : "远端", voice.stableId);
            continue;
        }
        const audio::AudioHandle handle{existing.soundId};
        engine.SetPosition(handle, glm::vec2(voice.x, voice.y));
        engine.SetVolume(handle, tone.volume);
        engine.SetPitch(handle, tone.pitch);
    }
    for (auto it = m_vehicleEngineVoices.begin();
         it != m_vehicleEngineVoices.end();) {
        if (desiredIds.contains(it->first)) {
            ++it;
            continue;
        }
        const audio::AudioHandle handle{it->second.soundId};
        if (handle.Valid()) {
            engine.Stop(handle);
            ZL_INFO("vehicle", "引擎音效停止（载??stableId={}??", it->first);
        }
        it = m_vehicleEngineVoices.erase(it);
    }
}

void WorldDemoState::StopVehicleAudio() {
    if (m_vehicleEngineVoices.empty()) {
        return;
    }
    if (m_ctx.audio != nullptr) {
        audio::AudioEngine& engine = m_ctx.audio->Engine();
        for (const auto& [stableId, voice] : m_vehicleEngineVoices) {
            const audio::AudioHandle handle{voice.soundId};
            if (handle.Valid()) {
                engine.Stop(handle);
                ZL_INFO("vehicle", "引擎音效停止（载??stableId={}??",
                        stableId);
            }
        }
    }
    m_vehicleEngineVoices.clear();
    ZL_INFO("vehicle", "引擎音效停止");
}

void WorldDemoState::PlayLocalVehicleRamSound() {
    if (m_ctx.audio == nullptr || m_vehicleTuning.ramSoundVolume <= 0.0f) {
        return;
    }
    m_ctx.audio->Engine().Play3D(
        audio::kVehicleRam,
        glm::vec2(m_vehicleMotion.x, m_vehicleMotion.y),
        audio::AudioGroup::SFX, m_vehicleTuning.ramSoundVolume, false, 18.0f);
}

void WorldDemoState::HonkVehicleHorn() {
    if (!m_driving || m_vehicle == entt::null) {
        return;
    }
    if (!VehicleHornReady(m_vehicleMotion.condition,
                          m_vehicleHornCooldown)) {
        if (m_vehicleMotion.condition <= 0.0f ||
            !std::isfinite(m_vehicleMotion.condition)) {
            SetNotice("车辆损坏，无法鸣??", kNoticeFailColor, 1.5f);
        }
        return;
    }
    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::HonkHorn);
        if (!SubmitPlayerCommand(command)) {
            SetNotice("喇叭请求发送失??", kNoticeFailColor, 1.5f);
            return;
        }
        m_vehicleHornCooldown = m_vehicleTuning.hornCooldownSeconds;
        SetNotice("喇叭：鸣笛请求已发??", kNoticeOkColor, 1.2f);
        ZL_INFO("vehicle", "载具喇叭请求：吸引半??{:.0f} ??",
                m_vehicleTuning.hornNoiseRadius);
        return;
    }
    if (m_ctx.events == nullptr) {
        return;
    }
    m_vehicleHornCooldown = m_vehicleTuning.hornCooldownSeconds;
    core::SoundEvent noise;
    noise.x = m_vehicleMotion.x;
    noise.y = m_vehicleMotion.y;
    noise.radius = m_vehicleTuning.hornNoiseRadius;
    noise.type = core::SoundType::Vehicle;
    noise.clip = audio::kVehicleHorn;
    m_ctx.events->EmitQueued(core::kOnSound, std::move(noise));
    SetNotice("喇叭：鸣笛（吸引 " +
                  std::to_string(static_cast<int>(std::lround(
                      m_vehicleTuning.hornNoiseRadius))) +
                  " 格）",
              kNoticeOkColor, 1.2f);
    ZL_INFO("vehicle", "载具喇叭：吸引半??{:.0f} ??",
            m_vehicleTuning.hornNoiseRadius);
}

void WorldDemoState::ToggleVehicleHeadlights() {
    if (!m_driving) {
        return;
    }
    if (m_networkMode) {
        if (m_ctx.ecs == nullptr || m_vehicle == entt::null ||
            !m_ctx.ecs->Alive(m_vehicle)) {
            return;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::ToggleHeadlights);
        std::string error;
        if (!SubmitPlayerCommand(command)) {
            SetNotice(error.empty() ? "车灯请求发送失??" : error,
                      kNoticeFailColor, 1.8f);
        }
        return;
    }
    m_vehicleMotion.headlights = !m_vehicleMotion.headlights;
    SyncVehicleStateToEcs();
    SetNotice(m_vehicleMotion.headlights ? "车灯已开??" : "车灯已关??",
              kNoticeInfoColor, 1.5f);
    ZL_INFO("vehicle", "车灯：{}", m_vehicleMotion.headlights ? "开" : "??");
}

VehicleSurface WorldDemoState::VehicleSurfaceAt(float x, float y, int z) const {
    if (m_ctx.world == nullptr) {
        return VehicleSurface::Grass;
    }
    const world::GridSquare* square = m_ctx.world->GetSquare(
        static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), z);
    if (square == nullptr) {
        return VehicleSurface::Grass;
    }
    const world::SquareObject* floor = square->Floor();
    if (floor == nullptr || floor->sprite.empty()) {
        return VehicleSurface::Grass;
    }
    return VehicleSurfaceForFloor(floor->sprite);
}

void WorldDemoState::UpdateVehicle(float tickDt) {
    if (m_networkMode || m_vehicle == entt::null || !m_driving) {
        return;
    }
    if (m_ctx.ecs == nullptr || !m_ctx.ecs->Alive(m_vehicle)) {
        m_vehicle = entt::null;
        m_driving = false;
        if (m_ctx.ecs != nullptr && m_player != entt::null &&
            m_ctx.ecs->Alive(m_player)) {
            m_ctx.ecs->Registry().remove<ecs::VehicleOccupant>(m_player);
        }
        StopVehicleAudio();
        m_playerSpriteBackup.reset();
        return;
    }
    if (m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        m_driving = false;
        StopVehicleAudio();
        m_playerSpriteBackup.reset();
        return;
    }

    m_vehicleNoiseCooldown =
        std::max(0.0f, m_vehicleNoiseCooldown - tickDt);
    m_vehicleRamWearCooldown =
        std::max(0.0f, m_vehicleRamWearCooldown - tickDt);
    m_vehicleBodyCollisionCooldown =
        std::max(0.0f, m_vehicleBodyCollisionCooldown - tickDt);
    m_vehicleHornCooldown =
        std::max(0.0f, m_vehicleHornCooldown - tickDt);
    m_vehicleZombieAttackCooldown =
        std::max(0.0f, m_vehicleZombieAttackCooldown - tickDt);
    UpdateLocalParkedVehicleMotions(tickDt);
    ApplyLocalVehicleAttacks();
    for (auto it = m_vehicleRamCooldowns.begin();
         it != m_vehicleRamCooldowns.end();) {
        it->second -= tickDt;
        if (it->second <= 0.0f) {
            it = m_vehicleRamCooldowns.erase(it);
        } else {
            ++it;
        }
    }

    float inputX = 0.0f;
    float inputY = 0.0f;
    if (m_moveCommandValid) {
        const glm::vec2 screen(m_lastMoveCommand.x, m_lastMoveCommand.y);
        if (glm::length(screen) > 0.0001f) {
            const glm::vec2 grid = glm::normalize(
                characters::InputSystem::ScreenDirToGridDir(glm::normalize(screen)));
            inputX = grid.x;
            inputY = grid.y;
        }
    }
    const auto blocked = [this](int x, int y, int z) {
        return m_ctx.world == nullptr || IsSquareBlocked(*m_ctx.world, x, y, z);
    };
    const VehicleTuning activeTuning =
        VehicleTuningForType(m_vehicleMotion.type, m_vehicleTuning);
    ecs::Entity bodyCollisionTarget = entt::null;
    const auto bodyBlocked = [this, &bodyCollisionTarget](
                                 float x, float y, int z, float radius) {
        bool hit = false;
        m_ctx.ecs->Registry()
            .view<ecs::Position, ecs::Vehicle>()
            .each([&](const ecs::Entity entity, const ecs::Position& position,
                      const ecs::Vehicle& otherVehicle) {
                if (entity == m_vehicle || position.z != z) {
                    return;
                }
                if (VehicleBodiesOverlap(
                        x, y, radius, position.x, position.y,
                        VehicleRadiusForType(otherVehicle.type))) {
                    hit = true;
                    if (bodyCollisionTarget == entt::null) {
                        bodyCollisionTarget = entity;
                    }
                }
            });
        return hit;
    };
    m_vehicleSurface =
        VehicleSurfaceAt(m_vehicleMotion.x, m_vehicleMotion.y, m_vehicleMotion.z);
    if (m_vehicleSurface != m_lastVehicleSurface) {
        const VehicleSurfaceProfile profile =
            VehicleSurfaceProfileFor(m_vehicleSurface, activeTuning);
        ZL_INFO("vehicle", "路面：{}（极??x{:.2f}，加??x{:.2f}，制??x{:.2f}??",
                VehicleSurfaceLabel(m_vehicleSurface), profile.maxSpeedScale,
                profile.accelerationScale, profile.brakingScale);
        m_lastVehicleSurface = m_vehicleSurface;
    }
    const VehicleMotion previousMotion = m_vehicleMotion;
    const VehicleStepResult vehicleStep =
        VehicleStep(&m_vehicleMotion, inputX, inputY, tickDt, blocked,
                    activeTuning, m_vehicleSurface, bodyBlocked);
    m_vehicleMotion.heading =
        VehicleNormalizeAngle(m_vehicleMotion.heading);
    VehicleHeadingVector(m_vehicleMotion, &m_vehicleHeadingX,
                         &m_vehicleHeadingY);
    ApplyLocalVehicleRam(&previousMotion);
    EmitLocalVehicleNoise();
    const float conditionScale =
        VehicleConditionPerformanceScale(m_vehicleMotion.condition);
    if (m_vehicleMotion.condition > 0.0f && conditionScale < 1.0f) {
        if (!m_vehicleLowConditionWarned) {
            m_vehicleLowConditionWarned = true;
            SetNotice("车况低：动力下降", kNoticeInfoColor, 2.0f);
            ZL_INFO("vehicle", "车况低：动力 x{:.2f}（车??{:.0f}%??",
                    conditionScale, m_vehicleMotion.condition * 100.0f);
        }
    } else if (conditionScale >= 1.0f) {
        m_vehicleLowConditionWarned = false;
    }
    if (vehicleStep.damaged) {
        SetNotice("车辆碰撞：车况 " +
                      std::to_string(static_cast<int>(
                          std::floor(m_vehicleMotion.condition * 100.0f))) +
                      "%",
                  kNoticeFailColor, 2.0f);
        ZL_INFO("vehicle", "碰撞：速度 {:.1f}，扣车况 {:.1f}%，剩??{:.0f}%",
                vehicleStep.impactSpeed, vehicleStep.damage * 100.0f,
                m_vehicleMotion.condition * 100.0f);
        SpawnVehicleHitFeedback(vehicleStep.damage, m_vehicleMotion.x,
                                m_vehicleMotion.y, m_vehicleMotion.z);
        PlayLocalVehicleRamSound();
    }
    if (vehicleStep.bodyCollision || vehicleStep.bodyContact) {
        ApplyLocalVehicleVehicleCollision(vehicleStep.impactSpeed,
                                          bodyCollisionTarget,
                                          previousMotion);
    }

    if (auto* vehiclePosition = m_ctx.ecs->Registry().try_get<ecs::Position>(m_vehicle);
        vehiclePosition != nullptr) {
        vehiclePosition->x = m_vehicleMotion.x;
        vehiclePosition->y = m_vehicleMotion.y;
        vehiclePosition->z = m_vehicleMotion.z;
    }
    SyncPlayerToVehicle();
    SyncVehicleStateToEcs();

    if (m_vehicleMotion.fuel <= 0.0f && !m_vehicleFuelWarned) {
        m_vehicleFuelWarned = true;
        SetNotice("燃料耗尽：车辆无法继续加速", kNoticeFailColor, 2.5f);
        ZL_INFO("vehicle", "燃料耗尽");
    }
}

void WorldDemoState::SyncVehicleVisualDirectionFromSprite(
    ecs::Entity entity) {
    m_vehicleVisualDirection = VehicleVisualDirection::South;
    m_vehicleHeadingX = 0.0f;
    m_vehicleHeadingY = 1.0f;
    if (m_ctx.ecs == nullptr || entity == entt::null ||
        !m_ctx.ecs->Alive(entity)) {
        return;
    }
    const auto* sprite =
        m_ctx.ecs->Registry().try_get<ecs::SpriteRef>(entity);
    if (sprite == nullptr) {
        return;
    }
    m_vehicleVisualDirection = VehicleVisualDirectionFromSpriteHash(
        sprite->sprite, m_vehicleVisualDirection);
    VehicleVisualDirectionVector(m_vehicleVisualDirection,
                                 &m_vehicleHeadingX, &m_vehicleHeadingY);
    if (const auto* state =
            m_ctx.ecs->Registry().try_get<ecs::Vehicle>(entity);
        state != nullptr) {
        m_vehicleMotion.type = VehicleTypeIsValid(state->type)
                                   ? state->type
                                   : ecs::VehicleType::Pickup;
        if (std::isfinite(state->heading)) {
            m_vehicleMotion.heading =
                VehicleNormalizeAngle(state->heading);
            VehicleHeadingVector(m_vehicleMotion, &m_vehicleHeadingX,
                                 &m_vehicleHeadingY);
            m_vehicleVisualDirection = VehicleVisualDirectionFor(
                m_vehicleHeadingX, m_vehicleHeadingY,
                m_vehicleVisualDirection);
            return;
        }
    }
    m_vehicleMotion.heading = std::atan2(m_vehicleHeadingY,
                                         m_vehicleHeadingX);
}

void WorldDemoState::UpdateVehicleVisual() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    std::uint64_t activeId = 0;
    if (m_vehicle != entt::null && m_ctx.ecs->Alive(m_vehicle)) {
        activeId = m_ctx.ecs->IdOf(m_vehicle).id;
        auto* sprite = registry.try_get<ecs::SpriteRef>(m_vehicle);
        if (sprite != nullptr) {
            const VehicleVisualDirection previous = m_vehicleVisualDirection;
            const VehicleVisualDirection current =
                VehicleVisualDirectionFromSpriteHash(sprite->sprite,
                                                     previous);
            VehicleHeadingVector(m_vehicleMotion, &m_vehicleHeadingX,
                                 &m_vehicleHeadingY);
            const bool hasVisualHeading =
                std::isfinite(m_vehicleMotion.heading);
            const VehicleVisualDirection next =
                hasVisualHeading
                    ? VehicleVisualDirectionFor(
                          m_vehicleHeadingX, m_vehicleHeadingY, current)
                    : current;
            m_vehicleVisualDirection = next;
            sprite->sprite =
                ecs::HashName(VehicleVisualSprite(next));
            m_vehicleVisualDirections[activeId] = next;
            if (next != previous) {
                ZL_INFO("vehicle",
                        "外观朝向：stableId={}，{}", activeId,
                        VehicleVisualLabel(next));
            }
        }
    }
    registry.view<ecs::Position, ecs::SpriteRef, ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Position&,
            const ecs::SpriteRef& sprite, const ecs::Vehicle& vehicleState) {
            const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
            if (stableId == 0 || stableId == activeId) {
                return;
            }
            VehicleVisualDirection& direction =
                m_vehicleVisualDirections
                    .try_emplace(stableId,
                                 VehicleVisualDirection::South)
                    .first->second;
            direction = VehicleVisualDirectionFromSpriteHash(
                sprite.sprite, direction);
            if (const auto parked =
                    m_localParkedVehicleMotions.find(stableId);
                parked != m_localParkedVehicleMotions.end()) {
                direction = VehicleVisualDirectionForMotion(
                    parked->second, direction);
            } else if (std::isfinite(vehicleState.heading)) {
                VehicleMotion headingMotion;
                headingMotion.heading =
                    VehicleNormalizeAngle(vehicleState.heading);
                direction = VehicleVisualDirectionForMotion(
                    headingMotion, direction);
            } else if (const auto* velocity =
                           registry.try_get<ecs::Velocity>(entity);
                       velocity != nullptr &&
                       std::hypot(velocity->x, velocity->y) > 0.05f) {
                direction = VehicleVisualDirectionFor(
                    velocity->x, velocity->y, direction);
            }
        });
}

void WorldDemoState::UpdateVehicleDamageSmoke(float frameDt) {
    const float dt = std::clamp(frameDt, 0.0f, 0.10f);
    if (dt <= 0.0f) {
        return;
    }
    auto& puffs = m_vehicleSmokePuffs;
    for (auto it = puffs.begin(); it != puffs.end();) {
        it->age += dt;
        if (it->age >= it->lifetime) {
            it = puffs.erase(it);
        } else {
            ++it;
        }
    }
    if (m_ctx.ecs == nullptr) {
        puffs.clear();
        m_vehicleSmokeRuntimes.clear();
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    std::unordered_set<std::uint64_t> seen;
    const auto headingFor = [&](ecs::Entity entity, std::uint64_t stableId,
                                float* headingX, float* headingY) {
        float x = 0.0f;
        float y = 0.0f;
        if (entity == m_vehicle) {
            x = m_vehicleHeadingX;
            y = m_vehicleHeadingY;
        }
        if (const auto* velocity =
                registry.try_get<ecs::Velocity>(entity);
            velocity != nullptr &&
            std::hypot(velocity->x, velocity->y) > 0.05f) {
            x = velocity->x;
            y = velocity->y;
        }
        float length = std::sqrt(x * x + y * y);
        if (!std::isfinite(length) || length < 0.0001f) {
            const auto found = m_vehicleVisualDirections.find(stableId);
            VehicleVisualDirectionVector(
                found != m_vehicleVisualDirections.end()
                    ? found->second
                    : VehicleVisualDirection::South,
                &x, &y);
            length = std::sqrt(x * x + y * y);
        }
        if (!std::isfinite(length) || length < 0.0001f) {
            x = 0.0f;
            y = 1.0f;
            length = 1.0f;
        }
        *headingX = x / length;
        *headingY = y / length;
    };
    registry.view<ecs::Position, ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::Vehicle& state) {
            const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
            if (stableId == 0) {
                return;
            }
            seen.insert(stableId);
            VehicleSmokeRuntime& runtime =
                m_vehicleSmokeRuntimes[stableId];
            const VehicleDamageSmokeLevel level =
                VehicleDamageSmokeLevelFor(state.condition);
            if (level != runtime.level) {
                if (level != VehicleDamageSmokeLevel::None ||
                    runtime.level != VehicleDamageSmokeLevel::None) {
                    ZL_INFO("vehicle",
                            "载具烟雾：stableId={}，车??{:.0f}%，等??{}"
                            "（smoke level={}??",
                            stableId,
                            std::clamp(state.condition, 0.0f, 1.0f) * 100.0f,
                            VehicleDamageSmokeLabel(level),
                            static_cast<int>(level));
                }
                runtime.level = level;
            }
            if (level == VehicleDamageSmokeLevel::None) {
                runtime.timer = 0.0f;
                return;
            }
            runtime.timer -= dt;
            if (runtime.timer > 0.0f) {
                return;
            }
            runtime.timer = VehicleDamageSmokeInterval(level);
            if (!m_view.Contains(position.x, position.y)) {
                return;
            }
            float headingX = 0.0f;
            float headingY = 1.0f;
            headingFor(entity, stableId, &headingX, &headingY);
            const float sideX = -headingY;
            const float sideY = headingX;
            ++m_vehicleSmokeSerial;
            const std::uint32_t serial = m_vehicleSmokeSerial;
            const float lateral =
                (static_cast<float>(serial % 5u) - 2.0f) * 0.11f;
            const float back = static_cast<float>(serial % 3u) * 0.07f;
            VehicleSmokePuff puff;
            puff.x = position.x + headingX * (0.82f - back) +
                     sideX * lateral;
            puff.y = position.y + headingY * (0.82f - back) +
                     sideY * lateral;
            puff.z = position.z;
            puff.lifetime = VehicleDamageSmokeLifetime(level);
            puff.size = level == VehicleDamageSmokeLevel::Light
                            ? 18.0f
                            : (level == VehicleDamageSmokeLevel::Heavy
                                   ? 24.0f
                                   : 30.0f);
            puff.driftX =
                sideX * (static_cast<float>(serial % 7u) - 3.0f) * 0.025f;
            puff.driftY =
                sideY * (static_cast<float>(serial % 7u) - 3.0f) * 0.025f;
            puff.level = level;
            if (puffs.size() >= 96) {
                puffs.erase(puffs.begin());
            }
            puffs.push_back(puff);
        });
    for (auto it = m_vehicleSmokeRuntimes.begin();
         it != m_vehicleSmokeRuntimes.end();) {
        if (seen.contains(it->first)) {
            ++it;
        } else {
            it = m_vehicleSmokeRuntimes.erase(it);
        }
    }
}

void WorldDemoState::DrawVehicleDamageSmoke(render::SpriteBatch& batch) {
    if (m_vehicleSmokePuffs.empty()) {
        return;
    }
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    const bgfx::TextureHandle softTexture = batch.SoftParticleTexture();
    if (bgfx::isValid(softTexture)) {
        if (!m_vehicleSmokeSoftLogged) {
            m_vehicleSmokeSoftLogged = true;
            ZL_INFO("vehicle",
                    "载具烟雾：软粒子（程序纹理，puffs={}??",
                    m_vehicleSmokePuffs.size());
        }
        for (const VehicleSmokePuff& puff : m_vehicleSmokePuffs) {
            const float t = std::clamp(
                puff.age / std::max(puff.lifetime, 0.01f), 0.0f, 1.0f);
            const glm::vec2 center =
                render::ToScreen(puff.x + puff.driftX * puff.age,
                                 puff.y + puff.driftY * puff.age, puff.z) -
                glm::vec2(0.0f, 12.0f + puff.age * 16.0f + t * 14.0f);
            const float size = VehicleSmokeParticleScale(puff.size, t);
            const float alpha =
                VehicleSmokeParticleAlpha(puff.level, t);
            const float coreSize = size * 0.72f;
            const std::uint32_t base =
                puff.level == VehicleDamageSmokeLevel::Light
                    ? 0xE0E8E8u
                    : (puff.level == VehicleDamageSmokeLevel::Heavy
                           ? 0xA8B0B0u
                           : 0x686868u);
            batch.DrawSprite(softTexture,
                             {center.x - size * 0.5f,
                              center.y - size * 0.5f, size, size},
                             0.0f, 0.0f, 1.0f, 1.0f,
                             withAlpha(base, alpha));
            batch.DrawSprite(softTexture,
                             {center.x - coreSize * 0.5f,
                              center.y - coreSize * 0.5f, coreSize, coreSize},
                             0.0f, 0.0f, 1.0f, 1.0f,
                             withAlpha(base, alpha * 0.62f));
            if (puff.level == VehicleDamageSmokeLevel::Critical &&
                t < 0.45f) {
                const float sparkSize = 4.0f + 3.0f * t;
                const float wobble =
                    std::sin((puff.age + puff.x + puff.y) * 9.0f);
                batch.DrawSprite(softTexture,
                                 {center.x + size * 0.22f + wobble * 5.0f -
                                      sparkSize * 0.5f,
                                  center.y + size * 0.08f - sparkSize * 0.5f,
                                  sparkSize, sparkSize},
                                 0.0f, 0.0f, 1.0f, 1.0f,
                                 withAlpha(0xF0A040u, alpha * 1.35f));
            }
        }
        return;
    }
    for (const VehicleSmokePuff& puff : m_vehicleSmokePuffs) {
        const float t =
            std::clamp(puff.age / std::max(puff.lifetime, 0.01f), 0.0f, 1.0f);
        const glm::vec2 center =
            render::ToScreen(puff.x + puff.driftX * puff.age,
                             puff.y + puff.driftY * puff.age, puff.z) -
            glm::vec2(0.0f, 12.0f + puff.age * 16.0f + t * 14.0f);
        const float size = puff.size * (0.70f + 0.90f * t);
        const float alpha = (1.0f - t) * 0.68f;
        const std::uint32_t base =
            puff.level == VehicleDamageSmokeLevel::Light
                ? 0xE0E8E8u
                : (puff.level == VehicleDamageSmokeLevel::Heavy
                       ? 0xA8B0B0u
                       : 0x686868u);
        batch.DrawRectFill(center.x - size * 0.55f,
                           center.y - size * 0.35f, size, size * 0.72f,
                           withAlpha(base, alpha));
        batch.DrawRectFill(center.x - size * 0.30f,
                           center.y - size * 0.82f, size * 0.60f,
                           size * 0.52f, withAlpha(base, alpha * 0.82f));
        batch.DrawRectFill(center.x - size * 0.12f,
                           center.y + size * 0.20f, size * 0.46f,
                           size * 0.42f, withAlpha(base, alpha * 0.62f));
        if (puff.level == VehicleDamageSmokeLevel::Critical &&
            t < 0.45f) {
            batch.DrawRectFill(center.x + size * 0.18f,
                               center.y + size * 0.12f, 4.0f, 4.0f,
                               withAlpha(0xD07030u, (1.0f - t) * 0.85f));
        }
    }
}

void WorldDemoState::UpdateVehicleDamageVisual() {
    if (m_ctx.ecs == nullptr) {
        m_vehicleDamageVisuals.clear();
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    std::unordered_set<std::uint64_t> seen;
    registry.view<ecs::Position, ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Position&,
            const ecs::Vehicle& state) {
            const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
            if (stableId == 0) {
                return;
            }
            seen.insert(stableId);
            const VehicleDamageVisualLevel level =
                VehicleDamageVisualLevelFor(state.condition);
            const auto found = m_vehicleDamageVisuals.find(stableId);
            const VehicleDamageVisualLevel previous =
                found != m_vehicleDamageVisuals.end()
                    ? found->second
                    : VehicleDamageVisualLevel::None;
            m_vehicleDamageVisuals[stableId] = level;
            if (level == previous) {
                return;
            }
            ZL_INFO("vehicle",
                    "载具破损外观：stableId={}，车??{:.0f}%，等??{}"
                    "（marks={}??",
                    stableId,
                    std::clamp(state.condition, 0.0f, 1.0f) * 100.0f,
                    VehicleDamageVisualLabel(level),
                    VehicleDamageVisualMarkCount(level));
        });
    for (auto it = m_vehicleDamageVisuals.begin();
         it != m_vehicleDamageVisuals.end();) {
        if (seen.contains(it->first)) {
            ++it;
        } else {
            it = m_vehicleDamageVisuals.erase(it);
        }
    }
}

void WorldDemoState::DrawVehicleDamageVisual(
    render::SpriteBatch& batch, const render::SpriteRect& rect,
    VehicleDamageVisualLevel level) {
    if (level == VehicleDamageVisualLevel::None || rect.w <= 0.0f ||
        rect.h <= 0.0f) {
        return;
    }

    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    struct ScratchMark {
        float x0;
        float y0;
        float x1;
        float y1;
    };
    static constexpr ScratchMark kScratches[10] = {
        {0.28f, 0.36f, 0.43f, 0.43f},
        {0.56f, 0.33f, 0.67f, 0.41f},
        {0.36f, 0.53f, 0.48f, 0.48f},
        {0.60f, 0.56f, 0.73f, 0.62f},
        {0.31f, 0.64f, 0.43f, 0.61f},
        {0.47f, 0.69f, 0.55f, 0.58f},
        {0.68f, 0.44f, 0.76f, 0.55f},
        {0.25f, 0.48f, 0.34f, 0.57f},
        {0.52f, 0.46f, 0.62f, 0.37f},
        {0.40f, 0.73f, 0.49f, 0.67f},
    };
    static constexpr float kDents[5][3] = {
        {0.38f, 0.42f, 0.34f},
        {0.62f, 0.50f, 0.40f},
        {0.48f, 0.57f, 0.32f},
        {0.31f, 0.51f, 0.28f},
        {0.68f, 0.64f, 0.30f},
    };

    const int markCount = VehicleDamageVisualMarkCount(level);
    const int dentCount = level == VehicleDamageVisualLevel::Dented
                              ? 2
                              : (level == VehicleDamageVisualLevel::Wrecked
                                     ? 5
                                     : 0);
    const float baseAlpha =
        level == VehicleDamageVisualLevel::Scuffed
            ? 0.82f
            : (level == VehicleDamageVisualLevel::Dented ? 0.90f : 1.0f);
    const bgfx::TextureHandle softTexture = batch.SoftParticleTexture();
    for (int i = 0; i < dentCount; ++i) {
        const float size = rect.w * kDents[i][2];
        const float cx = rect.x + rect.w * kDents[i][0];
        const float cy = rect.y + rect.h * kDents[i][1];
        const render::SpriteRect dent{cx - size * 0.5f,
                                      cy - size * 0.5f, size, size};
        if (bgfx::isValid(softTexture)) {
            batch.DrawSprite(softTexture, dent, 0.0f, 0.0f, 1.0f, 1.0f,
                             withAlpha(0x182430u, baseAlpha * 0.70f));
            batch.DrawSprite(softTexture,
                             {cx - size * 0.28f, cy - size * 0.28f,
                              size * 0.56f, size * 0.56f},
                             0.0f, 0.0f, 1.0f, 1.0f,
                             withAlpha(0x2080FFu, baseAlpha * 0.55f));
        } else {
            batch.DrawRectFill(dent.x, dent.y, dent.w, dent.h,
                               withAlpha(0x182430u, baseAlpha * 0.55f));
        }
    }
    for (int i = 0; i < markCount; ++i) {
        const ScratchMark& mark = kScratches[i];
        const glm::vec2 p0{rect.x + rect.w * mark.x0,
                           rect.y + rect.h * mark.y0};
        const glm::vec2 p1{rect.x + rect.w * mark.x1,
                           rect.y + rect.h * mark.y1};
        const float thickness = i % 3 == 0 ? 2.8f : 1.9f;
        batch.DrawLine(p0, p1, thickness + 1.1f,
                       withAlpha(0x182430u, baseAlpha * 0.80f));
        batch.DrawLine(p0, p1, thickness,
                       withAlpha(0x2080FFu, baseAlpha));
    }
}

bool WorldDemoState::ToggleVehicle() {
    if (m_networkMode) {
        if (m_buildMode || m_ctx.ecs == nullptr || m_player == entt::null) {
            return false;
        }
        net::Command command =
            MakePlayerCommand(m_riding ? net::CommandType::ExitVehicle
                                       : net::CommandType::EnterVehicle);
        if (!m_riding) {
            const ecs::Entity target =
                FindNearestVehicle(m_vehicleTuning.enterRange);
            if (target == entt::null || !m_ctx.ecs->Alive(target)) {
                return false;
            }
            const std::uint64_t vehicleId = m_ctx.ecs->IdOf(target).id;
            command.payload.entityId = vehicleId;
            const ReplicatedActor* actor =
                m_ctx.networkClient != nullptr
                    ? m_ctx.networkClient->Replication().Find(vehicleId)
                    : nullptr;
            if (actor != nullptr && actor->hasVehicle &&
                actor->vehicleDriverStableId != 0 &&
                actor->vehicleDriverStableId !=
                    m_ctx.networkClient->PlayerStableId()) {
                command.type = net::CommandType::EnterVehiclePassenger;
            }
        }
        std::string error;
        if (!SubmitPlayerCommand(command)) {
            SetNotice(error.empty() ? "载具命令发送失??" : error,
                      kNoticeFailColor, 1.8f);
        }
        return true;
    }
    if (m_buildMode || m_vehicle == entt::null) {
        return false;
    }
    float distance = 0.0f;
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        if (const auto* playerPosition =
                m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
            playerPosition != nullptr) {
            const float dx = m_vehicleMotion.x - playerPosition->x;
            const float dy = m_vehicleMotion.y - playerPosition->y;
            distance = std::sqrt(dx * dx + dy * dy);
        }
    }
    ZL_INFO("vehicle", "切换载具：驾驶={} 距离={:.2f}", m_driving, distance);
    if (m_driving) {
        ExitVehicle();
        return true;
    }
    if (!VehicleNearPlayer(m_vehicleTuning.enterRange)) {
        return false;
    }
    EnterVehicle();
    return m_driving;
}

void WorldDemoState::EnterVehicle() {
    if (m_ctx.ecs == nullptr || m_player == entt::null || m_vehicle == entt::null) {
        return;
    }
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->CloseInventory();
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* vehiclePosition = registry.try_get<ecs::Position>(m_vehicle);
    if (vehiclePosition == nullptr) {
        return;
    }
    m_vehicleMotion.x = vehiclePosition->x;
    m_vehicleMotion.y = vehiclePosition->y;
    m_vehicleMotion.z = vehiclePosition->z;
    m_vehicleMotion.vx = 0.0f;
    m_vehicleMotion.vy = 0.0f;
    m_playerSpriteBackup.reset();
    if (const auto* sprite = registry.try_get<ecs::SpriteRef>(m_player);
        sprite != nullptr) {
        m_playerSpriteBackup = *sprite;
        registry.remove<ecs::SpriteRef>(m_player);
    }
    m_driving = true;
    m_blockInputDown = false;
    if (auto* stance = registry.try_get<ecs::CombatStance>(m_player);
        stance != nullptr) {
        stance->blocking = false;
        stance->blockElapsed = 0.0f;
        stance->dodgeLeft = 0.0f;
        stance->dodgeInvulnLeft = 0.0f;
        stance->dodgeDirX = 0.0f;
        stance->dodgeDirY = 0.0f;
    }
    registry.emplace_or_replace<ecs::VehicleOccupant>(
        m_player, ecs::VehicleOccupant{m_vehicleTuning.driverDamageReduction});
    m_vehicleFuelWarned = m_vehicleMotion.fuel <= 0.0f;
    m_vehicleLowConditionWarned = false;
    m_vehicleNoiseCooldown = 0.0f;
    m_vehicleRamWearCooldown = 0.0f;
    m_vehicleBodyCollisionCooldown = 0.0f;
    m_vehicleHornCooldown = 0.0f;
    m_vehicleZombieAttackCooldown = 0.0f;
    m_vehicleRamCooldowns.clear();
    m_localParkedVehicleMotions.clear();
    m_vehicleSurface =
        VehicleSurfaceAt(m_vehicleMotion.x, m_vehicleMotion.y, m_vehicleMotion.z);
    m_lastVehicleSurface = VehicleSurface::Paved;
    m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
    m_moveCommandValid = false;
    m_followSnapped = false;
    SyncPlayerToVehicle();
    SetNotice("已上车：WASD 驾驶 · Enter 下车", kNoticeOkColor, 2.0f);
    ZL_INFO("vehicle",
            "上车：stableId={}，燃??{:.0f}%，驾驶员防护 {:.0f}%",
            m_ctx.ecs->IdOf(m_vehicle).id, m_vehicleMotion.fuel * 100.0f,
            m_vehicleTuning.driverDamageReduction * 100.0f);
}

void WorldDemoState::ExitVehicle() {
    if (!m_driving) {
        return;
    }
    float exitX = 0.0f;
    float exitY = 0.0f;
    if (!FindVehicleExitCell(&exitX, &exitY)) {
        SetNotice("无法下车：周围没有空地", kNoticeFailColor, 2.0f);
        return;
    }
    m_vehicleMotion.vx = 0.0f;
    m_vehicleMotion.vy = 0.0f;
    m_vehicleNoiseCooldown = 0.0f;
    m_vehicleRamWearCooldown = 0.0f;
    m_vehicleBodyCollisionCooldown = 0.0f;
    m_vehicleHornCooldown = 0.0f;
    m_vehicleZombieAttackCooldown = 0.0f;
    if (m_ctx.ecs != nullptr && m_player != entt::null &&
        m_ctx.ecs->Alive(m_player)) {
        entt::registry& registry = m_ctx.ecs->Registry();
        if (auto* playerPosition = registry.try_get<ecs::Position>(m_player);
            playerPosition != nullptr) {
            playerPosition->x = exitX;
            playerPosition->y = exitY;
            playerPosition->z = 0;
        }
        if (auto* velocity = registry.try_get<ecs::Velocity>(m_player);
            velocity != nullptr) {
            velocity->x = 0.0f;
            velocity->y = 0.0f;
        }
        if (auto* input = registry.try_get<ecs::MoveInput>(m_player); input != nullptr) {
            input->x = 0.0f;
            input->y = 0.0f;
        }
        if (auto* intent = registry.try_get<ecs::MoveIntent>(m_player);
            intent != nullptr) {
            intent->x = 0.0f;
            intent->y = 0.0f;
        }
        if (auto* request = registry.try_get<ecs::PathRequest>(m_player);
            request != nullptr) {
            request->newRequest = false;
            request->queryId = 0;
            request->state = ecs::PathStatus::None;
        }
        if (auto* follow = registry.try_get<ecs::PathFollow>(m_player);
            follow != nullptr) {
            follow->waypoints.clear();
            follow->index = 0;
            follow->status = ecs::PathStatus::None;
            follow->stuckTimer = 0.0f;
        }
        if (m_playerSpriteBackup.has_value()) {
            registry.emplace_or_replace<ecs::SpriteRef>(m_player,
                                                        *m_playerSpriteBackup);
        }
        if (m_ctx.physics != nullptr) {
            if (auto* rigid = registry.try_get<ecs::RigidBody>(m_player);
                rigid != nullptr && rigid->body != nullptr) {
                m_ctx.physics->TeleportBody(rigid->body, exitX, exitY, 0);
            }
        }
    }
    m_driving = false;
    if (m_ctx.ecs != nullptr && m_player != entt::null &&
        m_ctx.ecs->Alive(m_player)) {
        m_ctx.ecs->Registry().remove<ecs::VehicleOccupant>(m_player);
    }
    StopVehicleAudio();
    m_playerSpriteBackup.reset();
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->CloseInventory();
    }
    m_lastMoveCommand = glm::vec2(0.0f, 0.0f);
    m_moveCommandValid = false;
    m_followSnapped = false;
    SetNotice("已下??", kNoticeInfoColor, 1.2f);
    ZL_INFO("vehicle",
            "下车：stableId={}，格 ({:.1f}, {:.1f})，车??({:.1f}, {:.1f})??"
            "燃料 {:.1f}%",
            m_ctx.ecs != nullptr ? m_ctx.ecs->IdOf(m_vehicle).id : 0, exitX,
            exitY, m_vehicleMotion.x, m_vehicleMotion.y,
            m_vehicleMotion.fuel * 100.0f);
}

void WorldDemoState::SyncPlayerToVehicle() {
    if (m_ctx.ecs == nullptr || m_player == entt::null || m_vehicle == entt::null) {
        return;
    }
    if (!m_ctx.ecs->Alive(m_player) || !m_ctx.ecs->Alive(m_vehicle)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    position->x = m_vehicleMotion.x;
    position->y = m_vehicleMotion.y;
    position->z = m_vehicleMotion.z;
    if (auto* velocity = registry.try_get<ecs::Velocity>(m_player); velocity != nullptr) {
        velocity->x = 0.0f;
        velocity->y = 0.0f;
    }
    if (auto* input = registry.try_get<ecs::MoveInput>(m_player); input != nullptr) {
        input->x = 0.0f;
        input->y = 0.0f;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(m_player); intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(m_player);
        request != nullptr) {
        request->newRequest = false;
        request->queryId = 0;
        request->state = ecs::PathStatus::None;
    }
    if (auto* follow = registry.try_get<ecs::PathFollow>(m_player);
        follow != nullptr) {
        follow->waypoints.clear();
        follow->index = 0;
        follow->status = ecs::PathStatus::None;
        follow->stuckTimer = 0.0f;
    }
    if (m_ctx.physics != nullptr) {
        if (auto* rigid = registry.try_get<ecs::RigidBody>(m_player);
            rigid != nullptr && rigid->body != nullptr) {
            m_ctx.physics->TeleportBody(rigid->body, m_vehicleMotion.x,
                                        m_vehicleMotion.y, m_vehicleMotion.z);
        }
    }
}

bool WorldDemoState::FindVehicleExitCell(float* x, float* y) const {
    if (x == nullptr || y == nullptr || m_ctx.world == nullptr) {
        return false;
    }
    const int vehicleX = static_cast<int>(std::floor(m_vehicleMotion.x));
    const int vehicleY = static_cast<int>(std::floor(m_vehicleMotion.y));
    for (int radius = 1; radius <= 3; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int cellX = vehicleX + dx;
                const int cellY = vehicleY + dy;
                if (IsSquareBlocked(*m_ctx.world, cellX, cellY, 0)) {
                    continue;
                }
                *x = static_cast<float>(cellX) + 0.5f;
                *y = static_cast<float>(cellY) + 0.5f;
                return true;
            }
        }
    }
    return false;
}

ecs::Entity WorldDemoState::FindNearestVehicle(
    float range, float* outDistance) const {
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !std::isfinite(range) || range < 0.0f) {
        return entt::null;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* playerPosition = registry.try_get<ecs::Position>(m_player);
    if (playerPosition == nullptr) {
        return entt::null;
    }
    ecs::Entity best = entt::null;
    float bestDistanceSq = range * range;
    registry.view<ecs::Position, ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::Vehicle&) {
            if (!m_ctx.ecs->Alive(entity) || position.z != playerPosition->z) {
                return;
            }
            const float dx = position.x - playerPosition->x;
            const float dy = position.y - playerPosition->y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq <= bestDistanceSq) {
                best = entity;
                bestDistanceSq = distanceSq;
            }
        });
    if (best != entt::null && outDistance != nullptr) {
        *outDistance = std::sqrt(bestDistanceSq);
    }
    return best;
}

bool WorldDemoState::VehicleNearPlayer(float range) const {
    return FindNearestVehicle(range) != entt::null;
}

std::string WorldDemoState::VehicleHint() const {
    if (m_vehicle == entt::null) {
        return {};
    }
    const std::string massLabel =
        std::string(VehicleTypeName(m_vehicleMotion.type)) + " " +
        std::to_string(static_cast<int>(VehicleMassForType(
            m_vehicleMotion.type))) +
        "kg";
    const int fuelPercent = static_cast<int>(
        std::floor(std::clamp(m_vehicleMotion.fuel, 0.0f, 1.0f) * 100.0f));
    const int conditionPercent = static_cast<int>(
        std::floor(std::clamp(m_vehicleMotion.condition, 0.0f, 1.0f) * 100.0f));
    if (m_riding && !m_driving) {
        std::string text = "乘客中：";
        if (NetworkVehicleDriverSeatOpen()) {
            text += "F 接管驾驶 · ";
        }
        text += "E 使用 · R 装备 · 1-5 快捷??· Enter 下车 · I 背包/后备??· 质量 " +
                massLabel + " · 燃料 " +
                std::to_string(fuelPercent) + "% · 车况 " +
                std::to_string(conditionPercent) + "%";
        return text;
    }
    if (m_driving) {
        std::string text =
            "驾驶中：WASD 移动 · Enter 下车 · H 喇叭 · L 车灯" +
            std::string(m_vehicleMotion.headlights ? "开" : "关") +
            " · 质量 " + massLabel +
            " · 防护 " +
            std::to_string(static_cast<int>(std::lround(
                std::clamp(m_vehicleTuning.driverDamageReduction, 0.0f, 1.0f) *
                100.0f))) +
            "% · 燃料 " + std::to_string(fuelPercent) + "% · 车况 " +
            std::to_string(conditionPercent) + "% · 路面 " +
            std::string(VehicleSurfaceLabel(m_vehicleSurface));
        if (m_vehicleMotion.fuel <= 0.0f) {
            text += " · 燃料耗尽";
        }
        if (m_vehicleMotion.condition <= 0.0f) {
            text += " · 车辆损坏";
        } else if (m_vehicleMotion.condition <
                   kVehicleConditionFullPerformance) {
            text += " · 动力下降";
        }
        if (NetworkVehicleHasPassenger() &&
            VehicleSpeed(m_vehicleMotion) <=
                m_vehicleTuning.seatHandoffMaxSpeed) {
            text += " · F 让座";
        }
        return text;
    }
    if (m_vehicleMotion.condition <= 0.0f) {
        return "车辆损坏：按 E 维修（维修包） · I 后备箱";
    }
    std::string text =
        "皮卡??" + massLabel + "）：燃料 " + std::to_string(fuelPercent) +
        "% · 车况 " + std::to_string(conditionPercent) + "% · ";
    bool driverOccupied = false;
    bool passengerSeatsFull = false;
    if (m_networkMode && m_ctx.networkClient != nullptr &&
        m_vehicle != entt::null) {
        const ReplicatedActor* actor = m_ctx.networkClient->Replication().Find(
            m_ctx.ecs->IdOf(m_vehicle).id);
        driverOccupied =
            actor != nullptr && actor->hasVehicle &&
            actor->vehicleDriverStableId != 0;
        passengerSeatsFull =
            actor != nullptr && actor->hasVehicle &&
            actor->vehiclePassengerStableId != 0 &&
            actor->vehicleSecondPassengerStableId != 0;
    }
    if (driverOccupied && passengerSeatsFull) {
        text += "载具已满 · ";
    } else if (driverOccupied) {
        text += "Enter 乘坐 · ";
    } else {
        text += "Enter 上车 · ";
    }
    if (m_vehicleMotion.condition < 1.0f) {
        text += "E 维修";
    } else if (m_vehicleMotion.fuel < 1.0f) {
        text += "E 加油";
    } else {
        text += "E 维修/加油";
    }
    text += " · I 后备??";
    return text;
}

bool WorldDemoState::TryServiceVehicle() {
    if (m_networkMode) {
        if (m_buildMode || m_driving || m_ctx.ecs == nullptr ||
            m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
            return false;
        }
        const ecs::Entity vehicle =
            FindNearestVehicle(m_vehicleTuning.enterRange);
        if (vehicle == entt::null) {
            return false;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::ServiceVehicle);
        command.payload.entityId = m_ctx.ecs->IdOf(vehicle).id;
        std::string error;
        if (!SubmitPlayerCommand(command)) {
            SetNotice(error.empty() ? "维修/加油请求发送失??" : error,
                      kNoticeFailColor, 1.8f);
        }
        return true;
    }
    if (m_buildMode || m_driving || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr || m_player == entt::null ||
        m_vehicle == entt::null || !m_ctx.ecs->Alive(m_player) ||
        !m_ctx.ecs->Alive(m_vehicle) ||
        !VehicleNearPlayer(m_vehicleTuning.enterRange)) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* bag = registry.try_get<ecs::Container>(m_player);
    if (bag == nullptr) {
        return false;
    }
    const int repairItems = items::CountOf(*bag, m_vehicleRepairItem);
    const int fuelItems = items::CountOf(*bag, m_vehicleFuelItem);
    const VehicleServiceAction action =
        PlanVehicleService(m_vehicleMotion, repairItems, fuelItems);
    if (action == VehicleServiceAction::None) {
        return false;
    }

    const auto itemName = [this](const std::string& defId) {
        const items::ItemDef* def = m_ctx.items->Find(defId);
        return def != nullptr ? def->name : defId;
    };
    const auto consumeOne = [&bag](const std::string& defId) {
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
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) *
                                            100.0f));
    };

    if (action == VehicleServiceAction::MissingRepairKit) {
        SetNotice("缺少维修包", kNoticeFailColor, 2.0f);
        ZL_INFO("vehicle", "维修失败：缺少维修包");
        return true;
    }
    if (action == VehicleServiceAction::MissingFuelCan) {
        SetNotice("缺少燃料罐", kNoticeFailColor, 2.0f);
        ZL_INFO("vehicle", "加油失败：缺少燃料罐");
        return true;
    }
    if (action == VehicleServiceAction::Repair) {
        const int before = percent(m_vehicleMotion.condition);
        if (!consumeOne(m_vehicleRepairItem)) {
            SetNotice("维修失败：无法消耗维修包", kNoticeFailColor, 2.0f);
            ZL_WARN("vehicle", "维修失败：维修包扣除失败");
            return true;
        }
        (void)VehicleAddCondition(&m_vehicleMotion,
                                  m_vehicleTuning.repairAmount);
        SyncVehicleStateToEcs();
        const int after = percent(m_vehicleMotion.condition);
        SetNotice("已维修：车况 " + std::to_string(after) + "%",
                  kNoticeOkColor, 2.0f);
        ZL_INFO("vehicle", "维修：车??{}% ??{}%，消??{} x1", before, after,
                itemName(m_vehicleRepairItem));
        return true;
    }

    const int before = percent(m_vehicleMotion.fuel);
    if (!consumeOne(m_vehicleFuelItem)) {
        SetNotice("加油失败：无法消耗燃料罐", kNoticeFailColor, 2.0f);
        ZL_WARN("vehicle", "加油失败：燃料罐扣除失败");
        return true;
    }
    (void)VehicleAddFuel(&m_vehicleMotion, m_vehicleTuning.fuelPerCan);
    m_vehicleFuelWarned = m_vehicleMotion.fuel <= 0.0f;
    SyncVehicleStateToEcs();
    const int after = percent(m_vehicleMotion.fuel);
    SetNotice("已加油：燃料 " + std::to_string(after) + "%",
              kNoticeOkColor, 2.0f);
    ZL_INFO("vehicle", "加油：燃??{}% ??{}%，消??{} x1", before, after,
            itemName(m_vehicleFuelItem));
    return true;
}

void WorldDemoState::SetNotice(std::string text, uint32_t color, float seconds) {
    m_noticeText = std::move(text);
    m_noticeTone = color == kNoticeOkColor ? "ok"
                   : color == kNoticeFailColor ? "fail"
                                               : "info";
    m_noticeTimer = seconds;
    m_noticeDuration = seconds;
}

void WorldDemoState::RefreshRangedHud(ui::HudState& hud) {
    hud.rangedValid = false;
    hud.rangedName.clear();
    hud.rangedLoaded = 0;
    hud.rangedMagazine = 0;
    hud.rangedReserve = 0;
    hud.rangedReloading = false;
    hud.rangedReloadProgress = 0.0f;
    hud.rangedReloadRemaining = 0.0f;
    hud.rangedStatus.clear();
    hud.rangedTone = "info";

    const items::ItemDef* def = nullptr;
    ecs::ItemInstance* weapon = nullptr;
    if (m_ctx.ecs != nullptr) {
        (void)TryGetEquippedRangedWeapon(&def, &weapon);
    }
    if (m_networkMode && m_ctx.networkClient != nullptr &&
        m_ctx.networkClient->HasPlayerState()) {
        if (!m_networkRanged.valid) {
            return;
        }
        hud.rangedValid = true;
        hud.rangedName = def != nullptr ? def->name : "远程武器";
        hud.rangedLoaded = std::max(0, m_networkRanged.loaded);
        hud.rangedMagazine = std::max(1, m_networkRanged.magazine);
        hud.rangedReserve = std::max(0, m_networkRanged.reserve);
        hud.rangedReloading = m_networkRanged.reloading;
        if (hud.rangedReloading) {
            const float duration =
                std::max(0.01f, m_networkRanged.reloadDuration);
            const float elapsed =
                std::clamp(m_networkRanged.reloadElapsed, 0.0f, duration);
            hud.rangedReloadProgress = elapsed / duration;
            hud.rangedReloadRemaining = std::max(0.0f, duration - elapsed);
        }
    } else {
        if (def == nullptr || weapon == nullptr || !def->ranged.has_value()) {
            return;
        }
        hud.rangedValid = true;
        hud.rangedName = def->name;
        hud.rangedMagazine = std::max(1, def->ranged->magazine);
        hud.rangedLoaded =
            std::clamp(weapon->loadedAmmo, 0, hud.rangedMagazine);
        if (const auto* bag =
                m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
            bag != nullptr) {
            hud.rangedReserve =
                std::max(0, items::CountOf(*bag, def->ranged->ammoDefId));
        }
        hud.rangedReloading = m_rangedWeapon.reloading;
        if (hud.rangedReloading) {
            const float duration = std::max(0.01f, def->ranged->reloadSeconds);
            const float left =
                std::clamp(m_rangedWeapon.reloadLeft, 0.0f, duration);
            hud.rangedReloadProgress = (duration - left) / duration;
            hud.rangedReloadRemaining = left;
        }
    }
    hud.rangedStatus = ui::RangedStatusText(hud, &hud.rangedTone);
}

void WorldDemoState::RefreshHud() {
    if (m_ctx.ui == nullptr || m_ctx.gameTime == nullptr) {
        return;
    }
    if (m_runOutcome != RunOutcome::None) {
        return;  // 结算页已接管；避免旧 HUD 状态覆??闪烁
    }
    UpdatePoiTasks();
    UpdateMissions();
    UpdateObjective();
    if (m_runOutcome != RunOutcome::None) {
        return;
    }
    if (!m_ctx.contentNotice.empty()) {
        std::string notice = std::move(m_ctx.contentNotice);
        m_ctx.contentNotice.clear();
        ZL_INFO("content", "内容策略提示已显示：{}", notice);
        SetNotice(std::move(notice), kNoticeFailColor, 5.0f);
    }
    ui::HudState hud;
    const bool networkState =
        m_networkMode && m_ctx.networkClient != nullptr &&
        m_ctx.networkClient->HasPlayerState();
    hud.active = (m_ctx.ecs != nullptr && m_player != entt::null) || networkState;
    const auto& now = m_ctx.gameTime->Now();
    const int day =
        1 + static_cast<int>(m_ctx.gameTime->TotalGameMinutes() / (24.0 * 60.0));
    char timeText[16];
    std::snprintf(timeText, sizeof(timeText), "%02d:%02d", now.hour, now.minute);
    hud.day = "第" + std::to_string(day) + " 天";
    hud.time = timeText;
    hud.speed = core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed());
    hud.selectedSlot = std::clamp(m_hotbarSelected, 0, 4);
    for (ui::HudSlot& slot : hud.slots) {
        slot.label = "空";
    }
    RefreshRangedHud(hud);

    bool hasBleeding = false;
    bool inPain = false;
    bool foodPoisoned = false;
    bool staminaExhausted = false;
    bool staminaLow = false;
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        entt::registry& registry = m_ctx.ecs->Registry();
        if (const auto* health = registry.try_get<ecs::Health>(m_player);
            health != nullptr) {
            hud.playerValid = true;
            hud.health = health->current;
            hud.healthMax = health->max;
        }
        if (networkState && m_networkCombat.valid) {
            staminaLow = m_networkCombat.staminaMax > 0.0f &&
                         m_networkCombat.stamina <=
                             m_networkCombat.staminaMax * 0.30f;
        } else if (const auto* stamina =
                       registry.try_get<ecs::CombatStamina>(m_player);
                   stamina != nullptr) {
            hud.stamina = stamina->current;
            hud.staminaMax = stamina->max;
            staminaExhausted =
                std::isfinite(stamina->exhausted) && stamina->exhausted > 0.0f;
            staminaLow = stamina->max > 0.0f &&
                         std::isfinite(stamina->current) &&
                         stamina->current <= stamina->max * 0.30f;
        } else if (const auto* needs = registry.try_get<ecs::Needs>(m_player);
                   needs != nullptr) {
            hud.stamina = (1.0f - std::clamp(needs->fatigue, 0.0f, 1.0f)) * 100.0f;
            hud.staminaMax = 100.0f;
        }
        if (const auto* needs = registry.try_get<ecs::Needs>(m_player); needs != nullptr) {
            char status[112];
            const char* prefix =
                needs->warning >= 2 ? "危险 " : needs->warning == 1 ? "警告 " : "";
            std::snprintf(status, sizeof(status), "%s饥饿 %d%%  口渴 %d%%  疲劳 %d%%", prefix,
                          static_cast<int>(std::lround(
                              std::clamp(needs->hunger, 0.0f, 1.0f) * 100.0f)),
                          static_cast<int>(std::lround(
                              std::clamp(needs->thirst, 0.0f, 1.0f) * 100.0f)),
                          static_cast<int>(std::lround(
                              std::clamp(needs->fatigue, 0.0f, 1.0f) * 100.0f)));
            std::string statusText = status;
            int expiringFood = 0;
            if (m_ctx.items != nullptr) {
                if (const auto* bag = registry.try_get<ecs::Container>(m_player);
                    bag != nullptr) {
                    for (const ecs::ItemInstance& item : bag->items) {
                        const items::ItemDef* def = m_ctx.items->Find(item.defId);
                        if (def != nullptr && items::IsPerishable(*def) &&
                            item.condition <= m_foodWarningThreshold) {
                            ++expiringFood;
                        }
                    }
                }
            }
            if (expiringFood > 0) {
                statusText += "  食物将坏×" + std::to_string(expiringFood);
            }
            float foodNutrition = 0.0f;
            float foodPoisonRemaining = 0.0f;
            if (m_networkMode) {
                if (m_networkFood.valid) {
                    foodNutrition = m_networkFood.nutrition;
                    foodPoisonRemaining = m_networkFood.poisonRemaining;
                }
            } else if (const auto* food =
                           registry.try_get<ecs::FoodState>(m_player);
                       food != nullptr) {
                foodNutrition = food->nutrition;
                foodPoisonRemaining = food->poisonRemaining;
            }
            if (foodPoisonRemaining > 0.01f) {
                foodPoisoned = true;
                statusText += "  食物中毒 " +
                              std::to_string(static_cast<int>(
                                  std::ceil(foodPoisonRemaining))) +
                              "??";
            } else if (foodNutrition > 0.01f) {
                statusText += "  营养 " +
                              std::to_string(static_cast<int>(std::lround(
                                  std::clamp(foodNutrition, 0.0f, 1.0f) *
                                  100.0f))) +
                              "%";
            }
            if (staminaExhausted) {
                statusText += "  体力耗尽";
            } else if (staminaLow) {
                statusText += "  体力偏低";
            }
            hud.needsValid = true;
            hud.needsStatus = std::move(statusText);
            hud.needsTone =
                needs->warning >= 2 || foodPoisoned || staminaExhausted
                    ? "fail"
                    : needs->warning == 1 || expiringFood > 0 || staminaLow
                          ? "warn"
                          : "info";
        } else {
            hud.needsStatus = "状??--";
        }
        if (const auto* wounds = registry.try_get<ecs::Wounds>(m_player);
            wounds != nullptr && wounds->count > 0) {
            const int count = characters::WoundCount(*wounds);
            const float bleed = characters::CurrentBleedRate(*wounds);
            char status[128];
            if (bleed > 0.01f) {
                hasBleeding = true;
                std::snprintf(status, sizeof(status), "流血 %d 处伤口（%.1f HP/s）", count,
                              static_cast<double>(bleed));
                hud.woundTone = bleed >= 0.8f ? "fail" : "warn";
            } else {
                std::snprintf(status, sizeof(status), "伤口 %d 处（已止血）", count);
                hud.woundTone = "info";
            }
            hud.woundStatus = status;
        }
        if (const auto* pain = registry.try_get<ecs::Pain>(m_player);
            pain != nullptr && pain->level > 0.01f) {
            inPain = pain->level >= 0.3f;
            hud.painStatus = game::PainStatusText(*pain, &hud.painTone);
        }
        if (m_ctx.items != nullptr) {
            if (const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
                equipment != nullptr) {
                const game::PlayerArmor armor =
                    game::ResolvePlayerArmor(*equipment, *m_ctx.items);
                hud.armorStatus = game::ArmorStatusText(armor, &hud.armorTone);
            }
        }
        if (const auto* infection = registry.try_get<ecs::Infection>(m_player);
            infection != nullptr) {
            hud.infectionStatus =
                game::InfectionStatusText(*infection, &hud.infectionTone);
        }
        if (const auto* climate = registry.try_get<ecs::Climate>(m_player);
            climate != nullptr) {
            const game::WeatherState weather =
                m_ctx.simulation != nullptr ? m_ctx.simulation->CurrentWeather()
                                            : game::WeatherState{};
            hud.weatherStatus = game::WeatherStatusText(
                weather, *climate, &hud.weatherTone, &hud.weatherClass);
        }
        if (m_threatRemaining > 0.0f) {
            hud.threatStatus = m_threatStatus;
            hud.threatDirection = m_threatDirection;
            hud.threatRemaining = m_threatRemaining;
            hud.threatDuration = m_threatDuration;
        }
        if (const auto* container = registry.try_get<ecs::Container>(m_player);
            container != nullptr) {
            const std::size_t count = std::min<std::size_t>(container->items.size(), 5);
            for (std::size_t i = 0; i < count; ++i) {
                const ecs::ItemInstance& entry = container->items[i];
                const items::ItemDef* def = m_ctx.items != nullptr
                                                ? m_ctx.items->Find(entry.defId)
                                                : nullptr;
                hud.slots[i].label = def != nullptr ? def->name : entry.defId;
                hud.slots[i].count = entry.count;
            }
        }
        if (!m_networkMode) {
            RefreshObjectiveHud(hud);
            hud.siegeStatus = m_baseSiege.HudText();
            hud.siegeTone = m_baseSiege.HudTone();
            if (!hud.siegeStatus.empty()) {
                const std::string warning = SpecialWarningText();
                if (!warning.empty()) {
                    hud.siegeStatus += " | " + warning;
                }
            }
            hud.baseActive = m_baseHud.active;
            hud.baseStorageCount = m_baseHud.storageCount;
            hud.baseStorageWeight = m_baseHud.storageWeight;
            hud.baseStorageCapacity = m_baseHud.storageCapacity;
            hud.baseBeds = m_baseHud.beds;
            hud.baseCampfires = m_baseHud.campfires;
            hud.baseWorkbenches = m_baseHud.workbenches;
            hud.baseDefensesIntact = m_baseHud.intactDefenses;
            hud.baseDefensesDamaged = m_baseHud.damagedDefenses;
            hud.baseFarms = m_baseHud.farms;
            hud.baseFarmGrowing = m_baseHud.farmGrowing;
            hud.baseFarmMature = m_baseHud.farmMature;
            hud.baseFarmDiseased = m_baseHud.farmDiseased;
            hud.baseFarmThirsty = m_baseHud.farmThirsty;
            hud.baseFarmFertilized = m_baseHud.farmFertilized;
            hud.baseGenerators = m_baseHud.generators;
            hud.baseGeneratorsOnline = m_baseHud.generatorsOnline;
            hud.baseFridges = m_baseHud.fridges;
            hud.baseFridgesPowered = m_baseHud.fridgesPowered;
            hud.baseTraps = m_baseHud.traps;
            hud.baseTrapsArmed = m_baseHud.trapsArmed;
            hud.baseFloodlights = m_baseHud.floodlights;
            hud.baseFloodlightsPowered = m_baseHud.floodlightsPowered;
            hud.baseElectricFences = m_baseHud.electricFences;
            hud.baseElectricFencesPowered = m_baseHud.electricFencesPowered;
            hud.basePowerFuelMinutes = m_baseHud.powerFuelMinutes;
            hud.basePowerLoad = m_baseHud.powerLoad;
            hud.basePowerLowFuel = m_baseHud.powerLowFuel;
            hud.basePowerOutage = m_baseHud.powerOutage;
            hud.baseStatus = ui::BaseStatusText(hud, &hud.baseTone);
            RefreshPoiGuide(hud);
            RefreshMissionGuide(hud);
            RefreshEventGuide(hud);

            game::ActionHintState hint;
            hint.bleeding = hasBleeding;
            hint.inPain = inPain;
            hint.hasRangedWeapon = hud.rangedValid;
            hint.rangedEmpty = hud.rangedValid && hud.rangedLoaded <= 0;
            hint.rangedReloading = hud.rangedReloading;
            if (const auto* container = registry.try_get<ecs::Container>(m_player);
                container != nullptr && m_ctx.items != nullptr) {
                for (const ecs::ItemInstance& entry : container->items) {
                    const items::ItemDef* def = m_ctx.items->Find(entry.defId);
                    if (def == nullptr) {
                        continue;
                    }
                    hint.hasBandage = hint.hasBandage || def->use.bandage > 0.0f;
                    hint.hasPainkiller =
                        hint.hasPainkiller || def->use.painRelief > 0.0f;
                }
            }
            if (const auto* position = registry.try_get<ecs::Position>(m_player);
                position != nullptr) {
                const ecs::Entity containerEntity = game::FindNearestLootContainer(
                    *m_ctx.ecs, position->x, position->y, position->z,
                    m_inventoryRange);
                if (containerEntity != entt::null) {
                    const auto* loot =
                        registry.try_get<ecs::LootContainer>(containerEntity);
                    const items::LootTable* table =
                        loot != nullptr && m_ctx.lootTables != nullptr
                            ? m_ctx.lootTables->Find(loot->tableId)
                            : nullptr;
                    hint.nearbyContainer =
                        table != nullptr ? table->name : std::string("容器");
                } else {
                    const ecs::Entity groundEntity = game::FindNearestGroundItem(
                        *m_ctx.ecs, position->x, position->y, position->z,
                        m_inventoryRange);
                    if (groundEntity != entt::null) {
                        const auto* item =
                            registry.try_get<ecs::ItemInstance>(groundEntity);
                        const items::ItemDef* def =
                            item != nullptr && m_ctx.items != nullptr
                                ? m_ctx.items->Find(item->defId)
                                : nullptr;
                        hint.nearbyItem =
                            def != nullptr ? def->name : std::string("地面物品");
                    }
                }
                ecs::Entity animalCorpse = entt::null;
                if (const AnimalDef* animal =
                        FindNearestAnimalCorpse(m_inventoryRange,
                                                &animalCorpse);
                    animal != nullptr && animalCorpse != entt::null) {
                    hint.nearbyAnimalCorpse = AnimalCorpseName(*animal);
                    std::string toolName;
                    hint.animalCorpseMissingTool =
                        !HasAnimalHarvestTool(*animal, &toolName);
                    hint.animalCorpseToolName = std::move(toolName);
                    hint.animalCorpseSeconds = animal->corpseSearchSeconds;
                }
                if (!m_buildMode && m_sleepEnabled && !m_sleep.Active() &&
                    !m_sleepSelecting) {
                    PlayerBuildTarget bed;
                    if (FindNearbyBed(&bed)) {
                        hint.nearbyBed = bed.def->name;
                    }
                }
                if (!m_buildMode && m_ctx.simulation != nullptr &&
                    m_ctx.simulation->Fire() != nullptr) {
                    game::FireSystem* fire = m_ctx.simulation->Fire();
                    game::FireTarget target;
                    if (FindNearbyActiveFire(&target)) {
                        hint.nearbyFire =
                            target.material != nullptr ? target.material->name
                                                       : std::string("火焰");
                        hint.fireState = FirePhaseName(target.source.phase);
                        if (m_ctx.items != nullptr) {
                            const auto* container =
                                registry.try_get<ecs::Container>(m_player);
                            const game::FireExtinguishPlan plan =
                                container != nullptr
                                    ? game::PlanFireExtinguish(
                                          *container, *m_ctx.items,
                                          fire->Params())
                                    : game::FireExtinguishPlan{};
                            hint.fireExtinguishBareHands = plan.bareHands;
                            hint.fireExtinguishItem = plan.name;
                            hint.fireExtinguishChance = plan.chance;
                        }
                    } else {
                        int campfireX = 0;
                        int campfireY = 0;
                        if (FindNearbyCampfire(&campfireX, &campfireY)) {
                            hint.nearbyCampfire = "篝火";
                            if (const ecs::FireSource* source =
                                    fire->FindAt(*m_ctx.ecs, campfireX, campfireY,
                                                 position->z);
                                source != nullptr && FireIsActive(*source)) {
                                hint.campfireState =
                                    FirePhaseName(source->phase);
                            }
                        }
                    }
                }
                const ecs::Entity storage = FindNearbyStorage();
                if (storage != entt::null) {
                    const std::uint64_t storageOwner =
                        EntityOwnerStableId(storage);
                    hint.nearbyStorage =
                        StorageName(storage) +
                        StructureOwnerSuffix(storageOwner);
                    if (!CanEditLocalStructure(storageOwner)) {
                        hint.nearbyStorage += "（只读）";
                    }
                }
                if (m_ctx.forage != nullptr) {
                    if (const ForageNode* node =
                            NearestForageNode(m_forageInteractRange);
                        node != nullptr) {
                        if (const ForageZoneDef* zone =
                                m_ctx.forage->FindZone(node->zoneId);
                            zone != nullptr) {
                            hint.nearbyForage = zone->name;
                            hint.forageToolName = ForageToolName(*zone);
                            hint.forageMissingTool = !HasForageTool(*zone, nullptr);
                            hint.forageSeconds = ForageSearchSeconds(
                                *zone,
                                m_ctx.simulation != nullptr
                                    ? m_ctx.simulation->CurrentWeather()
                                    : WeatherState{},
                                PlayerProgression() != nullptr
                                    ? ecs::SkillLevel(
                                          *PlayerProgression(),
                                          ecs::SkillKind::Survival)
                                    : 0);
                            if (auto* forageState =
                                    registry.try_get<ecs::ForageState>(
                                        m_player);
                                forageState != nullptr) {
                                const double forageNow =
                                    m_ctx.gameTime->TotalGameMinutes();
                                hint.forageExhausted = !ForageNodeReady(
                                    *forageState, *node, forageNow);
                                hint.forageRespawnMinutes =
                                    ForageRespawnRemainingMinutes(
                                        *forageState, *node, forageNow);
                            }
                        }
                    }
                    if (const FishingSpot* spot =
                            NearestFishingSpot(m_fishingInteractRange);
                        spot != nullptr) {
                        if (const FishingSpotDef* def =
                                m_ctx.forage->FindSpot(spot->spotId);
                            def != nullptr) {
                            const WeatherState weather =
                                m_ctx.simulation != nullptr
                                    ? m_ctx.simulation->CurrentWeather()
                                    : WeatherState{};
                            const int survivalLevel =
                                PlayerProgression() != nullptr
                                    ? ecs::SkillLevel(
                                          *PlayerProgression(),
                                          ecs::SkillKind::Survival)
                                    : 0;
                            hint.nearbyFishing = def->name;
                            hint.fishingRodName =
                                FishingItemName(def->rodDefId);
                            hint.fishingBaitName =
                                FishingItemName(def->baitDefId);
                            hint.fishingMissingRod = !HasFishingRod(*def);
                            hint.fishingBaitCount = FishingBaitCount(*def);
                            hint.fishingMissingBait =
                                def->baitCost > 0 &&
                                hint.fishingBaitCount < def->baitCost;
                            hint.fishingSeconds = FishingWaitSeconds(
                                *def, weather, survivalLevel);
                        }
                    }
                }
            }
            if (const auto* position = registry.try_get<ecs::Position>(m_player);
                position != nullptr && !m_barricades.empty()) {
                float bestDistance = m_buildActionRange * m_buildActionRange;
                bool foundBarricade = false;
                const BuiltBarricade* nearest = nullptr;
                for (const auto& [key, barricade] : m_barricades) {
                    const int wx = BuildCellX(key);
                    const int wy = BuildCellY(key);
                    const float dx = position->x - (static_cast<float>(wx) + 0.5f);
                    const float dy = position->y - (static_cast<float>(wy) + 0.5f);
                    const float distance = dx * dx + dy * dy;
                    if (distance <= bestDistance) {
                        bestDistance = distance;
                        nearest = &barricade;
                        foundBarricade = true;
                    }
                }
                if (foundBarricade) {
                    hint.barricadeHp = nearest->hp;
                    hint.barricadeMaxHp = nearest->maxHp;
                    if (const BuildableDef* def =
                            BuildableForSprite(nearest->sprite);
                        def != nullptr) {
                        hint.structureName = def->name;
                    }
                }
            }
            PlayerBuildTarget repairTarget;
            const bool hasRepairTarget = FindNearestPlayerBuiltStructure(
                m_buildActionRange, true, true, &repairTarget);
            if (hasRepairTarget) {
                hint.barricadeHp = repairTarget.hp;
                hint.barricadeMaxHp = repairTarget.def->maxHp;
                hint.structureName = repairTarget.def->name +
                                     StructureOwnerSuffix(
                                         repairTarget.ownerStableId);
                const bool canEditRepair = CanEditLocalStructure(
                    repairTarget.ownerStableId);
                hint.structureReadOnly = !canEditRepair;
                hint.canRepairBarricade = canEditRepair;
                hint.repairWoodCost = m_repairWoodCost;
                hint.repairAmount = RepairAmountForPlayer();
            }
            PlayerBuildTarget doorTarget;
            if (FindNearbyDoor(&doorTarget)) {
                const bool canEditDoor = CanEditLocalStructure(
                    doorTarget.ownerStableId);
                hint.nearbyDoor = doorTarget.def->name +
                                  StructureOwnerSuffix(
                                      doorTarget.ownerStableId);
                hint.doorOpen = IsOpenDoorSprite(*doorTarget.def,
                                                 doorTarget.sprite);
                hint.barricadeHp = doorTarget.hp;
                hint.barricadeMaxHp = doorTarget.def->maxHp;
                hint.structureName = doorTarget.def->name +
                                     StructureOwnerSuffix(
                                         doorTarget.ownerStableId);
                hint.structureReadOnly = !canEditDoor;
            }
            PlayerBuildTarget intactTarget;
            if (hint.nearbyDoor.empty() && !hasRepairTarget &&
                FindNearestPlayerBuiltStructure(
                    m_buildActionRange, false, false, &intactTarget) &&
                intactTarget.def != nullptr && intactTarget.def->maxHp > 0.0f) {
                hint.barricadeHp = intactTarget.hp;
                hint.barricadeMaxHp = intactTarget.def->maxHp;
                hint.structureName = intactTarget.def->name +
                                     StructureOwnerSuffix(
                                         intactTarget.ownerStableId);
                hint.structureReadOnly =
                    !CanEditLocalStructure(intactTarget.ownerStableId);
            }
            const int damagedCount =
                CountDamagedPlayerStructuresNear(&hint.repairAllWood);
            if (!m_networkMode && damagedCount > 0) {
                hint.canRepairAll = true;
                hint.repairAllCount = damagedCount;
            }
            hud.actionHint = game::ActionHintText(hint, &hud.actionTone);
            if (!m_buildMode && !hint.bleeding && !hint.inPain &&
                hint.nearbyForage.empty() &&
                hint.nearbyAnimalCorpse.empty()) {
                const std::string homestead = HomesteadHint();
                if (!homestead.empty()) {
                    hud.actionHint = homestead;
                    hud.actionTone = "info";
                }
            }
            if (m_buildMode) {
                const BuildableDef* def = SelectedBuildable();
                if (def != nullptr) {
                    std::string reason;
                    const bool canPlace =
                        CanPlaceBuild(*def, m_hoverX, m_hoverY, &reason);
                    int woodCount = 0;
                    if (const auto* container =
                            registry.try_get<ecs::Container>(m_player);
                        container != nullptr) {
                        woodCount = items::CountOf(*container, m_buildWoodItem);
                    }
                    hud.actionHint = BuildMenuText() + " · 木材 " +
                                     std::to_string(woodCount) + "/" +
                                     std::to_string(def->woodCost) +
                                     " · 左键放置 · 滚轮切换";
                    if (BuildableSupportsVertical(*def)) {
                        hud.actionHint += " · R 旋转（建造优先，不换弹）";
                    }
                    hud.actionHint += " · B/ESC 取消";
                    if (!canPlace && !reason.empty()) {
                        hud.actionHint += " · " + reason;
                    }
                    PlayerBuildTarget demolishTarget;
                    const bool hasDemolish =
                        FindNearestPlayerBuiltStructure(
                            m_buildActionRange, false, false, &demolishTarget) ||
                        FindNearestPlayerBuiltStructure(
                            m_buildActionRange, false, false, &demolishTarget,
                            {}, true);
                    if (hasDemolish) {
                        const std::uint64_t owner =
                            demolishTarget.ownerStableId;
                        const bool canDemolish =
                            CanEditLocalStructure(owner);
                        const std::uint64_t token =
                            demolishTarget.structureId != 0
                                ? demolishTarget.structureId
                                : ecs::HashName(demolishTarget.sprite);
                        const bool pending = PendingDemolishMatches(
                            PendingDemolishKind::Map, demolishTarget.wx,
                            demolishTarget.wy,
                            static_cast<int>(demolishTarget.layer), token);
                        const std::string suffix =
                            StructureOwnerSuffix(owner);
                        if (!canDemolish) {
                            hud.actionHint +=
                                " · 无法拆除" + suffix + "（仅所有者）";
                        } else if (pending) {
                            hud.actionHint +=
                                " · X 再次确认拆除 " + demolishTarget.def->name +
                                "?? 秒内??" + suffix;
                        } else {
                            const int refund = BuildWoodRefund(
                                demolishTarget.def->woodCost,
                                m_demolishRefundRatio);
                            hud.actionHint +=
                                " · X 拆除（返??" + std::to_string(refund) +
                                " 木材??" + suffix;
                        }
                    } else if (const ecs::Entity storage = FindNearbyStorage(false);
                               storage != entt::null) {
                        const BuildableDef* storageDef = StorageBuildable(storage);
                        if (storageDef != nullptr) {
                            const std::uint64_t owner =
                                EntityOwnerStableId(storage);
                            const bool canDemolish =
                                CanEditLocalStructure(owner);
                            const std::uint64_t token =
                                m_ctx.ecs->IdOf(storage).id;
                            const auto* storagePosition =
                                m_ctx.ecs->Registry().try_get<ecs::Position>(
                                    storage);
                            const int storageWx =
                                storagePosition != nullptr
                                    ? static_cast<int>(
                                          std::floor(storagePosition->x))
                                    : 0;
                            const int storageWy =
                                storagePosition != nullptr
                                    ? static_cast<int>(
                                          std::floor(storagePosition->y))
                                    : 0;
                            const bool pending = PendingDemolishMatches(
                                PendingDemolishKind::Entity, storageWx,
                                storageWy, 0, token);
                            const std::string suffix =
                                StructureOwnerSuffix(owner);
                            if (!canDemolish) {
                                hud.actionHint +=
                                    " · 无法拆除仓库" + suffix +
                                    "（仅所有者）";
                            } else if (pending) {
                                hud.actionHint +=
                                    " · X 再次确认拆除仓库（3 秒内）" + suffix;
                            } else {
                                const int refund = BuildWoodRefund(
                                    storageDef->woodCost,
                                    m_demolishRefundRatio);
                                hud.actionHint +=
                                    " · X 拆除仓库（返还 " +
                                    std::to_string(refund) + " 木材??" +
                                    suffix;
                            }
                        }
                    } else if (const HomesteadTarget facility =
                                   FindNearestHomesteadTarget();
                               facility.kind == HomesteadTargetKind::Farm ||
                               facility.kind == HomesteadTargetKind::Generator ||
                               facility.kind == HomesteadTargetKind::Floodlight ||
                               facility.kind == HomesteadTargetKind::Trap) {
                        const std::uint64_t owner =
                            EntityOwnerStableId(facility.entity);
                        const std::uint64_t token =
                            m_ctx.ecs->IdOf(facility.entity).id;
                        const auto* facilityPosition =
                            m_ctx.ecs->Registry().try_get<ecs::Position>(
                                facility.entity);
                        if (facilityPosition != nullptr) {
                            const int wx = static_cast<int>(
                                std::floor(facilityPosition->x));
                            const int wy = static_cast<int>(
                                std::floor(facilityPosition->y));
                            const bool pending = PendingDemolishMatches(
                                PendingDemolishKind::Entity, wx, wy, 0,
                                token);
                            const std::string suffix =
                                StructureOwnerSuffix(owner);
                            std::string label =
                                facility.kind == HomesteadTargetKind::Farm
                                    ? "农田"
                                    : facility.kind ==
                                              HomesteadTargetKind::Generator
                                          ? "发电??"
                                          : facility.kind ==
                                                    HomesteadTargetKind::Floodlight
                                                ? "泛光??"
                                                : "陷阱";
                            if (!CanEditLocalStructure(owner)) {
                                hud.actionHint +=
                                    " · 无法拆除" + label + suffix +
                                    "（仅所有者）";
                            } else if (pending) {
                                hud.actionHint +=
                                    " · X 再次确认拆除 " + label +
                                    "?? 秒内??" + suffix;
                            } else {
                                hud.actionHint +=
                                    " · X 拆除" + label + suffix;
                            }
                        }
                    }
                    hud.actionTone = canPlace ? "info" : "warn";
                }
            }
            if (m_forageAction.active) {
                const float progress =
                    m_forageAction.duration > 0.0f
                        ? std::clamp(m_forageAction.elapsed /
                                         m_forageAction.duration,
                                     0.0f, 1.0f)
                        : 1.0f;
                hud.actionHint = "觅食中：" + m_forageAction.zoneName + " " +
                                 std::to_string(static_cast<int>(
                                     std::lround(progress * 100.0f))) +
                                 "% · E 取消";
                hud.actionTone = "info";
            }
            if (m_fishingAction.active) {
                if (m_fishingAction.biting) {
                    const float left = std::max(
                        0.0f, m_fishingAction.biteDuration -
                                  m_fishingAction.biteElapsed);
                    char text[128];
                    std::snprintf(text, sizeof(text),
                                  "咬钩！按 E 收杆（%s，%.1f 秒）",
                                  m_fishingAction.spotName.c_str(),
                                  static_cast<double>(left));
                    hud.actionHint = text;
                    hud.actionTone = "warn";
                } else {
                    const float progress =
                        m_fishingAction.waitDuration > 0.0f
                            ? std::clamp(m_fishingAction.elapsed /
                                             m_fishingAction.waitDuration,
                                         0.0f, 1.0f)
                            : 1.0f;
                    hud.actionHint =
                        "钓鱼中：" + m_fishingAction.spotName + " " +
                        std::to_string(static_cast<int>(
                            std::lround(progress * 100.0f))) +
                        "% · E 取消";
                    hud.actionTone = "info";
                }
            }
            if (m_animalHarvestAction.active) {
                const float progress =
                    m_animalHarvestAction.duration > 0.0f
                        ? std::clamp(m_animalHarvestAction.elapsed /
                                         m_animalHarvestAction.duration,
                                     0.0f, 1.0f)
                        : 1.0f;
                hud.actionHint = "剥取中：" + m_animalHarvestAction.corpseName +
                                 " " +
                                 std::to_string(static_cast<int>(
                                     std::lround(progress * 100.0f))) +
                                 "% · E 取消";
                hud.actionTone = "info";
            }
            hud.forageValid =
                m_forageAction.active || !hint.nearbyForage.empty();
            if (hud.forageValid) {
                hud.forageName = m_forageAction.active
                                     ? m_forageAction.zoneName
                                     : hint.nearbyForage;
                hud.forageToolName = hint.forageToolName;
                hud.forageMissingTool =
                    !m_forageAction.active && hint.forageMissingTool;
                hud.forageExhausted =
                    !m_forageAction.active && hint.forageExhausted;
                hud.forageRespawnMinutes = hint.forageRespawnMinutes;
                hud.forageSeconds = m_forageAction.active
                                        ? m_forageAction.duration
                                        : hint.forageSeconds;
                hud.forageSearching = m_forageAction.active;
                hud.forageProgress =
                    m_forageAction.active && m_forageAction.duration > 0.0f
                        ? std::clamp(m_forageAction.elapsed /
                                         m_forageAction.duration,
                                     0.0f, 1.0f)
                        : 0.0f;
            }
            hud.forageStatus =
                ui::ForageStatusText(hud, &hud.forageTone);
            hud.fishingValid =
                m_fishingAction.active || !hint.nearbyFishing.empty();
            if (hud.fishingValid) {
                hud.fishingName = m_fishingAction.active
                                      ? m_fishingAction.spotName
                                      : hint.nearbyFishing;
                hud.fishingRodName = hint.fishingRodName;
                hud.fishingBaitName = hint.fishingBaitName;
                hud.fishingMissingRod =
                    !m_fishingAction.active && hint.fishingMissingRod;
                hud.fishingMissingBait =
                    !m_fishingAction.active && hint.fishingMissingBait;
                hud.fishingSeconds = m_fishingAction.active
                                         ? m_fishingAction.waitDuration
                                         : hint.fishingSeconds;
                hud.fishingWaiting =
                    m_fishingAction.active && !m_fishingAction.biting;
                hud.fishingProgress =
                    hud.fishingWaiting && m_fishingAction.waitDuration > 0.0f
                        ? std::clamp(m_fishingAction.elapsed /
                                         m_fishingAction.waitDuration,
                                     0.0f, 1.0f)
                        : 0.0f;
                hud.fishingBiting =
                    m_fishingAction.active && m_fishingAction.biting;
                hud.fishingBiteRemaining =
                    hud.fishingBiting
                        ? std::max(0.0f, m_fishingAction.biteDuration -
                                             m_fishingAction.biteElapsed)
                        : 0.0f;
            }
            hud.fishingStatus =
                ui::FishingStatusText(hud, &hud.fishingTone);
        }
    }

    hud.siegeReportOpen = m_siegeReportOpen;
    hud.siegeReportTitle = m_siegeReportTitle;
    hud.siegeReportRating = m_siegeReportRating;
    hud.siegeReportTone = m_siegeReportTone;
    hud.siegeReportHint = m_siegeReportHint;
    hud.siegeReportLines = m_siegeReportLines;

    if (m_networkMode) {
        hud.siegeStatus = m_networkSiegeStatus;
        hud.siegeTone = m_networkSiegeTone;
        hud.baseActive = m_baseHud.active;
        hud.baseStorageCount = m_baseHud.storageCount;
        hud.baseStorageWeight = m_baseHud.storageWeight;
        hud.baseStorageCapacity = m_baseHud.storageCapacity;
        hud.baseBeds = m_baseHud.beds;
        hud.baseCampfires = m_baseHud.campfires;
        hud.baseWorkbenches = m_baseHud.workbenches;
        hud.baseDefensesIntact = m_baseHud.intactDefenses;
        hud.baseDefensesDamaged = m_baseHud.damagedDefenses;
        hud.baseFarms = m_baseHud.farms;
        hud.baseFarmGrowing = m_baseHud.farmGrowing;
        hud.baseFarmMature = m_baseHud.farmMature;
        hud.baseFarmDiseased = m_baseHud.farmDiseased;
        hud.baseFarmThirsty = m_baseHud.farmThirsty;
        hud.baseFarmFertilized = m_baseHud.farmFertilized;
        hud.baseGenerators = m_baseHud.generators;
        hud.baseGeneratorsOnline = m_baseHud.generatorsOnline;
        hud.baseFridges = m_baseHud.fridges;
        hud.baseFridgesPowered = m_baseHud.fridgesPowered;
        hud.baseTraps = m_baseHud.traps;
        hud.baseTrapsArmed = m_baseHud.trapsArmed;
        hud.baseFloodlights = m_baseHud.floodlights;
        hud.baseFloodlightsPowered = m_baseHud.floodlightsPowered;
        hud.baseElectricFences = m_baseHud.electricFences;
        hud.baseElectricFencesPowered = m_baseHud.electricFencesPowered;
        hud.basePowerFuelMinutes = m_networkPower.fuelMinutes;
        hud.basePowerLoad = m_networkPower.load;
        hud.basePowerLowFuel = m_networkPower.lowFuel;
        hud.basePowerOutage = m_networkPower.outage;
        hud.baseStatus = ui::BaseStatusText(hud, &hud.baseTone);
        RefreshObjectiveHud(hud);
        RefreshPoiGuide(hud);
        RefreshMissionGuide(hud);
        RefreshEventGuide(hud);
    }

    if (networkState && m_networkCombat.valid) {
        hud.stamina = std::max(0.0f, m_networkCombat.stamina);
        hud.staminaMax = std::max(1.0f, m_networkCombat.staminaMax);
        if (m_networkCombat.stance == net::kPlayerCombatStanceDodge ||
            m_networkCombat.dodgeInvulnLeft > 0.0f) {
            if (m_networkCombat.dodgeInvulnLeft > 0.0f) {
                char text[64];
                std::snprintf(text, sizeof(text), "闪避 · 无敌 %.1fs",
                              static_cast<double>(
                                  m_networkCombat.dodgeInvulnLeft));
                hud.combatStatus = text;
            } else {
                hud.combatStatus = "闪避";
            }
            hud.combatTone = "warn";
        } else if (m_networkCombat.guardBreakLeft > 0.0f) {
            char text[64];
            std::snprintf(text, sizeof(text), "破防恢复 %.1fs",
                          static_cast<double>(m_networkCombat.guardBreakLeft));
            hud.combatStatus = text;
            hud.combatTone = "fail";
        } else if (m_networkCombat.stance ==
                   net::kPlayerCombatStanceBlocking) {
            if (m_networkCombat.parryWindowLeft > 0.0f) {
                char text[64];
                std::snprintf(text, sizeof(text), "格挡 · 弹反窗口 %.1fs",
                              static_cast<double>(
                                  m_networkCombat.parryWindowLeft));
                hud.combatStatus = text;
            } else {
                hud.combatStatus = "格挡";
            }
            hud.combatTone = "ok";
        }
    }
    if (networkState && m_player == entt::null) {
        hud.playerValid = true;
        hud.health = m_networkHealth;
        hud.healthMax = m_networkHealthMax;
        if (m_networkCombat.valid) {
            hud.stamina = std::max(0.0f, m_networkCombat.stamina);
            hud.staminaMax = std::max(1.0f, m_networkCombat.staminaMax);
        } else {
            hud.stamina = 100.0f;
            hud.staminaMax = 100.0f;
        }
        hud.needsStatus = "状??--";
        hud.infectionStatus =
            game::InfectionStatusText(m_networkInfection, &hud.infectionTone);
        const game::WeatherState weather =
            m_ctx.simulation != nullptr ? m_ctx.simulation->CurrentWeather()
                                        : game::WeatherState{};
        hud.weatherStatus = game::WeatherStatusText(
            weather, m_networkClimate, &hud.weatherTone, &hud.weatherClass);
    }
    if (hud.actionHint.empty() && hud.playerValid) {
        game::ActionHintState hint;
        hint.hasRangedWeapon = hud.rangedValid;
        hint.rangedEmpty = hud.rangedValid && hud.rangedLoaded <= 0;
        hint.rangedReloading = hud.rangedReloading;
        hud.actionHint = game::ActionHintText(hint, &hud.actionTone);
    }
    if (const ecs::Progression* progression = PlayerProgression();
        progression != nullptr && m_progressionConfig.enabled) {
        hud.progressionValid = true;
        hud.progressionLevel = progression->level;
        hud.progressionXp = progression->xp;
        hud.progressionXpNext =
            game::CharacterXpToNext(*progression, m_progressionConfig);
        hud.progressionPerkPoints = progression->perkPoints;
        for (std::size_t i = 0; i < hud.progressionSkills.size(); ++i) {
            hud.progressionSkills[i] = game::ProgressionSkillLine(
                *progression, m_progressionConfig, static_cast<ecs::SkillKind>(i));
        }
        hud.progressionPerks =
            game::ProgressionPerkSummary(*progression, m_progressionConfig);
        const std::array<std::string, 3> choices = CurrentPerkChoices();
        hud.progressionChoiceCount = 0;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (choices[i].empty()) {
                continue;
            }
            const game::PerkRules* perk =
                game::FindPerk(m_progressionConfig, choices[i]);
            if (perk != nullptr) {
                hud.progressionChoices[i] =
                    game::PerkChoiceLine(*perk, static_cast<int>(i));
                ++hud.progressionChoiceCount;
            }
        }
        if (progression->perkPoints > 0) {
            hud.perkPrompt = "可分??Perk ×" +
                             std::to_string(progression->perkPoints) +
                             " · 按 1/2/3 选择 · C 查看角色";
        }
    }
    hud.characterOpen = m_characterOpen;
    if (m_sleep.Active() || m_sleepSelecting) {
        hud.actionHint = SleepStatusText();
        hud.actionTone = "info";
    } else if (networkState && m_networkSleep.phase != 0) {
        hud.actionHint = m_networkSleep.status;
        hud.actionTone = m_networkSleep.tone == 2 ? "fail"
                         : m_networkSleep.tone == 1 ? "ok"
                                                     : "info";
    }
    if (m_riding || VehicleNearPlayer(m_vehicleTuning.enterRange)) {
        hud.actionHint = VehicleHint();
        hud.actionTone =
            m_vehicleMotion.condition <= 0.0f ? "fail"
            : (m_vehicleMotion.condition < 0.35f ||
               m_vehicleMotion.fuel <= 0.0f)
                ? "warn"
                : "info";
    }
    SyncDialogueHud(hud);
    if (networkState && !m_networkPlayerAlive && m_noticeTimer <= 0.0f) {
        hud.notice = "等待复活";
        hud.noticeTone = "fail";
        hud.noticeRemaining = std::max(0.1f, m_networkRespawnSeconds);
        hud.noticeDuration = std::max(0.1f, m_networkRespawnSeconds);
    }

    if (m_noticeTimer > 0.0f && !m_noticeText.empty()) {
        hud.notice = m_noticeText;
        hud.noticeTone = m_noticeTone;
        hud.noticeRemaining = m_noticeTimer;
        hud.noticeDuration = std::max(m_noticeDuration, 0.1f);
    }
    if (ui::HudNoticeSuppressed(
            m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen(), m_tradeOpen,
            m_dialogueOpen)) {
        hud.notice.clear();
        hud.noticeRemaining = 0.0f;
    }
    const std::size_t modFieldCount =
        std::min(m_ctx.modHudFields.size(), ui::kMaxModHudFields);
    for (std::size_t i = 0; i < modFieldCount; ++i) {
        hud.modFields[i] = m_ctx.modHudFields[i];
    }
    hud.modFieldCount = static_cast<int>(modFieldCount);
    m_ctx.hudState = hud;
    m_ctx.ui->SetHudState(hud);
}

void WorldDemoState::RefreshInventory() {
    if (m_ctx.ui == nullptr) {
        return;
    }
    ui::InventoryState state;
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        m_ctx.ui->SetInventoryState(state);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr) {
        m_ctx.ui->SetInventoryState(state);
        return;
    }


    state.playerValid = true;
    state.capacity = container->capacityKg;
    state.weight = items::TotalWeight(*container, *m_ctx.items);
    state.bagStacks = static_cast<int>(container->items.size());

    const int repairKits =
        m_repairKitDefId.empty() || m_repairKitCount <= 0
            ? 0
            : items::CountOf(*container, m_repairKitDefId);
    const int bagCount = std::min<int>(static_cast<int>(container->items.size()),
                                       ui::kInventoryBagSlots);
    for (int i = 0; i < bagCount; ++i) {
        const ecs::ItemInstance& entry = container->items[static_cast<std::size_t>(i)];
        const items::ItemDef* def = m_ctx.items->Find(entry.defId);
        ui::InventorySlot& slot = state.bag[static_cast<std::size_t>(i)];
        slot.filled = true;
        slot.label = def != nullptr ? def->name : entry.defId;
        slot.count = entry.count;
        slot.usable = def != nullptr &&
                      (def->use.Any() || !def->unlocksRecipes.empty());
        slot.equippable = def != nullptr &&
                          (def->type == items::ItemType::Weapon ||
                           def->type == items::ItemType::Clothing);
        slot.condition = entry.condition;
        slot.perishable = def != nullptr && def->spoilDays > 0.0f;
        slot.damaged =
            def != nullptr &&
            (def->type == items::ItemType::Weapon ||
             def->type == items::ItemType::Clothing) &&
            def->maxStack == 1 && entry.condition > 0.0f &&
            entry.condition < 1.0f;
        slot.repairAvailable = slot.damaged && repairKits >= m_repairKitCount;
    }

    if (equipment != nullptr) {
        for (int i = 0; i < ui::kInventoryEquipmentSlots; ++i) {
            const auto& entry =
                equipment->slots[static_cast<std::size_t>(i)];
            if (!entry.has_value()) {
                continue;
            }
            const items::ItemDef* def = m_ctx.items->Find(entry->defId);
            ui::InventorySlot& slot = state.equipment[static_cast<std::size_t>(i)];
            slot.filled = true;
            slot.label = def != nullptr ? def->name : entry->defId;
            slot.count = entry->count;
            slot.condition = entry->condition;
            slot.damaged =
                def != nullptr &&
                (def->type == items::ItemType::Weapon ||
                 def->type == items::ItemType::Clothing) &&
                def->maxStack == 1 && entry->condition > 0.0f &&
                entry->condition < 1.0f;
        }
    }

    if (m_ctx.recipes != nullptr) {
        state.recipes.reserve(m_ctx.recipes->Size());
        for (const items::RecipeDef& def : m_ctx.recipes->All()) {
            ui::InventoryRecipe recipe;
            recipe.id = def.id;
            recipe.name = def.name;
            recipe.outputDefId = def.outputDefId;
            recipe.outputCount = def.outputCount;
            const items::ItemDef* output = m_ctx.items->Find(def.outputDefId);
            recipe.outputLabel = output != nullptr ? output->name : def.outputDefId;
            std::string reason;
            const bool inventoryOk =
                items::CanCraft(*container, *m_ctx.items, def, &reason);
            recipe.station = def.station;
            recipe.stationLabel = StationLabel(def.station);
            recipe.stationNearby =
                def.station.empty() ||
                (position != nullptr &&
                 StationNearby(def.station, position->x, position->y, position->z));
            recipe.known = RecipeKnown(def.id);
            recipe.canCraft = recipe.known && inventoryOk && recipe.stationNearby;
            recipe.ingredients.reserve(def.ingredients.size());
            for (const items::RecipeIngredient& ingredient : def.ingredients) {
                const items::ItemDef* ingredientDef =
                    m_ctx.items->Find(ingredient.defId);
                recipe.ingredients.push_back(ui::InventoryRecipeIngredient{
                    ingredientDef != nullptr ? ingredientDef->name : ingredient.defId,
                    ingredient.count, items::CountOf(*container, ingredient.defId)});
            }
            recipe.tools.reserve(def.tools.size());
            for (const items::RecipeIngredient& tool : def.tools) {
                const items::ItemDef* toolDef = m_ctx.items->Find(tool.defId);
                recipe.tools.push_back(ui::InventoryRecipeIngredient{
                    toolDef != nullptr ? toolDef->name : tool.defId, tool.count,
                    items::CountUsableTools(*container, tool.defId)});
            }
            const float efficiency = items::ToolEfficiency(*container, def);
            recipe.estimatedSeconds = CraftSecondsForPlayer(def, efficiency);
            recipe.category = items::RecipeCategoryToString(def.category);
            recipe.categoryLabel = items::RecipeCategoryLabel(def.category);
            if (def.cookware.has_value()) {
                const items::ItemDef* cookwareDef =
                    m_ctx.items->Find(def.cookware->defId);
                recipe.cookware = ui::InventoryRecipeIngredient{
                    cookwareDef != nullptr ? cookwareDef->name
                                            : def.cookware->defId,
                    def.cookware->count,
                    items::CountUsableTools(*container, def.cookware->defId)};
            }
            if (def.fuel.has_value()) {
                const items::ItemDef* fuelDef =
                    m_ctx.items->Find(def.fuel->defId);
                recipe.fuel = ui::InventoryRecipeIngredient{
                    fuelDef != nullptr ? fuelDef->name : def.fuel->defId,
                    def.fuel->count,
                    items::CountOf(*container, def.fuel->defId)};
            }
            state.recipes.push_back(std::move(recipe));
        }
    }

    const ecs::CraftingJob* networkJob =
        m_networkMode ? registry.try_get<ecs::CraftingJob>(m_player) : nullptr;
    if (networkJob != nullptr && networkJob->active && m_ctx.recipes != nullptr) {
        const items::RecipeDef* def =
            m_ctx.recipes->Find(networkJob->recipeId);
        if (def != nullptr) {
            const items::ItemDef* output = m_ctx.items->Find(def->outputDefId);
            state.crafting.active = true;
            state.crafting.recipeId = def->id;
            state.crafting.outputLabel =
                output != nullptr ? output->name : def->outputDefId;
            state.crafting.outputCount = def->outputCount;
            state.crafting.elapsed = networkJob->elapsed;
            state.crafting.duration = networkJob->duration;
            state.crafting.progress =
                networkJob->duration > 0.0f
                    ? std::clamp(networkJob->elapsed / networkJob->duration,
                                 0.0f, 1.0f)
                    : 0.0f;
            state.crafting.toolEfficiency = networkJob->toolEfficiency;
            state.crafting.batchCount = std::max(networkJob->batchTotal, 1);
            state.crafting.completedCount =
                std::clamp(networkJob->batchCompleted, 0,
                           state.crafting.batchCount);
        }
    } else if (!m_networkMode && m_crafting.active && m_ctx.recipes != nullptr) {
        const items::RecipeDef* def = m_ctx.recipes->Find(m_crafting.recipeId);
        if (def != nullptr) {
            const items::ItemDef* output = m_ctx.items->Find(def->outputDefId);
            state.crafting.active = true;
            state.crafting.recipeId = def->id;
            state.crafting.outputLabel =
                output != nullptr ? output->name : def->outputDefId;
            state.crafting.outputCount = def->outputCount;
            state.crafting.elapsed = m_crafting.elapsed;
            state.crafting.duration = m_crafting.duration;
            state.crafting.progress = m_crafting.Progress();
            state.crafting.toolEfficiency = m_crafting.toolEfficiency;
            state.crafting.batchCount = std::max(m_craftBatchTotal, 1);
            state.crafting.completedCount =
                std::clamp(m_craftBatchCompleted, 0, state.crafting.batchCount);
        }
    }
    if (m_ctx.recipes != nullptr) {
        const auto appendQueue = [&](const auto& queue) {
            state.crafting.queue.reserve(queue.size());
            for (const ecs::CraftQueueItem& queued : queue) {
                const items::RecipeDef* def =
                    m_ctx.recipes->Find(queued.recipeId);
                if (def == nullptr) {
                    continue;
                }
                const items::ItemDef* output =
                    m_ctx.items->Find(def->outputDefId);
                state.crafting.queue.push_back(ui::InventoryQueuedCraft{
                    def->id, output != nullptr ? output->name : def->outputDefId,
                    def->outputCount, queued.count});
            }
        };
        if (networkJob != nullptr) {
            appendQueue(networkJob->queue);
        } else if (!m_networkMode) {
            appendQueue(m_craftQueue);
        }
    }

    state.lootTitle = "附近地面";
    state.lootCanTakeAll = false;
    if (position != nullptr) {
        const ecs::Entity storage = FindNearbyStorage();
        if (storage != entt::null) {
            const auto* storageContainer =
                registry.try_get<ecs::Container>(storage);
            if (storageContainer != nullptr) {
                state.lootIsStorage = true;
                state.lootEntityId = m_ctx.ecs->IdOf(storage).id;
                const bool vehicleStorage =
                    registry.all_of<ecs::Vehicle>(storage);
                const int storageWeight = static_cast<int>(std::lround(
                    items::TotalWeight(*storageContainer, *m_ctx.items)));
                const int storageCapacity = static_cast<int>(std::lround(
                    storageContainer->capacityKg));
                if (vehicleStorage) {
                    state.lootTitle =
                        "后备箱：皮卡??" + std::to_string(storageWeight) +
                        "/" + std::to_string(storageCapacity) + " kg??";
                } else {
                    state.lootTitle =
                        "仓库??" + StorageName(storage) + "??" +
                        std::to_string(storageWeight) + "/" +
                        std::to_string(storageCapacity) + " kg??";
                }
                const std::uint64_t storageOwner =
                    EntityOwnerStableId(storage);
                if (!vehicleStorage) {
                    state.lootTitle += StructureOwnerSuffix(storageOwner);
                    if (!CanEditLocalStructure(storageOwner)) {
                        state.lootTitle += "（只读）";
                    }
                }
                if (storageContainer->items.empty()) {
                    state.lootTitle += "（空）";
                }
                state.lootStacks =
                    static_cast<int>(storageContainer->items.size());
                const int lootCount = std::min<int>(
                    static_cast<int>(storageContainer->items.size()),
                    ui::kInventoryLootSlots);
                for (int i = 0; i < lootCount; ++i) {
                    const ecs::ItemInstance& item =
                        storageContainer->items[static_cast<std::size_t>(i)];
                    const items::ItemDef* def = m_ctx.items->Find(item.defId);
                    ui::InventorySlot& slot = state.loot[static_cast<std::size_t>(i)];
                    slot.filled = true;
                    slot.label = def != nullptr ? def->name : item.defId;
                    slot.count = item.count;
                    slot.entityId = state.lootEntityId;
                    slot.condition = item.condition;
                    slot.perishable = def != nullptr && def->spoilDays > 0.0f;
                }
            }
        } else {
            const ecs::Entity groundEntity = game::FindNearestGroundItem(
                *m_ctx.ecs, position->x, position->y, position->z, m_inventoryRange);
            const ecs::Entity containerEntity = game::FindNearestLootContainer(
                *m_ctx.ecs, position->x, position->y, position->z, m_inventoryRange);
            bool useContainer = containerEntity != entt::null;
            if (useContainer && groundEntity != entt::null) {
                const auto* containerPosition =
                    registry.try_get<ecs::Position>(containerEntity);
                const auto* groundPosition =
                    registry.try_get<ecs::Position>(groundEntity);
                if (containerPosition != nullptr && groundPosition != nullptr) {
                    const float cdx = containerPosition->x - position->x;
                    const float cdy = containerPosition->y - position->y;
                    const float gdx = groundPosition->x - position->x;
                    const float gdy = groundPosition->y - position->y;
                    useContainer = cdx * cdx + cdy * cdy < gdx * gdx + gdy * gdy;
                }
            }
            if (useContainer) {
                if (m_ctx.ui->IsInventoryOpen()) {
                    if (m_networkMode) {
                        const std::uint64_t containerId =
                            m_ctx.ecs->IdOf(containerEntity).id;
                        if (containerId != 0 &&
                            containerId != m_lastNetworkLootSearchEntity) {
                            m_lastNetworkLootSearchEntity = containerId;
                            net::Command command = MakePlayerCommand(
                                net::CommandType::SearchLootContainer);
                            command.payload.entityId = containerId;
                            if (!SubmitPlayerCommand(command)) {
                                SetNotice("搜刮请求发送失败", kNoticeFailColor,
                                          1.5f);
                            }
                        }
                    } else {
                        SearchNearbyLootContainer();
                    }
                }
                const auto* loot =
                    registry.try_get<ecs::LootContainer>(containerEntity);
                const auto* lootContainer =
                    registry.try_get<ecs::Container>(containerEntity);
                if (loot != nullptr && lootContainer != nullptr) {
                    state.lootEntityId = m_ctx.ecs->IdOf(containerEntity).id;
                    const items::LootTable* table =
                        m_ctx.lootTables != nullptr
                            ? m_ctx.lootTables->Find(loot->tableId)
                            : nullptr;
                    const auto* animalCorpse =
                        registry.try_get<ecs::AnimalCorpse>(containerEntity);
                    if (animalCorpse != nullptr) {
                        const AnimalDef* animal =
                            m_ctx.animals != nullptr
                                ? m_ctx.animals->Find(animalCorpse->speciesId)
                                : nullptr;
                        state.lootTitle =
                            "尸体??" +
                            (animal != nullptr
                                 ? animal->name
                                 : animalCorpse->speciesId);
                    } else {
                        state.lootTitle =
                            "容器??" +
                            (table != nullptr ? table->name
                                              : std::string("未知容器"));
                    }
                    if (lootContainer->items.empty()) {
                        state.lootTitle += "（空）";
                    }
                    state.lootStacks =
                        static_cast<int>(lootContainer->items.size());
                    state.lootCanTakeAll =
                        !m_networkMode && !lootContainer->items.empty();
                    const int lootCount = std::min<int>(
                        static_cast<int>(lootContainer->items.size()),
                        ui::kInventoryLootSlots);
                    for (int i = 0; i < lootCount; ++i) {
                        const ecs::ItemInstance& item =
                            lootContainer->items[static_cast<std::size_t>(i)];
                        const items::ItemDef* def = m_ctx.items->Find(item.defId);
                        ui::InventorySlot& slot =
                            state.loot[static_cast<std::size_t>(i)];
                        slot.filled = true;
                        slot.label = def != nullptr ? def->name : item.defId;
                        slot.count = item.count;
                        slot.entityId = state.lootEntityId;
                        slot.condition = item.condition;
                        slot.perishable = def != nullptr && def->spoilDays > 0.0f;
                    }
                }
            } else {
                const std::vector<game::GroundItemInfo> ground =
                    game::GroundItemsInRadius(*m_ctx.ecs, position->x, position->y,
                                              position->z, m_inventoryRange);
                state.lootStacks = static_cast<int>(ground.size());
                const int lootCount = std::min<int>(
                    static_cast<int>(ground.size()), ui::kInventoryLootSlots);
                for (int i = 0; i < lootCount; ++i) {
                    const game::GroundItemInfo& info =
                        ground[static_cast<std::size_t>(i)];
                    const items::ItemDef* def = m_ctx.items->Find(info.item.defId);
                    ui::InventorySlot& slot = state.loot[static_cast<std::size_t>(i)];
                    slot.filled = true;
                    slot.label = def != nullptr ? def->name : info.item.defId;
                    slot.count = info.item.count;
                    slot.entityId = m_ctx.ecs->IdOf(info.entity).id;
                    slot.condition = info.item.condition;
                    slot.perishable = def != nullptr && def->spoilDays > 0.0f;
                }
            }
        }
    }

    if (m_noticeTimer > 0.0f && !m_noticeText.empty()) {
        state.status = m_noticeText;
        state.statusTone = m_noticeTone;
    }


    m_ctx.ui->SetInventoryState(state);
}

void WorldDemoState::ReturnOrDropItem(const ecs::ItemInstance& item,
                                      bool* dropped) {
    if (dropped != nullptr) {
        *dropped = false;
    }
    if (!item.Valid() || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container != nullptr) {
        const int added = items::AddItem(*container, *m_ctx.items, item);
        if (added >= item.count) {
            return;
        }
        ecs::ItemInstance remainder = item;
        remainder.count -= added;
        if (position != nullptr) {
            game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, remainder, position->x,
                                  position->y, position->z);
            if (dropped != nullptr) {
                *dropped = true;
            }
        }
        return;
    }
    if (position != nullptr) {
        game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, item, position->x, position->y,
                              position->z);
        if (dropped != nullptr) {
            *dropped = true;
        }
    }
}

void WorldDemoState::EquipBagItem(int index, ecs::EquipSlot slot) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (container == nullptr || equipment == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    const ecs::ItemInstance entry = container->items[static_cast<std::size_t>(index)];
    const items::ItemDef* def = m_ctx.items->Find(entry.defId);
    if (def == nullptr || !items::SlotAccepts(slot, def->type)) {
            SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
        return;
    }
    if (slot == ecs::EquipSlot::PrimaryHand) {
        m_rangedWeapon.CancelReload();
    }

    ecs::ItemInstance one = entry;
    one.count = 1;
    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), 1, &removed)) {
        return;
    }
    ecs::ItemInstance swapped;
    if (!items::Equip(*equipment, *m_ctx.items, slot, one, &swapped)) {
        ReturnOrDropItem(removed);
            SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
        return;
    }
    if (swapped.Valid()) {
        ReturnOrDropItem(swapped);
    }
    SetNotice("装备??" + def->name, kNoticeOkColor, 1.5f);
    ZL_INFO("items", "装备（背包槽 {}）：{}", index, def->name);
}

void WorldDemoState::EquipLootItem(std::uint64_t stableId, ecs::EquipSlot slot,
                                   int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
    if (entity == entt::null || !m_ctx.ecs->Alive(entity)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (equipment == nullptr) {
        return;
    }

    const bool isStorage = game::IsPlayerStorage(*m_ctx.ecs, entity);
    if (isStorage && !CanAccessLocalStorage(entity)) {
        return;
    }
    if (registry.all_of<ecs::LootContainer>(entity) || isStorage) {
        auto* source = registry.try_get<ecs::Container>(entity);
        if (source == nullptr) {
            return;
        }
        const int sourceIndex = index < 0 ? 0 : index;
        if (static_cast<std::size_t>(sourceIndex) >= source->items.size()) {
            SetNotice("容器物品不存在", kNoticeFailColor, 1.5f);
            return;
        }
        const ecs::ItemInstance entry =
            source->items[static_cast<std::size_t>(sourceIndex)];
        const items::ItemDef* def = m_ctx.items->Find(entry.defId);
        if (def == nullptr || !items::SlotAccepts(slot, def->type)) {
            SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
            return;
        }
        if (slot == ecs::EquipSlot::PrimaryHand) {
            m_rangedWeapon.CancelReload();
        }
        ecs::ItemInstance one = entry;
        one.count = 1;
        ecs::ItemInstance removed;
        if (!items::TakeAt(*source, static_cast<std::size_t>(sourceIndex), 1,
                           &removed)) {
            return;
        }
        ecs::ItemInstance swapped;
        if (!items::Equip(*equipment, *m_ctx.items, slot, one, &swapped)) {
            (void)items::AddItem(*source, *m_ctx.items, removed);
            SetNotice("装备失败", kNoticeFailColor, 1.5f);
            return;
        }
        if (m_ctx.events != nullptr) {
            m_ctx.events->EmitQueued(
                core::kOnItemPickup,
                core::ItemEvent{m_ctx.ecs->IdOf(entity).id, one.defId, 1});
        }
        if (swapped.Valid()) {
            ReturnOrDropItem(swapped);
        }
        SetNotice("装备??" + def->name, kNoticeOkColor, 1.5f);
        ZL_INFO("items", "装备（容器）：{}", def->name);
        return;
    }

    auto* instance = registry.try_get<ecs::ItemInstance>(entity);
    if (instance == nullptr) {
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(instance->defId);
    if (def == nullptr || !items::SlotAccepts(slot, def->type)) {
            SetNotice("无法装备该物品", kNoticeFailColor, 1.5f);
        return;
    }
    if (slot == ecs::EquipSlot::PrimaryHand) {
        m_rangedWeapon.CancelReload();
    }
    ecs::ItemInstance one = *instance;
    one.count = 1;
    ecs::ItemInstance swapped;
    if (!items::Equip(*equipment, *m_ctx.items, slot, one, &swapped)) {
        return;
    }
    const std::uint64_t entityId = m_ctx.ecs->IdOf(entity).id;
    instance->count -= 1;
    if (instance->count <= 0) {
        m_ctx.ecs->Destroy(entity);
    }
    if (m_ctx.events != nullptr) {
        m_ctx.events->EmitQueued(core::kOnItemPickup,
                                 core::ItemEvent{entityId, one.defId, 1});
    }
    if (swapped.Valid()) {
        ReturnOrDropItem(swapped);
    }
    SetNotice("装备??" + def->name, kNoticeOkColor, 1.5f);
    ZL_INFO("items", "装备（地面）：{}", def->name);
}

void WorldDemoState::UnequipEquipment(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0 || index >= ui::kInventoryEquipmentSlots) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (equipment == nullptr) {
        return;
    }
    if (index == static_cast<int>(ecs::EquipSlot::PrimaryHand)) {
        m_rangedWeapon.CancelReload();
    }
    ecs::ItemInstance removed;
    if (!items::Unequip(*equipment, static_cast<ecs::EquipSlot>(index), &removed)) {
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(removed.defId);
    ReturnOrDropItem(removed);
    SetNotice("卸下??" + (def != nullptr ? def->name : removed.defId), kNoticeInfoColor,
              1.5f);
    ZL_INFO("items", "卸下：{}", def != nullptr ? def->name : removed.defId);
}

void WorldDemoState::ApplyFoodUse(const items::ItemDef& def,
                                  const items::UseResult& use) {
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player) || !IsFoodUse(use.effect)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto& food = registry.get_or_emplace<ecs::FoodState>(m_player);
    const std::uint64_t seed = DeriveFoodPoisonSeed(
        m_ctx.ecs->IdOf(m_player).id, ++m_foodUseSerial, def.id);
    (void)ApplyFoodConsumption(food, def, use.spoiled, m_foodSafety, seed);
}

bool WorldDemoState::ApplyUseInfectionRisk(float risk, std::string_view defId) {
    if (!(risk > 0.0f) || m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* wounds = registry.try_get<ecs::Wounds>(m_player);
    if (wounds == nullptr || wounds->count == 0) {
        return false;
    }
    auto& infection = registry.get_or_emplace<ecs::Infection>(m_player);
    const std::uint64_t seed = DeriveFoodPoisonSeed(
        m_ctx.ecs->IdOf(m_player).id, m_ticksInState, defId);
    return characters::TryInfectWithRisk(infection, risk, seed, wounds);
}

void WorldDemoState::UseBagItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    const items::ItemDef* def =
        m_ctx.items->Find(container->items[static_cast<std::size_t>(index)].defId);
    if (def != nullptr && !def->unlocksRecipes.empty()) {
        UseBlueprintItem(index, *def);
        return;
    }
    auto* health = registry.try_get<ecs::Health>(m_player);
    if (health == nullptr) {
        return;
    }
    if (def != nullptr && def->use.painRelief > 0.0f) {
        const auto* pain = registry.try_get<ecs::Pain>(m_player);
        if (pain == nullptr || pain->baseLevel <= 0.01f) {
            SetNotice("当前没有需要缓解的疼痛", kNoticeFailColor, 1.5f);
            return;
        }
    }
    if (def != nullptr && def->use.infectionCure > 0.0f) {
        const auto* infection = registry.try_get<ecs::Infection>(m_player);
        if (infection == nullptr ||
            infection->stage == ecs::InfectionStage::None) {
            SetNotice("当前没有感染", kNoticeFailColor, 1.5f);
            return;
        }
    }
    const items::UseResult use =
        items::UseAt(*container, *m_ctx.items, static_cast<std::size_t>(index));
    if (!use.applied) {
        SetNotice("该物品不能使用", kNoticeFailColor, 1.5f);
        return;
    }
    auto* needs = registry.try_get<ecs::Needs>(m_player);
    const float healed = items::ApplyUseEffect(
        use.effect, health->max, &health->current,
        needs != nullptr ? &needs->hunger : nullptr,
        needs != nullptr ? &needs->thirst : nullptr,
        needs != nullptr ? &needs->fatigue : nullptr);
    const float foodDamage = use.spoiled ? m_spoiledFoodDamage : 0.0f;
    const float totalDamage = use.effect.healthPenalty + foodDamage;
    if (totalDamage > 0.0f) {
        health->current = std::max(0.0f, health->current - totalDamage);
    }
    if (def != nullptr) {
        ApplyFoodUse(*def, use);
    }
    characters::WoundTreatment treatment;
    if (use.effect.bandage > 0.0f) {
        if (auto* wounds = registry.try_get<ecs::Wounds>(m_player); wounds != nullptr) {
            treatment = characters::TreatWounds(*wounds, use.effect.bandage);
        }
    }
    characters::InfectionTreatment infectionTreatment;
    if (use.effect.infectionCure > 0.0f || use.effect.infectionSlow > 0.0f) {
        if (auto* infection = registry.try_get<ecs::Infection>(m_player);
            infection != nullptr) {
            infectionTreatment = characters::TreatInfection(
                *infection, use.effect.infectionCure, use.effect.infectionSlow,
                use.effect.infectionSlowSeconds,
                registry.try_get<ecs::Wounds>(m_player));
        }
    }
    bool painRelieved = false;
    if (use.effect.painRelief > 0.0f) {
        auto& pain = registry.get_or_emplace<ecs::Pain>(m_player);
        painRelieved = characters::ApplyPainRelief(
            pain, use.effect.painRelief, use.effect.painReliefSeconds);
    }
    const std::string name = def != nullptr ? def->name : use.defId;
    if (ApplyUseInfectionRisk(use.effect.infectionRisk, use.defId)) {
        SetNotice("使用了 " + name + "：污染导致感染", kNoticeFailColor, 2.0f);
        return;
    }
    if (treatment.wounds > 0) {
        std::string text = "使用??" + name + "：止血 " +
                           std::to_string(treatment.wounds) + " 处伤??";
        if (healed > 0.0f) {
            text += "??" + std::to_string(static_cast<int>(healed)) + " HP";
        }
        if (infectionTreatment.applied) {
            text += infectionTreatment.cured ? "，感染已清除" : "，感染延缓";
        }
        SetNotice(std::move(text), kNoticeOkColor, 1.8f);
    } else if (infectionTreatment.applied) {
        if (infectionTreatment.cured) {
            SetNotice("使用了 " + name + "：感染已清除", kNoticeOkColor, 1.8f);
        } else {
            char progress[64];
            std::snprintf(progress, sizeof(progress), "感染病程 -%.0f%%",
                          static_cast<double>(infectionTreatment.progressReduced *
                                              100.0f));
            SetNotice("使用??" + name + "??" + progress, kNoticeOkColor, 1.8f);
        }
    } else if (painRelieved) {
        SetNotice("使用??" + name + "：止??" +
                      std::to_string(static_cast<int>(use.effect.painReliefSeconds)) +
                      " ??",
                  kNoticeOkColor, 1.8f);
    } else if (use.effect.heal > 0.0f) {
        SetNotice("使用??" + name + "??" + std::to_string(static_cast<int>(healed)) +
                      " HP??",
                  kNoticeOkColor, 1.8f);
    } else if (totalDamage > 0.0f) {
        const char* reason = use.spoiled ? "食物已变质" : "水不干净";
        SetNotice("使用??" + name + "??" + reason + "??" +
                      std::to_string(static_cast<int>(std::lround(totalDamage))) +
                      " HP",
                  kNoticeInfoColor, 2.0f);
    } else {
        SetNotice("使用??" + name, kNoticeOkColor, 1.8f);
    }
    ZL_INFO("items", "使用（背包槽 {}）：{}（回血 {:.0f}，伤??{:.0f}，止血 {} 处，"
                     "止痛 {}，剩??{}??",
            index, name, healed, totalDamage, treatment.wounds,
            painRelieved ? "??" : "??", use.remaining);
}

bool WorldDemoState::UseBlueprintItem(int index, const items::ItemDef& def) {
    if (m_ctx.ecs == nullptr || m_ctx.recipes == nullptr || m_player == entt::null ||
        index < 0) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return false;
    }
    auto* known = registry.try_get<ecs::KnownRecipes>(m_player);
    if (known == nullptr) {
        known = &registry.emplace<ecs::KnownRecipes>(m_player);
    }

    std::vector<std::string> learned;
    for (const std::string& recipeId : def.unlocksRecipes) {
        if (m_ctx.recipes->Find(recipeId) == nullptr || RecipeKnown(recipeId)) {
            continue;
        }
        learned.push_back(recipeId);
    }
    if (learned.empty()) {
        SetNotice("该配方已学会，无需再次使用", kNoticeInfoColor, 1.8f);
        return true;
    }

    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), 1, &removed)) {
        return false;
    }
    std::string labels;
    for (const std::string& recipeId : learned) {
        known->recipeIds.push_back(recipeId);
        const items::RecipeDef* recipe = m_ctx.recipes->Find(recipeId);
        if (!labels.empty()) {
            labels += "??";
        }
        labels += recipe != nullptr ? recipe->name : recipeId;
    }
    std::sort(known->recipeIds.begin(), known->recipeIds.end());
    known->recipeIds.erase(std::unique(known->recipeIds.begin(), known->recipeIds.end()),
                           known->recipeIds.end());
    SetNotice("学会了配方：" + labels, kNoticeOkColor, 2.8f);
    ZL_INFO("items", "使用蓝图/杂志 '{}'，学会配方：{}", def.name, labels);
    RefreshInventory();
    return true;
}

void WorldDemoState::RepairBagItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return;
    }
    const items::RepairResult result =
        items::Repair(*container, *m_ctx.items, static_cast<std::size_t>(index),
                      m_repairKitDefId, m_repairKitCount, m_repairAmount);
    if (!result.ok) {
        SetNotice(result.error, kNoticeFailColor, 2.0f);
        ZL_WARN("items", "维修失败（背包槽 {}）：{}", index, result.error);
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(result.defId);
    const items::ItemDef* material = m_ctx.items->Find(result.materialDefId);
    const std::string name = def != nullptr ? def->name : result.defId;
    const std::string materialName =
        material != nullptr ? material->name : result.materialDefId;
    const int beforePct =
        static_cast<int>(std::lround(result.before * 100.0f));
    const int afterPct =
        static_cast<int>(std::lround(result.after * 100.0f));
    SetNotice("维修成功：" + name + " " + std::to_string(beforePct) + "% → " +
                  std::to_string(afterPct) + "%（消??" + materialName + " ×" +
                  std::to_string(result.materialCount) + "??",
              kNoticeOkColor, 2.5f);
    ZL_INFO("items", "维修成功（背包槽 {}）：{} {}% → {}%，消耗 {} ×{}", index,
            name, beforePct, afterPct, materialName, result.materialCount);
}

std::string WorldDemoState::StationLabel(std::string_view station) const {
    if (station.empty()) {
        return {};
    }
    const auto found = m_stationLabels.find(std::string(station));
    return found != m_stationLabels.end() ? found->second : std::string(station);
}

bool WorldDemoState::StationNearby(std::string_view station, float x, float y,
                                   int z) const {
    if (station.empty()) {
        return true;
    }
    if (m_ctx.world == nullptr || m_stationRange <= 0.0f) {
        return false;
    }
    return game::HasStationNearby(*m_ctx.world, x, y, z, station, m_stationRange);
}

bool WorldDemoState::RecipeKnown(std::string_view recipeId) const {
    if (!m_blueprintRecipes.contains(std::string(recipeId))) {
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    const auto* known =
        m_ctx.ecs->Registry().try_get<ecs::KnownRecipes>(m_player);
    if (known == nullptr) {
        return false;
    }
    return std::find(known->recipeIds.begin(), known->recipeIds.end(), recipeId) !=
           known->recipeIds.end();
}

void WorldDemoState::DropBagItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr || position == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    ecs::ItemInstance stack = container->items[static_cast<std::size_t>(index)];
    ecs::ItemInstance removed;
    if (!items::TakeAt(*container, static_cast<std::size_t>(index), stack.count,
                       &removed)) {
        return;
    }
    const ecs::Entity ground = game::SpawnGroundItem(
        *m_ctx.ecs, *m_ctx.items, removed, position->x, position->y, position->z);
    const items::ItemDef* def = m_ctx.items->Find(removed.defId);
    if (m_ctx.events != nullptr && ground != entt::null) {
        m_ctx.events->EmitQueued(
            core::kOnItemDrop,
            core::ItemEvent{m_ctx.ecs->IdOf(ground).id, removed.defId, removed.count});
    }
    SetNotice("丢弃??" + (def != nullptr ? def->name : removed.defId) + " ×" +
                  std::to_string(removed.count),
              kNoticeInfoColor, 1.5f);
    ZL_INFO("items", "丢弃（背包槽 {}）：{} x{}", index,
            def != nullptr ? def->name : removed.defId, removed.count);
}

void WorldDemoState::DropEquipmentItem(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        index < 0 || index >= ui::kInventoryEquipmentSlots) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (equipment == nullptr || position == nullptr) {
        return;
    }
    if (index == static_cast<int>(ecs::EquipSlot::PrimaryHand)) {
        m_rangedWeapon.CancelReload();
    }
    ecs::ItemInstance removed;
    if (!items::Unequip(*equipment, static_cast<ecs::EquipSlot>(index), &removed)) {
        return;
    }
    const ecs::Entity ground = game::SpawnGroundItem(
        *m_ctx.ecs, *m_ctx.items, removed, position->x, position->y, position->z);
    if (m_ctx.events != nullptr && ground != entt::null) {
        m_ctx.events->EmitQueued(
            core::kOnItemDrop,
            core::ItemEvent{m_ctx.ecs->IdOf(ground).id, removed.defId, removed.count});
    }
    const items::ItemDef* def = m_ctx.items->Find(removed.defId);
    SetNotice("丢弃??" + (def != nullptr ? def->name : removed.defId), kNoticeInfoColor,
              1.5f);
    ZL_INFO("items", "丢弃装备槽 {}：{}", index,
            def != nullptr ? def->name : removed.defId);
}

void WorldDemoState::PickupLootItem(std::uint64_t stableId, int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
    if (entity == entt::null || !m_ctx.ecs->Alive(entity)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return;
    }

    const bool isStorage = game::IsPlayerStorage(*m_ctx.ecs, entity);
    if (isStorage && !CanAccessLocalStorage(entity)) {
        return;
    }
    if (registry.all_of<ecs::LootContainer>(entity) || isStorage) {
        const auto* source = registry.try_get<ecs::Container>(entity);
        if (source == nullptr) {
            return;
        }
        const int slot = index < 0 ? 0 : index;
        const game::LootTakeResult result = game::TakeLootContainerItem(
            *m_ctx.ecs, *m_ctx.items, entity, *container, slot);
        if (!result.found) {
            SetNotice("容器里没有可拿取的物品", kNoticeInfoColor, 1.5f);
            return;
        }
        const items::ItemDef* def = m_ctx.items->Find(result.defId);
        const std::string name = def != nullptr ? def->name : result.defId;
        if (result.taken > 0) {
            if (result.remaining > 0) {
                SetNotice("取出??" + name + " ×" + std::to_string(result.taken) +
                              "（容器剩 " + std::to_string(result.remaining) + "??",
                          kNoticeOkColor, 2.0f);
            } else {
                SetNotice("取出??" + name + " ×" + std::to_string(result.taken),
                          kNoticeOkColor, 2.0f);
            }
            if (m_ctx.events != nullptr) {
                m_ctx.events->EmitQueued(
                    core::kOnItemPickup,
                    core::ItemEvent{m_ctx.ecs->IdOf(entity).id, result.defId,
                                    result.taken});
            }
            ZL_INFO("items", "拾取（容器）：{} x{}（容器剩??{}??", name, result.taken,
                    result.remaining);
        } else {
            SetNotice("背包已满", kNoticeFailColor, 1.5f);
        }
        return;
    }

    const game::PickupResult result =
        game::PickupGroundItem(*m_ctx.ecs, *m_ctx.items, *container, entity);
    if (!result.found) {
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(result.defId);
    const std::string name = def != nullptr ? def->name : result.defId;
    if (result.picked > 0) {
        if (result.groundRemaining > 0) {
            SetNotice("拾取??" + name + " ×" + std::to_string(result.picked) + "（地上剩 " +
                          std::to_string(result.groundRemaining) + "??",
                      kNoticeOkColor, 2.0f);
        } else {
            SetNotice("拾取??" + name + " ×" + std::to_string(result.picked),
                      kNoticeOkColor, 2.0f);
        }
        if (m_ctx.events != nullptr) {
            m_ctx.events->EmitQueued(
                core::kOnItemPickup,
                core::ItemEvent{result.entityId, result.defId, result.picked});
        }
        ZL_INFO("items", "拾取（地面）：{} x{}（剩??{}??", name, result.picked,
                result.groundRemaining);
    } else {
        SetNotice("背包已满", kNoticeFailColor, 1.5f);
    }
}

net::Command WorldDemoState::MakePlayerCommand(net::CommandType type) const {
    net::Command command;
    command.type = type;
    if (m_ctx.ecs != nullptr && m_player != entt::null && m_ctx.ecs->Alive(m_player)) {
        command.playerStableId = m_ctx.ecs->IdOf(m_player).id;
    }
    if (m_ctx.loop != nullptr) {
        command.tick = m_ctx.loop->TotalTicks();
    }
    return command;
}

bool WorldDemoState::SubmitPlayerCommand(const net::Command& command) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !m_ctx.ecs->Alive(m_player) ||
        command.playerStableId == 0) {
        return false;
    }
    std::string error;
    if (m_networkMode) {
        if (m_ctx.networkClient == nullptr ||
            !m_ctx.networkClient->SendCommand(command, &error)) {
            if (!error.empty()) {
                ZL_DEBUG("net", "玩家命令 {} 未发送：{}", net::CommandTypeName(command.type),
                         error);
            }
            return false;
        }
        return true;
    }
    if (m_ctx.commands == nullptr) {
        return false;
    }
    if (!m_ctx.commands->Submit(command, &error)) {
        if (!error.empty()) {
            ZL_WARN("net", "玩家命令 {} 未提交：{}", net::CommandTypeName(command.type), error);
        }
        return false;
    }
    return true;
}

void WorldDemoState::SubmitMoveCommand(float screenX, float screenY) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    if (!std::isfinite(screenX) || !std::isfinite(screenY)) {
        return;
    }
    if (m_moveCommandValid && std::fabs(screenX - m_lastMoveCommand.x) < 0.0001f &&
        std::fabs(screenY - m_lastMoveCommand.y) < 0.0001f) {
        return;
    }
    net::Command command = MakePlayerCommand(net::CommandType::Move);
    command.payload.x = screenX;
    command.payload.y = screenY;
    if (SubmitPlayerCommand(command)) {
        m_lastMoveCommand = glm::vec2(screenX, screenY);
        m_moveCommandValid = true;
    }
}

void WorldDemoState::SubmitInventoryAction(const ui::InventoryAction& action) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    net::Command command = MakePlayerCommand(net::CommandType::InventoryAction);
    const auto count = static_cast<std::uint16_t>(std::clamp(action.count, 1, 99));
    command.payload.flags =
        static_cast<std::uint16_t>(action.type) | static_cast<std::uint16_t>(count << 8);
    command.payload.area = static_cast<std::uint8_t>(action.from);
    command.payload.targetArea = static_cast<std::uint8_t>(action.to);
    command.payload.index = action.fromIndex;
    command.payload.targetIndex = action.toIndex;
    command.payload.entityId = action.fromEntityId;
    SubmitPlayerCommand(command);
}

void WorldDemoState::SubmitNetworkSleepCommand(net::CommandType type,
                                               std::uint16_t flags,
                                               float durationMinutes) {
    if (!m_networkMode || m_ctx.networkClient == nullptr) {
        return;
    }
    net::Command command = MakePlayerCommand(type);
    command.payload.flags = flags;
    command.payload.x = durationMinutes;
    std::string error;
    if (!SubmitPlayerCommand(command)) {
        SetNotice(error.empty() ? "睡眠请求发送失败" : error,
                  kNoticeFailColor, 1.8f);
    }
}

void WorldDemoState::HandleNetworkSleepInput(core::Input& input) {
    if (!m_networkMode || m_networkSleep.phase == 0) {
        return;
    }
    if (m_networkSleep.phase == net::kPlayerSleepPhaseVote) {
        if (input.IsActionPressed("Quit")) {
            SubmitNetworkSleepCommand(net::CommandType::SleepCancel);
            return;
        }
        if (!m_networkSleep.voted) {
            if (input.IsActionPressed("SleepVoteYes")) {
                SubmitNetworkSleepCommand(net::CommandType::SleepVote,
                                          net::kSleepVoteYesFlag);
            } else if (input.IsActionPressed("SleepVoteNo")) {
                SubmitNetworkSleepCommand(net::CommandType::SleepVote, 0);
            }
        }
        return;
    }
    if (m_networkSleep.phase == net::kPlayerSleepPhaseSleeping &&
        input.IsActionPressed("Quit")) {
        SubmitNetworkSleepCommand(net::CommandType::SleepCancel);
    }
}

bool WorldDemoState::ExecutePlayerCommand(const net::Command& command, std::string& error) {
    if (m_ctx.ecs == nullptr) {
        error = "ECS 未初始化";
        return false;
    }
    if (m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        error = "玩家尚未生成";
        return false;
    }
    const std::uint64_t playerId = m_ctx.ecs->IdOf(m_player).id;
    if (command.playerStableId != playerId) {
        error = "playerStableId 不匹配（命令 " + std::to_string(command.playerStableId) +
                "，本??" + std::to_string(playerId) + "??";
        return false;
    }
    if (!std::isfinite(command.payload.x) || !std::isfinite(command.payload.y)) {
        error = "命令 payload 浮点非法";
        return false;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    switch (command.type) {
        case net::CommandType::Move: {
            auto* move = registry.try_get<ecs::MoveInput>(m_player);
            if (move == nullptr) {
                move = &registry.emplace<ecs::MoveInput>(m_player);
            }
            move->x = command.payload.x;
            move->y = command.payload.y;
            return true;
        }
        case net::CommandType::MoveTo: {
            render::GridPick pick;
            pick.hit = true;
            pick.x = static_cast<int>(std::lround(command.payload.x));
            pick.y = static_cast<int>(std::lround(command.payload.y));
            HandleClickToMove(pick);
            return true;
        }
        case net::CommandType::Attack:
            PlayerAttack(command.payload.entityId,
                         (command.payload.flags & net::kAttackHeavyFlag) != 0);
            return true;
        case net::CommandType::PickupNearest:
            PickupNearestItem();
            return true;
        case net::CommandType::PickupEntity:
            PickupLootItem(command.payload.entityId, command.payload.index);
            return true;
        case net::CommandType::UseBag:
            if (command.payload.index < 0) {
                UseConsumableItem();
            } else {
                UseBagItem(command.payload.index);
            }
            return true;
        case net::CommandType::EquipBag:
            if (command.payload.index < 0) {
                ToggleEquipWeapon();
            } else {
                const int slot = command.payload.targetIndex >= 0 &&
                                         command.payload.targetIndex <
                                             ui::kInventoryEquipmentSlots
                                     ? command.payload.targetIndex
                                     : static_cast<int>(ecs::EquipSlot::PrimaryHand);
                EquipBagItem(command.payload.index, static_cast<ecs::EquipSlot>(slot));
            }
            return true;
        case net::CommandType::Unequip:
            UnequipEquipment(command.payload.index);
            return true;
        case net::CommandType::DropBag:
            if (command.payload.index < 0) {
                DropFirstItem();
            } else {
                DropBagItem(command.payload.index);
            }
            return true;
        case net::CommandType::DropEquipment:
            DropEquipmentItem(command.payload.index);
            return true;
        case net::CommandType::InventoryAction: {
            const auto actionType =
                static_cast<ui::InventoryActionType>(command.payload.flags & 0xFFu);
            const int count = static_cast<int>(command.payload.flags >> 8);
            if (count > 99 ||
                command.payload.area > static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                command.payload.targetArea >
                    static_cast<std::uint8_t>(ui::InventoryArea::Loot) ||
                static_cast<std::uint8_t>(actionType) >
                    static_cast<std::uint8_t>(ui::InventoryActionType::SortStorage)) {
                error = "背包命令字段非法";
                return false;
            }
            ui::InventoryAction action;
            action.type = actionType;
            action.from = static_cast<ui::InventoryArea>(command.payload.area);
            action.to = static_cast<ui::InventoryArea>(command.payload.targetArea);
            action.fromIndex = command.payload.index;
            action.toIndex = command.payload.targetIndex;
            action.fromEntityId = command.payload.entityId;
            action.count = count > 0 ? count : 1;
            HandleInventoryAction(action);
            return true;
        }
        case net::CommandType::Gunshot:
            EmitGunshot();
            return true;
        case net::CommandType::SelectHotbar:
            if (command.payload.index < 0 || command.payload.index >= 5) {
                error = "快捷栏索引越界";
                return false;
            }
            m_hotbarSelected = command.payload.index;
            ZL_DEBUG("ui", "快捷栏选择：槽 {}", m_hotbarSelected + 1);
            return true;
        case net::CommandType::ChoosePerk:
            if (!SelectPerk(command.payload.index)) {
                error = "Perk 选择无效";
                return false;
            }
            return true;
        case net::CommandType::InteractFacility:
            if (m_networkMode) {
                return true;
            }
            return TryInteractHomestead();
        case net::CommandType::TradeOffer:
            // 商人交易由服务器权威执行；本地不重复扣物/加物。
            return true;
        case net::CommandType::AcceptSettlementMission:
            // 聚落委托接取由服务器权威执行；任务日志随 ZLP1 回流。
            return true;
        case net::CommandType::SleepRequest:
        case net::CommandType::SleepVote:
        case net::CommandType::SleepCancel:
            error = "单机睡眠不走服务器命令";
            return false;
    }
    error = "未知命令类型";
    return false;
}

bool WorldDemoState::StartCraftBatch(const items::RecipeDef& recipe, int requested,
                                     std::string& error, int& actual) {
    actual = 0;
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        error = "玩家或注册表不可用";
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (!RecipeKnown(recipe.id)) {
        error = "尚未学会该配方：阅读配方杂志";
        return false;
    }
    if (!recipe.station.empty() &&
        (position == nullptr ||
         !StationNearby(recipe.station, position->x, position->y, position->z))) {
        error = "已离开" + StationLabel(recipe.station);
        return false;
    }
    if (container == nullptr) {
        error = "背包不可用";
        return false;
    }
    std::string reason;
    if (!items::CanCraft(*container, *m_ctx.items, recipe, &reason)) {
        error = reason.empty() ? "材料或背包空间不足" : reason;
        return false;
    }
    const int possible =
        items::MaxCraftCount(*container, *m_ctx.items, recipe,
                             std::clamp(requested, 1, 99));
    if (possible <= 0) {
        error = "材料或背包空间不足";
        return false;
    }
    const float efficiency = items::ToolEfficiency(*container, recipe);
    const float seconds = CraftSecondsForPlayer(recipe, efficiency);
    m_craftBatchTotal = possible;
    m_craftBatchCompleted = 0;
    m_crafting.Start(recipe.id, seconds, efficiency);
    actual = possible;
    return true;
}

std::string WorldDemoState::QueuedCraftLabel(std::string_view recipeId) const {
    if (m_ctx.recipes == nullptr) {
        return std::string(recipeId);
    }
    const items::RecipeDef* recipe = m_ctx.recipes->Find(recipeId);
    if (recipe == nullptr) {
        return std::string(recipeId);
    }
    const items::ItemDef* output =
        m_ctx.items != nullptr ? m_ctx.items->Find(recipe->outputDefId) : nullptr;
    const std::string label =
        output != nullptr ? output->name : recipe->outputDefId;
    return label + " ×" + std::to_string(recipe->outputCount);
}

bool WorldDemoState::StartNextQueuedCraft(std::string_view prefix) {
    if (m_craftQueue.empty()) {
        return false;
    }
    ecs::CraftQueueItem entry = std::move(m_craftQueue.front());
    m_craftQueue.erase(m_craftQueue.begin());
    const auto fail = [&](const std::string& reason) {
        const int remaining = static_cast<int>(m_craftQueue.size());
        m_craftQueue.clear();
        std::string notice(prefix);
        notice += "队列中断：" + reason;
        if (remaining > 0) {
            notice += "（剩余 " + std::to_string(remaining) + " 项已清空）";
        }
        SetNotice(notice, kNoticeFailColor, 3.0f);
        ZL_INFO("items", "制作队列中断：{}（剩余 {} 项已清空）", reason, remaining);
        return false;
    };
    if (m_ctx.recipes == nullptr) {
        return fail("配方未加载");
    }
    const items::RecipeDef* recipe =
        m_ctx.recipes->Find(entry.recipeId);
    if (recipe == nullptr) {
        return fail("配方不存在");
    }
    std::string error;
    int actual = 0;
    if (!StartCraftBatch(*recipe, entry.count, error, actual)) {
        return fail(error);
    }
    const items::ItemDef* output = m_ctx.items->Find(recipe->outputDefId);
    const std::string label =
        output != nullptr ? output->name : recipe->outputDefId;
    std::string notice(prefix);
    notice += "开始队列下一项：" + label + " ×" +
              std::to_string(recipe->outputCount) + "??" +
              std::to_string(actual) + " ??";
    if (actual < entry.count) {
        notice += "；请??" + std::to_string(entry.count) +
                  " 件，已按材料/空间截断";
    }
    notice += "??";
    SetNotice(notice, kNoticeInfoColor, 2.5f);
    ZL_INFO("items",
            "开始队列下一项：{} ×{}（配方 {}，请求 {} 件，实际 {} 件，剩余 {} 项）",
            label, recipe->outputCount, recipe->id, entry.count, actual,
            static_cast<int>(m_craftQueue.size()));
    return true;
}

void WorldDemoState::SyncCraftingJobToEcs() {
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* job = registry.try_get<ecs::CraftingJob>(m_player);
    if (job == nullptr) {
        job = &registry.emplace<ecs::CraftingJob>(m_player);
    }
    job->active = m_crafting.active;
    job->recipeId = m_crafting.active ? m_crafting.recipeId : std::string();
    job->elapsed = m_crafting.active ? m_crafting.elapsed : 0.0f;
    job->duration = m_crafting.active ? m_crafting.duration : 0.0f;
    job->toolEfficiency = m_crafting.active ? m_crafting.toolEfficiency : 1.0f;
    job->batchTotal = m_crafting.active ? m_craftBatchTotal : 0;
    job->batchCompleted = m_crafting.active ? m_craftBatchCompleted : 0;
    job->queue = m_craftQueue;
}

void WorldDemoState::RestoreCraftingJobFromEcs() {
    m_crafting.Cancel();
    m_craftBatchTotal = 0;
    m_craftBatchCompleted = 0;
    m_craftQueue.clear();
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    const auto* job =
        m_ctx.ecs->Registry().try_get<ecs::CraftingJob>(m_player);
    if (job == nullptr || (!job->active && job->queue.empty())) {
        return;
    }
    const auto appendWarning = [this](std::string warning) {
        if (m_ctx.saveWarning.empty()) {
            m_ctx.saveWarning = std::move(warning);
        } else {
            m_ctx.saveWarning += "??" + warning;
        }
    };
    const auto cancelRestore = [&](const std::string& reason) {
        m_craftQueue.clear();
        appendWarning("存档制作任务未恢复：" + reason);
        ZL_WARN("items", "读档制作任务未恢复：{}", reason);
    };
    if (!job->active) {
        cancelRestore("存档队列缺少活跃批次");
        return;
    }
    if (m_ctx.recipes == nullptr) {
        cancelRestore("配方未加载");
        return;
    }
    const items::RecipeDef* recipe = m_ctx.recipes->Find(job->recipeId);
    if (recipe == nullptr) {
        cancelRestore("配方不存在：" + job->recipeId);
        return;
    }
    if (!RecipeKnown(recipe->id)) {
        cancelRestore("尚未学会配方：" + recipe->id);
        return;
    }
    if (job->batchTotal < 1 || job->batchTotal > ecs::kMaxCraftBatchCount ||
        job->batchCompleted < 0 || job->batchCompleted >= job->batchTotal ||
        !(job->duration > 0.0f) || job->elapsed < 0.0f ||
        job->elapsed > job->duration + 1e-4f || !(job->toolEfficiency > 0.0f) ||
        job->toolEfficiency > 1.0f) {
        cancelRestore("批次进度非法");
        return;
    }

    int dropped = 0;
    for (const ecs::CraftQueueItem& entry : job->queue) {
        if (entry.count < 1 || entry.count > ecs::kMaxCraftBatchCount ||
            m_ctx.recipes->Find(entry.recipeId) == nullptr) {
            ++dropped;
            continue;
        }
        m_craftQueue.push_back(entry);
    }
    if (dropped > 0) {
        appendWarning("存档队列有 " + std::to_string(dropped) +
                      " 项配方不存在，已跳过");
    }

    m_crafting.active = true;
    m_crafting.recipeId = job->recipeId;
    m_crafting.elapsed = job->elapsed;
    m_crafting.duration = job->duration;
    m_crafting.toolEfficiency = job->toolEfficiency;
    m_craftBatchTotal = job->batchTotal;
    m_craftBatchCompleted = job->batchCompleted;
    const std::string label = QueuedCraftLabel(recipe->id);
    SetNotice("已恢复制作：" + label + "（" +
                  std::to_string(m_craftBatchCompleted) + "/" +
                  std::to_string(m_craftBatchTotal) + "，队??" +
                  std::to_string(m_craftQueue.size()) + " 项）",
              kNoticeInfoColor, 2.5f);
    ZL_INFO("items",
            "读档恢复制作：{}（配方 {}，进度 {}/{}, 已完成 {}/{}, 队列 {} 项）",
            label, recipe->id, m_crafting.elapsed, m_crafting.duration,
            m_craftBatchCompleted, m_craftBatchTotal,
            static_cast<int>(m_craftQueue.size()));
}

void WorldDemoState::HandleInventoryAction(const ui::InventoryAction& action) {
    if (m_ctx.ui == nullptr || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr || equipment == nullptr) {
        return;
    }

    switch (action.type) {
        case ui::InventoryActionType::Transfer:
            if (action.from == ui::InventoryArea::Backpack &&
                action.to == ui::InventoryArea::Backpack) {
                if (action.fromIndex >= 0 && action.toIndex >= 0) {
                    items::MoveStack(*container, *m_ctx.items,
                                     static_cast<std::size_t>(action.fromIndex),
                                     static_cast<std::size_t>(action.toIndex));
                }
            } else if (action.from == ui::InventoryArea::Backpack &&
                       action.to == ui::InventoryArea::Equipment) {
                if (action.toIndex >= 0 && action.toIndex < ui::kInventoryEquipmentSlots) {
                    EquipBagItem(action.fromIndex,
                                 static_cast<ecs::EquipSlot>(action.toIndex));
                }
            } else if (action.from == ui::InventoryArea::Backpack &&
                       action.to == ui::InventoryArea::Loot) {
                if (FindNearbyStorage() != entt::null) {
                    StoreBagItemInStorage(action.fromIndex);
                } else {
                    DropBagItem(action.fromIndex);
                }
            } else if (action.from == ui::InventoryArea::Equipment &&
                       action.to == ui::InventoryArea::Backpack) {
                UnequipEquipment(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Equipment &&
                       action.to == ui::InventoryArea::Loot) {
                DropEquipmentItem(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Loot &&
                       action.to == ui::InventoryArea::Backpack) {
                PickupLootItem(action.fromEntityId, action.fromIndex);
            } else if (action.from == ui::InventoryArea::Loot &&
                       action.to == ui::InventoryArea::Equipment) {
                if (action.toIndex >= 0 && action.toIndex < ui::kInventoryEquipmentSlots) {
                    EquipLootItem(action.fromEntityId,
                                  static_cast<ecs::EquipSlot>(action.toIndex),
                                  action.fromIndex);
                }
            }
            break;
        case ui::InventoryActionType::Use:
            UseBagItem(action.fromIndex);
            break;
        case ui::InventoryActionType::Equip: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= container->items.size()) {
                break;
            }
            const ecs::ItemInstance& entry =
                container->items[static_cast<std::size_t>(action.fromIndex)];
            const items::ItemDef* def = m_ctx.items->Find(entry.defId);
            if (def == nullptr) {
                break;
            }
            if (def->type == items::ItemType::Weapon) {
                EquipBagItem(action.fromIndex, ecs::EquipSlot::PrimaryHand);
            } else if (def->type == items::ItemType::Clothing) {
                EquipBagItem(action.fromIndex, ecs::EquipSlot::Body);
            } else {
                SetNotice("该物品不能装??", kNoticeFailColor, 1.5f);
            }
            break;
        }
        case ui::InventoryActionType::Unequip:
            UnequipEquipment(action.fromIndex);
            break;
        case ui::InventoryActionType::Drop:
            if (action.from == ui::InventoryArea::Backpack) {
                DropBagItem(action.fromIndex);
            } else if (action.from == ui::InventoryArea::Equipment) {
                DropEquipmentItem(action.fromIndex);
            }
            break;
        case ui::InventoryActionType::Repair:
            if (action.from != ui::InventoryArea::Backpack) {
                SetNotice("只能维修背包中的工具", kNoticeFailColor, 1.8f);
                break;
            }
            RepairBagItem(action.fromIndex);
            break;
        case ui::InventoryActionType::Pickup:
            PickupLootItem(action.fromEntityId, action.fromIndex);
            break;
        case ui::InventoryActionType::Craft: {
            if (m_ctx.recipes == nullptr) {
                SetNotice("制作不可用：配方未加载", kNoticeFailColor, 2.5f);
                break;
            }
            const items::RecipeDef* recipe =
                action.fromIndex >= 0
                    ? m_ctx.recipes->At(static_cast<std::size_t>(action.fromIndex))
                    : nullptr;
            if (recipe == nullptr) {
                SetNotice("配方不存在", kNoticeFailColor, 2.0f);
                break;
            }
            if (!RecipeKnown(recipe->id)) {
                SetNotice("尚未学会该配方：阅读配方杂志", kNoticeFailColor, 2.5f);
                ZL_INFO("items", "制作被拒绝（配方 {}）：尚未学会", recipe->id);
                break;
            }
            if (!recipe->station.empty() &&
                (position == nullptr ||
                 !StationNearby(recipe->station, position->x, position->y,
                                position->z))) {
                SetNotice("需要附??" + StationLabel(recipe->station),
                          kNoticeFailColor, 2.5f);
                ZL_INFO("items", "制作被拒绝（配方 {}）：需要附近{}", recipe->id,
                        StationLabel(recipe->station));
                break;
            }
            const int requested = std::clamp(action.count, 1, 99);
            const items::ItemDef* output = m_ctx.items->Find(recipe->outputDefId);
            const std::string label =
                output != nullptr ? output->name : recipe->outputDefId;
            if (m_crafting.active || !m_craftQueue.empty()) {
                if (static_cast<int>(m_craftQueue.size()) >= ecs::kMaxCraftQueueEntries) {
                    SetNotice("制作队列已满（最多 " +
                                  std::to_string(ecs::kMaxCraftQueueEntries) + " 项）",
                              kNoticeFailColor, 2.5f);
                    break;
                }
                m_craftQueue.push_back(ecs::CraftQueueItem{recipe->id, requested});
                SetNotice("已加入制作队列（??" +
                              std::to_string(m_craftQueue.size()) + " 项）??" +
                              label + " ×" +
                              std::to_string(recipe->outputCount) + "（请??" +
                              std::to_string(requested) + " 件）",
                          kNoticeInfoColor, 2.5f);
                ZL_INFO("items",
                        "加入制作队列：{} ×{}（配方 {}，请求 {} 件，队列 {} 项）",
                        label, recipe->outputCount, recipe->id, requested,
                        static_cast<int>(m_craftQueue.size()));
                if (!m_crafting.active) {
                    StartNextQueuedCraft({});
                }
                break;
            }
            std::string error;
            int actual = 0;
            if (!StartCraftBatch(*recipe, requested, error, actual)) {
                SetNotice(error, kNoticeFailColor, 2.5f);
                ZL_INFO("items", "制作被拒绝（配方 {}）：{}", recipe->id, error);
                break;
            }
            if (requested > 1) {
                std::string notice =
                    (actual > 1 ? "开始批量制作：" : "开始制作：") + label + " ×" +
                    std::to_string(recipe->outputCount) + "（共 " +
                    std::to_string(actual) + " ??";
                if (actual < requested) {
                    notice += "；请??" + std::to_string(requested) +
                              " 件，已按材料/空间截断";
                }
                notice += "??";
                SetNotice(notice, kNoticeInfoColor, 2.5f);
            } else {
                SetNotice("开始制作：" + label + " ×" +
                              std::to_string(recipe->outputCount),
                          kNoticeInfoColor, 1.5f);
            }
            ZL_INFO("items",
                    "开始制作：{} ×{}（配方 {}，请求 {} 件，实际 {} 件，耗时 {:.2f}s，"
                    "工具效率 {:.0f}%）",
                    label, recipe->outputCount, recipe->id, requested, actual,
                    m_crafting.duration, m_crafting.toolEfficiency * 100.0f);
            break;
        }
        case ui::InventoryActionType::CancelCraft: {
            if (!m_crafting.active && m_craftQueue.empty()) {
                SetNotice("没有正在制作的项目", kNoticeFailColor, 1.5f);
                break;
            }
            const std::string recipeId = m_crafting.recipeId;
            const int completed = m_craftBatchCompleted;
            const int total = m_craftBatchTotal;
            const int queued = static_cast<int>(m_craftQueue.size());
            m_crafting.Cancel();
            m_craftBatchTotal = 0;
            m_craftBatchCompleted = 0;
            m_craftQueue.clear();
            if (queued > 0) {
                if (completed > 0) {
                    SetNotice("已取消制作队列（当前已完??" +
                                  std::to_string(completed) + "/" +
                                  std::to_string(total) + "，队??" +
                                  std::to_string(queued) + " 项已清空）",
                              kNoticeInfoColor, 2.8f);
                } else {
                    SetNotice("已取消制作队列（队列 " + std::to_string(queued) +
                                  " 项已清空，材料未消耗）",
                              kNoticeInfoColor, 2.5f);
                }
            } else if (completed > 0) {
                SetNotice("已取消批量制作（已完成 " + std::to_string(completed) +
                              "/" + std::to_string(total) + "，当前材料未消耗）",
                          kNoticeInfoColor, 2.5f);
            } else {
                SetNotice("已取消制作（材料未消耗）", kNoticeInfoColor, 2.0f);
            }
            ZL_INFO("items",
                    "取消制作（配方 {}，已完成 {}/{}，清空队列 {} 项）", recipeId,
                    completed, total, queued);
            break;
        }
        case ui::InventoryActionType::QueueMoveUp:
        case ui::InventoryActionType::QueueMoveDown: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= m_craftQueue.size()) {
                SetNotice("队列项不存在", kNoticeFailColor, 1.5f);
                break;
            }
            const bool moveUp = action.type == ui::InventoryActionType::QueueMoveUp;
            const int delta = moveUp ? -1 : 1;
            const int target = action.fromIndex + delta;
            if (target < 0) {
                SetNotice("队列项已在队尾", kNoticeFailColor, 1.5f);
                break;
            }
            if (target >= static_cast<int>(m_craftQueue.size())) {
                SetNotice("队列项已在队尾", kNoticeFailColor, 1.5f);
                break;
            }
            const std::string label =
                QueuedCraftLabel(m_craftQueue[static_cast<std::size_t>(action.fromIndex)]
                                     .recipeId);
            std::swap(m_craftQueue[static_cast<std::size_t>(action.fromIndex)],
                      m_craftQueue[static_cast<std::size_t>(target)]);
            const std::string direction = moveUp ? "上移" : "下移";
            SetNotice("??" + direction + "队列??" +
                          std::to_string(action.fromIndex + 1) + " 项：" + label +
                          "（现为第 " + std::to_string(target + 1) + " 项）",
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("items", "制作队列{}：{} 第 {} 项 -> 第 {} 项（队列 {} 项）",
                    direction, label, action.fromIndex + 1, target + 1,
                    static_cast<int>(m_craftQueue.size()));
            break;
        }
        case ui::InventoryActionType::QueueRemove: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= m_craftQueue.size()) {
                SetNotice("队列项不存在", kNoticeFailColor, 1.5f);
                break;
            }
            const ecs::CraftQueueItem removed =
                m_craftQueue[static_cast<std::size_t>(action.fromIndex)];
            const std::string label = QueuedCraftLabel(removed.recipeId);
            m_craftQueue.erase(m_craftQueue.begin() + action.fromIndex);
            SetNotice("已移除队列第 " + std::to_string(action.fromIndex + 1) +
                          " 项：" + label + "（请??" +
                          std::to_string(removed.count) + " 件）",
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("items", "移除制作队列第 {} 项：{}（请求 {} 件，剩余 {} 项）",
                    action.fromIndex + 1, label, removed.count,
                    static_cast<int>(m_craftQueue.size()));
            break;
        }
        case ui::InventoryActionType::QueueSetCount: {
            if (action.fromIndex < 0 ||
                static_cast<std::size_t>(action.fromIndex) >= m_craftQueue.size()) {
                SetNotice("队列项不存在", kNoticeFailColor, 1.5f);
                break;
            }
            ecs::CraftQueueItem& entry =
                m_craftQueue[static_cast<std::size_t>(action.fromIndex)];
            const int count = std::clamp(action.count, 1, 99);
            if (entry.count == count) {
                break;
            }
            const int previous = entry.count;
            entry.count = count;
            const std::string label = QueuedCraftLabel(entry.recipeId);
            SetNotice("队列??" + std::to_string(action.fromIndex + 1) +
                          " 项数量：" + std::to_string(previous) + " ??" +
                          std::to_string(count) + " 件（" + label + "??",
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("items",
                    "调整制作队列第 {} 项数量：{} 请求 {} -> {} 件（队列 {} 项）",
                    action.fromIndex + 1, label, previous, count,
                    static_cast<int>(m_craftQueue.size()));
            break;
        }
        case ui::InventoryActionType::StoreAll:
            StoreAllBagInStorage();
            break;
        case ui::InventoryActionType::TakeAll:
            TakeAllFromStorage(action.fromEntityId);
            break;
        case ui::InventoryActionType::SortStorage:
            SortNearbyStorage();
            break;
    }
    RefreshInventory();
}

void WorldDemoState::UpdateCrafting(float tickDt) {
    if (!m_crafting.active || !m_crafting.Advance(tickDt)) {
        return;
    }
    const std::string recipeId = m_crafting.recipeId;
    m_crafting.Cancel();
    const auto finishFailure = [this](const std::string& reason) {
        const int completed = m_craftBatchCompleted;
        const int total = m_craftBatchTotal;
        const int queued = static_cast<int>(m_craftQueue.size());
        m_craftBatchTotal = 0;
        m_craftBatchCompleted = 0;
        m_craftQueue.clear();
        const std::string queueSuffix =
            queued > 0 ? "；队列 " + std::to_string(queued) + " 项已清空"
                       : std::string();
        if (completed > 0) {
            SetNotice("批量制作中断：已完成 " + std::to_string(completed) + "/" +
                          std::to_string(total) + "??" + reason + "??" +
                          queueSuffix,
                      kNoticeFailColor, 3.0f);
            ZL_INFO("items",
                    "批量制作中断（已完成 {}/{}，清空队列 {} 项）：{}", completed,
                    total, queued, reason);
        } else {
            SetNotice("制作失败：" + reason + queueSuffix, kNoticeFailColor, 2.5f);
            ZL_INFO("items", "制作失败（清空队列 {} 项）：{}", queued, reason);
        }
    };
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_ctx.recipes == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        const int queued = static_cast<int>(m_craftQueue.size());
        m_craftBatchTotal = 0;
        m_craftBatchCompleted = 0;
        m_craftQueue.clear();
        ZL_WARN("items", "制作中断：玩家或注册表不可用（配方 {}，清空队列 {} 项）",
                recipeId, queued);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    const items::RecipeDef* recipe = m_ctx.recipes->Find(recipeId);
    if (container == nullptr || recipe == nullptr) {
        finishFailure("配方或背包不可用");
        return;
    }
    if (!RecipeKnown(recipe->id)) {
        finishFailure("尚未学会配方");
        return;
    }
    if (!recipe->station.empty() &&
        (position == nullptr ||
         !StationNearby(recipe->station, position->x, position->y, position->z))) {
        finishFailure("已离开" + StationLabel(recipe->station));
        return;
    }
    const items::CraftResult result = items::Craft(*container, *m_ctx.items, *recipe);
    if (!result.ok) {
        finishFailure(result.error);
        return;
    }

    const int total = std::max(m_craftBatchTotal, 1);
    const int completed = ++m_craftBatchCompleted;
    const items::ItemDef* output = m_ctx.items->Find(result.outputDefId);
    const std::string label = output != nullptr ? output->name : result.outputDefId;
    std::string brokenTools;
    for (const std::string& defId : result.brokenToolDefIds) {
        const items::ItemDef* def = m_ctx.items->Find(defId);
        if (!brokenTools.empty()) {
            brokenTools += "??";
        }
        brokenTools += def != nullptr ? def->name : defId;
    }
    const std::string brokenSuffix =
        brokenTools.empty() ? std::string() : "??" + brokenTools + "已损??";
    GrantProgressionXp(
        ecs::SkillKind::Crafting,
        m_progressionConfig.craftXpPerItem *
            static_cast<float>(std::max(1, result.outputCount)),
        "制作");
    if (completed < total) {
        std::string reason;
        if (!RecipeKnown(recipe->id)) {
            reason = "尚未学会配方";
        } else if (!recipe->station.empty() &&
                   (position == nullptr ||
                    !StationNearby(recipe->station, position->x, position->y,
                                   position->z))) {
            reason = "已离开" + StationLabel(recipe->station);
        } else if (!items::CanCraft(*container, *m_ctx.items, *recipe, &reason)) {
            // reason 已由 CanCraft 填充
        }
        if (!reason.empty()) {
            finishFailure(reason);
            return;
        }
        const float efficiency = items::ToolEfficiency(*container, *recipe);
        const float seconds = CraftSecondsForPlayer(*recipe, efficiency);
        m_crafting.Start(recipe->id, seconds, efficiency);
        SetNotice("制作中：" + label + "??" + std::to_string(completed + 1) + "/" +
                      std::to_string(total) + "??" + brokenSuffix,
                  kNoticeInfoColor, 1.5f);
        ZL_INFO("items", "批量制作继续：{}（已完成 {}/{}，耗时 {:.2f}s，效率 {:.0f}%）",
                label, completed, total, seconds, efficiency * 100.0f);
        return;
    }

    m_craftBatchTotal = 0;
    m_craftBatchCompleted = 0;
    const std::string completion =
        total > 1
            ? "批量制作完成：" + label + " ×" +
                  std::to_string(result.outputCount * total) + "（共 " +
                  std::to_string(total) + " 件）" + brokenSuffix
            : "制作成功：" + label + " ×" +
                  std::to_string(result.outputCount) + brokenSuffix;
    if (brokenTools.empty()) {
        ZL_INFO("items", "制作成功：{} ×{}（配方 {}，批次 {}/{}）", label,
                result.outputCount, recipe->id, completed, total);
    } else {
        ZL_INFO("items", "制作成功：{} ×{}（配方 {}，批次 {}/{}）；工具损坏：{}",
                label, result.outputCount, recipe->id, completed, total, brokenTools);
    }
    if (!m_craftQueue.empty()) {
        StartNextQueuedCraft(completion + "??");
        return;
    }
    SetNotice(completion, kNoticeOkColor, total > 1 ? 3.0f : 2.5f);
}

void WorldDemoState::HandleClickToMove(const render::GridPick& pick) {
    if (m_ctx.ecs == nullptr || m_player == entt::null || !pick.hit) {
        return;  // 玩家未生??/ 拾取无效
    }
    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::MoveTo);
        command.payload.x = static_cast<float>(pick.x);
        command.payload.y = static_cast<float>(pick.y);
        if (SubmitPlayerCommand(command)) {
            m_markerX = pick.x;
            m_markerY = pick.y;
            m_markerTimer = 3.0f;
            m_markerColor = kMarkerActiveColor;
            SetNotice("pathfinding on server...", kNoticeInfoColor, 1.2f);
        }
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* request = registry.try_get<ecs::PathRequest>(m_player);
    if (request == nullptr) {
        ZL_WARN("game", "点击移动：玩家缺少 PathRequest 组件（内部错误）");
        return;
    }

    int targetX = pick.x;
    int targetY = pick.y;

    // 1) 地图范围校验（chunk 边界闭区间）
    const world::MapLoader::Meta& meta = m_ctx.chunkManager->Map();
    const world::ChunkCoord targetChunk = world::ChunkOf(targetX, targetY);
    const bool insideMap = meta.chunksX > 0 && meta.chunksY > 0 &&
                           targetChunk.cx >= meta.chunkX0 &&
                           targetChunk.cx < meta.chunkX0 + meta.chunksX &&
                           targetChunk.cy >= meta.chunkY0 &&
                           targetChunk.cy < meta.chunkY0 + meta.chunksY;
    if (!insideMap) {
        SetNotice("out of map bounds", kNoticeFailColor, 2.5f);
        ZL_INFO("game", "点击移动：目标格 ({}, {}) 超出地图（忽略）", targetX, targetY);
        return;
    }

    // 2) chunk 已加载校验（未加载 = 通行未知，不发起）
    const world::Chunk* chunk = FindChunk(targetChunk.cx, targetChunk.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        SetNotice("target chunk not loaded", kNoticeFailColor, 2.5f);
        ZL_INFO("game", "点击移动：目标格 ({}, {}) 所在 chunk 未加载（忽略）", targetX, targetY);
        return;
    }

    // 3) 目标格校验：阻挡 → 就近可走格（Chebyshev 环 ≤4；仍找不到 = 不可达，直接反馈）
    bool adjusted = false;
    if (IsSolidSquare(targetX, targetY)) {
        int nearX = 0;
        int nearY = 0;
        const int clickedX = targetX;
        const int clickedY = targetY;
        const bool found = ai::FindNearestWalkable(
            targetX, targetY, 4,
            [this](int wx, int wy) { return !IsSolidSquare(wx, wy); }, &nearX, &nearY);
        if (!found) {
            SetNotice("unreachable: target (" + std::to_string(targetX) + "," +
                          std::to_string(targetY) + ")",
                      kNoticeFailColor, 3.0f);
            m_markerX = targetX;
            m_markerY = targetY;
            m_markerTimer = 3.0f;
            m_markerColor = kMarkerFailColor;
            ZL_INFO("game", "点击移动：目标格 ({}, {}) 附近无可行格（不可达反馈）", targetX,
                    targetY);
            return;
        }
        adjusted = true;
        targetX = nearX;
        targetY = nearY;
        ZL_INFO("game", "点击移动：目标格 ({}, {}) 阻挡 → 就近可走格 ({}, {})", clickedX,
                clickedY, targetX, targetY);
    }

    // 4) 写 PathRequest（PathFollowSystem 下一 tick 提交后台查询）
    request->goalX = targetX;
    request->goalY = targetY;
    request->newRequest = true;
    SetNotice(adjusted ? "adjusted target; pathfinding..." : "pathfinding...", kNoticeInfoColor,
              1.2f);
    ZL_INFO("game", "点击移动：玩????目标??({}, {})", targetX, targetY);
}

void WorldDemoState::UpdatePathFeedback(double frameDt) {
    if (m_noticeTimer > 0.0f) {
        m_noticeTimer = std::max(0.0f, m_noticeTimer - static_cast<float>(frameDt));
    }
    if (m_markerTimer > 0.0f) {
        m_markerTimer = std::max(0.0f, m_markerTimer - static_cast<float>(frameDt));
    }
    m_markerPathActive = false;
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    const auto* request = m_ctx.ecs->Registry().try_get<ecs::PathRequest>(m_player);
    if (request == nullptr) {
        return;
    }
    const ecs::PathStatus state = request->state;
    if (state != m_lastPlayerPathState) {
        if (state == ecs::PathStatus::Arrived) {
            SetNotice("arrived", kNoticeOkColor, 1.5f);
            m_markerX = request->goalX;
            m_markerY = request->goalY;
            m_markerTimer = 1.5f;
            m_markerColor = kMarkerArriveColor;
            if (m_ctx.events != nullptr) {  // M7-T4：OnPlayerMove（点击移动到达路点；排队派发）
                const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
                const float px = position != nullptr ? position->x : 0.0f;
                const float py = position != nullptr ? position->y : 0.0f;
                const int pz = position != nullptr ? position->z : 0;
                m_ctx.events->EmitQueued(
                    core::kOnPlayerMove,
                    core::PlayerEvent{m_ctx.ecs->IdOf(m_player).id, px, py, pz});
            }
        } else if (state == ecs::PathStatus::Failed) {
            SetNotice("unreachable: target (" + std::to_string(request->goalX) + "," +
                          std::to_string(request->goalY) + ")",
                      kNoticeFailColor, 3.0f);
            m_markerX = request->goalX;
            m_markerY = request->goalY;
            m_markerTimer = 3.0f;
            m_markerColor = kMarkerFailColor;
        }
        m_lastPlayerPathState = state;
    }
    if (state == ecs::PathStatus::Pending || state == ecs::PathStatus::Following) {
        m_markerX = request->goalX;  // 寻路??跟随中：黄色目标标记常显
        m_markerY = request->goalY;
        m_markerPathActive = true;
        m_markerColor = kMarkerActiveColor;
    }
}

void WorldDemoState::SpawnDemoEntities() {
    if (m_ctx.simulation != nullptr) {
        m_ctx.simulation->SpawnDemoPopulation();
    }
}

// M5-T2 临时"感知"桩已删除：M5-T3 起由 ai::PerceptionSystem（视锥 + 视线 + 听觉）接管。
void WorldDemoState::KillNearestDemoEntity() {
    if (m_networkMode || m_ctx.ecs == nullptr) {
        return;  // 网络客户端不能直接修改权威世界
    }
    entt::registry& registry = m_ctx.ecs->Registry();

    // 参考点：玩家（未生成时用相机中心）
    glm::vec2 ref = render::FromScreen(m_ctx.camera->Center(), 0);
    if (m_player != entt::null) {
        if (const auto* playerPos = registry.try_get<ecs::Position>(m_player)) {
            ref = {playerPos->x, playerPos->y};
        }
    }

    ecs::Entity best = entt::null;
    float bestDistSq = 0.0f;
    registry.view<ecs::AiState, ecs::Health, ecs::Position>().each(
        [&](const ecs::Entity e, const ecs::AiState&, const ecs::Health& health,
            const ecs::Position& pos) {
            if (!m_ctx.ecs->Alive(e) || health.current <= 0.0f) {
                return;  // continue
            }
            const float dx = pos.x - ref.x;
            const float dy = pos.y - ref.y;
            const float distSq = dx * dx + dy * dy;
            if (best == entt::null || distSq < bestDistSq) {
                best = e;
                bestDistSq = distSq;
            }
        });
    if (best == entt::null) {
        SetNotice("demo kill: no live target nearby", kNoticeFailColor, 2.0f);
        return;
    }
    // M5-T6：改走统一伤害管线（硬直/击退/闪红 + 致死交给 DeathSystem 的倒地窗口）
    const auto* pos = registry.try_get<ecs::Position>(best);
    const std::uint64_t id = m_ctx.ecs->IdOf(best).id;
    if (m_ctx.combat != nullptr) {
        ecs::DamageEvent event;
        event.sourceId = m_player != entt::null ? m_ctx.ecs->IdOf(m_player).id : 0;
        event.type = ecs::DamageType::Debug;
        event.amount = 999.0f;
        float dx = pos != nullptr ? pos->x - ref.x : 1.0f;
        float dy = pos != nullptr ? pos->y - ref.y : 1.0f;
        const float len = std::sqrt(dx * dx + dy * dy);
        event.dirX = len > 0.0001f ? dx / len : 0.7071f;
        event.dirY = len > 0.0001f ? dy / len : 0.7071f;
        event.knockback = 2.5f;
        event.staggerSeconds = 0.35f;
        m_ctx.combat->ApplyDamage(*m_ctx.ecs, best, event);
    } else {
        registry.get<ecs::Health>(best).current = 0.0f;  // 无战斗系统（降级）：直接归零
    }
    SetNotice("demo kill: id " + std::to_string(id) + " -> corpse", kNoticeInfoColor, 2.5f);
    ZL_INFO("characters", "演示击杀：稳定 ID {}（格 ({:.1f}, {:.1f})）伤害管线致死（F6）", id,
            pos != nullptr ? pos->x : 0.0f, pos != nullptr ? pos->y : 0.0f);
}

void WorldDemoState::EmitGunshot() {
    if (m_ctx.ecs == nullptr || m_ctx.events == nullptr || m_player == entt::null) {
        SetNotice("gunshot: player not spawned", kNoticeFailColor, 2.0f);
        return;
    }
    const auto* playerPos = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (playerPos == nullptr) {
        return;
    }
    core::SoundEvent shot;
    shot.x = playerPos->x;
    shot.y = playerPos->y;
    shot.radius = m_gunshotRadius;
    shot.type = core::SoundType::Gunshot;
    // M5-T4/M11-T4：只发 OnSound；感知系统与音频系统同时消费（枪声可闻 + 引怪）。
    if (m_ctx.audio != nullptr) {
        m_ctx.audio->EmitWorldSound(shot, audio::kGunshot);
    } else {
        shot.clip = audio::kGunshot;
        m_ctx.events->EmitQueued(core::kOnSound, shot);
    }
    SetNotice("gunshot! r=" + std::to_string(static_cast<int>(m_gunshotRadius)) +
                  " (walls attenuate)",
              kNoticeInfoColor, 2.0f);
    ZL_INFO("ai", "枪声（F7）：声源 ({:.1f}, {:.1f}) 基础半径 {:.0f} 格（隔墙逐格衰减）", shot.x,
            shot.y, shot.radius);
}

void WorldDemoState::PlayerAttack(std::uint64_t targetStableId, bool heavy) {
    if (m_ctx.ecs == nullptr || m_ctx.combat == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* attack = registry.try_get<ecs::AttackIntent>(m_player);
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (attack == nullptr || playerPos == nullptr) {
        return;
    }
    const char* attackLabel = heavy ? "heavy attack" : "attack";

    if (m_driving || registry.all_of<ecs::VehicleOccupant>(m_player)) {
        SetNotice(std::string(attackLabel) + ": cannot attack while driving",
                  kNoticeFailColor, 1.2f);
        return;
    }
    if (const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
        hit != nullptr && std::isfinite(hit->staggerLeft) &&
        hit->staggerLeft > 0.0f) {
        SetNotice(std::string(attackLabel) + ": staggered", kNoticeFailColor, 1.0f);
        return;
    }
    if (const auto* stance = registry.try_get<ecs::CombatStance>(m_player);
        stance != nullptr &&
        (stance->blocking ||
         (std::isfinite(stance->guardBreakLeft) && stance->guardBreakLeft > 0.0f) ||
         (std::isfinite(stance->dodgeLeft) && stance->dodgeLeft > 0.0f))) {
        SetNotice(std::string(attackLabel) + ": stance locked",
                  kNoticeFailColor, 1.0f);
        return;
    }
    if (m_attackCooldownLeft > 0.0f) {
        SetNotice(std::string(attackLabel) + ": cooldown", kNoticeFailColor, 1.0f);
        return;
    }
    if (attack->pending) {
        SetNotice(std::string(attackLabel) + ": already swinging",
                  kNoticeFailColor, 1.0f);
        return;
    }

    // M15-T1：主手武器决定攻击参数；空手/未装备/无 combat 的旧数据走 config 默认值。
    // 武器耐久耗尽后拒绝攻击，需卸下/更换（维修包仍只修 (0,1) 区间）。
    const items::ItemDef* weaponDef = nullptr;
    const ecs::ItemInstance* weapon = nullptr;
    if (const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
        equipment != nullptr) {
        const auto& slot =
            equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
        if (slot.has_value()) {
            weapon = &*slot;
            if (!(weapon->condition > 0.0f)) {
                SetNotice(std::string(attackLabel) +
                              ": weapon broken, unequip or switch",
                          kNoticeFailColor, 1.8f);
                return;
            }
            if (m_ctx.items != nullptr) {
                weaponDef = m_ctx.items->Find(weapon->defId);
            }
        }
    }
    items::WeaponAttackParams base;
    base.damage = m_playerAttackDamage;
    base.range = m_playerAttackRange;
    base.arcDegrees = m_playerAttackArc;
    base.cooldownSeconds = m_playerAttackCooldown;
    base.knockback = m_playerAttackKnockback;
    base.staggerSeconds = m_playerAttackStagger;
    base.wearPerHit = m_playerAttackWear;
    const game::PlayerMeleeAttackPlan plan = game::ResolvePlayerMeleeAttack(
        weaponDef, base, heavy, m_ctx.combat->GetCombatRules());
    items::WeaponAttackParams params = plan.params;
    float progressionCooldownScale = 1.0f;
    if (const ecs::Progression* progression = PlayerProgression();
        progression != nullptr) {
        params.damage *=
            game::MeleeDamageMultiplier(*progression, m_progressionConfig);
        progressionCooldownScale =
            game::MeleeCooldownMultiplier(*progression, m_progressionConfig);
    }

    auto* stamina = registry.try_get<ecs::CombatStamina>(m_player);
    if (stamina == nullptr) {
        const auto* needs = registry.try_get<ecs::Needs>(m_player);
        const float staminaMax = characters::EffectiveMaxStamina(
            needs != nullptr ? needs->fatigue : 0.0f,
            m_ctx.combat->GetCombatRules());
        stamina = &registry.emplace<ecs::CombatStamina>(
            m_player, ecs::CombatStamina{staminaMax, staminaMax, 0.0f, 0.0f});
    }
    const characters::CombatRuleConfig& rules = m_ctx.combat->GetCombatRules();
    if (stamina->exhausted > 0.0f) {
        char text[96];
        std::snprintf(text, sizeof(text), "%s: exhausted (%.1fs)", attackLabel,
                      static_cast<double>(stamina->exhausted));
        SetNotice(text, kNoticeFailColor, 1.2f);
        return;
    }
    if (characters::StaminaExhausted(stamina->current, rules)) {
        SetNotice(std::string(attackLabel) + ": exhausted", kNoticeFailColor, 1.2f);
        return;
    }
    if (!(stamina->current >= params.staminaCost)) {
        char text[128];
        std::snprintf(text, sizeof(text), "%s: not enough stamina (need %.0f, have %.0f)",
                      attackLabel, static_cast<double>(params.staminaCost),
                      static_cast<double>(stamina->current));
        SetNotice(text, kNoticeFailColor, 1.2f);
        return;
    }

    // M12-T1：命令可携带目标 StableId；缺省仍自动瞄准最近僵尸。
    // M30-T3：无目标时按玩家朝向空挥，仍扣体??冷却并完成挥击动作??
    constexpr float kAutoAimRange = 3.0f;
    ecs::Entity best = entt::null;
    float bestDistSq = kAutoAimRange * kAutoAimRange;
    if (targetStableId != 0) {
        best = m_ctx.ecs->FindById(targetStableId);
        const auto* targetPos = best != entt::null
                                    ? registry.try_get<ecs::Position>(best)
                                    : nullptr;
        const auto* health = best != entt::null ? registry.try_get<ecs::Health>(best)
                                                : nullptr;
        const bool attackable =
            best != entt::null &&
            (registry.all_of<ecs::AiState>(best) ||
             registry.all_of<ecs::Animal>(best));
        if (best == entt::null || best == m_player || !attackable ||
            targetPos == nullptr || health == nullptr ||
            health->current <= 0.0f) {
            SetNotice(std::string(attackLabel) + ": target unavailable",
                      kNoticeFailColor, 1.5f);
            return;
        }
        const float dx = targetPos->x - playerPos->x;
        const float dy = targetPos->y - playerPos->y;
        bestDistSq = dx * dx + dy * dy;
        if (bestDistSq > kAutoAimRange * kAutoAimRange) {
            SetNotice(std::string(attackLabel) + ": target out of range",
                      kNoticeFailColor, 1.5f);
            return;
        }
    } else {
        registry.view<ecs::Health, ecs::Position>().each(
            [&](const ecs::Entity e, const ecs::Health& health,
                const ecs::Position& pos) {
                if (!m_ctx.ecs->Alive(e) || health.current <= 0.0f ||
                    (!registry.all_of<ecs::AiState>(e) &&
                     !registry.all_of<ecs::Animal>(e))) {
                    return;  // continue
                }
                const float dx = pos.x - playerPos->x;
                const float dy = pos.y - playerPos->y;
                const float distSq = dx * dx + dy * dy;
                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    best = e;
                }
            });
    }

    float dx = 1.0f;
    float dy = 0.0f;
    float targetDistance = 0.0f;
    if (best != entt::null) {
        const auto* targetPos = registry.try_get<ecs::Position>(best);
        dx = targetPos->x - playerPos->x;
        dy = targetPos->y - playerPos->y;
        const float len = std::sqrt(dx * dx + dy * dy);
        targetDistance = len;
        if (len > 0.0001f) {
            dx /= len;
            dy /= len;
        } else {
        dx = 1.0f;  // 完全重叠：给个确定方向
            dy = 0.0f;
        }
    } else if (const auto* facing = registry.try_get<ecs::Facing>(m_player);
               facing != nullptr) {
        game::FacingAttackDirection(facing->dir, &dx, &dy);
    }

    if (!m_ctx.combat->TrySpendStamina(*m_ctx.ecs, m_player, params.staminaCost)) {
        SetNotice(std::string(attackLabel) + ": stamina unavailable",
                  kNoticeFailColor, 1.2f);
        return;
    }
    attack->targetId = best != entt::null ? m_ctx.ecs->IdOf(best).id : 0;
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
    attack->pending = true;  // 下一 tick CombatSystem 推进前摇并结??
    float cooldownSeconds = std::max(0.0f, params.cooldownSeconds);
    if (const auto* pain = registry.try_get<ecs::Pain>(m_player); pain != nullptr) {
        cooldownSeconds *= std::clamp(pain->attackCooldownScale, 1.0f, 3.0f);
    }
    cooldownSeconds *= progressionCooldownScale;
    m_attackCooldownLeft = cooldownSeconds;
    if (auto* facing = registry.try_get<ecs::Facing>(m_player); facing != nullptr) {
        facing->dir = characters::AnimationSystem::FacingFromVelocity(dx, dy);  // 挥击朝向反馈
    }
    const std::string weaponName =
        weaponDef != nullptr ? weaponDef->name : std::string("fists");
    std::string notice = std::string(attackLabel) + ": " + weaponName + " -> " +
                         (best != entt::null
                              ? "id " + std::to_string(attack->targetId)
                              : std::string("air"));
    if (stamina->exhausted > 0.0f) {
        notice += " (exhausted)";
    } else if (stamina->max > 0.0f &&
               stamina->current <= stamina->max * 0.30f) {
        notice += " (low stamina)";
    }
    SetNotice(notice, kNoticeInfoColor, 1.2f);
    ZL_INFO("characters",
            "玩家挥击（{}）：武器 {}（耐久 {:.0f}%），目标 {}，距??{:.2f} 格，"
            "伤害 {:.0f}，范??{:.2f}，前??{:.2f}s，体??{:.0f}/{:.0f}，冷??{:.2f}s",
            heavy ? "LSHIFT 重击" : "SPACE 轻击", weaponName,
            weapon != nullptr ? weapon->condition * 100.0f : 100.0f,
            best != entt::null ? std::to_string(attack->targetId) : std::string("空挥"),
            static_cast<double>(targetDistance), params.damage, params.range,
            static_cast<double>(attack->windupSeconds),
            static_cast<double>(stamina->current),
            static_cast<double>(stamina->max),
            static_cast<double>(cooldownSeconds));
}

void WorldDemoState::PlayerBlock(bool down, bool announce) {
    if (m_ctx.ecs == nullptr || m_ctx.combat == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* stance = registry.try_get<ecs::CombatStance>(m_player);
    if (!down) {
        if (stance != nullptr && stance->blocking) {
            stance->blocking = false;
            stance->blockElapsed = 0.0f;
            ZL_INFO("characters", "玩家松开格挡");
        }
        return;
    }
    if (m_driving || registry.all_of<ecs::VehicleOccupant>(m_player)) {
        if (announce) {
            SetNotice("格挡：驾驶中不可??", kNoticeFailColor, 1.0f);
        }
        return;
    }
    if (stance != nullptr && stance->blocking) {
        return;
    }
    if (m_sleep.Active() || m_sleepSelecting || m_forageAction.active ||
        m_fishingAction.active || m_animalHarvestAction.active) {
        return;
    }
    if (const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
        hit != nullptr && std::isfinite(hit->staggerLeft) &&
        hit->staggerLeft > 0.0f) {
        if (announce) {
            SetNotice("格挡：硬直中", kNoticeFailColor, 1.0f);
        }
        return;
    }
    if (stance != nullptr && std::isfinite(stance->guardBreakLeft) &&
        stance->guardBreakLeft > 0.0f) {
        if (announce) {
            SetNotice("格挡：破防恢复中", kNoticeFailColor, 1.2f);
        }
        return;
    }
    if (stance != nullptr && std::isfinite(stance->dodgeLeft) &&
        stance->dodgeLeft > 0.0f) {
        if (announce) {
            SetNotice("格挡：闪避中", kNoticeFailColor, 1.0f);
        }
        return;
    }
    if (const auto* attack = registry.try_get<ecs::AttackIntent>(m_player);
        attack != nullptr && attack->pending) {
        if (announce) {
            SetNotice("格挡：攻击中", kNoticeFailColor, 1.0f);
        }
        return;
    }
    auto* stamina = registry.try_get<ecs::CombatStamina>(m_player);
    if (stamina == nullptr) {
        if (announce) {
            SetNotice("格挡：体力状态未就绪", kNoticeFailColor, 1.2f);
        }
        return;
    }
    const characters::CombatRuleConfig& rules = m_ctx.combat->GetCombatRules();
    if (stamina->exhausted > 0.0f ||
        characters::StaminaExhausted(stamina->current, rules)) {
        if (announce) {
            SetNotice("格挡：体力不??", kNoticeFailColor, 1.2f);
        }
        return;
    }

    const items::ItemDef* weaponDef = nullptr;
    if (const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
        equipment != nullptr) {
        const auto& slot =
            equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
        if (slot.has_value() && slot->condition > 0.0f &&
            m_ctx.items != nullptr) {
            weaponDef = m_ctx.items->Find(slot->defId);
        }
    }
    const game::PlayerBlockPlan plan = game::ResolvePlayerBlock(weaponDef);
    if (stance == nullptr) {
        stance = &registry.emplace<ecs::CombatStance>(m_player);
    }
    stance->blocking = true;
    stance->blockElapsed = 0.0f;
    stance->blockReduction = plan.blockReduction;
    stance->blockStaminaCost = plan.blockStaminaCost;
    if (announce) {
        SetNotice("格挡", kNoticeOkColor, 0.8f);
    }
    ZL_INFO("characters", "玩家格挡：武??{}，伤害倍率 {:.0f}%，体力消??{}",
            weaponDef != nullptr ? weaponDef->name : std::string("fists"),
            static_cast<double>(
                characters::BlockDamageScale(
                    plan.blockReduction >= 0.0f
                        ? std::optional<float>(plan.blockReduction)
                        : std::nullopt,
                    rules) *
                100.0f),
            plan.blockStaminaCost >= 0.0f
                ? std::to_string(static_cast<int>(plan.blockStaminaCost))
                : std::string("默认"));
}

void WorldDemoState::PlayerDodge(float screenX, float screenY) {
    if (m_ctx.ecs == nullptr || m_ctx.combat == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (m_driving || registry.all_of<ecs::VehicleOccupant>(m_player)) {
        SetNotice("闪避：驾驶中不可??", kNoticeFailColor, 1.0f);
        return;
    }
    if (m_sleep.Active() || m_sleepSelecting || m_forageAction.active ||
        m_fishingAction.active || m_animalHarvestAction.active) {
        return;
    }
    auto* stance = registry.try_get<ecs::CombatStance>(m_player);
    if (stance == nullptr) {
        stance = &registry.emplace<ecs::CombatStance>(m_player);
    }
    if (const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
        hit != nullptr && std::isfinite(hit->staggerLeft) &&
        hit->staggerLeft > 0.0f) {
        SetNotice("闪避：硬直中", kNoticeFailColor, 1.0f);
        return;
    }
    if (std::isfinite(stance->guardBreakLeft) && stance->guardBreakLeft > 0.0f) {
        SetNotice("闪避：破防恢复中", kNoticeFailColor, 1.2f);
        return;
    }
    if (stance->blocking) {
        SetNotice("闪避：先松开格挡", kNoticeFailColor, 1.0f);
        return;
    }
    if (std::isfinite(stance->dodgeLeft) && stance->dodgeLeft > 0.0f) {
        return;
    }
    if (std::isfinite(stance->dodgeCooldown) && stance->dodgeCooldown > 0.0f) {
        char text[64];
        std::snprintf(text, sizeof(text), "闪避冷却 %.1fs",
                      static_cast<double>(stance->dodgeCooldown));
        SetNotice(text, kNoticeFailColor, 1.0f);
        return;
    }
    if (const auto* attack = registry.try_get<ecs::AttackIntent>(m_player);
        attack != nullptr && attack->pending) {
        SetNotice("闪避：攻击中", kNoticeFailColor, 1.0f);
        return;
    }
    auto* stamina = registry.try_get<ecs::CombatStamina>(m_player);
    if (stamina == nullptr) {
        SetNotice("闪避：体力状态未就绪", kNoticeFailColor, 1.2f);
        return;
    }
    const characters::CombatRuleConfig& rules = m_ctx.combat->GetCombatRules();
    if (stamina->exhausted > 0.0f ||
        characters::StaminaExhausted(stamina->current, rules)) {
        SetNotice("闪避：体力不??", kNoticeFailColor, 1.2f);
        return;
    }
    const float cost = characters::DodgeStaminaCost(rules);
    if (!(stamina->current >= cost)) {
        SetNotice("闪避：体力不??", kNoticeFailColor, 1.2f);
        return;
    }

    float worldX = 0.0f;
    float worldY = 0.0f;
    if (std::isfinite(screenX) && std::isfinite(screenY) &&
        (std::abs(screenX) > 0.0001f || std::abs(screenY) > 0.0001f)) {
        const glm::vec2 grid = glm::normalize(
            characters::InputSystem::ScreenDirToGridDir(
                glm::normalize(glm::vec2(screenX, screenY))));
        worldX = grid.x;
        worldY = grid.y;
    }
    ecs::Direction facing = ecs::Direction::Down;
    if (const auto* facingComponent = registry.try_get<ecs::Facing>(m_player);
        facingComponent != nullptr) {
        facing = facingComponent->dir;
    }
    const game::PlayerDodgePlan plan =
        game::ResolvePlayerDodge(worldX, worldY, facing, rules);
    if (!(plan.speed > 0.0f)) {
        SetNotice("闪避：位移配置无??", kNoticeFailColor, 1.2f);
        return;
    }
    if (!m_ctx.combat->TrySpendStamina(*m_ctx.ecs, m_player, cost)) {
        SetNotice("闪避：体力不??", kNoticeFailColor, 1.2f);
        return;
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
    if (auto* intent = registry.try_get<ecs::MoveIntent>(m_player);
        intent != nullptr) {
        intent->x = plan.dirX * plan.speed;
        intent->y = plan.dirY * plan.speed;
    }
    if (const auto* position = registry.try_get<ecs::Position>(m_player);
        position != nullptr) {
        PlayCombatCue(CombatCue::Dodge, position->x, position->y);
    }
    SetNotice("闪避", kNoticeOkColor, 0.8f);
    ZL_INFO("characters",
            "玩家闪避：方??({:.2f}, {:.2f})，速度 {:.2f} ??s，无??{:.2f}s??"
            "体力 {:.0f}/{:.0f}",
            static_cast<double>(plan.dirX), static_cast<double>(plan.dirY),
            static_cast<double>(plan.speed), static_cast<double>(invuln),
            static_cast<double>(stamina->current),
            static_cast<double>(stamina->max));
}

bool WorldDemoState::TryGetEquippedRangedWeapon(
    const items::ItemDef** outDef, ecs::ItemInstance** outWeapon) {
    if (outDef != nullptr) {
        *outDef = nullptr;
    }
    if (outWeapon != nullptr) {
        *outWeapon = nullptr;
    }
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    if (equipment == nullptr) {
        return false;
    }
    auto& slot =
        equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
    if (!slot.has_value()) {
        return false;
    }
    const items::ItemDef* def = m_ctx.items->Find(slot->defId);
    if (def == nullptr || !def->ranged.has_value()) {
        return false;
    }
    if (outDef != nullptr) {
        *outDef = def;
    }
    if (outWeapon != nullptr) {
        *outWeapon = &*slot;
    }
    return true;
}

bool WorldDemoState::TryGetAimDirection(const glm::vec2& mousePx, float* dirX,
                                        float* dirY) const {
    if (dirX == nullptr || dirY == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return false;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }
    const render::GridPick pick = PickMouseCell(mousePx);
    float dx = static_cast<float>(pick.x) + 0.5f - position->x;
    float dy = static_cast<float>(pick.y) + 0.5f - position->y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.0001f) {
        dx /= length;
        dy /= length;
    } else {
        dx = 1.0f;
        dy = 0.0f;
    }
    *dirX = dx;
    *dirY = dy;
    return true;
}

void WorldDemoState::PlayerRangedAttack(const glm::vec2& mousePx) {
    if (m_networkMode || m_buildMode || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    if (m_ctx.ui != nullptr &&
        (m_ctx.ui->IsInventoryOpen() || m_ctx.ui->IsMenuOpen())) {
        return;
    }

    const items::ItemDef* def = nullptr;
    ecs::ItemInstance* weapon = nullptr;
    if (!TryGetEquippedRangedWeapon(&def, &weapon)) {
        SetNotice("fire: no ranged weapon equipped", kNoticeFailColor, 1.5f);
        return;
    }

    std::string reason;
    if (!m_rangedWeapon.CanFire(*def, *weapon, &reason)) {
        SetNotice("fire: " + reason, kNoticeFailColor, 1.2f);
        return;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (playerPos == nullptr) {
        return;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (!TryGetAimDirection(mousePx, &dx, &dy)) {
        return;
    }

    const items::RangedCombat& ranged = *def->ranged;
    --weapon->loadedAmmo;
    m_rangedWeapon.cooldownLeft = std::max(0.0f, ranged.cooldownSeconds);
    bool broken = false;
    if (ranged.wearPerShot > 0.0f) {
        if (auto* equipment = registry.try_get<ecs::Equipment>(m_player);
            equipment != nullptr) {
            broken = items::WearEquippedWeapon(*equipment, ranged.wearPerShot);
        }
    }
    if (auto* facing = registry.try_get<ecs::Facing>(m_player); facing != nullptr) {
        facing->dir = characters::AnimationSystem::FacingFromVelocity(dx, dy);
    }

    ++m_rangedShotSerial;
    const float baseAngle = std::atan2(dy, dx);
    const std::uint64_t ownerId = m_ctx.ecs->IdOf(m_player).id;
    SpawnRangedEffect(playerPos->x + dx * 0.4f, playerPos->y + dy * 0.4f,
                      playerPos->z, 0.07f, 2, 0xFFFFC040u);
    core::SoundEvent shot = MakeRangedGunshot(playerPos->x, playerPos->y, ranged);
    if (m_ctx.audio != nullptr) {
        m_ctx.audio->EmitWorldSound(shot, audio::kGunshot);
    } else if (m_ctx.events != nullptr) {
        shot.clip = audio::kGunshot;
        m_ctx.events->EmitQueued(core::kOnSound, std::move(shot));
    }
    for (int i = 0; i < ranged.pellets; ++i) {
        const float offset = glm::radians(items::RangedSpreadOffsetDegrees(
            ranged, m_rangedShotSerial, static_cast<std::uint32_t>(i)));
        const float angle = baseAngle + offset;
        SpawnRangedProjectile(playerPos->x, playerPos->y, playerPos->z,
                              std::cos(angle), std::sin(angle), ranged, ownerId);
    }

    const int ammoLeft = weapon->loadedAmmo;
    std::string text = "fire: " + def->name + " " + std::to_string(ammoLeft) +
                       "/" + std::to_string(ranged.magazine);
    if (ammoLeft <= 0) {
        text += " (R reload)";
    }
    if (broken) {
        text += " (weapon broken)";
    }
    SetNotice(std::move(text), broken ? kNoticeFailColor : kNoticeInfoColor, 1.5f);
    ZL_INFO("items",
            "远程开火：{} 弹药 {}/{}，弹丸 {}，方向 ({:.2f}, {:.2f})，伤害 {:.0f}，"
            "射程 {:.1f}，弹??{:.1f}，噪声半??{:.0f}，冷??{:.2f}s，耐久 {:.0f}%{}",
            def->name, ammoLeft, ranged.magazine, ranged.pellets, dx, dy,
            ranged.damage, ranged.range, ranged.projectileSpeed,
            ranged.noiseRadius, ranged.cooldownSeconds,
            weapon->condition * 100.0f, broken ? "（已损坏??" : "");
}

void WorldDemoState::SpawnRangedProjectile(float startX, float startY, int z,
                                           float dirX, float dirY,
                                           const items::RangedCombat& ranged,
                                           std::uint64_t ownerStableId) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    const float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001f) {
        return;
    }
    dirX /= length;
    dirY /= length;
    const float speed = std::max(0.1f, ranged.projectileSpeed);
    const float range = std::max(0.1f, ranged.range);

    entt::registry& registry = m_ctx.ecs->Registry();
    const ecs::Entity entity = m_ctx.ecs->Create();
    if (entity == entt::null) {
        return;
    }
    registry.emplace<ecs::Position>(
        entity,
        ecs::Position{startX + dirX * 0.35f, startY + dirY * 0.35f, z});
    registry.emplace<ecs::Velocity>(entity,
                                    ecs::Velocity{dirX * speed, dirY * speed});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName("projectile.bullet"), 1,
                               0xFFFFC040u});

    ecs::Projectile value;
    value.ownerStableId = ownerStableId;
    value.lifetime = std::clamp(range / speed + 0.25f, 0.1f, 10.0f);
    value.radius = 0.2f;
    value.damage = ranged.damage;
    value.knockback = 0.0f;
    value.staggerSeconds = 0.0f;
    value.hitPlayers = false;
    value.startX = startX;
    value.startY = startY;
    value.maxDistance = range;
    value.falloffStart = ranged.falloffStart;
    value.falloffEnd = ranged.falloffEnd;
    value.falloffScale = ranged.falloffScale;
    registry.emplace<ecs::Projectile>(entity, value);
}

void WorldDemoState::UpdateRangedProjectiles(float tickDt) {
    if (m_ctx.ecs == nullptr || !(tickDt > 0.0f)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    std::vector<ecs::Entity> destroy;
    struct Hit {
        ecs::Entity target = entt::null;
        std::uint64_t ownerStableId = 0;
        float damage = 0.0f;
        float dirX = 0.0f;
        float dirY = 0.0f;
    };
    struct Impact {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
    };
    std::vector<Hit> hits;
    std::vector<Impact> impacts;

    registry.view<ecs::Projectile, ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity entity, ecs::Projectile& projectile,
            const ecs::Position& position, const ecs::Velocity& velocity) {
            projectile.lifetime -= tickDt;
            bool remove = projectile.lifetime <= 0.0f;
            bool showImpact = false;
            Impact impact{position.x, position.y, position.z};
            const float travelled =
                std::sqrt((position.x - projectile.startX) *
                              (position.x - projectile.startX) +
                          (position.y - projectile.startY) *
                              (position.y - projectile.startY));
            if (!remove && projectile.maxDistance > 0.0f &&
                travelled >= projectile.maxDistance) {
                remove = true;
            }

            if (!remove && m_ctx.world != nullptr) {
                const int wx = static_cast<int>(std::floor(position.x));
                const int wy = static_cast<int>(std::floor(position.y));
                if (IsSquareBlocked(*m_ctx.world, wx, wy, position.z)) {
                    remove = true;
                    showImpact = true;
                }
            }

            ecs::Entity best = entt::null;
            float bestDistanceSq = 1.0e9f;
            if (!remove) {
                registry.view<ecs::Health, ecs::Position>().each(
                    [&](const ecs::Entity target, const ecs::Health& health,
                        const ecs::Position& targetPosition) {
                        if (target == entity || target == m_player ||
                            health.current <= 0.0f ||
                            registry.any_of<ecs::NoCombatTarget>(target) ||
                            registry.all_of<ecs::PlayerTag>(target)) {
                            return;
                        }
                        if (targetPosition.z != position.z) {
                            return;
                        }
                        const float dx = targetPosition.x - position.x;
                        const float dy = targetPosition.y - position.y;
                        const float distanceSq = dx * dx + dy * dy;
                        const float reach = projectile.radius + 0.35f;
                        if (distanceSq <= reach * reach &&
                            distanceSq < bestDistanceSq) {
                            bestDistanceSq = distanceSq;
                            best = target;
                        }
                    });
            }
            if (best != entt::null) {
                remove = true;
                showImpact = true;
                const auto* targetPosition = registry.try_get<ecs::Position>(best);
                if (targetPosition != nullptr) {
                    impact = Impact{targetPosition->x, targetPosition->y,
                                    targetPosition->z};
                }
                const float distance =
                    std::sqrt((impact.x - projectile.startX) *
                                  (impact.x - projectile.startX) +
                              (impact.y - projectile.startY) *
                                  (impact.y - projectile.startY));
                const float damage = items::RangedDamageAtDistance(
                    projectile.damage, distance, projectile.falloffStart,
                    projectile.falloffEnd, projectile.falloffScale);
                const float speed =
                    std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
                hits.push_back(Hit{
                    best, projectile.ownerStableId, damage,
                    speed > 0.0001f ? velocity.x / speed : 0.0f,
                    speed > 0.0001f ? velocity.y / speed : 0.0f});
            }

            if (remove) {
                destroy.push_back(entity);
                if (showImpact) {
                    impacts.push_back(impact);
                }
            }
        });

    for (const ecs::Entity entity : destroy) {
        m_ctx.ecs->Destroy(entity);
    }
    if (m_ctx.combat != nullptr) {
        for (const Hit& hit : hits) {
            if (!m_ctx.ecs->Alive(hit.target) || !(hit.damage > 0.0f)) {
                continue;
            }
            const auto* health = registry.try_get<ecs::Health>(hit.target);
            if (health == nullptr || health->current <= 0.0f) {
                continue;
            }
            ecs::DamageEvent event;
            event.sourceId = hit.ownerStableId;
            event.type = ecs::DamageType::Blunt;
            event.amount = hit.damage;
            event.dirX = hit.dirX;
            event.dirY = hit.dirY;
            event.knockback = 0.0f;
            event.staggerSeconds = 0.0f;
            m_ctx.combat->ApplyDamage(*m_ctx.ecs, hit.target, event);
        }
    }
    std::size_t impactSounds = 0;
    for (const Impact& impact : impacts) {
        SpawnRangedEffect(impact.x, impact.y, impact.z, 0.3f, 2, 0xFF7040FFu);
        if (impactSounds < 2) {
            PlayRangedSound(audio::kImpact, impact.x, impact.y, 10.0f);
            ++impactSounds;
        }
    }
}

void WorldDemoState::UpdateRangedPresentation(float tickDt) {
    if (m_ctx.ecs == nullptr || !(tickDt > 0.0f)) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    std::vector<ecs::Entity> destroy;
    registry.view<ecs::TransientEffect>().each(
        [&](const ecs::Entity entity, ecs::TransientEffect& effect) {
            effect.remaining -= tickDt;
            if (effect.remaining <= 0.0f) {
                destroy.push_back(entity);
            }
        });
    for (const ecs::Entity entity : destroy) {
        m_ctx.ecs->Destroy(entity);
    }
}

void WorldDemoState::SpawnRangedEffect(float x, float y, int z, float lifetime,
                                       int layer, std::uint32_t tint) {
    if (m_ctx.ecs == nullptr || !(lifetime > 0.0f)) {
        return;
    }
    const ecs::Entity entity = m_ctx.ecs->Create();
    if (entity == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.emplace<ecs::Position>(entity, ecs::Position{x, y, z});
    registry.emplace<ecs::SpriteRef>(
        entity, ecs::SpriteRef{ecs::HashName("effect.impact"), layer, tint});
    registry.emplace<ecs::TransientEffect>(
        entity, ecs::TransientEffect{std::max(0.01f, lifetime)});
}

void WorldDemoState::PlayRangedSound(const char* clip, float x, float y,
                                     float maxDistance) {
    if (m_ctx.audio == nullptr || clip == nullptr || clip[0] == '\0') {
        return;
    }
    m_ctx.audio->Engine().Play3D(
        clip, glm::vec2(x, y), audio::AudioGroup::SFX, 0.85f, false,
        std::max(1.0f, maxDistance));
}

void WorldDemoState::PlayCombatCue(CombatCue cue, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    const CombatCueSpec& spec = CombatCueSpecFor(cue);
    core::SoundEvent sound;
    sound.x = x;
    sound.y = y;
    sound.radius = spec.radius;
    sound.type = core::SoundType::Impact;
    sound.clip = spec.clip;
    if (m_ctx.audio != nullptr) {
        m_ctx.audio->EmitWorldSound(std::move(sound));
    } else if (m_ctx.events != nullptr) {
        m_ctx.events->EmitQueued(core::kOnSound, std::move(sound));
    }
    ZL_INFO("audio", "combat cue {} at ({:.2f},{:.2f}) radius {:.1f}",
            spec.name, static_cast<double>(x), static_cast<double>(y),
            static_cast<double>(spec.radius));
}

bool WorldDemoState::TryStartRangedReload() {
    const items::ItemDef* def = nullptr;
    ecs::ItemInstance* weapon = nullptr;
    if (!TryGetEquippedRangedWeapon(&def, &weapon)) {
        return false;
    }
    if (m_rangedWeapon.reloading) {
        SetNotice("reload: already reloading", kNoticeInfoColor, 1.2f);
        return true;
    }

    std::string reason;
    if (m_ctx.ecs == nullptr || m_ctx.ecs->Registry().try_get<ecs::Container>(m_player) ==
                                    nullptr) {
        SetNotice("reload: no bag", kNoticeFailColor, 1.5f);
        return true;
    }
    const auto& bag =
        *m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    const int available = items::CountOf(bag, def->ranged->ammoDefId);
    if (!m_rangedWeapon.StartReload(*def, *weapon, available, &reason)) {
        SetNotice("reload: " + reason, kNoticeFailColor, 1.5f);
        return true;
    }

    const auto* hit = m_ctx.ecs->Registry().try_get<ecs::HitReaction>(m_player);
    m_rangedReloadHitSerial = hit != nullptr ? hit->hitSerial : 0;
    SetNotice("reloading " + def->name + " (" +
                  std::to_string(m_rangedWeapon.reloadAmount) + " rounds)",
              kNoticeInfoColor, 1.8f);
    ZL_INFO("items", "开始换弹：{}，装填 {} 发，耗时 {:.1f}s", def->name,
            m_rangedWeapon.reloadAmount, m_rangedWeapon.reloadLeft);
    return true;
}

void WorldDemoState::UpdateRangedWeapon(float tickDt) {
    m_rangedWeapon.Tick(tickDt);
    if (!m_rangedWeapon.reloading) {
        return;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        m_rangedWeapon.CancelReload();
        return;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
    if (hit != nullptr && hit->hitSerial != m_rangedReloadHitSerial) {
        m_rangedWeapon.CancelReload();
        SetNotice("reload interrupted by hit", kNoticeFailColor, 1.8f);
        return;
    }
    if (const auto* health = registry.try_get<ecs::Health>(m_player);
        health != nullptr && health->current <= 0.0f) {
        m_rangedWeapon.CancelReload();
        return;
    }

    const items::ItemDef* def = nullptr;
    ecs::ItemInstance* weapon = nullptr;
    if (!TryGetEquippedRangedWeapon(&def, &weapon)) {
        m_rangedWeapon.CancelReload();
        SetNotice("reload cancelled: weapon changed", kNoticeFailColor, 1.5f);
        return;
    }

    std::string reason;
    const items::RangedReloadEvent event =
        m_rangedWeapon.TickReload(tickDt, def, weapon, &reason);
    if (event == items::RangedReloadEvent::Cancelled) {
        SetNotice("reload cancelled: " + reason, kNoticeFailColor, 1.5f);
        return;
    }
    if (event != items::RangedReloadEvent::Completed) {
        return;
    }

    auto* bag = registry.try_get<ecs::Container>(m_player);
    if (bag == nullptr) {
        m_rangedWeapon.CancelReload();
        SetNotice("reload cancelled: no bag", kNoticeFailColor, 1.5f);
        return;
    }
    const int requested = m_rangedWeapon.reloadAmount;
    const int space = std::max(0, def->ranged->magazine - weapon->loadedAmmo);
    const int available = items::CountOf(*bag, def->ranged->ammoDefId);
    const int toLoad = std::min({requested, space, available});
    if (toLoad <= 0) {
        m_rangedWeapon.CancelReload();
        SetNotice("reload cancelled: no ammo", kNoticeFailColor, 1.5f);
        return;
    }
    const int removed =
        items::RemoveItem(*bag, def->ranged->ammoDefId, toLoad);
    weapon->loadedAmmo =
        std::min(def->ranged->magazine, weapon->loadedAmmo + removed);
    m_rangedWeapon.CancelReload();
    SetNotice("reloaded " + def->name + " " +
                  std::to_string(weapon->loadedAmmo) + "/" +
                  std::to_string(def->ranged->magazine),
              kNoticeOkColor, 1.8f);
    ZL_INFO("items", "换弹完成：{}，装填 {} 发，剩余弹药 {}", def->name, removed,
            items::CountOf(*bag, def->ranged->ammoDefId));
}

void WorldDemoState::DespawnDemoEntities() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::Position, ecs::Velocity>().each(
        [&](const ecs::Entity e, ecs::Position&, ecs::Velocity&) {
            if (m_ctx.physics != nullptr) {
                if (const auto* rigid = registry.try_get<ecs::RigidBody>(e);
                    rigid != nullptr && rigid->body != nullptr) {
                    m_ctx.physics->DestroyBody(rigid->body);
                }
            }
            m_ctx.ecs->Destroy(e);
        });
    m_ctx.ecs->Flush();
    m_player = entt::null;
}

void WorldDemoState::EnsureDemoItemsScattered() {
    if (m_networkMode || m_demoItemsScattered || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr) {
        return;
    }
    // 与玩家生成相同的门控：目标 chunk 就绪后再散落（走位图/格子数据可靠）
    const world::ChunkCoord targetChunk = world::ChunkOf(70, 24);
    const world::Chunk* target = FindChunk(targetChunk.cx, targetChunk.cy);
    if (target == nullptr || target->State() != world::ChunkState::Loaded) {
        return;  // 下帧再试
    }
    if (m_ctx.world == nullptr) {
        return;
    }
    const int spawned = game::SpawnDemoGroundItems(
        *m_ctx.ecs, *m_ctx.items, *m_ctx.world, 70.0f, 24.0f);
    m_demoItemsScattered = true;
    ZL_INFO("items", "地面物品散落：{} 处（村庄附近；F 拾取最近）", spawned);
}

void WorldDemoState::EnsureDemoLootContainers() {
    if (m_networkMode || m_demoLootContainersSpawned || m_ctx.ecs == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.lootTables->Size() == 0) {
        return;
    }
    const world::ChunkCoord targetChunk = world::ChunkOf(70, 24);
    const world::Chunk* target = FindChunk(targetChunk.cx, targetChunk.cy);
    if (target == nullptr || target->State() != world::ChunkState::Loaded) {
        return;
    }
    if (m_ctx.world == nullptr) {
        return;
    }
    const int spawned = game::SpawnDemoLootContainers(
        *m_ctx.ecs, *m_ctx.lootTables, *m_ctx.world, 70.0f, 24.0f);
    m_demoLootContainersSpawned = true;
    ZL_INFO("items", "战利品容器生成：{} 个（村庄附近；打开背包搜索）", spawned);
}

void WorldDemoState::EnsureHouseLootContainers() {
    if (m_networkMode || m_houseLootContainersSpawned || m_ctx.ecs == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.lootTables->Size() == 0) {
        return;
    }
    const world::ChunkCoord targetChunk = world::ChunkOf(72, 21);
    const world::Chunk* target = FindChunk(targetChunk.cx, targetChunk.cy);
    if (target == nullptr || target->State() != world::ChunkState::Loaded) {
        return;
    }
    if (m_ctx.world == nullptr) {
        return;
    }
    const int spawned = game::SpawnHouseLootContainers(
        *m_ctx.ecs, *m_ctx.lootTables, *m_ctx.world);
    m_houseLootContainersSpawned = true;
    if (spawned > 0) {
        ZL_INFO("items", "房屋战利品容器生成：{} 个（屋内 workshop/kitchen/crate）", spawned);
    }
}

void WorldDemoState::InitializePoiRuntime() {
    m_poiRuntime.clear();
    m_poiNearId.clear();
}

void WorldDemoState::EnsurePoiContent() {
    if (m_networkMode || !m_poiConfig.enabled || m_ctx.ecs == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.world == nullptr) {
        return;
    }
    const ecs::PoiTasks* tasks = PlayerPoiTasks();
    for (const PoiDef& poi : m_poiConfig.defs) {
        const world::ChunkCoord coord =
            world::ChunkOf(static_cast<int>(std::floor(poi.markerX)),
                           static_cast<int>(std::floor(poi.markerY)));
        const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
        if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
            continue;  // 流式加载??POI 附近后再生成
        }
        PoiRuntime& runtime = m_poiRuntime[poi.id];
        if (!runtime.containersSpawned) {
            int spawned = 0;
            for (const PoiContainerDef& container : poi.containers) {
                if (game::HasLootContainerNear(*m_ctx.ecs, container.tableId,
                                               container.x, container.y, 0, 1.0f)) {
                    continue;
                }
                if (game::SpawnLootContainer(*m_ctx.ecs, *m_ctx.lootTables,
                                             container.tableId, container.x,
                                             container.y, 0,
                                             container.sprite) != entt::null) {
                    ++spawned;
                }
            }
            runtime.containersSpawned = true;
            if (spawned > 0) {
                ZL_INFO("items", "POI 容器生成：{}（{} 个）", poi.name, spawned);
            }
        }
        if (runtime.guardsSpawned) {
            continue;
        }
        runtime.guardsSpawned = true;
        if (tasks != nullptr && game::IsPoiCompleted(tasks->completed, poi.id)) {
            continue;
        }
        if (poi.enemies.count > 0 && CountPoiGuards(poi.id) == 0) {
            int spawned = 0;
            for (int i = 0; i < poi.enemies.count; ++i) {
                int wx = 0;
                int wy = 0;
                if (!FindPoiGuardCell(poi, i, &wx, &wy)) {
                    continue;
                }
                const game::PoiGuardKind kind =
                    game::PoiGuardKindForSpawn(poi.enemies, i);
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
                const ecs::Entity zombie = m_ctx.simulation != nullptr
                                               ? m_ctx.simulation->SpawnZombieAt(
                                                     static_cast<float>(wx) + 0.5f,
                                                     static_cast<float>(wy) + 0.5f,
                                                     variant)
                                               : entt::null;
                if (zombie == entt::null) {
                    continue;
                }
                m_ctx.ecs->Registry().emplace_or_replace<ecs::PoiGuard>(
                    zombie, ecs::PoiGuard{poi.id});
                ++spawned;
            }
            ZL_INFO("poi", "POI 守军生成：{}（{}/{}，威胁 {}）", poi.name, spawned,
                    poi.enemies.count, game::PoiThreatLabel(poi.threat));
        }
    }
}

bool WorldDemoState::FindPoiGuardCell(const PoiDef& poi, int index, int* wx,
                                      int* wy) const {
    if (wx == nullptr || wy == nullptr || m_ctx.world == nullptr) {
        return false;
    }
    for (int attempt = 0; attempt < 32; ++attempt) {
        const float angle =
            (static_cast<float>(index) * 2.3999631f) +
            (static_cast<float>(attempt) * 0.6180339f);
        const float radius =
            2.5f + static_cast<float>((index + attempt) % 4) * 1.2f;
        const int x = static_cast<int>(std::floor(
            poi.markerX + std::cos(angle) * radius));
        const int y = static_cast<int>(std::floor(
            poi.markerY + std::sin(angle) * radius));
        const world::ChunkCoord coord = world::ChunkOf(x, y);
        const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
        if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
            continue;
        }
        if (IsSquareBlocked(*m_ctx.world, x, y, 0)) {
            continue;
        }
        *wx = x;
        *wy = y;
        return true;
    }
    return false;
}

int WorldDemoState::CountPoiGuards(std::string_view poiId) const {
    if (m_ctx.ecs == nullptr) {
        return 0;
    }
    int count = 0;
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::PoiGuard, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::PoiGuard& guard,
            const ecs::Health& health) {
            if (m_ctx.ecs->Alive(entity) && guard.poiId == poiId &&
                health.current > 0.0f) {
                ++count;
            }
        });
    return count;
}

const ecs::PoiTasks* WorldDemoState::PlayerPoiTasks() const {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return nullptr;
    }
    return m_ctx.ecs->Registry().try_get<ecs::PoiTasks>(m_player);
}

bool WorldDemoState::PoiTargetSearched(const PoiDef& poi) const {
    if (m_ctx.ecs == nullptr || poi.task.targetTable.empty()) {
        return false;
    }
    bool found = false;
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::LootContainer>().each(
        [&](const ecs::Entity entity, const ecs::LootContainer& loot) {
            if (!found && m_ctx.ecs->Alive(entity) && loot.searched &&
                loot.tableId == poi.task.targetTable) {
                found = true;
            }
        });
    return found;
}

void WorldDemoState::UpdatePoiTasks() {
    if (m_networkMode || !m_poiConfig.enabled || m_ctx.ecs == nullptr ||
        m_player == entt::null || m_runOutcome != RunOutcome::None) {
        return;
    }
    auto* tasks = m_ctx.ecs->Registry().try_get<ecs::PoiTasks>(m_player);
    if (tasks == nullptr) {
        tasks = &m_ctx.ecs->Registry().emplace<ecs::PoiTasks>(m_player);
    }
    for (const PoiDef& poi : m_poiConfig.defs) {
        if (game::IsPoiCompleted(tasks->completed, poi.id)) {
            continue;
        }
        const bool searched = PoiTargetSearched(poi);
        const int liveGuards = CountPoiGuards(poi.id);
        if (!game::ShouldCompletePoiTask(poi, searched, liveGuards)) {
            continue;
        }
        game::MarkPoiCompleted(&tasks->completed, poi.id);
        if (poi.task.rewardXp > 0.0f) {
            GrantProgressionXp(ecs::SkillKind::Survival, poi.task.rewardXp,
                               "POI 任务");
        }
        bool rewardDropped = false;
        if (!poi.task.rewardItem.empty() && m_ctx.items != nullptr &&
            m_ctx.items->Find(poi.task.rewardItem) != nullptr) {
            ReturnOrDropItem(ecs::ItemInstance{poi.task.rewardItem,
                                               std::max(1, poi.task.rewardCount),
                                               1.0f},
                             &rewardDropped);
        }
        SetNotice("POI 任务完成：" + poi.name + "（" +
                      std::to_string(static_cast<int>(poi.task.rewardXp)) +
                      " 经验??" +
                      (rewardDropped ? "（背包已满，奖励掉落脚下）" : ""),
                  kNoticeOkColor, 5.0f);
        ZL_INFO("poi", "任务完成：{}（{}），剩余守军 0，目标 {}", poi.name,
                poi.id, poi.task.targetTable);
    }
}

void WorldDemoState::RefreshObjectiveHud(ui::HudState& hud) {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* objective = registry.try_get<ecs::Objective>(m_player);
    if (objective == nullptr) {
        return;
    }
    const bool hasPart = PlayerHasRadioPart();
    hud.objective = game::ObjectiveText(objective->step, hasPart,
                                        m_extractionX, m_extractionY);
    hud.objectiveTone = game::ObjectiveTone(objective->step);
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    const float dx = position->x - m_radioX;
    const float dy = position->y - m_radioY;
    if (dx * dx + dy * dy >
        m_objectiveInteractRange * m_objectiveInteractRange) {
        return;
    }
    if (objective->step == ecs::ObjectiveStep::RepairRadio) {
        hud.objectiveHint = hasPart ? "E 修理电台" : "缺少无线电零件";
    } else if (objective->step == ecs::ObjectiveStep::FindRadioPart) {
        hud.objectiveHint = "需要无线电零件";
    } else if (objective->step == ecs::ObjectiveStep::ReachExtraction) {
        hud.objectiveHint = "电台已修复";
    } else {
        hud.objectiveHint = "目标已完成";
    }
}

void WorldDemoState::RefreshPoiGuide(ui::HudState& hud) {
    if (!m_poiConfig.enabled || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return;
    }
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    const ecs::PoiTasks* tasks = PlayerPoiTasks();
    const PoiDef* nearest = nullptr;
    float nearestDistSq = 1.0e30f;
    float nearestDx = 0.0f;
    float nearestDy = 0.0f;
    int nearestGuards = 0;
    bool nearestSearched = false;
    for (const PoiDef& poi : m_poiConfig.defs) {
        bool completed =
            tasks != nullptr && game::IsPoiCompleted(tasks->completed, poi.id);
        int liveGuards = 0;
        bool targetSearched = false;
        if (m_networkMode) {
            const auto found = m_networkPoiProgress.find(poi.id);
            if (found != m_networkPoiProgress.end()) {
                completed = found->second.completed;
                liveGuards = found->second.liveGuards;
                targetSearched = found->second.targetSearched;
            }
        } else {
            liveGuards = CountPoiGuards(poi.id);
            targetSearched = PoiTargetSearched(poi);
        }
        if (completed) {
            continue;
        }
        const float dx = poi.markerX - position->x;
        const float dy = poi.markerY - position->y;
        const float distSq = dx * dx + dy * dy;
        if (distSq < nearestDistSq) {
            nearest = &poi;
            nearestDistSq = distSq;
            nearestDx = dx;
            nearestDy = dy;
            nearestGuards = liveGuards;
            nearestSearched = targetSearched;
        }
    }
    if (nearest == nullptr) {
        hud.poiGuide = "POI · 全部区域已完成";
        hud.poiGuideTone = "ok";
        return;
    }
    const float distance = std::sqrt(nearestDistSq);
    if (distance > m_poiConfig.guideRange) {
        return;  // 远处只保留世??边缘标记，避??HUD 常驻噪声
    }
    hud.poiGuide = game::PoiGuideText(*nearest, nearestDx, nearestDy, distance,
                                      nearestSearched, nearestGuards);
    hud.poiGuideTone =
        game::PoiGuideTone(*nearest, nearestSearched, nearestGuards);
    if (distance <= 12.0f && m_poiNearId != nearest->id) {
        m_poiNearId = nearest->id;
        ZL_INFO("poi", "进入 POI：{}（威??{}，任务：{}??", nearest->name,
                game::PoiThreatLabel(nearest->threat),
                game::PoiTaskText(*nearest, nearestSearched, nearestGuards));
    }
}

void WorldDemoState::InitializeMissionRuntime() {
    m_missionFocusId.clear();
    m_lastMissionKills = 0;
    m_missionKillsInitialized = false;
    m_randomEventRuntime.clear();
    m_randomEventReady.clear();
    m_randomEventKilled.clear();
    m_coldSnapEventId.clear();
    if (m_ctx.simulation != nullptr) {
        m_ctx.simulation->ClearWeatherOverride();
    }
}

int WorldDemoState::CurrentMissionDay() const {
    if (m_ctx.gameTime == nullptr) {
        return 1;
    }
    return 1 + static_cast<int>(m_ctx.gameTime->TotalGameMinutes() /
                                (24.0 * 60.0));
}

ecs::MissionLog* WorldDemoState::PlayerMissions() {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return nullptr;
    }
    return m_ctx.ecs->Registry().try_get<ecs::MissionLog>(m_player);
}

const ecs::MissionLog* WorldDemoState::PlayerMissions() const {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return nullptr;
    }
    return m_ctx.ecs->Registry().try_get<ecs::MissionLog>(m_player);
}

void WorldDemoState::ActivateMission(ecs::MissionLog& log,
                                     const MissionDef& def, int day) {
    ecs::MissionRunState* state = game::FindMissionState(&log, def.id);
    if (state == nullptr) {
        log.missions.push_back(ecs::MissionRunState{});
        state = &log.missions.back();
    }
    state->id = def.id;
    state->progress.assign(def.objectives.size(), 0);
    state->status = ecs::MissionRunStatus::Active;
    state->day = day;
    state->rewardClaimed = false;
    ZL_INFO("missions", "接取任务：{}（{}）", def.name, def.id);
}

void WorldDemoState::UpdateMissionAvailability(ecs::MissionLog& log, int day) {
    for (const MissionDef& def : m_missionConfig.defs) {
        if (!def.autoActivate) {
            continue;
        }
        if (game::MissionCanActivate(def, log, day)) {
            ActivateMission(log, def, day);
        }
    }
}

bool WorldDemoState::GrantMissionReward(const MissionDef& def) {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    bool dropped = false;
    if (def.reward.xp > 0.0f) {
        GrantProgressionXp(MissionSkillKind(def.reward.skill), def.reward.xp,
                           "任务奖励");
    }
    if (!def.reward.item.empty() && m_ctx.items != nullptr &&
        m_ctx.items->Find(def.reward.item) != nullptr) {
        ReturnOrDropItem(ecs::ItemInstance{def.reward.item,
                                           std::max(1, def.reward.itemCount),
                                           1.0f},
                         &dropped);
    }
    if (!def.reward.extraItem.empty() && m_ctx.items != nullptr &&
        m_ctx.items->Find(def.reward.extraItem) != nullptr) {
        ReturnOrDropItem(ecs::ItemInstance{
                             def.reward.extraItem,
                             std::max(1, def.reward.extraItemCount), 1.0f},
                         &dropped);
    }
    if (!def.reward.blueprint.empty() && m_ctx.recipes != nullptr &&
        m_ctx.recipes->Find(def.reward.blueprint) != nullptr) {
        auto* known =
            m_ctx.ecs->Registry().try_get<ecs::KnownRecipes>(m_player);
        if (known == nullptr) {
            known = &m_ctx.ecs->Registry().emplace<ecs::KnownRecipes>(m_player);
        }
        if (std::find(known->recipeIds.begin(), known->recipeIds.end(),
                      def.reward.blueprint) == known->recipeIds.end()) {
            known->recipeIds.push_back(def.reward.blueprint);
            ZL_INFO("missions", "任务授予图纸：{}", def.reward.blueprint);
        }
    }
    return dropped;
}

void WorldDemoState::CompleteMission(ecs::MissionRunState& state,
                                     const MissionDef& def) {
    if (state.status != ecs::MissionRunStatus::Active) {
        return;
    }
    state.status = ecs::MissionRunStatus::Completed;
    state.rewardClaimed = true;
    if (m_missionFocusId == def.id) {
        m_missionFocusId.clear();
    }
    const bool rewardDropped = GrantMissionReward(def);
    const bool randomEvent = def.kind == MissionKind::Random;
    std::string notice = randomEvent ? "事件完成：" : "任务完成：";
    notice += def.name;
    if (!def.reward.label.empty()) {
        notice += " · " + def.reward.label;
    }
    if (rewardDropped) {
        notice += "（背包已满，奖励掉落脚下）";
    }
    if (!randomEvent && !def.objectives.empty() &&
        !def.objectives.front().description.empty()) {
        notice += " · " + def.objectives.front().description;
    }
    if (def.kind == MissionKind::Once &&
        !game::MissionHasNextStep(m_missionConfig, def)) {
        notice += "（任务链完成）";
    }
    SetNotice(notice, kNoticeOkColor, 5.0f);
    if (randomEvent) {
        ZL_INFO("events", "事件完成：{}（{}），奖励 {}", def.name, def.id,
                def.reward.label);
    } else {
        ZL_INFO("missions", "任务完成：{}（{}），奖励 {}", def.name, def.id,
                def.reward.label);
    }
}

void WorldDemoState::FailMission(ecs::MissionRunState& state,
                                 const MissionDef& def,
                                 std::string_view reason, int day) {
    if (state.status != ecs::MissionRunStatus::Active) {
        return;
    }
    state.status = ecs::MissionRunStatus::Failed;
    state.rewardClaimed = false;
    state.day = day;
    if (m_missionFocusId == def.id) {
        m_missionFocusId.clear();
    }
    SetNotice("任务失败：" + def.name + "（" + std::string(reason) + "）",
              kNoticeFailColor, 4.0f);
    ZL_INFO("missions", "任务失败：{}（{}），原因：{}", def.name, def.id,
            reason);
}

void WorldDemoState::RecordMissionEvent(MissionObjectiveKind kind,
                                        std::string_view target, int amount) {
    if (!m_missionConfig.enabled || amount <= 0) {
        return;
    }
    ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        return;
    }
    for (ecs::MissionRunState& state : log->missions) {
        if (state.status != ecs::MissionRunStatus::Active) {
            continue;
        }
        const MissionDef* def = m_missionConfig.Find(state.id);
        if (def == nullptr) {
            continue;
        }
        state.progress.resize(def->objectives.size(), 0);
        for (std::size_t i = 0; i < def->objectives.size(); ++i) {
            const MissionObjectiveDef& objective = def->objectives[i];
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
}

void WorldDemoState::EnsureRandomEventContentForActiveMissions(
    ecs::MissionLog& log, int day) {
    if (m_networkMode || m_ctx.ecs == nullptr || m_ctx.lootTables == nullptr ||
        m_player == entt::null) {
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    RandomEventSpawnCallbacks callbacks;
    if (m_ctx.world != nullptr) {
        callbacks.walkable = [this](int x, int y) {
            return !IsSquareBlocked(*m_ctx.world, x, y, 0);
        };
    }
    if (m_ctx.simulation != nullptr) {
        callbacks.spawnZombie =
            [this](float x, float y, ecs::ZombieVariantKind variant,
                   std::uint64_t stableId) {
                return m_ctx.simulation->SpawnZombieAt(x, y, variant, stableId);
            };
    }
    for (ecs::MissionRunState& state : log.missions) {
        if (state.status != ecs::MissionRunStatus::Active) {
            continue;
        }
        const MissionDef* def = m_missionConfig.Find(state.id);
        if (def == nullptr || def->kind != MissionKind::Random) {
            continue;
        }
        const RandomEventKind kind = RandomEventKindForMission(def->id);
        if (kind == RandomEventKind::None) {
            continue;
        }
        int progress = 0;
        for (const int value : state.progress) {
            progress += std::max(0, value);
        }
        const std::string key = def->id + ":" + std::to_string(day);
        if (progress > 0 || m_randomEventReady.contains(key)) {
            continue;
        }
        RandomEventSpawnResult result;
        if (!EnsureRandomEventContent(*m_ctx.ecs, *m_ctx.lootTables, def->id,
                                      day, progress, position->x, position->y,
                                      callbacks, &result)) {
            continue;
        }
        m_randomEventReady.insert(key);
        m_randomEventRuntime[def->id] =
            RandomEventRuntime{result.siteX, result.siteY};
        if (kind == RandomEventKind::ColdSnap &&
            m_coldSnapEventId != def->id) {
            m_coldSnapEventId = def->id;
            if (m_ctx.simulation != nullptr) {
                m_ctx.simulation->SetWeatherOverride(WeatherKind::ColdSnap,
                                                     15.0f);
            }
        }
        std::string trigger = "事件触发：" + def->name;
        if (kind == RandomEventKind::ColdSnap) {
            trigger += " · 寒潮来临，持续 15 分钟";
        } else {
            trigger += " · 目标" +
                       game::PoiDirectionLabel(
                           result.siteX - position->x,
                           result.siteY - position->y) +
                       ' ' +
                       std::to_string(static_cast<int>(std::lround(
                           std::hypot(result.siteX - position->x,
                                      result.siteY - position->y)))) +
                       " ??";
        }
        SetNotice(trigger, kNoticeInfoColor, 5.0f);
        ZL_INFO("events", "事件触发：{}（{}，站点 ({:.1f},{:.1f})）",
                def->name, RandomEventKindName(kind),
                static_cast<double>(result.siteX),
                static_cast<double>(result.siteY));
    }
}

void WorldDemoState::PruneRandomEventActors() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    std::vector<ecs::Entity> actors;
    m_ctx.ecs->Registry().view<ecs::RandomEventActor>().each(
        [&](const ecs::Entity entity, const ecs::RandomEventActor&) {
            actors.push_back(entity);
        });
    for (const ecs::Entity actor : actors) {
        m_ctx.ecs->Destroy(actor);
    }
}

void WorldDemoState::UpdateMissions() {
    if (m_networkMode || !m_missionConfig.enabled || m_ctx.ecs == nullptr ||
        m_player == entt::null || m_runOutcome != RunOutcome::None) {
        return;
    }
    ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        log = &m_ctx.ecs->Registry().emplace<ecs::MissionLog>(m_player);
    }
    const int day = CurrentMissionDay();
    bool newDay = log->lastDay != day;
    bool expiredRandom = false;
    if (newDay) {
        for (const MissionDef& def : m_missionConfig.defs) {
            if (def.kind != MissionKind::Random) {
                continue;
            }
            const ecs::MissionRunState* state =
                game::FindMissionState(*log, def.id);
            expiredRandom = expiredRandom || (state != nullptr &&
                                              state->status ==
                                                  ecs::MissionRunStatus::Active);
        }
        PruneRandomEventActors();
        m_randomEventRuntime.clear();
        m_randomEventReady.clear();
        m_randomEventKilled.clear();
        m_coldSnapEventId.clear();
        log->missions.erase(
            std::remove_if(log->missions.begin(), log->missions.end(),
                           [&](const ecs::MissionRunState& state) {
                               const MissionDef* def =
                                   m_missionConfig.Find(state.id);
                               return def != nullptr &&
                                      def->kind == MissionKind::Random;
                           }),
            log->missions.end());
        log->lastDay = day;
    }

    bool dailyRefreshed = false;
    for (const MissionDef& def : m_missionConfig.defs) {
        if (def.kind != MissionKind::Daily || day < def.minDay ||
            !def.autoActivate) {
            continue;
        }
        ecs::MissionRunState* state = game::FindMissionState(log, def.id);
        if (state == nullptr || state->day != day) {
            dailyRefreshed = dailyRefreshed || state != nullptr;
            ActivateMission(*log, def, day);
        }
    }
    if (expiredRandom) {
        SetNotice("随机事件已结束", kNoticeInfoColor, 3.0f);
    } else if (dailyRefreshed) {
        SetNotice("每日任务已刷新", kNoticeInfoColor, 3.0f);
    }

    if (log->randomDay != day) {
        const MissionDef* offered = nullptr;
        for (const MissionDef& def : m_missionConfig.defs) {
            if (def.kind != MissionKind::Random || day < def.minDay ||
                !def.autoActivate) {
                continue;
            }
            if (game::MissionRandomRoll(m_missionConfig.randomSeed, day, def.id,
                                        def.chance)) {
                ActivateMission(*log, def, day);
                offered = &def;
            }
        }
        log->randomDay = day;
        if (offered != nullptr) {
            SetNotice("随机事件：" + offered->name, kNoticeInfoColor, 4.0f);
        }
    }

    UpdateMissionAvailability(*log, day);
    EnsureRandomEventContentForActiveMissions(*log, day);

    std::uint64_t kills = 0;
    if (m_ctx.combat != nullptr) {
        kills = m_ctx.combat->GetStats().kills;
    }
    if (!m_missionKillsInitialized) {
        m_lastMissionKills = kills;
        m_missionKillsInitialized = true;
    } else if (kills > m_lastMissionKills) {
        RecordMissionEvent(MissionObjectiveKind::Kill, "any",
                           static_cast<int>(std::min<std::uint64_t>(
                               kills - m_lastMissionKills, 1000000u)));
        m_lastMissionKills = kills;
    } else if (kills < m_lastMissionKills) {
        m_lastMissionKills = kills;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::RandomEventActor, ecs::Health>().each(
        [&](const ecs::Entity entity, const ecs::RandomEventActor& actor,
            const ecs::Health& health) {
            if (health.current > 0.0f || !m_ctx.ecs->Alive(entity)) {
                return;
            }
            const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
            if (stableId != 0 && m_randomEventKilled.insert(stableId).second) {
                RecordMissionEvent(MissionObjectiveKind::Kill, actor.eventId,
                                   1);
            }
        });
    const auto* position = registry.try_get<ecs::Position>(m_player);
    const ecs::PoiTasks* poiTasks = PlayerPoiTasks();
    for (ecs::MissionRunState& state : log->missions) {
        if (state.status != ecs::MissionRunStatus::Active) {
            continue;
        }
        const MissionDef* def = m_missionConfig.Find(state.id);
        if (def == nullptr) {
            state.status = ecs::MissionRunStatus::Failed;
            continue;
        }
        if (game::MissionIsExpired(*def, state, day)) {
            FailMission(state, *def, "超时", day);
            continue;
        }
        state.progress.resize(def->objectives.size(), 0);
        for (std::size_t i = 0; i < def->objectives.size(); ++i) {
            const MissionObjectiveDef& objective = def->objectives[i];
            int value = state.progress[i];
            if (objective.kind == MissionObjectiveKind::CollectItem) {
                value = std::min(objective.count,
                                 CountMissionItem(registry, m_player,
                                                  objective.target));
            } else if (objective.kind == MissionObjectiveKind::Poi) {
                if (poiTasks != nullptr &&
                    game::IsPoiCompleted(poiTasks->completed,
                                         objective.target)) {
                    value = objective.count;
                }
            } else if (objective.kind == MissionObjectiveKind::Loot &&
                       objective.target != "any") {
                bool found = false;
                registry.view<ecs::LootContainer>().each(
                    [&](const ecs::Entity entity,
                        const ecs::LootContainer& loot) {
                        if (!found && m_ctx.ecs->Alive(entity) &&
                            loot.searched &&
                            loot.tableId == objective.target) {
                            found = true;
                        }
                    });
                if (found) {
                    value = objective.count;
                }
            } else if (objective.kind == MissionObjectiveKind::Interact &&
                       objective.target == "radio") {
                const auto* objectiveState =
                    registry.try_get<ecs::Objective>(m_player);
                if (objectiveState != nullptr &&
                    (objectiveState->step ==
                         ecs::ObjectiveStep::ReachExtraction ||
                     objectiveState->step == ecs::ObjectiveStep::Complete)) {
                    value = objective.count;
                }
            } else if (objective.kind == MissionObjectiveKind::Reach &&
                       position != nullptr) {
                const float dx = position->x - objective.x;
                const float dy = position->y - objective.y;
                if (dx * dx + dy * dy <= objective.radius * objective.radius) {
                    value = objective.count;
                }
            }
            state.progress[i] = std::max(state.progress[i], value);
        }
        if (game::IsMissionComplete(*def, state)) {
            CompleteMission(state, *def);
        }
    }

    bool focusValid = false;
    if (!m_missionFocusId.empty()) {
        const ecs::MissionRunState* focusState =
            game::FindMissionState(*log, m_missionFocusId);
        focusValid = focusState != nullptr &&
                     focusState->status == ecs::MissionRunStatus::Active;
    }
    if (!focusValid) {
        m_missionFocusId.clear();
        m_missionFocusId = FocusedMissionId();
    }
}

std::vector<const MissionDef*> WorldDemoState::SortedActiveMissions() const {
    std::vector<game::MissionFocusCandidate> candidates;
    const ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        return {};
    }
    const auto* position =
        m_ctx.ecs != nullptr && m_player != entt::null
            ? m_ctx.ecs->Registry().try_get<ecs::Position>(m_player)
            : nullptr;
    for (const MissionDef& def : m_missionConfig.defs) {
        const ecs::MissionRunState* state =
            game::FindMissionState(*log, def.id);
        if (state == nullptr ||
            state->status != ecs::MissionRunStatus::Active) {
            continue;
        }
        float distance = 1.0e30f;
        float targetX = 0.0f;
        float targetY = 0.0f;
        if (position != nullptr &&
            MissionTargetPoint(def, *state, &targetX, &targetY)) {
            const float dx = targetX - position->x;
            const float dy = targetY - position->y;
            distance = std::sqrt(dx * dx + dy * dy);
        }
        candidates.push_back(game::MissionFocusCandidate{&def, distance});
    }
    std::sort(candidates.begin(), candidates.end(), game::MissionFocusLess);
    std::vector<const MissionDef*> order;
    order.reserve(candidates.size());
    for (const game::MissionFocusCandidate& candidate : candidates) {
        order.push_back(candidate.def);
    }
    return order;
}

ecs::MissionRunState* WorldDemoState::FocusedMissionState() {
    ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        return nullptr;
    }
    if (!m_missionFocusId.empty()) {
        ecs::MissionRunState* state =
            game::FindMissionState(log, m_missionFocusId);
        if (state != nullptr &&
            state->status == ecs::MissionRunStatus::Active) {
            return state;
        }
    }
    const std::vector<const MissionDef*> order = SortedActiveMissions();
    if (!order.empty()) {
        m_missionFocusId = order.front()->id;
        return game::FindMissionState(log, m_missionFocusId);
    }
    m_missionFocusId.clear();
    return nullptr;
}

std::string WorldDemoState::FocusedMissionId() const {
    const ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        return {};
    }
    if (!m_missionFocusId.empty()) {
        const ecs::MissionRunState* state =
            game::FindMissionState(*log, m_missionFocusId);
        if (state != nullptr &&
            state->status == ecs::MissionRunStatus::Active) {
            return m_missionFocusId;
        }
    }
    const std::vector<const MissionDef*> order = SortedActiveMissions();
    return order.empty() ? std::string{} : order.front()->id;
}

void WorldDemoState::CycleMissionFocus() {
    if (!m_missionConfig.enabled || m_player == entt::null) {
        return;
    }
    const std::vector<const MissionDef*> active = SortedActiveMissions();
    if (active.empty()) {
        SetNotice("当前没有进行中的任务", kNoticeInfoColor, 2.0f);
        return;
    }
    const std::string current = FocusedMissionId();
    std::size_t index = 0;
    for (std::size_t i = 0; i < active.size(); ++i) {
        if (active[i]->id == current) {
            index = (i + 1) % active.size();
            break;
        }
    }
    m_missionFocusId = active[index]->id;
    SetNotice("任务焦点：" + active[index]->name, kNoticeInfoColor, 2.0f);
    ZL_INFO("missions", "任务焦点：{}（{}）", active[index]->name,
            active[index]->id);
}

void WorldDemoState::AbandonFocusedMission() {
    ecs::MissionRunState* state = FocusedMissionState();
    if (state == nullptr) {
        SetNotice("当前没有可放弃的任务", kNoticeInfoColor, 2.0f);
        return;
    }
    const MissionDef* def = m_missionConfig.Find(state->id);
    if (m_networkMode) {
        for (std::size_t i = 0; i < m_missionConfig.defs.size(); ++i) {
            if (m_missionConfig.defs[i].id != state->id) {
                continue;
            }
            net::Command command = MakePlayerCommand(
                net::CommandType::AbandonMission);
            command.payload.index = static_cast<std::int32_t>(i);
            if (SubmitPlayerCommand(command)) {
                m_missionFocusId.clear();
                SetNotice("已发送放弃任务请求", kNoticeInfoColor, 1.5f);
            } else {
                SetNotice("放弃任务请求发送失??", kNoticeFailColor, 1.5f);
            }
            return;
        }
        return;
    }
    game::AbandonMission(PlayerMissions(), state->id, CurrentMissionDay());
    m_missionFocusId.clear();
    const std::string name = def != nullptr ? def->name : state->id;
    std::string notice = "任务已放弃：" + name;
    if (def != nullptr && def->kind == MissionKind::Once) {
        notice += "（次日可重新接取）";
    }
    SetNotice(notice, kNoticeFailColor, 4.0f);
    ZL_INFO("missions", "任务放弃：{}（{}）", name, state->id);
}

bool WorldDemoState::MissionTargetPoint(const MissionDef& def,
                                        const ecs::MissionRunState& state,
                                        float* x, float* y) const {
    if (x == nullptr || y == nullptr) {
        return false;
    }
    const auto* playerPosition =
        m_ctx.ecs != nullptr && m_player != entt::null
            ? m_ctx.ecs->Registry().try_get<ecs::Position>(m_player)
            : nullptr;
    const bool randomEvent = IsRandomEventMission(def.id);
    for (std::size_t i = 0; i < def.objectives.size(); ++i) {
        const MissionObjectiveDef& objective = def.objectives[i];
        if (game::MissionObjectiveProgress(objective, &state, i) >=
            objective.count) {
            continue;
        }
        if (randomEvent &&
            (objective.kind == MissionObjectiveKind::Kill ||
             (objective.kind == MissionObjectiveKind::Loot &&
              objective.target == "any"))) {
            if (def.id == "event_supply_drop" && m_ctx.ecs != nullptr &&
                playerPosition != nullptr) {
                float bestDistSq = 0.0f;
                bool found = false;
                m_ctx.ecs->Registry()
                    .view<ecs::Position, ecs::LootContainer>()
                    .each([&](const ecs::Entity entity,
                              const ecs::Position& position,
                              const ecs::LootContainer& loot) {
                        if (!m_ctx.ecs->Alive(entity) || loot.searched ||
                            loot.tableId != "event_supply_crate") {
                            return;
                        }
                        const float dx = position.x - playerPosition->x;
                        const float dy = position.y - playerPosition->y;
                        const float distSq = dx * dx + dy * dy;
                        if (!found || distSq < bestDistSq) {
                            found = true;
                            bestDistSq = distSq;
                            *x = position.x;
                            *y = position.y;
                        }
                    });
                if (found) {
                    return true;
                }
            }
            const auto runtime = m_randomEventRuntime.find(def.id);
            if (runtime != m_randomEventRuntime.end()) {
                *x = runtime->second.siteX;
                *y = runtime->second.siteY;
                return true;
            }
            if (playerPosition != nullptr &&
                RandomEventFallbackSite(def.id, state.day, playerPosition->x,
                                        playerPosition->y, x, y)) {
                return true;
            }
            return false;
        }
        if (objective.kind == MissionObjectiveKind::Reach) {
            *x = objective.x;
            *y = objective.y;
            return true;
        }
        if (objective.kind == MissionObjectiveKind::Poi) {
            const PoiDef* poi = m_poiConfig.Find(objective.target);
            if (poi == nullptr) {
                return false;
            }
            *x = poi->markerX;
            *y = poi->markerY;
            return true;
        }
        if (objective.kind == MissionObjectiveKind::Loot &&
            objective.target != "any") {
            bool found = false;
            if (m_ctx.ecs != nullptr) {
                m_ctx.ecs->Registry()
                    .view<ecs::Position, ecs::LootContainer>()
                    .each([&](const ecs::Entity entity,
                              const ecs::Position& position,
                              const ecs::LootContainer& loot) {
                        if (!found && m_ctx.ecs->Alive(entity) &&
                            loot.tableId == objective.target) {
                            *x = position.x;
                            *y = position.y;
                            found = true;
                        }
                    });
            }
            if (found) {
                return true;
            }
            for (const PoiDef& poi : m_poiConfig.defs) {
                for (const PoiContainerDef& container : poi.containers) {
                    if (container.tableId == objective.target) {
                        *x = container.x;
                        *y = container.y;
                        return true;
                    }
                }
            }
            if (randomEvent && playerPosition != nullptr) {
                return RandomEventFallbackSite(
                    def.id, state.day, playerPosition->x,
                    playerPosition->y, x, y);
            }
            return false;
        }
        if (objective.kind == MissionObjectiveKind::Interact &&
            objective.target == "radio") {
            *x = m_radioX;
            *y = m_radioY;
            return true;
        }
        return false;
    }
    return false;
}

void WorldDemoState::RefreshMissionGuide(ui::HudState& hud) {
    if (!m_missionConfig.enabled || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return;
    }
    const ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        return;
    }
    const ecs::MissionRunState* state = nullptr;
    const MissionDef* def = nullptr;
    const std::string focus = FocusedMissionId();
    if (!focus.empty()) {
        state = game::FindMissionState(*log, focus);
        def = m_missionConfig.Find(focus);
    }
    if (state == nullptr || def == nullptr ||
        state->status != ecs::MissionRunStatus::Active) {
        hud.missionGuide = "任务 · 暂无进行中";
        hud.missionTone = "info";
        return;
    }
    std::string directionHint;
    float targetX = 0.0f;
    float targetY = 0.0f;
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position != nullptr &&
        MissionTargetPoint(*def, *state, &targetX, &targetY)) {
        const float dx = targetX - position->x;
        const float dy = targetY - position->y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 1.0f) {
            directionHint = "目标就在附近";
        } else {
            std::ostringstream hint;
            hint << "目标" << game::PoiDirectionLabel(dx, dy) << ' '
                 << std::fixed << std::setprecision(0) << distance << " ??";
            directionHint = hint.str();
        }
    }
    hud.missionGuide = game::MissionGuideText(*def, *state, directionHint);
    hud.missionTone = game::MissionGuideTone(*def);
}

void WorldDemoState::RefreshEventGuide(ui::HudState& hud) {
    hud.eventGuide.clear();
    hud.eventTone = "warn";
    if (!m_missionConfig.enabled || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return;
    }
    const ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    const MissionDef* best = nullptr;
    const ecs::MissionRunState* bestState = nullptr;
    float bestDistance = 1.0e30f;
    float bestX = 0.0f;
    float bestY = 0.0f;
    for (const MissionDef& def : m_missionConfig.defs) {
        if (def.kind != MissionKind::Random ||
            !game::IsRandomEventMission(def.id)) {
            continue;
        }
        const ecs::MissionRunState* state =
            game::FindMissionState(*log, def.id);
        if (state == nullptr ||
            state->status != ecs::MissionRunStatus::Active) {
            continue;
        }
        float distance = 1.0e30f;
        float targetX = 0.0f;
        float targetY = 0.0f;
        if (position != nullptr &&
            MissionTargetPoint(def, *state, &targetX, &targetY)) {
            const float dx = targetX - position->x;
            const float dy = targetY - position->y;
            distance = std::sqrt(dx * dx + dy * dy);
        }
        if (best == nullptr || distance < bestDistance) {
            best = &def;
            bestState = state;
            bestDistance = distance;
            bestX = targetX;
            bestY = targetY;
        }
    }
    if (best == nullptr || bestState == nullptr) {
        return;
    }
    std::string directionHint;
    if (position != nullptr && bestDistance < 1.0e29f) {
        const float dx = bestX - position->x;
        const float dy = bestY - position->y;
        if (bestDistance <= 1.0f) {
            directionHint = "目标就在附近";
        } else {
            std::ostringstream hint;
            hint << "目标" << game::PoiDirectionLabel(dx, dy) << ' '
                 << std::fixed << std::setprecision(0) << bestDistance << " ??";
            directionHint = hint.str();
        }
    }
    hud.eventGuide =
        game::MissionEventGuideText(*best, *bestState, directionHint);
    hud.eventTone = "warn";
}

void WorldDemoState::SearchNearbyLootContainer() {
    if (m_networkMode || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_ctx.lootTables == nullptr || m_player == entt::null) {
        return;
    }
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    const ecs::Entity container = game::FindNearestLootContainer(
        *m_ctx.ecs, position->x, position->y, position->z, m_inventoryRange);
    if (container == entt::null) {
        return;
    }
    const auto* loot =
        m_ctx.ecs->Registry().try_get<ecs::LootContainer>(container);
    if (loot == nullptr || loot->searched) {
        return;
    }
    const game::LootSearchResult result =
        game::SearchLootContainer(*m_ctx.ecs, *m_ctx.items, *m_ctx.lootTables,
                                  container);
    if (!result.found) {
        return;
    }
    const items::LootTable* table = m_ctx.lootTables->Find(result.tableId);
    const std::string name = table != nullptr ? table->name : "容器";
    if (!result.error.empty()) {
        SetNotice("搜索失败：" + result.error, kNoticeFailColor, 2.5f);
        ZL_WARN("items", "搜索容器失败：{}", result.error);
        return;
    }
    if (!result.searchedNow) {
        return;
    }
    float lootXp = m_progressionConfig.lootXpPerContainer;
    if (const ecs::Progression* progression = PlayerProgression();
        progression != nullptr) {
        lootXp *= game::LootXpMultiplier(*progression, m_progressionConfig);
    }
    GrantProgressionXp(ecs::SkillKind::Survival, lootXp, "搜刮");
    RecordMissionEvent(MissionObjectiveKind::Loot, result.tableId, 1);
    const char* eventBox = game::RandomEventLootName(result.tableId);
    if (eventBox != nullptr) {
        const std::string detail = result.totalItems > 0
                                       ? "发现 " +
                                             std::to_string(result.totalItems) +
                                             " 件物??"
                                       : "为空";
        SetNotice("发现事件箱：" + std::string(eventBox) + " · " + detail,
                  kNoticeOkColor, 3.5f);
        ZL_INFO("events", "事件箱发现：{}（{}），{}", eventBox,
                result.tableId, detail);
    } else if (result.totalItems > 0) {
        SetNotice("搜索??" + name + "：发??" + std::to_string(result.totalItems) +
                      " 件物??",
                  kNoticeOkColor, 2.5f);
    } else {
        SetNotice("搜索??" + name + "：空??", kNoticeInfoColor, 2.0f);
    }
}

void WorldDemoState::EnsureForageState() {
    if (m_networkMode || m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (!registry.all_of<ecs::ForageState>(m_player)) {
        registry.emplace<ecs::ForageState>(m_player);
    }
}

std::string WorldDemoState::ForageToolName(const ForageZoneDef& zone) const {
    if (zone.toolDefId.empty()) {
        return {};
    }
    if (m_ctx.items != nullptr) {
        if (const items::ItemDef* def = m_ctx.items->Find(zone.toolDefId);
            def != nullptr) {
            return def->name;
        }
    }
    return zone.toolDefId;
}

bool WorldDemoState::HasForageTool(const ForageZoneDef& zone,
                                   std::string* toolName) const {
    if (toolName != nullptr) {
        *toolName = ForageToolName(zone);
    }
    if (zone.toolDefId.empty() || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return zone.toolDefId.empty();
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (const auto* container = registry.try_get<ecs::Container>(m_player);
        container != nullptr &&
        items::CountUsableTools(*container, zone.toolDefId) > 0) {
        return true;
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
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

bool WorldDemoState::WearForageTool(const ForageZoneDef& zone) {
    if (zone.toolDefId.empty() || zone.toolWear <= 0.0f ||
        m_ctx.ecs == nullptr || m_player == entt::null) {
        return true;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (auto* container = registry.try_get<ecs::Container>(m_player);
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
    if (auto* equipment = registry.try_get<ecs::Equipment>(m_player);
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

const ForageNode* WorldDemoState::NearestForageNode(float range) const {
    if (range <= 0.0f || m_ctx.ecs == nullptr || m_player == entt::null ||
        m_forageNodes.empty()) {
        return nullptr;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return nullptr;
    }
    const float rangeSq = range * range;
    const ForageNode* best = nullptr;
    float bestDistance = rangeSq;
    for (const ForageNode& node : m_forageNodes) {
        if (node.z != position->z) {
            continue;
        }
        const float dx = (static_cast<float>(node.x) + 0.5f) - position->x;
        const float dy = (static_cast<float>(node.y) + 0.5f) - position->y;
        const float distance = dx * dx + dy * dy;
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = &node;
        }
    }
    return best;
}

void WorldDemoState::RefreshForageNodes(float tickDt) {
    if (m_networkMode || m_ctx.world == nullptr || m_ctx.forage == nullptr ||
        m_ctx.ecs == nullptr || m_player == entt::null || m_ctx.gameTime == nullptr) {
        m_forageNodes.clear();
        m_fishingSpots.clear();
        m_foragePlanValid = false;
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr || position->z != 0) {
        m_forageNodes.clear();
        m_fishingSpots.clear();
        m_foragePlanValid = false;
        return;
    }
    EnsureForageState();
    if (auto* state = m_ctx.ecs->Registry().try_get<ecs::ForageState>(m_player);
        state != nullptr) {
        ForagePrune(*state, m_ctx.gameTime->TotalGameMinutes());
    }
    m_foragePlanTimer -= std::max(0.0f, tickDt);
    const float moveX = position->x - m_foragePlanX;
    const float moveY = position->y - m_foragePlanY;
    if (m_foragePlanValid && m_foragePlanTimer > 0.0f &&
        moveX * moveX + moveY * moveY <= 1.0f) {
        return;
    }
    const int centerX = static_cast<int>(std::floor(position->x));
    const int centerY = static_cast<int>(std::floor(position->y));
    const int radius = static_cast<int>(std::ceil(m_foragePlanRadius));
    const ForageBounds bounds{centerX - radius, centerY - radius,
                              centerX + radius, centerY + radius};
    m_forageNodes =
        PlanForageNodes(*m_ctx.forage, *m_ctx.world, m_poiConfig, bounds);
    const int fishRadius = static_cast<int>(std::ceil(m_fishingPlanRadius));
    const ForageBounds fishBounds{centerX - fishRadius, centerY - fishRadius,
                                  centerX + fishRadius, centerY + fishRadius};
    m_fishingSpots =
        PlanFishingSpots(*m_ctx.forage, *m_ctx.world, m_poiConfig, fishBounds);
    m_foragePlanX = position->x;
    m_foragePlanY = position->y;
    m_foragePlanValid = true;
    m_foragePlanTimer = m_forageScanSeconds;
}

bool WorldDemoState::TryStartForageSearch() {
    if (m_networkMode) {
        if (m_networkSurvival.mode != net::kPlayerSurvivalModeNone) {
            return true;
        }
        const ForageNode* node = NearestForageNode(m_forageInteractRange);
        if (node == nullptr) {
            return false;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::ForageStart);
        command.payload.x = static_cast<float>(node->x) + 0.5f;
        command.payload.y = static_cast<float>(node->y) + 0.5f;
        command.payload.targetIndex = node->z;
        SubmitPlayerCommand(command);
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        m_ctx.forage == nullptr || m_ctx.gameTime == nullptr) {
        return false;
    }
    const ForageNode* node = NearestForageNode(m_forageInteractRange);
    if (node == nullptr) {
        return false;
    }
    const ForageZoneDef* zone = m_ctx.forage->FindZone(node->zoneId);
    if (zone == nullptr) {
        return false;
    }
    EnsureForageState();
    auto* state = m_ctx.ecs->Registry().try_get<ecs::ForageState>(m_player);
    if (state != nullptr &&
        !ForageNodeReady(*state, *node, m_ctx.gameTime->TotalGameMinutes())) {
        const int minutes = std::max(
            1, static_cast<int>(std::ceil(ForageRespawnRemainingMinutes(
                   *state, *node, m_ctx.gameTime->TotalGameMinutes()))));
        SetNotice("已耗尽??" + zone->name + "（约 " + std::to_string(minutes) +
                      " 分钟后刷新）",
                  kNoticeInfoColor, 2.0f);
        return true;
    }
    std::string toolName;
    if (!HasForageTool(*zone, &toolName)) {
        SetNotice("需??" +
                      (toolName.empty() ? std::string("工具") : toolName) +
                      "才能采集" + zone->name,
                  kNoticeFailColor, 2.0f);
        return true;
    }
    const WeatherState weather =
        m_ctx.simulation != nullptr ? m_ctx.simulation->CurrentWeather()
                                    : WeatherState{};
    const ecs::Progression* progression = PlayerProgression();
    const int survivalLevel =
        progression != nullptr
            ? ecs::SkillLevel(*progression, ecs::SkillKind::Survival)
            : 0;
    const float duration = ForageSearchSeconds(*zone, weather, survivalLevel);
    m_forageAction = {};
    m_forageAction.active = true;
    m_forageAction.zoneId = zone->id;
    m_forageAction.zoneName = zone->name;
    m_forageAction.tableId = node->tableId;
    m_forageAction.x = node->x;
    m_forageAction.y = node->y;
    m_forageAction.z = node->z;
    m_forageAction.duration = duration;
    if (const auto* hit = m_ctx.ecs->Registry().try_get<ecs::HitReaction>(m_player);
        hit != nullptr) {
        m_forageAction.startHitSerial = hit->hitSerial;
    }
    SubmitMoveCommand(0.0f, 0.0f);
    char status[128];
    std::snprintf(status, sizeof(status), "开始觅食：%s（%.1f 秒）",
                  zone->name.c_str(), static_cast<double>(duration));
    SetNotice(status, kNoticeInfoColor, 2.0f);
    ZL_INFO("forage", "开始觅食：{} ({},{})，耗时 {:.2f}s", zone->id, node->x,
            node->y, static_cast<double>(duration));
    return true;
}

void WorldDemoState::UpdateForageSearch(float tickDt) {
    if (!m_forageAction.active) {
        return;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player) || m_ctx.gameTime == nullptr) {
        CancelForageSearch("目标失效", true);
        return;
    }
    if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
        CancelForageSearch("打开背包", true);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        CancelForageSearch("目标失效", true);
        return;
    }
    const float dx = (static_cast<float>(m_forageAction.x) + 0.5f) - position->x;
    const float dy = (static_cast<float>(m_forageAction.y) + 0.5f) - position->y;
    const float limit = m_forageInteractRange + 1.0f;
    if (dx * dx + dy * dy > limit * limit) {
        CancelForageSearch("离开范围", true);
        return;
    }
    if (const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
        hit != nullptr && hit->hitSerial != m_forageAction.startHitSerial) {
        CancelForageSearch("受击", true);
        return;
    }
    m_forageAction.elapsed += std::max(0.0f, tickDt);
    if (m_forageAction.elapsed >= m_forageAction.duration) {
        CompleteForageSearch();
    }
}

void WorldDemoState::CompleteForageSearch() {
    const ForageAction action = m_forageAction;
    m_forageAction = {};
    if (!action.active || m_ctx.forage == nullptr || m_ctx.items == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null || m_ctx.gameTime == nullptr) {
        return;
    }
    const ForageZoneDef* zone = m_ctx.forage->FindZone(action.zoneId);
    if (zone == nullptr) {
        return;
    }
    const ForageNode node{action.zoneId, action.tableId, action.x, action.y,
                          action.z};
    EnsureForageState();
    auto* state = m_ctx.ecs->Registry().try_get<ecs::ForageState>(m_player);
    const double now = m_ctx.gameTime->TotalGameMinutes();
    if (state != nullptr && !ForageNodeReady(*state, node, now)) {
        return;
    }
    if (!HasForageTool(*zone, nullptr)) {
        SetNotice("工具已不在背包中", kNoticeFailColor, 2.0f);
        return;
    }
    const items::LootTable* table = m_ctx.lootTables->Find(action.tableId);
    if (table == nullptr) {
        return;
    }
    ecs::Container rolled;
    rolled.capacityKg = table->capacityKg;
    const std::uint64_t seed = items::DeriveLootSeed(
        m_ctx.lootTables->Seed(), table->id,
        ForageLootSeed(m_ctx.forage->Seed(), node));
    (void)items::RollLootTable(*table, *m_ctx.items, rolled, seed);

    std::string found;
    for (const ecs::ItemInstance& item : rolled.items) {
        const items::ItemDef* def = m_ctx.items->Find(item.defId);
        if (!found.empty()) {
            found += "??";
        }
        found += (def != nullptr ? def->name : item.defId) + "×" +
                 std::to_string(item.count);
    }

    auto* playerContainer =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    int addedTotal = 0;
    bool dropped = false;
    for (const ecs::ItemInstance& item : rolled.items) {
        if (playerContainer == nullptr) {
            ReturnOrDropItem(item, &dropped);
            continue;
        }
        const int added =
            items::AddItem(*playerContainer, *m_ctx.items, item);
        addedTotal += added;
        if (added < item.count) {
            ecs::ItemInstance remainder = item;
            remainder.count -= added;
            ReturnOrDropItem(remainder, &dropped);
        }
    }

    if (state != nullptr) {
        ForageMarkHarvested(*state, node, zone->respawnMinutes, now);
    }
    (void)WearForageTool(*zone);
    GrantProgressionXp(ecs::SkillKind::Survival, zone->survivalXp, "觅食");
    if (rolled.items.empty()) {
        SetNotice("采集了" + zone->name + "：什么也没找到", kNoticeInfoColor,
                  2.0f);
    } else {
        std::string text = "采集??" + zone->name + "??" + found;
        if (dropped) {
            text += "（背包已满，部分掉落脚下）";
        }
        SetNotice(text, addedTotal > 0 ? kNoticeOkColor : kNoticeFailColor, 3.0f);
    }
    ZL_INFO("forage", "完成觅食：{} ({},{})，获得 {} 件，掉落={}", zone->id,
            node.x, node.y, addedTotal, dropped);
}

void WorldDemoState::CancelForageSearch(std::string_view reason, bool notice) {
    if (!m_forageAction.active) {
        return;
    }
    const std::string zoneName = m_forageAction.zoneName;
    m_forageAction = {};
    if (notice) {
        std::string text = "觅食中断：" + zoneName;
        if (!reason.empty()) {
            text += "??";
            text += reason;
            text += "??";
        }
        SetNotice(std::move(text), kNoticeInfoColor, 1.8f);
    }
    ZL_INFO("forage", "取消觅食：{}（{}）", zoneName,
            reason.empty() ? "主动取消" : std::string(reason));
}

void WorldDemoState::UpdateForage(float tickDt) {
    if (m_networkMode) {
        return;
    }
    RefreshForageNodes(tickDt);
    UpdateForageSearch(tickDt);
}

void WorldDemoState::UpdateFishing(float tickDt) {
    if (m_networkMode) {
        return;
    }
    UpdateFishingSearch(tickDt);
}

const AnimalDef* WorldDemoState::FindNearestAnimalCorpse(
    float range, ecs::Entity* corpseEntity) const {
    if (corpseEntity != nullptr) {
        *corpseEntity = entt::null;
    }
    if (m_ctx.ecs == nullptr || m_ctx.animals == nullptr ||
        m_player == entt::null || range <= 0.0f) {
        return nullptr;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return nullptr;
    }
    float bestDistance = range * range;
    ecs::Entity best = entt::null;
    const AnimalDef* bestDef = nullptr;
    m_ctx.ecs->Registry()
        .view<ecs::AnimalCorpse, ecs::Position>()
        .each([&](const ecs::Entity entity, const ecs::AnimalCorpse& corpse,
                  const ecs::Position& corpsePosition) {
            if (!m_ctx.ecs->Alive(entity) || corpse.harvested ||
                corpsePosition.z != position->z) {
                return;
            }
            const AnimalDef* def = m_ctx.animals->Find(corpse.speciesId);
            if (def == nullptr) {
                return;
            }
            const float dx = corpsePosition.x - position->x;
            const float dy = corpsePosition.y - position->y;
            const float distance = dx * dx + dy * dy;
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = entity;
                bestDef = def;
            }
        });
    if (corpseEntity != nullptr) {
        *corpseEntity = best;
    }
    return bestDef;
}

std::string WorldDemoState::AnimalCorpseName(const AnimalDef& def) const {
    return def.name + "尸体";
}

bool WorldDemoState::HasAnimalHarvestTool(const AnimalDef& def,
                                          std::string* toolName) const {
    if (toolName != nullptr) {
        *toolName = def.corpseTool;
        if (m_ctx.items != nullptr && !def.corpseTool.empty()) {
            if (const items::ItemDef* item = m_ctx.items->Find(def.corpseTool);
                item != nullptr) {
                *toolName = item->name;
            }
        }
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return def.corpseTool.empty();
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* bag = registry.try_get<ecs::Container>(m_player);
    const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    return game::AnimalCorpseToolAvailable(def, bag, equipment, nullptr);
}

bool WorldDemoState::TryStartAnimalHarvest() {
    if (m_animalHarvestAction.active || m_networkAnimalHarvest.active) {
        return true;
    }
    if (m_networkMode) {
        if (m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
            m_ctx.animals == nullptr || m_player == entt::null) {
            return false;
        }
        ecs::Entity corpse = entt::null;
        const AnimalDef* def =
            FindNearestAnimalCorpse(m_inventoryRange, &corpse);
        if (def == nullptr || corpse == entt::null) {
            return false;
        }
        std::string toolName;
        if (!HasAnimalHarvestTool(*def, &toolName)) {
            SetNotice("需??" +
                          (toolName.empty() ? std::string("工具") : toolName) +
                          "才能剥取" + AnimalCorpseName(*def),
                      kNoticeFailColor, 2.0f);
            return true;
        }
        const std::uint64_t corpseId = m_ctx.ecs->IdOf(corpse).id;
        if (corpseId == 0) {
            return false;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::AnimalHarvestStart);
        command.payload.entityId = corpseId;
        SubmitPlayerCommand(command);
        m_animalHarvestAction = {};
        m_animalHarvestAction.active = true;
        m_animalHarvestAction.corpseId = corpseId;
        m_animalHarvestAction.speciesId = def->id;
        m_animalHarvestAction.corpseName = AnimalCorpseName(*def);
        m_animalHarvestAction.duration =
            std::clamp(def->corpseSearchSeconds, 0.1f, 30.0f);
        SubmitMoveCommand(0.0f, 0.0f);
        return true;
    }
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.animals == nullptr ||
        m_player == entt::null) {
        return false;
    }
    ecs::Entity corpse = entt::null;
    const AnimalDef* def =
        FindNearestAnimalCorpse(m_inventoryRange, &corpse);
    if (def == nullptr || corpse == entt::null) {
        return false;
    }
    std::string toolName;
    if (!HasAnimalHarvestTool(*def, &toolName)) {
        SetNotice("需??" +
                      (toolName.empty() ? std::string("工具") : toolName) +
                      "才能剥取" + AnimalCorpseName(*def),
                  kNoticeFailColor, 2.0f);
        return true;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(corpse);
    if (position == nullptr) {
        return false;
    }
    const float duration =
        std::clamp(def->corpseSearchSeconds, 0.1f, 30.0f);
    m_animalHarvestAction = {};
    m_animalHarvestAction.active = true;
    m_animalHarvestAction.corpseId = m_ctx.ecs->IdOf(corpse).id;
    m_animalHarvestAction.speciesId = def->id;
    m_animalHarvestAction.corpseName = AnimalCorpseName(*def);
    m_animalHarvestAction.x = position->x;
    m_animalHarvestAction.y = position->y;
    m_animalHarvestAction.z = position->z;
    m_animalHarvestAction.duration = duration;
    if (const auto* hit =
            m_ctx.ecs->Registry().try_get<ecs::HitReaction>(m_player);
        hit != nullptr) {
        m_animalHarvestAction.startHitSerial = hit->hitSerial;
    }
    SubmitMoveCommand(0.0f, 0.0f);
    char status[128];
    std::snprintf(status, sizeof(status), "开始剥取：%s（%.1f 秒）",
                  m_animalHarvestAction.corpseName.c_str(),
                  static_cast<double>(duration));
    SetNotice(status, kNoticeInfoColor, 2.0f);
    ZL_INFO("animals", "开始剥取：{}（实体 {}），耗时 {:.2f}s", def->id,
            m_animalHarvestAction.corpseId, static_cast<double>(duration));
    return true;
}

void WorldDemoState::UpdateAnimalHarvest(float tickDt) {
    if (!m_animalHarvestAction.active) {
        return;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        CancelAnimalHarvest("目标失效", true);
        return;
    }
    if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
        CancelAnimalHarvest("打开背包", true);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const ecs::Entity corpse =
        m_ctx.ecs->FindById(m_animalHarvestAction.corpseId);
    const auto* corpseData =
        corpse != entt::null ? registry.try_get<ecs::AnimalCorpse>(corpse)
                             : nullptr;
    if (corpseData == nullptr || corpseData->harvested) {
        CancelAnimalHarvest("目标失效", true);
        return;
    }
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        CancelAnimalHarvest("目标失效", true);
        return;
    }
    const float dx = m_animalHarvestAction.x - position->x;
    const float dy = m_animalHarvestAction.y - position->y;
    const float limit = m_inventoryRange + 1.0f;
    if (dx * dx + dy * dy > limit * limit) {
        CancelAnimalHarvest("离开范围", true);
        return;
    }
    if (const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
        hit != nullptr &&
        hit->hitSerial != m_animalHarvestAction.startHitSerial) {
        CancelAnimalHarvest("受击", true);
        return;
    }
    m_animalHarvestAction.elapsed += std::max(0.0f, tickDt);
    if (m_animalHarvestAction.elapsed >= m_animalHarvestAction.duration) {
        CompleteAnimalHarvest();
    }
}

void WorldDemoState::CompleteAnimalHarvest() {
    const AnimalHarvestAction action = m_animalHarvestAction;
    m_animalHarvestAction = {};
    if (!action.active || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.animals == nullptr ||
        m_player == entt::null) {
        return;
    }
    const ecs::Entity corpse = m_ctx.ecs->FindById(action.corpseId);
    if (corpse == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* corpseData = registry.try_get<ecs::AnimalCorpse>(corpse);
    if (corpseData == nullptr || corpseData->harvested) {
        return;
    }
    auto* bag = registry.try_get<ecs::Container>(m_player);
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    const AnimalHarvestResult result = game::HarvestAnimalCorpse(
        *m_ctx.ecs, *m_ctx.items, *m_ctx.lootTables, *m_ctx.animals, corpse,
        bag, equipment);
    if (result.missingTool) {
        std::string toolName = result.toolId;
        if (m_ctx.items != nullptr) {
            if (const items::ItemDef* item = m_ctx.items->Find(result.toolId);
                item != nullptr) {
                toolName = item->name;
            }
        }
        SetNotice("需??" +
                      (toolName.empty() ? std::string("工具") : toolName) +
                      "才能剥取" + action.corpseName,
                  kNoticeFailColor, 2.0f);
        return;
    }
    if (!result.error.empty() && !result.harvested) {
        SetNotice("剥取失败：" + result.error, kNoticeFailColor, 2.5f);
        return;
    }

    std::string found;
    if (const auto* container = registry.try_get<ecs::Container>(corpse);
        container != nullptr) {
        for (const ecs::ItemInstance& item : container->items) {
            const items::ItemDef* def = m_ctx.items->Find(item.defId);
            if (!found.empty()) {
                found += "??";
            }
            found += (def != nullptr ? def->name : item.defId) + "×" +
                     std::to_string(item.count);
        }
    }
    if (result.harvested) {
        std::string text = "剥取??" + action.corpseName + "??" +
                           (found.empty() ? std::string("没有可用材料") : found);
        if (!found.empty()) {
            text += "（战利品在尸体中，按 I 取走）";
        }
        if (result.toolBroke) {
            text += "（工具已损坏）";
        }
        SetNotice(text, found.empty() ? kNoticeInfoColor : kNoticeOkColor,
                  3.0f);
        ZL_INFO("animals", "剥取完成：{}，实体 {}，{} 堆/{} 件", action.speciesId,
                action.corpseId, result.itemStacks, result.itemsAdded);
    } else if (!result.error.empty()) {
        SetNotice("剥取失败：" + result.error, kNoticeFailColor, 2.5f);
    }
}

void WorldDemoState::CancelAnimalHarvest(std::string_view reason,
                                         bool notice) {
    if (!m_animalHarvestAction.active) {
        return;
    }
    const std::string corpseName = m_animalHarvestAction.corpseName;
    m_animalHarvestAction = {};
    if (notice) {
        std::string text = "剥取中断：" + corpseName;
        if (!reason.empty()) {
            text += "??";
            text += reason;
            text += "??";
        }
        SetNotice(std::move(text), kNoticeInfoColor, 1.8f);
    }
    ZL_INFO("animals", "取消剥取：{}（{}）", corpseName,
            reason.empty() ? "主动取消" : std::string(reason));
}

void WorldDemoState::CancelNetworkAnimalHarvest() {
    if (!m_networkMode ||
        (!m_networkAnimalHarvest.active && !m_animalHarvestAction.active)) {
        return;
    }
    m_networkAnimalHarvest = {};
    m_animalHarvestAction = {};
    SubmitPlayerCommand(
        MakePlayerCommand(net::CommandType::AnimalHarvestCancel));
}

std::string WorldDemoState::FishingItemName(std::string_view defId) const {
    if (defId.empty()) {
        return {};
    }
    if (m_ctx.items != nullptr) {
        if (const items::ItemDef* def = m_ctx.items->Find(std::string(defId));
            def != nullptr) {
            return def->name;
        }
    }
    return std::string(defId);
}

bool WorldDemoState::HasFishingRod(const FishingSpotDef& spot) const {
    if (spot.rodDefId.empty() || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return spot.rodDefId.empty();
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (const auto* container = registry.try_get<ecs::Container>(m_player);
        container != nullptr &&
        items::CountUsableTools(*container, spot.rodDefId) > 0) {
        return true;
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
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

int WorldDemoState::FishingBaitCount(const FishingSpotDef& spot) const {
    if (spot.baitDefId.empty() || spot.baitCost <= 0 ||
        m_ctx.ecs == nullptr || m_player == entt::null) {
        return 0;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* container = registry.try_get<ecs::Container>(m_player);
    return container != nullptr
               ? items::CountOf(*container, spot.baitDefId)
               : 0;
}

bool WorldDemoState::ConsumeFishingBait(const FishingSpotDef& spot) {
    if (spot.baitDefId.empty() || spot.baitCost <= 0) {
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        items::CountOf(*container, spot.baitDefId) < spot.baitCost) {
        return false;
    }
    return items::RemoveItem(*container, spot.baitDefId, spot.baitCost) ==
           spot.baitCost;
}

bool WorldDemoState::WearFishingRod(const FishingSpotDef& spot) {
    if (spot.rodDefId.empty() || spot.rodWear <= 0.0f ||
        m_ctx.ecs == nullptr || m_player == entt::null) {
        return true;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (auto* container = registry.try_get<ecs::Container>(m_player);
        container != nullptr) {
        for (auto it = container->items.begin(); it != container->items.end();
             ++it) {
            if (it->defId != spot.rodDefId || it->condition <= 0.0f) {
                continue;
            }
            it->condition = std::max(0.0f, it->condition - spot.rodWear);
            if (it->condition <= 0.0f) {
                container->items.erase(it);
                SetNotice("鱼竿已损坏", kNoticeFailColor, 2.0f);
            }
            return true;
        }
    }
    if (auto* equipment = registry.try_get<ecs::Equipment>(m_player);
        equipment != nullptr) {
        for (auto& slot : equipment->slots) {
            if (!slot.has_value() || slot->defId != spot.rodDefId ||
                slot->condition <= 0.0f) {
                continue;
            }
            slot->condition = std::max(0.0f, slot->condition - spot.rodWear);
            if (slot->condition <= 0.0f) {
                slot.reset();
                SetNotice("鱼竿已损坏", kNoticeFailColor, 2.0f);
            }
            return true;
        }
    }
    return false;
}

const FishingSpot* WorldDemoState::NearestFishingSpot(float range) const {
    if (range <= 0.0f || m_ctx.ecs == nullptr || m_player == entt::null ||
        m_ctx.forage == nullptr || m_ctx.world == nullptr ||
        m_fishingSpots.empty()) {
        return nullptr;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return nullptr;
    }
    const float rangeSq = range * range;
    const FishingSpot* best = nullptr;
    float bestDistance = rangeSq;
    for (const FishingSpot& spot : m_fishingSpots) {
        if (spot.z != position->z) {
            continue;
        }
        const FishingSpotDef* def = m_ctx.forage->FindSpot(spot.spotId);
        if (def == nullptr ||
            !FishingSpotWaterValid(*def, *m_ctx.world, spot)) {
            continue;
        }
        const float dx = (static_cast<float>(spot.x) + 0.5f) - position->x;
        const float dy = (static_cast<float>(spot.y) + 0.5f) - position->y;
        const float distance = dx * dx + dy * dy;
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = &spot;
        }
    }
    return best;
}

bool WorldDemoState::TryStartFishing() {
    if (m_networkMode) {
        if (m_networkSurvival.mode != net::kPlayerSurvivalModeNone) {
            return true;
        }
        const FishingSpot* spot = NearestFishingSpot(m_fishingInteractRange);
        if (spot == nullptr) {
            return false;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::FishingStart);
        command.payload.x = static_cast<float>(spot->x) + 0.5f;
        command.payload.y = static_cast<float>(spot->y) + 0.5f;
        command.payload.targetIndex = spot->z;
        SubmitPlayerCommand(command);
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        m_ctx.forage == nullptr || m_ctx.world == nullptr ||
        m_ctx.items == nullptr) {
        return false;
    }
    const FishingSpot* spot = NearestFishingSpot(m_fishingInteractRange);
    if (spot == nullptr) {
        return false;
    }
    const FishingSpotDef* def = m_ctx.forage->FindSpot(spot->spotId);
    if (def == nullptr ||
        !FishingSpotWaterValid(*def, *m_ctx.world, *spot)) {
        return false;
    }
    if (!HasFishingRod(*def)) {
        SetNotice("需??" +
                      (def->rodDefId.empty()
                           ? std::string("鱼竿")
                           : FishingItemName(def->rodDefId)) +
                      "才能钓鱼",
                  kNoticeFailColor, 2.0f);
        return true;
    }
    if (def->baitCost > 0 && FishingBaitCount(*def) < def->baitCost) {
        SetNotice("需??" +
                      (def->baitDefId.empty()
                           ? std::string("鱼饵")
                           : FishingItemName(def->baitDefId)) +
                      "才能钓鱼",
                  kNoticeFailColor, 2.0f);
        return true;
    }
    if (!ConsumeFishingBait(*def)) {
        SetNotice("鱼饵不足", kNoticeFailColor, 2.0f);
        return true;
    }
    const WeatherState weather =
        m_ctx.simulation != nullptr ? m_ctx.simulation->CurrentWeather()
                                    : WeatherState{};
    const ecs::Progression* progression = PlayerProgression();
    const int survivalLevel =
        progression != nullptr
            ? ecs::SkillLevel(*progression, ecs::SkillKind::Survival)
            : 0;
    const float wait = FishingWaitSeconds(*def, weather, survivalLevel);
    const float chance = FishingCatchChance(*def, weather, survivalLevel);
    m_fishingAction = {};
    m_fishingAction.active = true;
    m_fishingAction.spotId = spot->spotId;
    m_fishingAction.spotName = def->name;
    m_fishingAction.tableId = def->tableId;
    m_fishingAction.trashTableId = def->trashTableId;
    m_fishingAction.rodDefId = def->rodDefId;
    m_fishingAction.baitDefId = def->baitDefId;
    m_fishingAction.x = spot->x;
    m_fishingAction.y = spot->y;
    m_fishingAction.z = spot->z;
    m_fishingAction.waterX = spot->waterX;
    m_fishingAction.waterY = spot->waterY;
    m_fishingAction.waterZ = spot->waterZ;
    m_fishingAction.waitDuration = wait;
    m_fishingAction.biteDuration = def->biteWindowSeconds;
    m_fishingAction.catchChance = chance;
    if (const auto* hit =
            m_ctx.ecs->Registry().try_get<ecs::HitReaction>(m_player);
        hit != nullptr) {
        m_fishingAction.startHitSerial = hit->hitSerial;
    }
    SubmitMoveCommand(0.0f, 0.0f);
    char status[160];
    std::snprintf(status, sizeof(status), "抛竿 %s，等待咬钩（%.1f 秒）",
                  def->name.c_str(), static_cast<double>(wait));
    SetNotice(status, kNoticeInfoColor, 2.0f);
    ZL_INFO("fishing", "开始钓鱼：{} ({},{}) → 水面 ({},{})，等待 {:.2f}s，成功率 {:.2f}",
            def->id, spot->x, spot->y, spot->waterX, spot->waterY,
            static_cast<double>(wait), static_cast<double>(chance));
    return true;
}

void WorldDemoState::UpdateFishingSearch(float tickDt) {
    if (!m_fishingAction.active) {
        return;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player) || m_ctx.forage == nullptr ||
        m_ctx.world == nullptr) {
        CancelFishing("目标失效", true);
        return;
    }
    if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
        CancelFishing("打开背包", true);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        CancelFishing("目标失效", true);
        return;
    }
    const FishingSpotDef* def = m_ctx.forage->FindSpot(m_fishingAction.spotId);
    if (def == nullptr) {
        CancelFishing("钓鱼点失效", true);
        return;
    }
    const FishingSpot spot{m_fishingAction.spotId, m_fishingAction.tableId,
                           m_fishingAction.x, m_fishingAction.y,
                           m_fishingAction.z, m_fishingAction.waterX,
                           m_fishingAction.waterY, m_fishingAction.waterZ};
    if (!FishingSpotWaterValid(*def, *m_ctx.world, spot)) {
        CancelFishing("水面变化", true);
        return;
    }
    const float dx = (static_cast<float>(m_fishingAction.x) + 0.5f) - position->x;
    const float dy = (static_cast<float>(m_fishingAction.y) + 0.5f) - position->y;
    const float limit = m_fishingInteractRange + 1.0f;
    if (dx * dx + dy * dy > limit * limit) {
        CancelFishing("离开范围", true);
        return;
    }
    if (const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
        hit != nullptr && hit->hitSerial != m_fishingAction.startHitSerial) {
        CancelFishing("受击", true);
        return;
    }
    if (!m_fishingAction.biting) {
        m_fishingAction.elapsed += std::max(0.0f, tickDt);
        if (m_fishingAction.elapsed >= m_fishingAction.waitDuration) {
            m_fishingAction.biting = true;
            m_fishingAction.biteElapsed = 0.0f;
            SetNotice("咬钩了！??E 收杆??" + m_fishingAction.spotName,
                      kNoticeInfoColor, 2.0f);
            ZL_INFO("fishing", "咬钩：{}", m_fishingAction.spotId);
        }
        return;
    }
    m_fishingAction.biteElapsed += std::max(0.0f, tickDt);
    if (m_fishingAction.biteElapsed >= m_fishingAction.biteDuration) {
        const std::string name = m_fishingAction.spotName;
        (void)WearFishingRod(*def);
        m_fishingAction = {};
        SetNotice("错过收杆，鱼跑了：" + name, kNoticeInfoColor, 2.0f);
        ZL_INFO("fishing", "错过收杆：{}", name);
    }
}

void WorldDemoState::ResolveFishing(bool caught) {
    const FishingAction action = m_fishingAction;
    m_fishingAction = {};
    if (!action.active || m_ctx.forage == nullptr || m_ctx.items == nullptr ||
        m_ctx.lootTables == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return;
    }
    const FishingSpotDef* def = m_ctx.forage->FindSpot(action.spotId);
    if (def == nullptr) {
        return;
    }
    const std::string tableId =
        caught ? action.tableId : action.trashTableId;
    const items::LootTable* table =
        tableId.empty() ? nullptr : m_ctx.lootTables->Find(tableId);
    ecs::Container rolled;
    if (table != nullptr) {
        rolled.capacityKg = table->capacityKg;
        const FishingSpot spot{action.spotId, action.tableId, action.x,
                               action.y, action.z, action.waterX, action.waterY,
                               action.waterZ};
        const std::uint64_t seed = items::DeriveLootSeed(
            m_ctx.lootTables->Seed(), table->id,
            FishingLootSeed(m_ctx.forage->Seed(), spot, caught));
        (void)items::RollLootTable(*table, *m_ctx.items, rolled, seed);
    }
    std::string found;
    for (const ecs::ItemInstance& item : rolled.items) {
        const items::ItemDef* itemDef = m_ctx.items->Find(item.defId);
        if (!found.empty()) {
            found += "??";
        }
        found += (itemDef != nullptr ? itemDef->name : item.defId) + "×" +
                 std::to_string(item.count);
    }
    auto* playerContainer =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    int addedTotal = 0;
    bool dropped = false;
    for (const ecs::ItemInstance& item : rolled.items) {
        if (playerContainer == nullptr) {
            ReturnOrDropItem(item, &dropped);
            continue;
        }
        const int added =
            items::AddItem(*playerContainer, *m_ctx.items, item);
        addedTotal += added;
        if (added < item.count) {
            ecs::ItemInstance remainder = item;
            remainder.count -= added;
            ReturnOrDropItem(remainder, &dropped);
        }
    }
    (void)WearFishingRod(*def);
    if (caught) {
        GrantProgressionXp(ecs::SkillKind::Survival, def->survivalXp, "钓鱼");
    }
    if (!caught) {
        SetNotice(found.empty()
                      ? "收杆太早，鱼跑了"
                      : "没钓到鱼，捞到：" + found,
                  kNoticeInfoColor, 2.5f);
    } else if (rolled.items.empty()) {
        SetNotice("钓到鱼了，但鱼篓是空的", kNoticeInfoColor, 2.0f);
    } else {
        std::string text = "钓到??" + found;
        if (dropped) {
            text += "（背包已满，部分掉落脚下）";
        }
        SetNotice(text, addedTotal > 0 ? kNoticeOkColor : kNoticeFailColor, 3.0f);
    }
    ZL_INFO("fishing", "完成钓鱼：{}，成功={}，获得 {} 件，掉落={}", def->id,
            caught, addedTotal, dropped);
}

void WorldDemoState::CancelFishing(std::string_view reason, bool notice) {
    if (!m_fishingAction.active) {
        return;
    }
    const std::string name = m_fishingAction.spotName;
    m_fishingAction = {};
    if (notice) {
        std::string text = "钓鱼中断：" + name;
        if (!reason.empty()) {
            text += "??";
            text += reason;
            text += "??";
        }
        SetNotice(std::move(text), kNoticeInfoColor, 1.8f);
    }
    ZL_INFO("fishing", "取消钓鱼：{}（{}）", name,
            reason.empty() ? "主动取消" : std::string(reason));
}

bool WorldDemoState::PlayerHasRadioPart() const {
    if (m_ctx.ecs == nullptr || m_player == entt::null || m_ctx.items == nullptr) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (const auto* container = registry.try_get<ecs::Container>(m_player);
        container != nullptr && items::CountOf(*container, m_radioPartDefId) > 0) {
        return true;
    }
    if (const auto* equipment = registry.try_get<ecs::Equipment>(m_player);
        equipment != nullptr) {
        for (const auto& slot : equipment->slots) {
            if (slot.has_value() && slot->defId == m_radioPartDefId) {
                return true;
            }
        }
    }
    return false;
}

bool WorldDemoState::RadioPartExistsInWorld() const {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    bool found = false;
    registry.view<ecs::Container>().each(
        [&](const ecs::Entity entity, const ecs::Container& container) {
            if (!found && m_ctx.ecs->Alive(entity) &&
                items::CountOf(container, m_radioPartDefId) > 0) {
                found = true;
            }
        });
    if (found) {
        return true;
    }
    registry.view<ecs::ItemInstance>().each(
        [&](const ecs::Entity entity, const ecs::ItemInstance& item) {
            if (!found && m_ctx.ecs->Alive(entity) && item.defId == m_radioPartDefId) {
                found = true;
            }
        });
    if (found) {
        return true;
    }
    registry.view<ecs::Equipment>().each([&](const ecs::Equipment& equipment) {
        for (const auto& slot : equipment.slots) {
            if (!found && slot.has_value() && slot->defId == m_radioPartDefId) {
                found = true;
            }
        }
    });
    return found;
}

void WorldDemoState::UpdateRunStats(double tickDt) {
    if (m_networkMode || m_runOutcome != RunOutcome::None || m_ctx.ecs == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_player)) {
        return;
    }
    if (m_ctx.gameTime != nullptr && m_ctx.gameTime->SpeedScale() <= 0.0) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* health = registry.try_get<ecs::Health>(m_player);
    if (health == nullptr) {
        return;
    }
    const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
    if (m_runStatsPlayer != m_player) {
        m_runStatsPlayer = m_player;
        m_runSurvivalSeconds = 0.0;
        m_runInjuries = 0;
        m_runDamageTaken = 0.0f;
        m_runDeathCause = ecs::DamageCause::Unknown;
        m_runLastHealth = health->current;
        m_runHitSerial = hit != nullptr ? hit->hitSerial : 0;
        return;
    }

    m_runSurvivalSeconds += std::max(0.0, tickDt);
    if (hit != nullptr && hit->hitSerial > m_runHitSerial) {
        m_runInjuries += hit->hitSerial - m_runHitSerial;
    }
    if (hit != nullptr) {
        m_runHitSerial = hit->hitSerial;
    }

    const bool tookDamage =
        m_runLastHealth > health->current && m_runLastHealth >= 0.0f;
    if (tookDamage) {
        m_runDamageTaken += m_runLastHealth - health->current;
        if (const auto* last = registry.try_get<ecs::LastDamage>(m_player);
            last != nullptr) {
            m_runDeathCause = last->cause;
        }
    }
    m_runLastHealth = health->current;
}

std::string WorldDemoState::BuildRunStats() const {
    game::RunSummary summary;
    summary.victory = m_runOutcome == RunOutcome::Escaped;
    summary.deathCause = m_runDeathCause;
    summary.survivalSeconds = m_runSurvivalSeconds;
    summary.injuries = m_runInjuries;
    summary.damageTaken = m_runDamageTaken;
    if (m_ctx.gameTime != nullptr) {
        const auto& now = m_ctx.gameTime->Now();
        summary.day =
            1 + static_cast<int>(m_ctx.gameTime->TotalGameMinutes() / (24.0 * 60.0));
        summary.hour = now.hour;
        summary.minute = now.minute;
    }
    summary.kills = m_ctx.combat != nullptr ? m_ctx.combat->GetStats().kills : 0;
    return game::RunSummaryText(summary);
}

ecs::Progression* WorldDemoState::PlayerProgression() {
    if (m_networkMode) {
        return m_networkProgressionValid ? &m_networkProgression : nullptr;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return nullptr;
    }
    return m_ctx.ecs->Registry().try_get<ecs::Progression>(m_player);
}

const ecs::Progression* WorldDemoState::PlayerProgression() const {
    if (m_networkMode) {
        return m_networkProgressionValid ? &m_networkProgression : nullptr;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        return nullptr;
    }
    return m_ctx.ecs->Registry().try_get<ecs::Progression>(m_player);
}

void WorldDemoState::ApplyProgressionRuntime() {
    if (m_networkMode || m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* progression = registry.try_get<ecs::Progression>(m_player);
    if (progression == nullptr) {
        progression = &registry.emplace<ecs::Progression>(m_player);
    }
    game::RefreshDerived(*progression, m_progressionConfig);
    if (auto* container = registry.try_get<ecs::Container>(m_player);
        container != nullptr) {
        container->capacityKg =
            m_playerCapacityKg + progression->carryCapacityBonusKg;
    }
    if (auto* control = registry.try_get<ecs::LocalControl>(m_player);
        control != nullptr) {
        control->walkSpeed =
            m_baseWalkSpeed *
            game::MoveSpeedMultiplier(*progression, m_progressionConfig);
    }
}

void WorldDemoState::InitializeProgressionRuntime() {
    if (m_networkMode || m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    if (m_ctx.ecs->Registry().try_get<ecs::Progression>(m_player) == nullptr) {
        m_ctx.ecs->Registry().emplace<ecs::Progression>(m_player);
    }
    m_progressionPlayer = m_player;
    if (m_ctx.gameTime != nullptr) {
        m_lastSurvivalXpMinute = m_ctx.gameTime->TotalGameMinutes();
    }
    ApplyProgressionRuntime();
    if (const ecs::Progression* progression = PlayerProgression();
        progression != nullptr) {
        ZL_INFO("progression", "成长数据就绪：Lv.{}，Perk 点 {}，负重 +{:.1f}kg",
                progression->level, progression->perkPoints,
                progression->carryCapacityBonusKg);
    }
}

void WorldDemoState::GrantProgressionXp(ecs::SkillKind kind, float baseAmount,
                                        const char* reason) {
    if (m_networkMode || !m_progressionConfig.enabled) {
        return;
    }
    ecs::Progression* progression = PlayerProgression();
    if (progression == nullptr || !(baseAmount > 0.0f)) {
        return;
    }
    const int oldLevel = progression->level;
    const int oldSkillLevel = ecs::SkillLevel(*progression, kind);
    const game::ProgressionAward award =
        game::AwardXp(*progression, kind, baseAmount, m_progressionConfig);
    ApplyProgressionRuntime();
    const game::SkillRules* skill = game::FindSkill(m_progressionConfig, kind);
    const std::string skillName = skill != nullptr ? skill->name : "技??";
    if (award.characterLevels > 0) {
        SetNotice("升级！Lv." + std::to_string(progression->level) +
                      "（可分配 Perk ×" +
                      std::to_string(progression->perkPoints) +
                      "，按 1/2/3 选择??",
                  kNoticeOkColor, 3.0f);
    } else if (progression->level > oldLevel) {
        SetNotice("升级！Lv." + std::to_string(progression->level),
                  kNoticeOkColor, 2.5f);
    } else if (ecs::SkillLevel(*progression, kind) > oldSkillLevel) {
        SetNotice(skillName + " 升到 Lv." +
                      std::to_string(ecs::SkillLevel(*progression, kind)),
                  kNoticeOkColor, 2.0f);
    } else if (reason != nullptr && baseAmount >= 40.0f) {
        ZL_DEBUG("progression", "{}经验 +{:.0f}", reason,
                 static_cast<double>(baseAmount));
    }
}

void WorldDemoState::UpdateProgression(float tickDt) {
    if (m_networkMode || !m_progressionConfig.enabled || m_ctx.ecs == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_player) ||
        m_runOutcome != RunOutcome::None) {
        return;
    }
    if (m_progressionPlayer != m_player) {
        InitializeProgressionRuntime();
    }
    if (PlayerProgression() == nullptr) {
        return;
    }

    // 近战：CombatSystem 只记录实际命中的扇形判定；空挥/未命中不产生经验。
    if (m_ctx.combat != nullptr) {
        const int oldLevel = PlayerProgression()->level;
        const int oldMelee = ecs::SkillLevel(*PlayerProgression(),
                                             ecs::SkillKind::Melee);
        std::vector<ecs::Entity> changed;
        game::AwardCombatHits(*m_ctx.ecs, *m_ctx.combat, m_progressionConfig,
                              &changed);
        const bool localChanged =
            std::find(changed.begin(), changed.end(), m_player) != changed.end();
        if (localChanged) {
            ApplyProgressionRuntime();
            const ecs::Progression& progression = *PlayerProgression();
            if (progression.level > oldLevel) {
                SetNotice("升级！Lv." + std::to_string(progression.level) +
                              "（可分配 Perk ×" +
                              std::to_string(progression.perkPoints) +
                              "，按 1/2/3 选择??",
                          kNoticeOkColor, 3.0f);
            } else if (ecs::SkillLevel(progression, ecs::SkillKind::Melee) >
                       oldMelee) {
                SetNotice("近战升到 Lv." +
                              std::to_string(ecs::SkillLevel(
                                  progression, ecs::SkillKind::Melee)),
                          kNoticeOkColor, 2.0f);
            }
        }
    }

    // 生存：按完整游戏小时结算；睡眠/倍速跳过的游戏时间同样计入。
    if (m_ctx.gameTime == nullptr) {
        return;
    }
    const double now = m_ctx.gameTime->TotalGameMinutes();
    if (m_lastSurvivalXpMinute < 0.0 || now < m_lastSurvivalXpMinute) {
        m_lastSurvivalXpMinute = now;
        return;
    }
    const double delta = now - m_lastSurvivalXpMinute;
    if (delta < 60.0) {
        return;
    }
    const int hours = static_cast<int>(std::floor(delta / 60.0));
    m_lastSurvivalXpMinute += static_cast<double>(hours) * 60.0;
    GrantProgressionXp(
        ecs::SkillKind::Survival,
        m_progressionConfig.survivalXpPerHour * static_cast<float>(hours),
        "生存");
    (void)tickDt;
}

bool WorldDemoState::SelectPerk(int index) {
    if (index < 0 || index >= 3) {
        return false;
    }
    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::ChoosePerk);
        command.payload.index = index;
        return SubmitPlayerCommand(command);
    }
    ecs::Progression* progression = PlayerProgression();
    if (progression == nullptr) {
        SetNotice("Perk 选择无效", kNoticeFailColor, 1.5f);
        return false;
    }
    const std::array<std::string, 3> choices = CurrentPerkChoices();
    const std::string chosen =
        index < static_cast<int>(choices.size()) ? choices[static_cast<std::size_t>(index)]
                                                 : std::string();
    if (!game::ChoosePerkByIndex(*progression, index, m_progressionConfig)) {
        SetNotice("Perk 选择无效", kNoticeFailColor, 1.5f);
        return false;
    }
    ApplyProgressionRuntime();
    std::string name = "Perk";
    if (const game::PerkRules* perk = game::FindPerk(m_progressionConfig, chosen);
        perk != nullptr) {
        name = perk->name;
    }
    SetNotice("已选择??" + name + "（剩??Perk ×" +
                  std::to_string(progression->perkPoints) + "??",
              kNoticeOkColor, 2.5f);
    return true;
}

int WorldDemoState::PendingPerkPoints() const {
    const ecs::Progression* progression = PlayerProgression();
    return progression != nullptr ? progression->perkPoints : 0;
}

std::array<std::string, 3> WorldDemoState::CurrentPerkChoices() const {
    if (m_networkMode) {
        return m_networkPerkChoices;
    }
    if (const ecs::Progression* progression = PlayerProgression();
        progression != nullptr) {
        return game::PerkChoiceIds(*progression, m_progressionConfig);
    }
    return {};
}

float WorldDemoState::RepairAmountForPlayer() const {
    const ecs::Progression* progression = PlayerProgression();
    if (progression == nullptr) {
        return m_structureRepairAmount;
    }
    return m_structureRepairAmount *
           game::RepairAmountMultiplier(*progression, m_progressionConfig);
}

float WorldDemoState::CraftSecondsForPlayer(const items::RecipeDef& recipe,
                                            float efficiency) const {
    const float base = items::EffectiveCraftSeconds(recipe, efficiency);
    const ecs::Progression* progression = PlayerProgression();
    if (progression == nullptr) {
        return base;
    }
    return base *
           game::CraftDurationScale(*progression, m_progressionConfig);
}

const BuildableDef* WorldDemoState::SelectedBuildable() const {
    if (m_buildables.empty()) {
        return nullptr;
    }
    const int index = std::clamp(
        m_buildIndex, 0, static_cast<int>(m_buildables.size()) - 1);
    return &m_buildables[static_cast<std::size_t>(index)];
}

void WorldDemoState::RebuildBuildables() {
    if (m_ctx.config == nullptr) {
        return;
    }
    m_buildables = LoadBuildableDefs(*m_ctx.config);
    AppendBuildableDefs(m_buildables, m_ctx.modBuildables);
    for (BuildableDef& def : m_buildables) {
        if (!def.requiresMission.empty() &&
            m_missionConfig.Find(def.requiresMission) == nullptr) {
            ZL_WARN("building", "建造项 '{}' 的任务门槛 '{}' 不存在，已解除",
                    def.id, def.requiresMission);
            def.requiresMission.clear();
        }
    }
    if (m_buildables.empty()) {
        m_buildIndex = 0;
    } else {
        m_buildIndex = std::clamp(
            m_buildIndex, 0, static_cast<int>(m_buildables.size()) - 1);
    }
    ZL_INFO("building", "建造目录：{} 项（mod {} 项）", m_buildables.size(),
            m_ctx.modBuildables.size());
}

void WorldDemoState::RefreshDynamicContent() {
    if (m_ctx.poiConfig != nullptr) {
        m_poiConfig = *m_ctx.poiConfig;
    }
    if (m_ctx.missionConfig != nullptr) {
        m_missionConfig = *m_ctx.missionConfig;
    }
    if (m_ctx.settlements != nullptr) {
        m_settlementConfig = *m_ctx.settlements;
    }
    if (!m_missionFocusId.empty() &&
        m_missionConfig.Find(m_missionFocusId) == nullptr) {
        m_missionFocusId.clear();
    }
    RebuildBuildables();
    m_dynamicContentApplied = m_ctx.dynamicContentCount;
    m_contentReloadApplied = m_ctx.contentReloadCount;
    ZL_INFO("content", "世界状态内容刷新：建筑 {}，POI {}，任务 {}，聚落 {}",
            m_buildables.size(), m_poiConfig.defs.size(),
            m_missionConfig.defs.size(), m_settlementConfig.defs.size());
}

const BuildableDef* WorldDemoState::BuildableForSprite(
    std::string_view sprite) const {
    for (const BuildableDef& def : m_buildables) {
        if (BuildableMatchesSprite(def, sprite)) {
            return &def;
        }
    }
    return nullptr;
}

std::string WorldDemoState::BuildMenuText() const {
    const BuildableDef* def = SelectedBuildable();
    if (def == nullptr) {
        return {};
    }
    std::string text = "建??[" +
                       std::string(BuildCategoryLabel(def->category)) + "] " +
                       def->name + " (" +
                       std::to_string(m_buildIndex + 1) + "/" +
                       std::to_string(m_buildables.size()) + ")";
    if (BuildableSupportsVertical(*def)) {
        text += m_buildOrientationVertical ? " · 竖向" : " · 横向";
    }
    if (!def->requiresMission.empty()) {
        const ecs::MissionLog* log = PlayerMissions();
        const ecs::MissionRunState* state =
            log != nullptr ? game::FindMissionState(*log, def->requiresMission)
                           : nullptr;
        if (state == nullptr ||
            state->status != ecs::MissionRunStatus::Completed) {
            const MissionDef* mission =
                m_missionConfig.Find(def->requiresMission);
            text += " · 需任务??" +
                    (mission != nullptr ? mission->name : def->requiresMission);
        }
    }
    return text;
}

void WorldDemoState::ToggleBuildOrientation() {
    const BuildableDef* def = SelectedBuildable();
    if (def == nullptr) {
        return;
    }
    if (!BuildableSupportsVertical(*def)) {
        SetNotice(def->name + " 没有可旋转的朝向", kNoticeInfoColor, 1.2f);
        return;
    }
    m_buildOrientationVertical = !m_buildOrientationVertical;
    SetNotice("建造朝向：" +
                  std::string(m_buildOrientationVertical ? "竖向" : "横向"),
              kNoticeInfoColor, 1.2f);
}

void WorldDemoState::ToggleBuildMode() {
    if (m_runOutcome != RunOutcome::None) {
        return;
    }
    if (m_buildables.empty()) {
        SetNotice("没有可用的建造项", kNoticeFailColor, 2.0f);
        return;
    }
    m_buildMode = !m_buildMode;
    if (!m_buildMode) {
        m_pendingDemolish = {};
        ZL_INFO("building", "退出建造模式");
        SetNotice("退出建造模式", kNoticeInfoColor, 1.2f);
        return;
    }
    m_buildIndex = std::clamp(
        m_buildIndex, 0, static_cast<int>(m_buildables.size()) - 1);
    const BuildableDef* def = SelectedBuildable();
    ZL_INFO("building", "进入建造模式：{}（{} 木材）", def->id, def->woodCost);
    SetNotice("建造模式：" + def->name + "（" +
                  std::to_string(def->woodCost) + " 木材） · 滚轮切换",
              kNoticeInfoColor, 2.5f);
}

bool WorldDemoState::CanPlaceBuild(const BuildableDef& def, int wx, int wy,
                                   std::string* reason) const {
    if (reason != nullptr) {
        reason->clear();
    }
    const auto fail = [reason](std::string text) {
        if (reason != nullptr) {
            *reason = std::move(text);
        }
        return false;
    };
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return fail("无法建??");
    }
    if (def.id == "farm_plot" && !m_homesteadConfig.farm.enabled) {
        return fail("农业系统已关闭");
    }
    if ((def.id == "generator" || def.id == "fridge") &&
        !m_homesteadConfig.power.enabled) {
        return fail("电力系统已关闭");
    }
    if (def.id == "spike_trap" && !m_homesteadConfig.trap.enabled) {
        return fail("陷阱系统已关闭");
    }
    if (!def.requiresMission.empty()) {
        const ecs::MissionLog* log = PlayerMissions();
        const ecs::MissionRunState* state =
            log != nullptr ? game::FindMissionState(*log, def.requiresMission)
                           : nullptr;
        if (state == nullptr ||
            state->status != ecs::MissionRunStatus::Completed) {
            const MissionDef* mission =
                m_missionConfig.Find(def.requiresMission);
            return fail("需要完成任务：" +
                        (mission != nullptr ? mission->name
                                            : def.requiresMission));
        }
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    const auto* health = registry.try_get<ecs::Health>(m_player);
    const auto* container = registry.try_get<ecs::Container>(m_player);
    if (position == nullptr || health == nullptr || health->current <= 0.0f ||
        container == nullptr) {
        return fail("无法建??");
    }
    const float distance = std::hypot(
        position->x - (static_cast<float>(wx) + 0.5f),
        position->y - (static_cast<float>(wy) + 0.5f));
    if (distance > m_buildRange) {
        return fail("距离过远");
    }
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return fail("目标区域未加载");
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    const world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
    if (!square.HasFloor()) {
        return fail("目标格没有地面");
    }
    if (!IsBuildCellFreeFor(def, square)) {
        return fail("目标格被占用");
    }
    bool occupied = false;
    registry.view<ecs::Position>().each(
        [&](ecs::Entity, const ecs::Position& entityPos) {
            if (!occupied && entityPos.z == 0 &&
                static_cast<int>(std::floor(entityPos.x)) == wx &&
                static_cast<int>(std::floor(entityPos.y)) == wy) {
                occupied = true;
            }
        });
    if (occupied) {
        return fail("目标格被角色或物品占用");
    }
    if (items::CountOf(*container, m_buildWoodItem) < def.woodCost) {
        return fail("木材不足（需要 " + std::to_string(def.woodCost) + "）");
    }
    return true;
}

bool WorldDemoState::FindNearestPlayerBuiltStructure(
    float range, bool barricadesOnly, bool damagedOnly, PlayerBuildTarget* out,
    std::string_view requiredId, bool includeFloors, bool editableOnly) const {
    if (out == nullptr || range <= 0.0f || m_ctx.ecs == nullptr ||
        m_ctx.world == nullptr || m_player == entt::null) {
        return false;
    }
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }

    const int minX =
        static_cast<int>(std::floor(position->x - range));
    const int maxX =
        static_cast<int>(std::ceil(position->x + range));
    const int minY =
        static_cast<int>(std::floor(position->y - range));
    const int maxY =
        static_cast<int>(std::ceil(position->y + range));
    const float rangeSq = range * range;
    float bestDistance = rangeSq;
    bool found = false;
    const std::array<world::SquareLayer, 3> layers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture,
        world::SquareLayer::Floor};
    const std::size_t layerCount = includeFloors ? layers.size() : 2u;

    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            for (std::size_t layerIndex = 0; layerIndex < layerCount;
                 ++layerIndex) {
                const world::SquareLayer layer = layers[layerIndex];
                const auto& objects =
                    chunk->Square(local.lx, local.ly, 0).Layer(layer);
                for (std::size_t objectIndex = 0; objectIndex < objects.size();
                     ++objectIndex) {
                    const world::SquareObject& object = objects[objectIndex];
                    if (!object.playerBuilt) {
                        continue;
                    }
                    if (editableOnly &&
                        !CanEditLocalStructure(object.ownerStableId)) {
                        continue;
                    }
                    const BuildableDef* def = BuildableForSprite(object.sprite);
                    if (def == nullptr) {
                        continue;
                    }
                    if (!requiredId.empty() && def->id != requiredId) {
                        continue;
                    }
                    if (barricadesOnly && def->maxHp <= 0.0f) {
                        continue;
                    }
                    if (damagedOnly &&
                        !IsRepairableBarricade(object, def->maxHp)) {
                        continue;
                    }
                    const float dx =
                        position->x - (static_cast<float>(wx) + 0.5f);
                    const float dy =
                        position->y - (static_cast<float>(wy) + 0.5f);
                    const float distance = dx * dx + dy * dy;
                    if (distance > rangeSq || distance > bestDistance) {
                        continue;
                    }
                    bestDistance = distance;
                    out->wx = wx;
                    out->wy = wy;
                    out->objectIndex = objectIndex;
                    out->layer = layer;
                    out->def = def;
                    out->sprite = object.sprite;
                    out->hp = def->maxHp > 0.0f
                                  ? ResolveBarricadeHp(object, def->maxHp)
                                  : 0.0f;
                    out->structureId = object.structureId;
                    out->ownerStableId = object.ownerStableId;
                    found = true;
                }
            }
        }
    }
    return found;
}

ecs::Entity WorldDemoState::FindNearestFloodlight(float range,
                                                  bool damagedOnly,
                                                  float* outDistance,
                                                  bool editableOnly) const {
    if (outDistance != nullptr) {
        *outDistance = 0.0f;
    }
    if (range <= 0.0f || m_ctx.ecs == nullptr || m_player == entt::null) {
        return entt::null;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (playerPos == nullptr) {
        return entt::null;
    }
    const float rangeSq = range * range;
    float bestDistance = rangeSq;
    ecs::Entity best = entt::null;
    registry.view<ecs::Floodlight, ecs::Position>().each(
        [&](const ecs::Entity entity, const ecs::Floodlight&,
            const ecs::Position& position) {
            if (position.z != playerPos->z) {
                return;
            }
            if (editableOnly &&
                !CanEditLocalStructure(EntityOwnerStableId(entity))) {
                return;
            }
            const auto* health = registry.try_get<ecs::Health>(entity);
            if (damagedOnly &&
                (health == nullptr || health->current <= 0.0f ||
                 health->current >= health->max - 0.01f)) {
                return;
            }
            const float dx = position.x - playerPos->x;
            const float dy = position.y - playerPos->y;
            const float distance = dx * dx + dy * dy;
            if (distance > bestDistance) {
                return;
            }
            bestDistance = distance;
            best = entity;
        });
    if (best != entt::null && outDistance != nullptr) {
        *outDistance = std::sqrt(bestDistance);
    }
    return best;
}

bool WorldDemoState::TryRepairNearestBarricade() {
    if (m_runOutcome != RunOutcome::None || m_buildActionCooldown > 0.0f) {
        return false;
    }
    PlayerBuildTarget target;
    const bool hasDamagedStructure = FindNearestPlayerBuiltStructure(
        m_buildActionRange, true, true, &target, {}, false, true);
    const float structureDistance =
        hasDamagedStructure ? std::sqrt(BuildTargetDistanceSq(target)) : 0.0f;
    float lightDistance = 0.0f;
    const ecs::Entity light =
        FindNearestFloodlight(m_buildActionRange, true, &lightDistance, true);
    if (!hasDamagedStructure && light == entt::null) {
        PlayerBuildTarget fullTarget;
        const bool hasFullStructure = FindNearestPlayerBuiltStructure(
            m_buildActionRange, true, false, &fullTarget, {}, false, true);
        const ecs::Entity fullLight =
            FindNearestFloodlight(m_buildActionRange, false, nullptr, true);
        if (!hasFullStructure && fullLight == entt::null) {
            SetNotice("附近没有可维修的结构", kNoticeFailColor, 1.5f);
            return false;
        }
        SetNotice("结构耐久已满", kNoticeInfoColor, 1.2f);
        return false;
    }

    if (light != entt::null &&
        (!hasDamagedStructure || lightDistance < structureDistance)) {
        if (m_ctx.ecs == nullptr) {
            return false;
        }
        entt::registry& registry = m_ctx.ecs->Registry();
        auto* health = registry.try_get<ecs::Health>(light);
        const auto* position = registry.try_get<ecs::Position>(light);
        if (health == nullptr || position == nullptr || health->max <= 0.0f) {
            return false;
        }
        const std::uint64_t owner = EntityOwnerStableId(light);
        if (!CanEditLocalStructure(owner)) {
            SetNotice("该设施属于 " + StructureOwnerLabel(owner) +
                          "，只有所有者可维修",
                      kNoticeFailColor, 1.8f);
            return false;
        }
        if (health->current >= health->max - 0.01f) {
            SetNotice("泛光灯耐久已满", kNoticeInfoColor, 1.2f);
            return false;
        }
        if (m_networkMode) {
            net::Command command =
                MakePlayerCommand(net::CommandType::RepairStructure);
            command.payload.x = static_cast<float>(
                static_cast<int>(std::floor(position->x)));
            command.payload.y = static_cast<float>(
                static_cast<int>(std::floor(position->y)));
            if (!SubmitPlayerCommand(command)) {
                SetNotice("维修请求发送失??", kNoticeFailColor, 1.5f);
                return false;
            }
            m_buildActionCooldown = m_buildActionCooldownSeconds;
            SetNotice("已发送维修请求", kNoticeInfoColor, 1.2f);
            return true;
        }
        if (m_ctx.items == nullptr || m_player == entt::null) {
            return false;
        }
        auto* container =
            registry.try_get<ecs::Container>(m_player);
        if (container == nullptr ||
            items::CountOf(*container, m_buildWoodItem) < m_repairWoodCost) {
            SetNotice("维修需??" + std::to_string(m_repairWoodCost) +
                          " 木材",
                      kNoticeFailColor, 1.8f);
            return false;
        }
        const float oldHp = health->current;
        const float newHp = std::min(
            health->max, health->current + RepairAmountForPlayer());
        if (newHp <= oldHp) {
            SetNotice("泛光灯耐久已满", kNoticeInfoColor, 1.2f);
            return false;
        }
        if (items::RemoveItem(*container, m_buildWoodItem, m_repairWoodCost) !=
            m_repairWoodCost) {
            SetNotice("维修失败：木材不??", kNoticeFailColor, 1.8f);
            return false;
        }
        health->current = newHp;
        m_buildActionCooldown = m_buildActionCooldownSeconds;
        SetNotice("维修 泛光灯：HP " +
                      std::to_string(static_cast<int>(std::lround(newHp))) +
                      "/" +
                      std::to_string(static_cast<int>(
                          std::lround(health->max))) +
                      "??" + std::to_string(m_repairWoodCost) + " 木材??",
                  kNoticeOkColor, 2.0f);
        ZL_INFO("homestead", "维修泛光灯：({}, {}) HP {:.0f}/{:.0f}，剩余木材 {}",
                position->x, position->y, newHp, health->max,
                items::CountOf(*container, m_buildWoodItem));
        return true;
    }

    if (!CanEditLocalStructure(target.ownerStableId)) {
        SetNotice("该结构属于 " + StructureOwnerLabel(target.ownerStableId) +
                      "，只有所有者可维修",
                  kNoticeFailColor, 1.8f);
        return false;
    }

    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::RepairStructure);
        command.payload.x = static_cast<float>(target.wx);
        command.payload.y = static_cast<float>(target.wy);
        if (!SubmitPlayerCommand(command)) {
            SetNotice("维修请求发送失??", kNoticeFailColor, 1.5f);
            return false;
        }
        m_buildActionCooldown = m_buildActionCooldownSeconds;
            SetNotice("已发送维修请求", kNoticeInfoColor, 1.2f);
        return true;
    }
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return false;
    }
    auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        items::CountOf(*container, m_buildWoodItem) < m_repairWoodCost) {
        SetNotice("维修需??" + std::to_string(m_repairWoodCost) + " 木材",
                  kNoticeFailColor, 1.8f);
        return false;
    }

    const world::ChunkCoord coord = world::ChunkOf(target.wx, target.wy);
    world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        SetNotice("维修失败：目标区域未加载", kNoticeFailColor, 1.8f);
        return false;
    }
    const world::SquareLocal local =
        world::SquareLocalOf(target.wx, target.wy);
    auto& objects =
        chunk->Square(local.lx, local.ly, 0).Layer(target.layer);
    if (target.objectIndex >= objects.size()) {
        return false;
    }
    world::SquareObject& object = objects[target.objectIndex];
    if (!object.playerBuilt ||
        !BuildableMatchesSprite(*target.def, object.sprite)) {
        return false;
    }

    const float oldHp =
        ResolveBarricadeHp(object, target.def->maxHp);
    const float newHp = RepairBarricadeHp(
        oldHp, target.def->maxHp, RepairAmountForPlayer());
    if (newHp <= oldHp) {
        SetNotice("结构耐久已满", kNoticeInfoColor, 1.2f);
        return false;
    }
    if (items::RemoveItem(*container, m_buildWoodItem, m_repairWoodCost) !=
        m_repairWoodCost) {
        SetNotice("维修失败：木材不??", kNoticeFailColor, 1.8f);
        return false;
    }

    object.hp = newHp;
    if (object.blocksMovement) {
        m_barricades[BuildCellKey(target.wx, target.wy)] =
            BuiltBarricade{object.sprite, newHp, target.def->maxHp};
    }
    m_buildActionCooldown = m_buildActionCooldownSeconds;
    SetNotice("维修 " + target.def->name + "：HP " +
                  std::to_string(static_cast<int>(std::lround(newHp))) + "/" +
                  std::to_string(
                      static_cast<int>(std::lround(target.def->maxHp))) +
                  "??" + std::to_string(m_repairWoodCost) + " 木材??",
              kNoticeOkColor, 2.0f);
    ZL_INFO("building", "维修 {}??{}, {}) HP {:.0f}/{:.0f}，剩余木??{}",
            target.def->id, target.wx, target.wy, newHp,
            target.def->maxHp,
            items::CountOf(*container, m_buildWoodItem));
    return true;
}

int WorldDemoState::CountDamagedPlayerStructuresNear(int* woodCost) const {
    if (woodCost != nullptr) {
        *woodCost = 0;
    }
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr || m_player == entt::null) {
        return 0;
    }
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return 0;
    }
    const int minX = static_cast<int>(std::floor(position->x - m_buildActionRange));
    const int maxX = static_cast<int>(std::ceil(position->x + m_buildActionRange));
    const int minY = static_cast<int>(std::floor(position->y - m_buildActionRange));
    const int maxY = static_cast<int>(std::ceil(position->y + m_buildActionRange));
    const float rangeSq = m_buildActionRange * m_buildActionRange;
    int count = 0;
    constexpr std::array<world::SquareLayer, 2> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture};
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx = position->x - (static_cast<float>(wx) + 0.5f);
            const float dy = position->y - (static_cast<float>(wy) + 0.5f);
            if (dx * dx + dy * dy > rangeSq) {
                continue;
            }
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            for (const world::SquareLayer layer : kLayers) {
                for (const world::SquareObject& object :
                     chunk->Square(local.lx, local.ly, 0).Layer(layer)) {
                    if (!object.playerBuilt ||
                        !CanEditLocalStructure(object.ownerStableId)) {
                        continue;
                    }
                    const BuildableDef* def = BuildableForSprite(object.sprite);
                    if (def != nullptr &&
                        IsRepairableBarricade(object, def->maxHp)) {
                        ++count;
                    }
                }
            }
        }
    }
    if (woodCost != nullptr) {
        *woodCost = count * m_repairWoodCost;
    }
    return count;
}

bool WorldDemoState::TryRepairAllStructures() {
    if (m_runOutcome != RunOutcome::None || m_buildActionCooldown > 0.0f) {
        return false;
    }
    if (m_networkMode) {
        SetNotice("联机模式暂不支持维修全部，请逐个维修", kNoticeInfoColor,
                  1.8f);
        return false;
    }
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return false;
    }
    struct Candidate {
        int wx = 0;
        int wy = 0;
        std::size_t objectIndex = 0;
        world::SquareLayer layer = world::SquareLayer::Furniture;
        const BuildableDef* def = nullptr;
        float newHp = 0.0f;
    };
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }
    std::vector<Candidate> candidates;
    const int minX = static_cast<int>(std::floor(position->x - m_buildActionRange));
    const int maxX = static_cast<int>(std::ceil(position->x + m_buildActionRange));
    const int minY = static_cast<int>(std::floor(position->y - m_buildActionRange));
    const int maxY = static_cast<int>(std::ceil(position->y + m_buildActionRange));
    const float rangeSq = m_buildActionRange * m_buildActionRange;
    constexpr std::array<world::SquareLayer, 2> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture};
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx = position->x - (static_cast<float>(wx) + 0.5f);
            const float dy = position->y - (static_cast<float>(wy) + 0.5f);
            if (dx * dx + dy * dy > rangeSq) {
                continue;
            }
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            for (const world::SquareLayer layer : kLayers) {
                const auto& objects =
                    chunk->Square(local.lx, local.ly, 0).Layer(layer);
                for (std::size_t objectIndex = 0; objectIndex < objects.size();
                     ++objectIndex) {
                    const world::SquareObject& object = objects[objectIndex];
                    if (!object.playerBuilt ||
                        !CanEditLocalStructure(object.ownerStableId)) {
                        continue;
                    }
                    const BuildableDef* def = BuildableForSprite(object.sprite);
                    if (def == nullptr ||
                        !IsRepairableBarricade(object, def->maxHp)) {
                        continue;
                    }
                    const float hp = ResolveBarricadeHp(object, def->maxHp);
                    candidates.push_back(
                        Candidate{wx, wy, objectIndex, layer, def,
                                  RepairBarricadeHp(hp, def->maxHp,
                                                    RepairAmountForPlayer())});
                }
            }
        }
    }
    if (candidates.empty()) {
        SetNotice("附近没有需要维修的结构", kNoticeInfoColor, 1.5f);
        return false;
    }
    const int totalWood =
        static_cast<int>(candidates.size()) * m_repairWoodCost;
    auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        items::CountOf(*container, m_buildWoodItem) < totalWood) {
        SetNotice("维修全部需要 " + std::to_string(totalWood) +
                      " 木材??" + std::to_string(candidates.size()) + " 个结构）",
                  kNoticeFailColor, 2.0f);
        return false;
    }
    if (totalWood > 0 &&
        items::RemoveItem(*container, m_buildWoodItem, totalWood) != totalWood) {
        SetNotice("维修失败：木材不??", kNoticeFailColor, 1.8f);
        return false;
    }
    int repaired = 0;
    for (const Candidate& candidate : candidates) {
        const world::ChunkCoord coord =
            world::ChunkOf(candidate.wx, candidate.wy);
        world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
        if (chunk == nullptr ||
            chunk->State() != world::ChunkState::Loaded) {
            continue;
        }
        const world::SquareLocal local =
            world::SquareLocalOf(candidate.wx, candidate.wy);
        auto& objects =
            chunk->Square(local.lx, local.ly, 0).Layer(candidate.layer);
        if (candidate.objectIndex >= objects.size()) {
            continue;
        }
        world::SquareObject& object = objects[candidate.objectIndex];
        if (!object.playerBuilt ||
            !BuildableMatchesSprite(*candidate.def, object.sprite)) {
            continue;
        }
        object.hp = candidate.newHp;
        if (object.blocksMovement) {
            m_barricades[BuildCellKey(candidate.wx, candidate.wy)] =
                BuiltBarricade{object.sprite, candidate.newHp,
                               candidate.def->maxHp};
        }
        ++repaired;
    }
    m_buildActionCooldown = m_buildActionCooldownSeconds;
    SetNotice("维修全部：" + std::to_string(repaired) + " 个结构（-" +
                  std::to_string(totalWood) + " 木材??",
              kNoticeOkColor, 2.2f);
    ZL_INFO("building", "批量维修 {} 个结构，消耗 {} 木材", repaired,
            totalWood);
    return repaired > 0;
}

bool WorldDemoState::FindNearbyDoor(PlayerBuildTarget* out,
                                    bool editableOnly) const {
    return FindNearestPlayerBuiltStructure(m_buildActionRange, false, false, out,
                                           "door", false, editableOnly);
}

float WorldDemoState::BuildTargetDistanceSq(
    const PlayerBuildTarget& target) const {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return 0.0f;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return 0.0f;
    }
    const float dx =
        position->x - (static_cast<float>(target.wx) + 0.5f);
    const float dy =
        position->y - (static_cast<float>(target.wy) + 0.5f);
    return dx * dx + dy * dy;
}

bool WorldDemoState::IsCellOccupiedByLiving(int wx, int wy) const {
    if (m_ctx.ecs == nullptr) {
        return false;
    }
    bool occupied = false;
    m_ctx.ecs->Registry().view<ecs::Position, ecs::Health>().each(
        [&](ecs::Entity, const ecs::Position& position,
            const ecs::Health& health) {
            if (occupied || health.current <= 0.0f || position.z != 0) {
                return;
            }
            occupied = static_cast<int>(std::floor(position.x)) == wx &&
                       static_cast<int>(std::floor(position.y)) == wy;
        });
    return occupied;
}

bool WorldDemoState::TryToggleNearestDoor() {
    if (m_runOutcome != RunOutcome::None || m_buildActionCooldown > 0.0f) {
        return false;
    }
    PlayerBuildTarget target;
    if (!FindNearbyDoor(&target, true)) {
        if (FindNearbyDoor(&target)) {
            SetNotice("该门属于 " +
                          StructureOwnerLabel(target.ownerStableId) +
                          "，只有所有者可开??",
                      kNoticeFailColor, 1.8f);
        }
        return false;
    }
    if (!CanEditLocalStructure(target.ownerStableId)) {
        SetNotice("该门属于 " + StructureOwnerLabel(target.ownerStableId) +
                      "，只有所有者可开??",
                  kNoticeFailColor, 1.8f);
        return false;
    }
    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::ToggleDoor);
        command.payload.x = static_cast<float>(target.wx);
        command.payload.y = static_cast<float>(target.wy);
        if (!SubmitPlayerCommand(command)) {
            SetNotice("开关门请求发送失??", kNoticeFailColor, 1.5f);
            return false;
        }
        m_buildActionCooldown = 0.25f;
        SetNotice("已发送开关门请求", kNoticeInfoColor, 1.2f);
        return true;
    }
    if (m_ctx.world == nullptr) {
        return false;
    }
    const world::ChunkCoord coord = world::ChunkOf(target.wx, target.wy);
    world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(target.wx, target.wy);
    auto& objects = chunk->Square(local.lx, local.ly, 0).Layer(target.layer);
    if (target.objectIndex >= objects.size()) {
        return false;
    }
    world::SquareObject& object = objects[target.objectIndex];
    if (!object.playerBuilt || !target.def->door ||
        !BuildableMatchesSprite(*target.def, object.sprite)) {
        return false;
    }
    const bool vertical = IsVerticalBuildSprite(*target.def, object.sprite);
    const bool open = IsOpenDoorSprite(*target.def, object.sprite);
    if (open && IsCellOccupiedByLiving(target.wx, target.wy)) {
        SetNotice("门口被挡住，无法关门", kNoticeFailColor, 1.5f);
        return true;
    }
    object.sprite =
        std::string(BuildableSprite(*target.def, vertical, !open));
    object.blocksMovement = open;
    if (object.blocksMovement) {
        m_barricades[BuildCellKey(target.wx, target.wy)] =
            BuiltBarricade{object.sprite,
                           ResolveBarricadeHp(object, target.def->maxHp),
                           target.def->maxHp};
    } else {
        m_barricades.erase(BuildCellKey(target.wx, target.wy));
    }
    RebuildChunkCollision(coord.cx, coord.cy);
    m_buildActionCooldown = 0.25f;
    SetNotice(std::string(open ? "关门??" : "开门：") + target.def->name,
              kNoticeOkColor, 1.4f);
    ZL_INFO("building", "{} {}??{}, {})", open ? "关门" : "开??",
            target.def->id, target.wx, target.wy);
    return true;
}

bool WorldDemoState::TryDemolishNearestStructure() {
    if (!m_buildMode || m_runOutcome != RunOutcome::None ||
        m_buildActionCooldown > 0.0f) {
        return false;
    }
    PlayerBuildTarget target;
    bool hasStructure =
        FindNearestPlayerBuiltStructure(m_buildActionRange, false, false, &target);
    if (!hasStructure) {
        hasStructure = FindNearestPlayerBuiltStructure(
            m_buildActionRange, false, false, &target, {}, true);
    }
    if (m_networkMode) {
        int wx = 0;
        int wy = 0;
        int layer = 0;
        PendingDemolishKind kind = PendingDemolishKind::None;
        std::uint64_t token = 0;
        std::uint64_t owner = 0;
        std::string name;
        bool found = hasStructure;
        if (found) {
            wx = target.wx;
            wy = target.wy;
            layer = static_cast<int>(target.layer);
            kind = PendingDemolishKind::Map;
            token = target.structureId != 0 ? target.structureId
                                            : ecs::HashName(target.sprite);
            owner = target.ownerStableId;
            name = target.def != nullptr ? target.def->name : "结构";
        } else {
            const HomesteadTarget homestead = FindNearestHomesteadTarget();
            if (homestead.kind == HomesteadTargetKind::Farm ||
                homestead.kind == HomesteadTargetKind::Generator ||
                homestead.kind == HomesteadTargetKind::Floodlight ||
                homestead.kind == HomesteadTargetKind::Trap) {
                const auto* position =
                    m_ctx.ecs->Registry().try_get<ecs::Position>(
                        homestead.entity);
                if (position != nullptr) {
                    wx = static_cast<int>(std::floor(position->x));
                    wy = static_cast<int>(std::floor(position->y));
                    kind = PendingDemolishKind::Entity;
                    token = m_ctx.ecs->IdOf(homestead.entity).id;
                    owner = EntityOwnerStableId(homestead.entity);
                    const char* id =
                        homestead.kind == HomesteadTargetKind::Farm
                            ? "farm_plot"
                            : homestead.kind ==
                                      HomesteadTargetKind::Generator
                                  ? "generator"
                                  : homestead.kind ==
                                            HomesteadTargetKind::Floodlight
                                        ? "floodlight"
                                        : "spike_trap";
                    const BuildableDef* facilityDef = BuildableById(id);
                    name = facilityDef != nullptr ? facilityDef->name : id;
                    found = true;
                }
            } else if (const ecs::Entity storage = FindNearbyStorage(false);
                       storage != entt::null) {
                if (const auto* storagePosition =
                        m_ctx.ecs->Registry().try_get<ecs::Position>(storage);
                    storagePosition != nullptr) {
                    wx = static_cast<int>(std::floor(storagePosition->x));
                    wy = static_cast<int>(std::floor(storagePosition->y));
                    kind = PendingDemolishKind::Entity;
                    token = m_ctx.ecs->IdOf(storage).id;
                    owner = EntityOwnerStableId(storage);
                    const BuildableDef* storageDef =
                        StorageBuildable(storage);
                    name = storageDef != nullptr ? storageDef->name : "仓库";
                    found = true;
                }
            }
        }
        if (!found || kind == PendingDemolishKind::None) {
            SetNotice("附近没有可拆除的己方结构", kNoticeFailColor, 1.5f);
            return false;
        }
        if (!CanEditLocalStructure(owner)) {
            SetNotice("该结构属于 " + StructureOwnerLabel(owner) +
                          "，只有所有者可拆除",
                      kNoticeFailColor, 1.8f);
            return false;
        }
        if (!BeginOrConfirmDemolish(kind, wx, wy, layer, token, name)) {
            return true;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::DemolishStructure);
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.flags = net::kDemolishConfirmAcceptFlag;
        if (!SubmitPlayerCommand(command)) {
            SetNotice("拆除请求发送失??", kNoticeFailColor, 1.5f);
            return false;
        }
        m_buildActionCooldown = m_buildActionCooldownSeconds;
        SetNotice("已发送拆除请求", kNoticeInfoColor, 1.2f);
        return true;
    }
    const ecs::Entity storage = FindNearbyStorage(false);
    if (!hasStructure && storage == entt::null) {
        if (TryDemolishHomestead()) {
            return true;
        }
        SetNotice("附近没有可拆除的己方结构", kNoticeFailColor, 1.5f);
        return false;
    }
    if (storage != entt::null) {
        float storageDistance = 0.0f;
        float structureDistance = 0.0f;
        const auto* playerPosition =
            m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
        const auto* storagePosition =
            m_ctx.ecs->Registry().try_get<ecs::Position>(storage);
        if (playerPosition == nullptr) {
            return false;
        }
        if (storagePosition != nullptr) {
            storageDistance = std::hypot(
                playerPosition->x - storagePosition->x,
                playerPosition->y - storagePosition->y);
        }
        if (hasStructure) {
            structureDistance = std::hypot(
                playerPosition->x - (static_cast<float>(target.wx) + 0.5f),
                playerPosition->y - (static_cast<float>(target.wy) + 0.5f));
        }
        const bool storageInRange = storageDistance <= m_buildActionRange;
        if (storageInRange &&
            (!hasStructure || storageDistance <= structureDistance)) {
            return TryDemolishStorage(storage);
        }
    }
    if (!hasStructure) {
        SetNotice("附近没有可拆除的己方结构", kNoticeFailColor, 1.5f);
        return false;
    }

    const world::ChunkCoord coord = world::ChunkOf(target.wx, target.wy);
    world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        SetNotice("拆除失败：目标区域未加载", kNoticeFailColor, 1.8f);
        return false;
    }
    const world::SquareLocal local =
        world::SquareLocalOf(target.wx, target.wy);
    world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
    auto& objects = square.Layer(target.layer);
    if (target.objectIndex >= objects.size()) {
        return false;
    }
    const world::SquareObject& object = objects[target.objectIndex];
    if (!object.playerBuilt ||
        !BuildableMatchesSprite(*target.def, object.sprite)) {
        return false;
    }
    const std::uint64_t owner = object.ownerStableId;
    if (!CanEditLocalStructure(owner)) {
        SetNotice("该结构属于 " + StructureOwnerLabel(owner) +
                      "，只有所有者可拆除",
                  kNoticeFailColor, 1.8f);
        return false;
    }
    const std::uint64_t token =
        object.structureId != 0 ? object.structureId
                                : ecs::HashName(object.sprite);
    if (!BeginOrConfirmDemolish(PendingDemolishKind::Map, target.wx,
                                target.wy,
                                static_cast<int>(target.layer), token,
                                target.def->name)) {
        return true;
    }

    const std::string name = target.def->name;
    const bool blocksMovement = object.blocksMovement;
    const std::string underlay = object.underlay;
    const int refund =
        BuildWoodRefund(target.def->woodCost, m_demolishRefundRatio);
    objects.erase(objects.begin() +
                  static_cast<std::ptrdiff_t>(target.objectIndex));
    if (target.layer == world::SquareLayer::Floor) {
        square.SetFloor(world::SquareObject{
            underlay.empty() ? std::string("tile.floor.grass") : underlay, 0, 0,
            false});
    } else if (blocksMovement) {
        RebuildChunkCollision(coord.cx, coord.cy);
    }
    m_barricades.erase(BuildCellKey(target.wx, target.wy));

    if (refund > 0) {
        ecs::ItemInstance wood;
        wood.defId = m_buildWoodItem;
        wood.count = refund;
        ReturnOrDropItem(wood);
    }

    m_buildActionCooldown = m_buildActionCooldownSeconds;
    std::string notice = "拆除 " + name;
    if (refund > 0) {
        notice += "（返??" + std::to_string(refund) + " 木材??";
    }
    SetNotice(std::move(notice), kNoticeOkColor, 2.0f);
    ZL_INFO("building", "拆除 {}??{}, {})，返??{} 木材", target.def->id,
            target.wx, target.wy, refund);
    return true;
}

ecs::Entity WorldDemoState::FindNearbyStorage(bool includeVehicle) const {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return entt::null;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return entt::null;
    }
    ecs::Entity vehicle = m_vehicle;
    if (!m_driving && !m_riding) {
        const ecs::Entity nearest =
            FindNearestVehicle(m_vehicleTuning.enterRange);
        if (nearest != entt::null) {
            vehicle = nearest;
        }
    }
    if (includeVehicle && !m_driving && vehicle != entt::null &&
        m_ctx.ecs->Alive(vehicle) &&
        registry.all_of<ecs::PlayerStorage, ecs::Container>(vehicle)) {
        const auto* vehiclePosition =
            registry.try_get<ecs::Position>(vehicle);
        if (vehiclePosition != nullptr && vehiclePosition->z == position->z) {
            return vehicle;
        }
    }

    ecs::Entity best = entt::null;
    float bestDistSq = m_inventoryRange * m_inventoryRange;
    registry.view<ecs::Position, ecs::PlayerStorage, ecs::Container>().each(
        [&](const ecs::Entity entity, const ecs::Position& storagePosition,
            const ecs::PlayerStorage&, const ecs::Container&) {
            if (entity == m_vehicle ||
                registry.all_of<ecs::Vehicle>(entity)) {
                return;
            }
            if (storagePosition.z != position->z) {
                return;
            }
            const float dx = storagePosition.x - position->x;
            const float dy = storagePosition.y - position->y;
            const float distSq = dx * dx + dy * dy;
            if (distSq > bestDistSq) {
                return;
            }
            if (best == entt::null || distSq < bestDistSq) {
                best = entity;
                bestDistSq = distSq;
            }
        });
    return best;
}

bool WorldDemoState::CanAccessLocalStorage(ecs::Entity storage) {
    const std::uint64_t owner = EntityOwnerStableId(storage);
    if (CanEditLocalStructure(owner)) {
        return true;
    }
    SetNotice("该仓库属于 " + StructureOwnerLabel(owner) +
                  "，只有所有者可存取",
              kNoticeFailColor, 1.8f);
    return false;
}

const BuildableDef* WorldDemoState::StorageBuildable(ecs::Entity storage) const {
    if (!game::IsPlayerStorage(*m_ctx.ecs, storage)) {
        return nullptr;
    }
    const auto* sprite =
        m_ctx.ecs->Registry().try_get<ecs::SpriteRef>(storage);
    const auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(storage);
    const auto* meta =
        m_ctx.ecs->Registry().try_get<ecs::PlayerStorage>(storage);
    if (sprite == nullptr || container == nullptr || meta == nullptr) {
        return nullptr;
    }
    for (const BuildableDef& def : m_buildables) {
        if (IsStorageBuildable(def) &&
            ecs::HashName(def.sprite) == sprite->sprite &&
            def.storageSlots == meta->maxStacks &&
            std::abs(def.storageKg - container->capacityKg) <= 0.01f) {
            return &def;
        }
    }
    return nullptr;
}

std::string WorldDemoState::StorageName(ecs::Entity storage) const {
    if (m_ctx.ecs != nullptr && storage != entt::null &&
        m_ctx.ecs->Alive(storage) &&
        m_ctx.ecs->Registry().all_of<ecs::Vehicle>(storage)) {
        return "皮卡后备箱";
    }
    const BuildableDef* def = StorageBuildable(storage);
    return def != nullptr ? def->name : "仓库";
}

void WorldDemoState::StoreBagItemInStorage(int index) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity storage = FindNearbyStorage();
    if (storage == entt::null) {
        SetNotice("附近没有仓库", kNoticeFailColor, 1.5f);
        return;
    }
    if (!CanAccessLocalStorage(storage)) {
        return;
    }
    auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        static_cast<std::size_t>(index) >= container->items.size()) {
        return;
    }
    const std::string defId =
        container->items[static_cast<std::size_t>(index)].defId;
    const game::StorageTransferResult result =
        game::StorePlayerStorageItem(*m_ctx.ecs, *m_ctx.items, storage,
                                     *container, index, 9999);
    const items::ItemDef* def = m_ctx.items->Find(defId);
    const std::string name = def != nullptr ? def->name : defId;
    if (result.moved > 0) {
        SetNotice("存入 " + name + " ×" + std::to_string(result.moved),
                  kNoticeOkColor, 1.5f);
        ZL_INFO("items", "仓库存入：{} x{}", name, result.moved);
    } else {
        SetNotice("仓库已满，无法存入 " + name, kNoticeFailColor, 1.8f);
    }
}

void WorldDemoState::StoreAllBagInStorage() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity storage = FindNearbyStorage();
    if (storage == entt::null) {
        SetNotice("附近没有仓库", kNoticeFailColor, 1.5f);
        return;
    }
    if (!CanAccessLocalStorage(storage)) {
        return;
    }
    auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return;
    }
    const game::StorageTransferResult result =
        game::StoreAllInPlayerStorage(*m_ctx.ecs, *m_ctx.items, storage, *container);
    if (result.moved > 0) {
        std::string text = "已存??" + std::to_string(result.moved) + " 件物??";
        if (!container->items.empty()) {
            text += "，背包剩??" + std::to_string(container->items.size()) +
                    " 堆（仓库已满）";
        }
        SetNotice(std::move(text), kNoticeOkColor, 1.8f);
        ZL_INFO("items", "仓库全部存入：{} ??/ {} ??", result.moved,
                result.stacksMoved);
    } else {
        SetNotice("仓库已满或背包为空", kNoticeInfoColor, 1.8f);
    }
}

void WorldDemoState::TakeAllFromStorage(std::uint64_t fromEntityId) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    if (fromEntityId != 0) {
        const ecs::Entity entity = m_ctx.ecs->FindById(fromEntityId);
        if (entity != entt::null &&
            m_ctx.ecs->Registry().try_get<ecs::LootContainer>(entity) != nullptr &&
            !game::IsPlayerStorage(*m_ctx.ecs, entity)) {
            TakeAllFromLootContainer(entity);
            return;
        }
    }
    const ecs::Entity storage = FindNearbyStorage();
    if (storage == entt::null) {
        SetNotice("附近没有仓库", kNoticeFailColor, 1.5f);
        return;
    }
    if (!CanAccessLocalStorage(storage)) {
        return;
    }
    auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return;
    }
    const game::StorageTransferResult result =
        game::TakeAllFromPlayerStorage(*m_ctx.ecs, *m_ctx.items, storage, *container);
    if (result.moved > 0) {
        std::string text = "已取??" + std::to_string(result.moved) + " 件物??";
        if (const auto* storageContainer =
                m_ctx.ecs->Registry().try_get<ecs::Container>(storage);
            storageContainer != nullptr && !storageContainer->items.empty()) {
            text += "，仓库剩??" +
                    std::to_string(storageContainer->items.size()) +
                    " 堆（背包已满）";
        }
        SetNotice(std::move(text), kNoticeOkColor, 1.8f);
        ZL_INFO("items", "仓库全部取出：{} 件 / {} 堆", result.moved,
                result.stacksMoved);
    } else {
        SetNotice("仓库为空或背包已满", kNoticeInfoColor, 1.8f);
    }
}

void WorldDemoState::TakeAllFromLootContainer(ecs::Entity containerEntity) {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null ||
        containerEntity == entt::null || !m_ctx.ecs->Alive(containerEntity)) {
        return;
    }
    auto* bag = m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (bag == nullptr) {
        return;
    }
    const game::LootTakeAllResult result = game::TakeAllLootContainerItems(
        *m_ctx.ecs, *m_ctx.items, containerEntity, *bag);
    if (result.moved > 0) {
        std::string text = "已取??" + std::to_string(result.moved) + " 件物??";
        if (result.remainingStacks > 0) {
            text += "，容器剩??" + std::to_string(result.remainingStacks) +
                    " 堆（背包已满）";
        }
        SetNotice(std::move(text), kNoticeOkColor, 1.8f);
        ZL_INFO("items", "容器全部取出：{} 件，剩余 {} 堆", result.moved,
                result.remainingStacks);
    } else {
        SetNotice("容器为空或背包已??", kNoticeInfoColor, 1.8f);
    }
}

void WorldDemoState::SortNearbyStorage() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    const ecs::Entity storage = FindNearbyStorage();
    if (storage == entt::null) {
        SetNotice("附近没有仓库", kNoticeFailColor, 1.5f);
        return;
    }
    if (!CanAccessLocalStorage(storage)) {
        return;
    }
    game::SortPlayerStorage(*m_ctx.ecs, *m_ctx.items, storage);
    SetNotice("仓库已整理", kNoticeInfoColor, 1.5f);
    ZL_INFO("items", "整理仓库：{}", StorageName(storage));
}

bool WorldDemoState::TryDemolishStorage(ecs::Entity storage) {
    if (!game::IsPlayerStorage(*m_ctx.ecs, storage) || m_ctx.items == nullptr) {
        return false;
    }
    if (m_ctx.ecs->Registry().all_of<ecs::Vehicle>(storage)) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* storagePosition = registry.try_get<ecs::Position>(storage);
    if (storagePosition == nullptr) {
        return false;
    }
    const float storageX = storagePosition->x;
    const float storageY = storagePosition->y;
    const int storageZ = storagePosition->z;
    const BuildableDef* def = StorageBuildable(storage);
    const std::string name = def != nullptr ? def->name : "仓库";
    const std::uint64_t owner = EntityOwnerStableId(storage);
    if (!CanEditLocalStructure(owner)) {
        SetNotice("该仓库属于 " + StructureOwnerLabel(owner) +
                      "，只有所有者可拆除",
                  kNoticeFailColor, 1.8f);
        return false;
    }
    const int wx = static_cast<int>(std::floor(storageX));
    const int wy = static_cast<int>(std::floor(storageY));
    const std::uint64_t token = m_ctx.ecs->IdOf(storage).id;
    if (!BeginOrConfirmDemolish(PendingDemolishKind::Entity, wx, wy, 0, token,
                                name)) {
        return true;
    }
    const int refund = def != nullptr
                           ? BuildWoodRefund(def->woodCost, m_demolishRefundRatio)
                           : 0;
    std::vector<ecs::ItemInstance> contents =
        game::DetachPlayerStorageContents(*m_ctx.ecs, storage);
    auto* playerContainer = registry.try_get<ecs::Container>(m_player);
    for (const ecs::ItemInstance& item : contents) {
        if (!item.Valid()) {
            continue;
        }
        int added = 0;
        if (playerContainer != nullptr) {
            added = items::AddItem(*playerContainer, *m_ctx.items, item);
        }
        if (added < item.count) {
            ecs::ItemInstance remainder = item;
            remainder.count -= added;
            game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, remainder,
                                  storageX, storageY, storageZ);
        }
    }
    m_ctx.ecs->Destroy(storage);
    if (refund > 0) {
        ecs::ItemInstance wood;
        wood.defId = m_buildWoodItem;
        wood.count = refund;
        ReturnOrDropItem(wood);
    }
    m_buildActionCooldown = m_buildActionCooldownSeconds;
    std::string notice = "拆除 " + name;
    if (refund > 0) {
        notice += "（返??" + std::to_string(refund) + " 木材??";
    }
    SetNotice(std::move(notice), kNoticeOkColor, 2.0f);
    ZL_INFO("building", "拆除仓库：({}, {})，返还 {} 木材，掉落 {} 堆",
            storageX, storageY, refund, static_cast<int>(contents.size()));
    return true;
}

bool WorldDemoState::TryPlaceSelectedBuild(int wx, int wy) {
    if (!m_buildMode) {
        return false;
    }
    const BuildableDef* def = SelectedBuildable();
    if (def == nullptr) {
        return false;
    }
    std::string reason;
    if (!CanPlaceBuild(*def, wx, wy, &reason)) {
        ZL_INFO("building", "建造被拒绝：{} at ({}, {})", reason, wx, wy);
        SetNotice("无法建造：" + reason, kNoticeFailColor, 2.0f);
        return false;
    }
    if (m_networkMode) {
        net::Command command =
            MakePlayerCommand(net::CommandType::PlaceStructure);
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.index = m_buildIndex;
        command.payload.flags =
            m_buildOrientationVertical ? std::uint16_t{1} : std::uint16_t{0};
        if (!SubmitPlayerCommand(command)) {
            SetNotice("建造请求发送失??", kNoticeFailColor, 1.5f);
            return false;
        }
        m_buildActionCooldown = m_buildActionCooldownSeconds;
        SetNotice("已发送建造请求：" + def->name, kNoticeInfoColor, 1.5f);
        return true;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr ||
        items::RemoveItem(*container, m_buildWoodItem, def->woodCost) !=
            def->woodCost) {
        SetNotice("无法建造：木材不足", kNoticeFailColor, 2.0f);
        return false;
    }
    const auto refundWood = [&]() {
        ecs::ItemInstance wood;
        wood.defId = m_buildWoodItem;
        wood.count = def->woodCost;
        ReturnOrDropItem(wood);
    };
    const auto finishFacility = [&](const char* logLabel) {
        m_buildActionCooldown = m_buildActionCooldownSeconds;
        SetNotice("建造了 " + def->name + "??" +
                      std::to_string(def->woodCost) + " 木材??",
                  kNoticeOkColor, 2.0f);
        GrantProgressionXp(ecs::SkillKind::Building,
                           m_progressionConfig.buildXpPerStructure, "建??");
        ZL_INFO("homestead", "建造{} {}??{}, {})", logLabel, def->id, wx, wy);
        return true;
    };
    if (def->id == "farm_plot") {
        if (!m_homesteadConfig.farm.enabled) {
            refundWood();
            SetNotice("农业系统已关闭", kNoticeFailColor, 2.0f);
            return false;
        }
        if (SpawnFarmPlot(*m_ctx.ecs, static_cast<float>(wx) + 0.5f,
                          static_cast<float>(wy) + 0.5f, 0,
                          m_homesteadConfig.farm) == entt::null) {
            refundWood();
            SetNotice("无法建造：农田创建失败", kNoticeFailColor, 2.0f);
            return false;
        }
        return finishFacility("农田");
    }
    if (def->id == "generator") {
        if (!m_homesteadConfig.power.enabled) {
            refundWood();
            SetNotice("电力系统已关闭", kNoticeFailColor, 2.0f);
            return false;
        }
        if (SpawnGenerator(*m_ctx.ecs, static_cast<float>(wx) + 0.5f,
                           static_cast<float>(wy) + 0.5f, 0,
                           m_homesteadConfig.power) == entt::null) {
            refundWood();
            SetNotice("无法建造：发电机创建失??", kNoticeFailColor, 2.0f);
            return false;
        }
        return finishFacility("发电??");
    }
    if (def->id == "floodlight") {
        if (!m_homesteadConfig.power.enabled) {
            refundWood();
            SetNotice("电力系统已关闭", kNoticeFailColor, 2.0f);
            return false;
        }
        if (SpawnFloodlight(*m_ctx.ecs, static_cast<float>(wx) + 0.5f,
                            static_cast<float>(wy) + 0.5f, 0,
                            def->maxHp) == entt::null) {
            refundWood();
            SetNotice("无法建造：泛光灯创建失??", kNoticeFailColor, 2.0f);
            return false;
        }
        return finishFacility("泛光??");
    }
    if (def->id == "fridge") {
        if (!m_homesteadConfig.power.enabled) {
            refundWood();
            SetNotice("电力系统已关闭", kNoticeFailColor, 2.0f);
            return false;
        }
        if (SpawnColdStorage(*m_ctx.ecs, static_cast<float>(wx) + 0.5f,
                             static_cast<float>(wy) + 0.5f, 0, def->sprite,
                             def->storageKg, def->storageSlots) == entt::null) {
            refundWood();
            SetNotice("无法建造：冷藏柜创建失??", kNoticeFailColor, 2.0f);
            return false;
        }
        return finishFacility("冷藏??");
    }
    if (def->id == "spike_trap") {
        if (!m_homesteadConfig.trap.enabled) {
            refundWood();
            SetNotice("陷阱系统已关闭", kNoticeFailColor, 2.0f);
            return false;
        }
        if (SpawnSpikeTrap(*m_ctx.ecs, static_cast<float>(wx) + 0.5f,
                           static_cast<float>(wy) + 0.5f, 0,
                           m_homesteadConfig.trap) == entt::null) {
            refundWood();
            SetNotice("无法建造：陷阱创建失败", kNoticeFailColor, 2.0f);
            return false;
        }
        return finishFacility("陷阱");
    }
    if (IsStorageBuildable(*def)) {
        const ecs::Entity storage = game::SpawnPlayerStorage(
            *m_ctx.ecs, static_cast<float>(wx) + 0.5f,
            static_cast<float>(wy) + 0.5f, 0, def->sprite, def->storageKg,
            def->storageSlots);
        if (storage == entt::null) {
            ecs::ItemInstance wood;
            wood.defId = m_buildWoodItem;
            wood.count = def->woodCost;
            ReturnOrDropItem(wood);
            SetNotice("无法建造：仓库创建失败", kNoticeFailColor, 2.0f);
            return false;
        }
        m_buildActionCooldown = m_buildActionCooldownSeconds;
        SetNotice("建造了 " + def->name + "??" +
                      std::to_string(def->woodCost) + " 木材??",
                  kNoticeOkColor, 2.0f);
        GrantProgressionXp(ecs::SkillKind::Building,
                           m_progressionConfig.buildXpPerStructure, "建??");
        ZL_INFO("building", "建造仓??{}??{}, {})，容??{:.1f}kg/{} ??",
                def->id, wx, wy, def->storageKg, def->storageSlots);
        return true;
    }
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        SetNotice("无法建造：目标区域未加??", kNoticeFailColor, 2.0f);
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
    world::SquareObject built =
        MakeBuiltObject(*def, m_buildOrientationVertical);
    if (def->layer == world::SquareLayer::Floor) {
        if (const world::SquareObject* floor = square.Floor();
            floor != nullptr) {
            built.underlay = floor->sprite;
        }
        square.SetFloor(std::move(built));
    } else {
        square.AddObject(def->layer, std::move(built));
    }
    const world::SquareObject& placed = def->layer == world::SquareLayer::Floor
                                            ? *square.Floor()
                                            : square.Layer(def->layer).back();
    if (def->maxHp > 0.0f && placed.blocksMovement) {
        m_barricades[BuildCellKey(wx, wy)] = BuiltBarricade{
            placed.sprite, def->maxHp, def->maxHp};
    }
    if (placed.blocksMovement) {
        RebuildChunkCollision(coord.cx, coord.cy);
    }
    SetNotice("建造了 " + def->name + "??" + std::to_string(def->woodCost) +
                  " 木材??",
              kNoticeOkColor, 2.0f);
    GrantProgressionXp(ecs::SkillKind::Building,
                       m_progressionConfig.buildXpPerStructure, "建??");
    ZL_INFO("building", "建??{}??{}, {})，剩余木??{}", def->id, wx, wy,
            items::CountOf(*container, m_buildWoodItem));
    return true;
}

void WorldDemoState::RebuildChunkCollision(int chunkX, int chunkY) {
    world::Chunk* chunk = FindChunk(chunkX, chunkY);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return;
    }
    if (m_ctx.physics != nullptr) {
        m_ctx.physics->BuildChunkStatics(chunkX, chunkY, *chunk);
    }
    if (m_ctx.pathfinding != nullptr) {
        m_ctx.pathfinding->OnChunkLoaded(*chunk);
    }
}

void WorldDemoState::SyncBarricadeHp(std::uint64_t key) {
    const auto it = m_barricades.find(key);
    if (it == m_barricades.end()) {
        return;
    }
    const int wx = BuildCellX(key);
    const int wy = BuildCellY(key);
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    constexpr std::array<world::SquareLayer, 2> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture};
    for (const world::SquareLayer layer : kLayers) {
        for (world::SquareObject& object :
             chunk->Square(local.lx, local.ly, 0).Layer(layer)) {
            if (object.playerBuilt && object.sprite == it->second.sprite) {
                object.hp = it->second.hp;
                return;
            }
        }
    }
}

void WorldDemoState::RemoveBarricade(std::uint64_t key) {
    const auto it = m_barricades.find(key);
    if (it == m_barricades.end()) {
        return;
    }
    const int wx = BuildCellX(key);
    const int wy = BuildCellY(key);
    const std::string sprite = it->second.sprite;
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    if (world::Chunk* chunk = FindChunk(coord.cx, coord.cy); chunk != nullptr) {
        const world::SquareLocal local = world::SquareLocalOf(wx, wy);
        world::GridSquare& square = chunk->Square(local.lx, local.ly, 0);
        constexpr std::array<world::SquareLayer, 2> kLayers{
            world::SquareLayer::Wall, world::SquareLayer::Furniture};
        bool removed = false;
        for (const world::SquareLayer layer : kLayers) {
            auto& objects = square.Layer(layer);
            const std::size_t before = objects.size();
            objects.erase(std::remove_if(objects.begin(), objects.end(),
                                         [&](const world::SquareObject& object) {
                                             return object.playerBuilt &&
                                                    object.sprite == sprite;
                                         }),
                          objects.end());
            removed = removed || objects.size() != before;
        }
        if (removed) {
            RebuildChunkCollision(coord.cx, coord.cy);
        }
    }
    m_barricades.erase(it);
    entt::registry* registry =
        m_ctx.ecs != nullptr ? &m_ctx.ecs->Registry() : nullptr;
    if (registry != nullptr && m_player != entt::null) {
        if (const auto* position = registry->try_get<ecs::Position>(m_player);
            position != nullptr &&
            std::hypot(position->x - (static_cast<float>(wx) + 0.5f),
                       position->y - (static_cast<float>(wy) + 0.5f)) <= 6.0f) {
            SetNotice("己方结构被摧毁", kNoticeFailColor, 2.5f);
        }
    }
    ZL_INFO("building", "己方结构被摧毁：({}, {})", wx, wy);
}

void WorldDemoState::TrackNearbyBarricades() {
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr || m_player == entt::null) {
        return;
    }
    const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    const BaseSiegePoint& siegeCenter = m_baseSiege.Center();
    const float scanX = m_baseSiege.Active() ? siegeCenter.x : position->x;
    const float scanY = m_baseSiege.Active() ? siegeCenter.y : position->y;
    const int px = static_cast<int>(std::floor(scanX));
    const int py = static_cast<int>(std::floor(scanY));
    const int kScanRadius = m_baseSiege.Active() ? 16 : 8;
    for (int wy = py - kScanRadius; wy <= py + kScanRadius; ++wy) {
        for (int wx = px - kScanRadius; wx <= px + kScanRadius; ++wx) {
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            const world::Chunk& constChunk = *chunk;
            const world::GridSquare& square =
                constChunk.Square(local.lx, local.ly, 0);
            constexpr std::array<world::SquareLayer, 2> kLayers{
                world::SquareLayer::Wall, world::SquareLayer::Furniture};
            for (const world::SquareLayer layer : kLayers) {
                const auto& objects = square.Layer(layer);
                for (std::size_t objectIndex = 0; objectIndex < objects.size();
                     ++objectIndex) {
                    const world::SquareObject& object = objects[objectIndex];
                    if (!object.playerBuilt || !object.blocksMovement) {
                        continue;
                    }
                    const BuildableDef* def = BuildableForSprite(object.sprite);
                    if (def == nullptr || def->maxHp <= 0.0f) {
                        continue;
                    }
                    const float hp = ResolveBarricadeHp(object, def->maxHp);
                    if (object.hp != hp) {
                        chunk->Square(local.lx, local.ly, 0)
                            .Layer(layer)[objectIndex]
                            .hp = hp;
                    }
                    m_barricades[BuildCellKey(wx, wy)] =
                        BuiltBarricade{object.sprite, hp, def->maxHp};
                }
            }
        }
    }
}

void WorldDemoState::UpdateBuilding(float tickDt) {
    if (m_runOutcome != RunOutcome::None) {
        return;
    }
    m_buildActionCooldown =
        std::max(0.0f, m_buildActionCooldown - tickDt);
    if (m_networkMode) {
        return;
    }
    m_barricadeScanTimer -= tickDt;
    if (m_barricadeScanTimer <= 0.0f) {
        m_barricadeScanTimer = 0.5f;
        TrackNearbyBarricades();
    }
    for (auto& [entityId, remaining] : m_barricadeAttackCooldowns) {
        (void)entityId;
        remaining = std::max(0.0f, remaining - tickDt);
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const std::vector<FloodlightAttack> lightAttacks =
        TickZombieFloodlightAttacks(
            registry, m_floodlightAttackCooldowns, tickDt,
            m_buildZombieDamage, m_buildZombieCooldown, m_breakerDamage,
            m_breakerCooldown, 1.4f, m_breakerRange);
    int destroyedLights = 0;
    for (const FloodlightAttack& attack : lightAttacks) {
        if (!attack.destroyed) {
            continue;
        }
        ++destroyedLights;
        const auto* lightPos = registry.try_get<ecs::Position>(attack.light);
        ZL_INFO("homestead", "事件僵尸摧毁泛光灯：({}, {})，剩余 HP 0",
                lightPos != nullptr ? lightPos->x : 0.0f,
                lightPos != nullptr ? lightPos->y : 0.0f);
        m_ctx.ecs->Destroy(attack.light);
    }
    if (destroyedLights > 0) {
        SetNotice("泛光灯被摧毁 ×" + std::to_string(destroyedLights),
                  kNoticeFailColor, 2.0f);
    }
    if (m_barricades.empty()) {
        return;
    }
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (playerPos == nullptr) {
        return;
    }
    registry.view<ecs::AiState, ecs::Health, ecs::Position>().each(
        [&](ecs::Entity entity, const ecs::AiState& ai, const ecs::Health& health,
            const ecs::Position& pos) {
            const bool siegeZombie =
                registry.any_of<ecs::BaseSiegeZombie>(entity);
            const float playerRange = siegeZombie ? 24.0f : 10.0f;
            const auto attack = TickZombieBarricadeAttack(
                registry, entity, ai, health, pos, playerPos->x, playerPos->y,
                m_barricades, m_barricadeAttackCooldowns, m_buildZombieDamage,
                m_buildZombieCooldown, 1.5f, playerRange, m_breakerRange,
                m_breakerDamage, m_breakerCooldown, m_breakerWindup);
            if (!attack.has_value()) {
                return;
            }
            if (attack->warningStarted) {
                SetNotice("破门者正在蓄力砸墙，快打断它", kNoticeFailColor,
                          2.5f);
                core::SoundEvent warning;
                warning.x = static_cast<float>(attack->wx) + 0.5f;
                warning.y = static_cast<float>(attack->wy) + 0.5f;
                warning.radius = 10.0f;
                warning.type = core::SoundType::ZombieGrowl;
                if (m_ctx.audio != nullptr) {
                    m_ctx.audio->EmitWorldSound(std::move(warning));
                } else if (m_ctx.events != nullptr) {
                    m_ctx.events->EmitQueued(core::kOnSound,
                                             std::move(warning));
                }
                return;
            }
            core::SoundEvent impact;
            impact.x = static_cast<float>(attack->wx) + 0.5f;
            impact.y = static_cast<float>(attack->wy) + 0.5f;
            impact.radius = 6.0f;
            impact.type = core::SoundType::Impact;
            impact.clip = audio::kImpact;
            if (m_ctx.audio != nullptr) {
                m_ctx.audio->EmitWorldSound(std::move(impact));
            } else if (m_ctx.events != nullptr) {
                m_ctx.events->EmitQueued(core::kOnSound, std::move(impact));
            }
            if (attack->destroyed) {
                RemoveBarricade(attack->key);
            } else {
                SyncBarricadeHp(attack->key);
            }
        });
}

void WorldDemoState::InitializeBaseSiege() {
    if (m_ctx.config == nullptr) {
        return;
    }
    core::Config& config = *m_ctx.config;
    BaseSiegeConfig siege;
    siege.enabled =
        !m_networkMode && config.Get<bool>("baseEvent.enabled", true);
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
    siege.specialStartWave =
        config.Get<int>("baseEvent.specialStartWave", 1);
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

    BaseSiegeCallbacks callbacks;
    callbacks.now = [this]() {
        BaseSiegeTime time;
        if (m_ctx.gameTime == nullptr) {
            time.paused = true;
            return time;
        }
        const core::DateTime& now = m_ctx.gameTime->Now();
        // M20-F3：事件缩放使用“生存第几天”，避免公历绝对日放大尸潮。
        time.day = 1 + static_cast<int>(
                           m_ctx.gameTime->TotalGameMinutes() / (24.0 * 60.0));
        time.hour = now.hour;
        time.minute = now.minute;
        time.paused =
            m_ctx.gameTime->GetSpeed() == core::GameTime::Speed::Paused;
        return time;
    };
    callbacks.playerPos = [this](BaseSiegePoint* out) {
        if (out == nullptr || m_ctx.ecs == nullptr || m_player == entt::null) {
            return false;
        }
        entt::registry& registry = m_ctx.ecs->Registry();
        const auto* position = registry.try_get<ecs::Position>(m_player);
        if (position == nullptr) {
            return false;
        }
        if (const auto* health = registry.try_get<ecs::Health>(m_player);
            health != nullptr && health->current <= 0.0f) {
            return false;
        }
        out->x = position->x;
        out->y = position->y;
        return true;
    };
    callbacks.structureCount = [this](const BaseSiegePoint& point,
                                      float radius) {
        return CountPlayerStructuresNear(point.x, point.y, radius, false);
    };
    callbacks.barricadeCount = [this](const BaseSiegePoint& point,
                                      float radius) {
        return CountPlayerStructuresNear(point.x, point.y, radius, true);
    };
    callbacks.structureList = [this](const BaseSiegePoint& point,
                                     float radius) {
        return ListPlayerStructuresNear(point.x, point.y, radius);
    };
    callbacks.isWalkableCell = [this](int wx, int wy) {
        return IsBaseSiegeSpawnCell(wx, wy);
    };
    callbacks.spawnZombie =
        [this](int wx, int wy, ecs::ZombieSpecialKind special) {
            return SpawnBaseSiegeZombie(wx, wy, special);
        };
    callbacks.isAlive = [this](std::uint64_t stableId) {
        if (m_ctx.ecs == nullptr) {
            return false;
        }
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        return entity != ecs::EcsWorld::kNull && m_ctx.ecs->Alive(entity);
    };
    callbacks.despawnZombie = [this](std::uint64_t stableId) {
        DespawnBaseSiegeZombie(stableId);
    };
    callbacks.notice = [this](const std::string& text,
                              const std::string& tone) {
        ShowBaseSiegeNotice(text, tone);
    };
    callbacks.onResult = [this](const BaseSiegeResult& result) {
        return GrantBaseSiegeReward(result);
    };
    m_baseSiege.Configure(std::move(siege), std::move(callbacks));
}

void WorldDemoState::UpdateBaseSiege(float tickDt) {
    if (m_networkMode || m_runOutcome != RunOutcome::None) {
        return;
    }
    m_baseSiege.Update(tickDt);
    if (m_siegeReportOpen &&
        m_baseSiege.Phase() == BaseSiegePhase::Warning) {
        m_siegeReportOpen = false;
    }
    UpdateElectricFences(tickDt);
    if (!m_baseSiege.Active()) {
        return;
    }
    UpdateTraps(tickDt);
    DriveBaseSiegeZombies();
}

void WorldDemoState::UpdateElectricFences(float tickDt) {
    for (auto& [key, remaining] : m_electricFenceCooldowns) {
        (void)key;
        remaining = std::max(0.0f, remaining - std::max(0.0f, tickDt));
    }
    m_electricNoticeCooldown =
        std::max(0.0f, m_electricNoticeCooldown - std::max(0.0f, tickDt));
    if (!m_homesteadConfig.power.enabled || m_ctx.ecs == nullptr ||
        !m_baseSiege.Active() || m_poweredFenceCells.empty()) {
        return;
    }
    if (m_homesteadConfig.power.deviceRequiresNight &&
        (m_ctx.gameTime == nullptr || !m_ctx.gameTime->IsNight())) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const float damage = m_homesteadConfig.power.electricFenceDamage;
    constexpr float range = 1.35f;
    for (const auto& [key, barricade] : m_barricades) {
        if (barricade.sprite != kElectricFenceSprite ||
            !m_poweredFenceCells.contains(key)) {
            continue;
        }
        if (auto cooldown = m_electricFenceCooldowns.find(key);
            cooldown != m_electricFenceCooldowns.end() &&
            cooldown->second > 0.0f) {
            continue;
        }
        const float fenceX = static_cast<float>(BuildCellX(key)) + 0.5f;
        const float fenceY = static_cast<float>(BuildCellY(key)) + 0.5f;
        ecs::Entity target = entt::null;
        float bestDistance = range * range;
        registry.view<ecs::BaseSiegeZombie, ecs::AiState, ecs::Health,
                      ecs::Position>()
            .each([&](const ecs::Entity entity, const ecs::AiState& ai,
                      const ecs::Health& health,
                      const ecs::Position& position) {
                if (health.current <= 0.0f ||
                    ai.status == ecs::AiStatus::Dead ||
                    registry.any_of<ecs::Dying>(entity)) {
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
        if (target == entt::null || m_ctx.combat == nullptr) {
            continue;
        }
        const auto* targetPosition = registry.try_get<ecs::Position>(target);
        ecs::DamageEvent event;
        event.type = ecs::DamageType::Blunt;
        event.amount = damage;
        event.staggerSeconds =
            m_homesteadConfig.power.electricFenceStaggerSeconds;
        if (targetPosition != nullptr) {
            float dx = targetPosition->x - fenceX;
            float dy = targetPosition->y - fenceY;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.0001f) {
                event.dirX = dx / length;
                event.dirY = dy / length;
            }
        }
        if (!m_ctx.combat->ApplyDamage(*m_ctx.ecs, target, event)) {
            continue;
        }
        m_electricFenceCooldowns[key] =
            m_homesteadConfig.power.electricFenceCooldownSeconds;
        if (m_electricNoticeCooldown <= 0.0f) {
            SetNotice("电网电击：夜间事件僵尸受到 " +
                          std::to_string(static_cast<int>(std::lround(damage))) +
                          " 伤害",
                      kNoticeOkColor, 1.2f);
            m_electricNoticeCooldown = 1.0f;
        }
    }
}

void WorldDemoState::UpdateTraps(float tickDt) {
    if (!m_homesteadConfig.trap.enabled || m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::SpikeTrap, ecs::Position>().each(
        [&](const ecs::Entity trapEntity, ecs::SpikeTrap& trap,
            const ecs::Position& trapPosition) {
            AdvanceTrap(trap, tickDt);
            if (!TrapCanTrigger(trap)) {
                return;
            }
            ecs::Entity target = entt::null;
            float bestDistance = trap.radius * trap.radius;
            registry.view<ecs::BaseSiegeZombie, ecs::AiState, ecs::Health,
                          ecs::Position>()
                .each([&](const ecs::Entity entity, const ecs::AiState& ai,
                          const ecs::Health& health,
                          const ecs::Position& position) {
                    if (health.current <= 0.0f ||
                        ai.status == ecs::AiStatus::Dead ||
                        registry.any_of<ecs::Dying>(entity)) {
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
            if (target == entt::null) {
                return;
            }
            const auto* targetPosition =
                registry.try_get<ecs::Position>(target);
            ecs::DamageEvent event;
            event.type = ecs::DamageType::Blunt;
            event.amount = trap.damage;
            event.knockback = 0.8f;
            event.staggerSeconds = trap.staggerSeconds;
            if (targetPosition != nullptr) {
                float dx = targetPosition->x - trapPosition.x;
                float dy = targetPosition->y - trapPosition.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.0001f) {
                    event.dirX = dx / length;
                    event.dirY = dy / length;
                }
            }
            bool triggered = false;
            if (m_ctx.combat != nullptr) {
                triggered = m_ctx.combat->ApplyDamage(*m_ctx.ecs, target, event);
            } else if (auto* health = registry.try_get<ecs::Health>(target);
                       health != nullptr) {
                health->current = 0.0f;
                triggered = true;
            }
            if (!triggered) {
                return;
            }
            (void)ConsumeTrapUse(trap);
            SetNotice("陷阱触发：尖刺命中僵尸（剩余 " +
                          std::to_string(trap.uses) + " 次）",
                      kNoticeOkColor, 1.8f);
            core::SoundEvent impact;
            impact.x = trapPosition.x;
            impact.y = trapPosition.y;
            impact.radius = 6.0f;
            impact.type = core::SoundType::Impact;
            impact.clip = audio::kImpact;
            if (m_ctx.audio != nullptr) {
                m_ctx.audio->EmitWorldSound(std::move(impact));
            } else if (m_ctx.events != nullptr) {
                m_ctx.events->EmitQueued(core::kOnSound, std::move(impact));
            }
            ZL_INFO("homestead", "陷阱 {} 命中，剩??{} ??",
                    m_ctx.ecs->IdOf(trapEntity).id, trap.uses);
        });
}

void WorldDemoState::UpdateBaseHud(float frameDt) {
    if (m_runOutcome != RunOutcome::None) {
        m_baseHud = {};
        m_baseHudScanTimer = 0.0f;
        return;
    }
    m_baseHudScanTimer -= std::max(0.0f, frameDt);
    if (m_baseHudScanTimer > 0.0f) {
        return;
    }
    m_baseHudScanTimer = kBaseHudRefreshSeconds;
    ScanBaseHud();
}

void WorldDemoState::ScanBaseHud() {
    BaseHudSnapshot snapshot;
    snapshot.powerFuelMinutes = m_baseHud.powerFuelMinutes;
    snapshot.powerLoad = m_baseHud.powerLoad;
    snapshot.powerLowFuel = m_baseHud.powerLowFuel;
    snapshot.powerOutage = m_baseHud.powerOutage;
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr ||
        m_player == entt::null) {
        m_baseHud = snapshot;
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* playerPosition = registry.try_get<ecs::Position>(m_player);
    if (playerPosition == nullptr) {
        m_baseHud = snapshot;
        return;
    }
    const float rangeSq = kBaseHudRadius * kBaseHudRadius;

    registry.view<ecs::Position, ecs::PlayerStorage>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::PlayerStorage&) {
            if (registry.all_of<ecs::Vehicle>(entity)) {
                return;
            }
            if (position.z != playerPosition->z) {
                return;
            }
            const float dx = position.x - playerPosition->x;
            const float dy = position.y - playerPosition->y;
            if (dx * dx + dy * dy > rangeSq) {
                return;
            }
            snapshot.active = true;
            ++snapshot.storageCount;
            const auto* container = registry.try_get<ecs::Container>(entity);
            if (container == nullptr) {
                return;
            }
            snapshot.storageCapacity += container->capacityKg;
            if (m_ctx.items != nullptr) {
                snapshot.storageWeight +=
                    items::TotalWeight(*container, *m_ctx.items);
            }
        });

    const int minX = static_cast<int>(
        std::floor(playerPosition->x - kBaseHudRadius));
    const int maxX = static_cast<int>(
        std::ceil(playerPosition->x + kBaseHudRadius));
    const int minY = static_cast<int>(
        std::floor(playerPosition->y - kBaseHudRadius));
    const int maxY = static_cast<int>(
        std::ceil(playerPosition->y + kBaseHudRadius));
    constexpr std::array<world::SquareLayer, 3> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture,
        world::SquareLayer::Floor};
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx =
                playerPosition->x - (static_cast<float>(wx) + 0.5f);
            const float dy =
                playerPosition->y - (static_cast<float>(wy) + 0.5f);
            if (dx * dx + dy * dy > rangeSq) {
                continue;
            }
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            for (const world::SquareLayer layer : kLayers) {
                for (const world::SquareObject& object :
                     chunk->Square(local.lx, local.ly, playerPosition->z)
                         .Layer(layer)) {
                    if (!object.playerBuilt) {
                        continue;
                    }
                    snapshot.active = true;
                    const BuildableDef* def = BuildableForSprite(object.sprite);
                    if (def == nullptr) {
                        continue;
                    }
                    if (def->id == "bed") {
                        ++snapshot.beds;
                    } else if (def->id == "campfire") {
                        ++snapshot.campfires;
                    } else if (def->id == "workbench") {
                        ++snapshot.workbenches;
                    } else if (def->id == "electric_fence") {
                        ++snapshot.electricFences;
                        if (m_poweredFenceCells.contains(
                                BuildCellKey(wx, wy))) {
                            ++snapshot.electricFencesPowered;
                        }
                    }
                    if (def->maxHp > 0.0f) {
                        if (IsRepairableBarricade(object, def->maxHp)) {
                            ++snapshot.damagedDefenses;
                        } else {
                            ++snapshot.intactDefenses;
                        }
                    }
                }
            }
        }
    }

    const auto inRange = [&](const ecs::Position& position) {
        if (position.z != playerPosition->z) {
            return false;
        }
        const float dx = position.x - playerPosition->x;
        const float dy = position.y - playerPosition->y;
        return dx * dx + dy * dy <= rangeSq;
    };
    registry.view<ecs::Position, ecs::FarmPlot>().each(
        [&](const ecs::Entity, const ecs::Position& position,
            const ecs::FarmPlot& plot) {
            if (!inRange(position)) {
                return;
            }
            snapshot.active = true;
            ++snapshot.farms;
            if (plot.fertilized) {
                ++snapshot.farmFertilized;
            }
            if (plot.stage == ecs::CropStage::Growing) {
                ++snapshot.farmGrowing;
                if (plot.diseased) {
                    ++snapshot.farmDiseased;
                } else if (plot.waterMinutes <= 0.5f) {
                    ++snapshot.farmThirsty;
                }
            } else if (plot.stage == ecs::CropStage::Mature) {
                ++snapshot.farmMature;
            }
        });
    registry.view<ecs::Position, ecs::Generator>().each(
        [&](const ecs::Entity, const ecs::Position& position,
            const ecs::Generator& generator) {
            if (!inRange(position)) {
                return;
            }
            snapshot.active = true;
            ++snapshot.generators;
            if (generator.running && generator.fuelMinutes > 0.0f) {
                ++snapshot.generatorsOnline;
            }
        });
    registry.view<ecs::Position, ecs::ColdStorage>().each(
        [&](const ecs::Entity, const ecs::Position& position,
            const ecs::ColdStorage& storage) {
            if (!inRange(position)) {
                return;
            }
            snapshot.active = true;
            ++snapshot.fridges;
            if (storage.powered) {
                ++snapshot.fridgesPowered;
            }
        });
    registry.view<ecs::Position, ecs::Floodlight>().each(
        [&](const ecs::Entity, const ecs::Position& position,
            const ecs::Floodlight& floodlight) {
            if (!inRange(position)) {
                return;
            }
            snapshot.active = true;
            ++snapshot.floodlights;
            if (floodlight.powered) {
                ++snapshot.floodlightsPowered;
            }
        });
    registry.view<ecs::Position, ecs::SpikeTrap>().each(
        [&](const ecs::Entity, const ecs::Position& position,
            const ecs::SpikeTrap& trap) {
            if (!inRange(position)) {
                return;
            }
            snapshot.active = true;
            ++snapshot.traps;
            if (trap.armed && trap.uses > 0) {
                ++snapshot.trapsArmed;
            }
        });
    m_baseHud = snapshot;
}

void WorldDemoState::InitializeHomestead() {
    if (m_ctx.config == nullptr) {
        return;
    }
    m_homesteadConfig = LoadHomesteadConfig(*m_ctx.config);
    if (const SettlementDef* settlement =
            PrimarySettlement(m_settlementConfig);
        settlement != nullptr) {
        m_homesteadConfig.trade.restockDays = settlement->restockDays;
    }
    m_tradeOpen = false;
    m_tradeIndex = 0;
    CloseSettlementDialogue();
    m_lastHomesteadMinute = -1.0;
    m_homesteadRuntimeReady = false;
    m_farmSeedCropId.clear();
    ZL_INFO("homestead",
            "生存设施配置：农业={} 电力={} 陷阱={} 交易={}（商人 {:g},{:g}）",
            m_homesteadConfig.farm.enabled ? "开" : "??",
            m_homesteadConfig.power.enabled ? "开" : "??",
            m_homesteadConfig.trap.enabled ? "开" : "??",
            m_homesteadConfig.trade.enabled ? "开" : "??",
            m_homesteadConfig.trade.merchantX,
            m_homesteadConfig.trade.merchantY);
}

void WorldDemoState::EnsureHomesteadRuntime() {
    if (m_networkMode || m_homesteadRuntimeReady || m_runOutcome != RunOutcome::None ||
        m_ctx.ecs == nullptr || m_ctx.world == nullptr || m_player == entt::null ||
        !m_homesteadConfig.trade.enabled) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    bool found = false;
    registry.view<ecs::MerchantStock>().each(
        [&](const ecs::Entity, const ecs::MerchantStock&) { found = true; });
    if (found) {
        m_homesteadRuntimeReady = true;
        return;
    }
    const SettlementDef* settlement =
        PrimarySettlement(m_settlementConfig);
    const SettlementNpcPoint* merchantPoint =
        settlement != nullptr
            ? FindSettlementNpc(*settlement, SettlementNpcKind::Merchant)
            : nullptr;
    float targetX = m_homesteadConfig.trade.merchantX;
    float targetY = m_homesteadConfig.trade.merchantY;
    if (merchantPoint != nullptr) {
        targetX = merchantPoint->x;
        targetY = merchantPoint->y;
    } else if (m_player != entt::null) {
        if (const auto* playerPosition =
                registry.try_get<ecs::Position>(m_player);
            playerPosition != nullptr) {
            targetX = playerPosition->x + 2.0f;
            targetY = playerPosition->y + 1.0f;
        }
    }
    int wx = 0;
    int wy = 0;
    if (!ai::FindNearestWalkable(
            static_cast<int>(std::floor(targetX)),
            static_cast<int>(std::floor(targetY)), 6,
            [this](int x, int y) { return !IsSolidSquare(x, y); }, &wx,
            &wy)) {
        return;
    }
    const ecs::Entity merchant = SpawnMerchant(
        *m_ctx.ecs, static_cast<float>(wx) + 0.5f,
        static_cast<float>(wy) + 0.5f, 0);
    if (merchant == entt::null) {
        return;
    }
    if (auto* stock = registry.try_get<ecs::MerchantStock>(merchant);
        stock != nullptr && settlement != nullptr) {
        stock->stock = settlement->merchantStock;
        stock->restockDay = 0;
    }
    m_homesteadRuntimeReady = true;
    if (settlement != nullptr) {
        ZL_INFO("homestead", "商人已就位：({}, {})（聚落 {}）", wx, wy,
                settlement->id);
    } else {
        ZL_INFO("homestead", "商人已就位：({}, {})", wx, wy);
    }
}

void WorldDemoState::EnsureSettlementNpcs() {
    if (m_networkMode || m_runOutcome != RunOutcome::None ||
        m_ctx.ecs == nullptr || m_ctx.world == nullptr ||
        !m_settlementConfig.enabled) {
        return;
    }
    const SettlementDef* settlement = PrimarySettlement(m_settlementConfig);
    if (settlement == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    bool merchantConfigured = false;
    bool hasGuard = false;
    bool hasRequester = false;
    registry.view<SettlementNpc>().each(
        [&](const ecs::Entity, const SettlementNpc& npc) {
            if (npc.settlementId != settlement->id) {
                return;
            }
            merchantConfigured =
                merchantConfigured ||
                npc.kind == SettlementNpcKind::Merchant;
            hasGuard = hasGuard || npc.kind == SettlementNpcKind::Guard;
            hasRequester =
                hasRequester || npc.kind == SettlementNpcKind::Requester;
        });
    if (!merchantConfigured) {
        registry.view<ecs::MerchantStock, ecs::Position>().each(
            [&](const ecs::Entity entity, const ecs::MerchantStock&,
                const ecs::Position& position) {
                if (merchantConfigured) {
                    return;
                }
                const SettlementNpcPoint* merchantPoint =
                    FindSettlementNpc(*settlement,
                                      SettlementNpcKind::Merchant);
                ConfigureSettlementNpc(
                    *m_ctx.ecs, entity, settlement->id,
                    SettlementNpcKind::Merchant, position.x, position.y,
                    merchantPoint != nullptr ? merchantPoint->wanderRadius
                                             : 1.5f);
                merchantConfigured = true;
            });
    }

    auto spawnNpc = [&](SettlementNpcKind kind) {
        const SettlementNpcPoint* point =
            FindSettlementNpc(*settlement, kind);
        if (point == nullptr) {
            return false;
        }
        const int wx = static_cast<int>(std::floor(point->x));
        const int wy = static_cast<int>(std::floor(point->y));
        if (IsSolidSquare(wx, wy)) {
            return false;
        }
        const ecs::Entity npc = SpawnSettlementNpc(
            *m_ctx.ecs, settlement->id, kind,
            static_cast<float>(wx) + 0.5f, static_cast<float>(wy) + 0.5f,
            point->wanderRadius,
            kind == SettlementNpcKind::Guard
                ? settlement->defense.guardHealth
                : 0.0f);
        if (npc == entt::null) {
            return false;
        }
        if (kind == SettlementNpcKind::Guard && m_ctx.simulation != nullptr) {
            if (const SettlementNpcSystem* system =
                    m_ctx.simulation->SettlementNpcs();
                system != nullptr) {
                system->ApplyGuardHealth(*m_ctx.ecs, npc, settlement->id);
            }
        }
        ZL_INFO("settlement", "聚落 NPC 已就位：{}（{}，格 {},{}??",
                SettlementNpcKindName(kind), settlement->id, wx, wy);
        return true;
    };
    if (!hasGuard) {
        const SettlementNpcSystem* system =
            m_ctx.simulation != nullptr
                ? m_ctx.simulation->SettlementNpcs()
                : nullptr;
        if (system == nullptr || !system->IsFallen(settlement->id)) {
            (void)spawnNpc(SettlementNpcKind::Guard);
        }
    }
    if (!hasRequester) {
        (void)spawnNpc(SettlementNpcKind::Requester);
    }
}

void WorldDemoState::StoreSettlementState() {
    if (m_ctx.ecs == nullptr || m_ctx.simulation == nullptr) {
        return;
    }
    const SettlementNpcSystem* system =
        m_ctx.simulation->SettlementNpcs();
    if (system == nullptr) {
        return;
    }
    ecs::Entity entity = m_ctx.ecs->FindById(kSettlementStateStableId);
    if (entity == entt::null) {
        entity = m_ctx.ecs->CreateWithStableId(kSettlementStateStableId);
    }
    if (entity == entt::null) {
        return;
    }
    SettlementState state;
    system->CaptureState(*m_ctx.ecs, &state);
    m_ctx.ecs->Registry().emplace_or_replace<SettlementState>(
        entity, std::move(state));
}

void WorldDemoState::ApplySettlementState() {
    if (m_ctx.ecs == nullptr || m_ctx.simulation == nullptr) {
        return;
    }
    SettlementNpcSystem* system = m_ctx.simulation->SettlementNpcs();
    if (system == nullptr) {
        return;
    }
    bool applied = false;
    m_ctx.ecs->Registry().view<SettlementState>().each(
        [&](const SettlementState& state) {
            if (!applied) {
                system->ApplyState(state);
                applied = true;
            }
        });
}

void WorldDemoState::PollSettlementDefenseNotices() {
    if (m_networkMode) {
        if (m_ctx.networkClient == nullptr ||
            !m_ctx.networkClient->HasPlayerState()) {
            return;
        }
        if (m_dialogueOpen && !m_dialogueSettlementId.empty()) {
            const auto it =
                m_networkSettlements.find(m_dialogueSettlementId);
            if (it != m_networkSettlements.end() &&
                (it->second.fallen || it->second.raidActive)) {
                CloseSettlementDialogue();
            }
        }
        if (m_tradeOpen) {
            for (const auto& [id, status] : m_networkSettlements) {
                static_cast<void>(id);
                if (status.fallen || status.raidActive) {
                    m_tradeOpen = false;
                    break;
                }
            }
        }
        if (m_networkSettlementNoticeSerial !=
            m_lastSettlementNoticeSerial) {
            m_lastSettlementNoticeSerial =
                m_networkSettlementNoticeSerial;
            if (!m_networkSettlementNotice.empty()) {
                const std::uint32_t color =
                    m_networkSettlementNoticeTone == "ok"
                        ? kNoticeOkColor
                        : (m_networkSettlementNoticeTone == "fail"
                               ? kNoticeFailColor
                               : kNoticeInfoColor);
                SetNotice(m_networkSettlementNotice, color, 3.5f);
            }
        }
        return;
    }
    if (m_ctx.simulation == nullptr) {
        return;
    }
    const SettlementNpcSystem* system =
        m_ctx.simulation->SettlementNpcs();
    if (system == nullptr || system->NoticeSerial() == 0) {
        return;
    }
    if (m_dialogueOpen && !m_dialogueSettlementId.empty() &&
        (system->RaidActive(m_dialogueSettlementId) ||
         system->IsFallen(m_dialogueSettlementId))) {
        CloseSettlementDialogue();
    }
    if (m_tradeOpen) {
        for (const SettlementDef& settlement : m_settlementConfig.defs) {
            if (system->RaidActive(settlement.id) ||
                system->IsFallen(settlement.id)) {
                m_tradeOpen = false;
                break;
            }
        }
    }
    if (system->NoticeSerial() == m_lastSettlementNoticeSerial) {
        return;
    }
    m_lastSettlementNoticeSerial = system->NoticeSerial();
    const uint32_t color =
        system->NoticeTone() == "ok"
            ? kNoticeOkColor
            : (system->NoticeTone() == "fail" ? kNoticeFailColor
                                              : kNoticeInfoColor);
    SetNotice(system->NoticeText(), color, 3.5f);
}

void WorldDemoState::UpdateHomestead(float tickDt) {
    m_powerVisualTime += std::max(0.0f, tickDt);
    if (m_networkMode || m_runOutcome != RunOutcome::None ||
        m_ctx.ecs == nullptr || m_ctx.gameTime == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const double now = m_ctx.gameTime->TotalGameMinutes();
    float gameMinutes = 0.0f;
    if (m_lastHomesteadMinute >= 0.0 && now > m_lastHomesteadMinute) {
        gameMinutes = static_cast<float>(now - m_lastHomesteadMinute);
    }
    m_lastHomesteadMinute = now;

    if (m_homesteadConfig.farm.enabled && gameMinutes > 0.0f) {
        const FarmSeason season =
            FarmSeasonFromMonth(m_ctx.gameTime->Now().month);
        const WeatherState weather =
            m_ctx.simulation != nullptr ? m_ctx.simulation->CurrentWeather()
                                        : WeatherState{};
        const bool raining = weather.kind == WeatherKind::Rain &&
                             weather.rainIntensity > 0.0f;
        registry.view<ecs::FarmPlot>().each(
            [&](const ecs::Entity, ecs::FarmPlot& plot) {
                const CropDef* crop =
                    m_ctx.crops != nullptr ? m_ctx.crops->Find(plot.cropId)
                                           : nullptr;
                AdvanceFarmPlot(plot, gameMinutes, m_homesteadConfig.farm,
                                crop, season, raining);
            });
    }

    UpdatePowerDevices(gameMinutes, tickDt);

    if (m_homesteadConfig.trade.enabled) {
        const int day =
            1 + static_cast<int>(now / (24.0 * 60.0));
        registry.view<ecs::MerchantStock>().each(
            [&](const ecs::Entity, ecs::MerchantStock& stock) {
                ClampMerchantStock(stock);
                if (stock.restockDay <= 0 ||
                    day >= stock.restockDay + m_homesteadConfig.trade.restockDays) {
                    const auto& offers = MerchantOffers();
                    for (std::size_t i = 0; i < offers.size() && i < stock.stock.size();
                         ++i) {
                        stock.stock[i] = offers[i].maxStock;
                    }
                    stock.restockDay = day;
                }
            });
    }
}

void WorldDemoState::UpdatePowerDevices(float gameMinutes, float tickDt) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const PowerConfig& config = m_homesteadConfig.power;
    const bool night = m_ctx.gameTime != nullptr && m_ctx.gameTime->IsNight();
    const bool devicesActive = !config.deviceRequiresNight || night;
    int totalGenerators = 0;
    registry.view<ecs::Generator>().each(
        [&](const ecs::Entity, ecs::Generator& generator) {
            ++totalGenerators;
            generator.load = 0.0f;
        });
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
        m_poweredFenceCells.clear();
        m_poweredFloodlights.clear();
        m_electricFenceCells.clear();
        m_powerScanTimer = 0.0f;
        m_baseHud.powerFuelMinutes = 0.0f;
        m_baseHud.powerLoad = 0.0f;
        m_baseHud.powerLowFuel = false;
        m_baseHud.powerOutage = false;
        m_powerWasOnline = false;
        m_powerLowFuelWarned = false;
        return;
    }

    struct SourceRuntime {
        ecs::Entity entity = entt::null;
        PowerSource supply;
    };
    std::vector<SourceRuntime> sources;
    const auto collectSources = [&]() {
        sources.clear();
        registry.view<ecs::Generator, ecs::Position>().each(
            [&](const ecs::Entity entity, ecs::Generator& generator,
                const ecs::Position& position) {
                generator.load = 0.0f;
                if (sources.size() >= 8 || !generator.running ||
                    generator.fuelMinutes <= 0.0f || generator.radius <= 0.0f) {
                    return;
                }
                sources.push_back(SourceRuntime{
                    entity, PowerSource{position.x, position.y,
                                        generator.radius}});
            });
    };

    std::unordered_set<std::uint64_t> seenFenceCells;
    const auto collectFenceCells = [&]() {
        m_electricFenceCells.clear();
        seenFenceCells.clear();
        for (const SourceRuntime& source : sources) {
            const int scanRadius = std::clamp(
                static_cast<int>(std::ceil(source.supply.radius)), 1, 12);
            const int cx = static_cast<int>(std::floor(source.supply.x));
            const int cy = static_cast<int>(std::floor(source.supply.y));
            for (int wy = cy - scanRadius; wy <= cy + scanRadius; ++wy) {
                for (int wx = cx - scanRadius; wx <= cx + scanRadius; ++wx) {
                    if (m_electricFenceCells.size() >= 256) {
                        return;
                    }
                    const int dx = wx - cx;
                    const int dy = wy - cy;
                    if (dx * dx + dy * dy > scanRadius * scanRadius) {
                        continue;
                    }
                    const std::uint64_t key = BuildCellKey(wx, wy);
                    if (!seenFenceCells.insert(key).second) {
                        continue;
                    }
                    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
                    const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
                    if (chunk == nullptr ||
                        chunk->State() != world::ChunkState::Loaded) {
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
                                object.sprite == kElectricFenceSprite &&
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
                        m_electricFenceCells.emplace_back(wx, wy);
                    }
                }
            }
        }
    };

    const auto assignDevices = [&]() {
        std::vector<PowerSource> supplies;
        supplies.reserve(sources.size());
        for (SourceRuntime& source : sources) {
            auto* generator = registry.try_get<ecs::Generator>(source.entity);
            if (generator != nullptr) {
                generator->load = 0.0f;
            }
            supplies.push_back(source.supply);
        }
        const auto powerAt = [&](float x, float y) {
            return FindPowerSourceFor(supplies, x, y);
        };

        registry.view<ecs::ColdStorage, ecs::Position>().each(
            [&](const ecs::Entity, ecs::ColdStorage& storage,
                const ecs::Position& position) {
                const int source = powerAt(position.x, position.y);
                storage.powered = source >= 0;
                if (source >= 0) {
                    if (auto* generator = registry.try_get<ecs::Generator>(
                            sources[static_cast<std::size_t>(source)].entity);
                        generator != nullptr) {
                        generator->load += config.coldStorageLoad;
                    }
                }
            });

        m_poweredFloodlights.clear();
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
                if (source < 0) {
                    return;
                }
                if (devicesActive) {
                    if (auto* generator = registry.try_get<ecs::Generator>(
                            sources[static_cast<std::size_t>(source)].entity);
                        generator != nullptr) {
                        generator->load += config.floodlightLoad;
                    }
                    m_poweredFloodlights.push_back(
                        PowerGlow{position.x, position.y,
                                  config.floodlightRadius});
                }
            });

        m_poweredFenceCells.clear();
        for (const auto& [wx, wy] : m_electricFenceCells) {
            const int source = powerAt(static_cast<float>(wx) + 0.5f,
                                       static_cast<float>(wy) + 0.5f);
            if (source < 0) {
                continue;
            }
            m_poweredFenceCells.insert(BuildCellKey(wx, wy));
            if (devicesActive) {
                if (auto* generator = registry.try_get<ecs::Generator>(
                        sources[static_cast<std::size_t>(source)].entity);
                    generator != nullptr) {
                    generator->load += config.electricFenceLoad;
                }
            }
        }
    };

    collectSources();
    m_powerScanTimer -= std::max(0.0f, tickDt);
    if (m_powerScanTimer <= 0.0f) {
        m_powerScanTimer = 0.5f;
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
            AdvanceGenerator(*generator, gameMinutes,
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

    m_baseHud.powerLoad = 0.0f;
    float maxEffectiveMinutes = 0.0f;
    bool lowFuel = false;
    for (const SourceRuntime& source : sources) {
        const auto* generator = registry.try_get<ecs::Generator>(source.entity);
        if (generator == nullptr || !generator->running) {
            continue;
        }
        m_baseHud.powerLoad += config.baseLoad + generator->load;
        const float effective =
            GeneratorEffectiveMinutes(*generator, config.baseLoad);
        maxEffectiveMinutes = std::max(maxEffectiveMinutes, effective);
        if (config.lowFuelWarningMinutes > 0.0f &&
            effective < config.lowFuelWarningMinutes) {
            lowFuel = true;
        }
    }
    m_baseHud.powerFuelMinutes = maxEffectiveMinutes;
    m_baseHud.powerLowFuel = lowFuel;
    m_baseHud.powerOutage = totalGenerators > 0 && sources.empty();
    if (m_baseHud.powerLowFuel && !m_powerLowFuelWarned) {
        SetNotice("低燃料：发电机不足 " +
                      std::to_string(static_cast<int>(
                          std::lround(config.lowFuelWarningMinutes))) +
                      " 分钟",
                  kNoticeFailColor, 3.0f);
        m_powerLowFuelWarned = true;
    } else if (!m_baseHud.powerLowFuel) {
        m_powerLowFuelWarned = false;
    }
    if (m_baseHud.powerOutage && m_powerWasOnline) {
        SetNotice("停电：发电机已停止供电", kNoticeFailColor, 3.0f);
        m_powerWasOnline = false;
    } else if (!sources.empty()) {
        m_powerWasOnline = true;
    } else if (totalGenerators == 0) {
        m_powerWasOnline = false;
    }
}

bool WorldDemoState::TryInteractHomestead() {
    if (m_runOutcome != RunOutcome::None || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr || m_player == entt::null) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (container == nullptr) {
        return false;
    }
    const auto itemName = [this](std::string_view defId) {
        const items::ItemDef* def = m_ctx.items->Find(std::string(defId));
        return def != nullptr ? def->name : std::string(defId);
    };
    if (!m_buildMode && TryInteractSettlementNpc()) {
        return true;
    }
    const HomesteadTarget target = FindNearestHomesteadTarget();

    if (target.kind == HomesteadTargetKind::Merchant) {
        m_tradeOpen = true;
        m_tradeIndex = std::clamp(m_tradeIndex, 0,
                                  static_cast<int>(kMerchantOfferCount) - 1);
        SubmitMoveCommand(0.0f, 0.0f);
        if (m_ctx.ui != nullptr) {
            m_ctx.ui->CloseInventory();
        }
            SetNotice("开始交易：幸存者商人", kNoticeInfoColor, 1.5f);
        return true;
    }

    if (target.kind != HomesteadTargetKind::None &&
        target.kind != HomesteadTargetKind::Merchant) {
        const std::uint64_t owner = EntityOwnerStableId(target.entity);
        if (!CanEditLocalStructure(owner)) {
            SetNotice("该设施属于 " + StructureOwnerLabel(owner) +
                          "，只有所有者可操作",
                      kNoticeFailColor, 1.8f);
            return true;
        }
    }

    if (m_networkMode) {
        if (target.kind == HomesteadTargetKind::None ||
            target.entity == entt::null) {
            return false;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::InteractFacility);
        command.payload.entityId = m_ctx.ecs->IdOf(target.entity).id;
        if (target.kind == HomesteadTargetKind::Farm) {
            const auto* plot =
                m_ctx.ecs->Registry().try_get<ecs::FarmPlot>(target.entity);
            if (plot != nullptr && plot->stage == ecs::CropStage::Empty &&
                m_ctx.crops != nullptr) {
                if (const CropDef* selected = SelectedFarmCrop();
                    selected != nullptr) {
                    const auto& crops = m_ctx.crops->All();
                    for (std::size_t i = 0; i < crops.size(); ++i) {
                        if (crops[i].id == selected->id) {
                            command.payload.flags = kFarmActionPlant;
                            command.payload.index =
                                static_cast<std::int32_t>(i);
                            break;
                        }
                    }
                }
            }
        }
        if (!SubmitPlayerCommand(command)) {
            return false;
        }
        SubmitMoveCommand(0.0f, 0.0f);
        return true;
    }

    if (target.kind == HomesteadTargetKind::Farm) {
        const ecs::Entity entity = target.entity;
        if (entity != entt::null) {
            auto* plot = registry.try_get<ecs::FarmPlot>(entity);
            if (plot == nullptr) {
                return true;
            }
            const auto findCrop = [&](std::string_view cropId)
                -> const CropDef* {
                return m_ctx.crops != nullptr ? m_ctx.crops->Find(cropId)
                                              : nullptr;
            };
            if (plot->stage == ecs::CropStage::Empty) {
                const CropDef* selected = SelectedFarmCrop();
                if (m_ctx.crops != nullptr && m_ctx.crops->Size() > 0 &&
                    selected == nullptr) {
                    SetNotice("农田需要可种植的种子", kNoticeFailColor,
                              1.8f);
                    return true;
                }
                const std::string seedItem =
                    selected != nullptr ? selected->seedItem
                                        : m_homesteadConfig.farm.seedItem;
                if (items::CountOf(*container, seedItem) < 1 ||
                    items::RemoveItem(*container, seedItem, 1) != 1) {
                    SetNotice("农田需??" + itemName(seedItem),
                              kNoticeFailColor, 1.8f);
                    return true;
                }
                const bool planted =
                    selected != nullptr
                        ? PlantFarmPlot(*plot, *selected)
                        : PlantFarmPlot(*plot, "cabbage",
                                        m_homesteadConfig.farm.growMinutes,
                                        m_homesteadConfig.farm.harvestYield);
                if (!planted) {
                    SetNotice("种植失败", kNoticeFailColor, 1.5f);
                    return true;
                }
                const std::string cropName =
                    selected != nullptr ? selected->name : "白菜";
                SetNotice("已种植" + cropName + "；下一步需要浇水",
                          kNoticeOkColor, 2.0f);
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
                    SetNotice("清理了病害作物", kNoticeInfoColor, 1.5f);
                    return true;
                }
                const CropDef* crop = findCrop(plot->cropId);
                const float waterMinutes =
                    crop != nullptr ? crop->waterMinutes
                                    : m_homesteadConfig.farm.waterMinutes;
                const float maxWaterMinutes =
                    crop != nullptr ? crop->maxWaterMinutes
                                    : m_homesteadConfig.farm.maxWaterMinutes;
                if (items::CountOf(*container, m_homesteadConfig.farm.waterItem) < 1) {
                    SetNotice("浇水需??" + itemName(m_homesteadConfig.farm.waterItem),
                              kNoticeFailColor, 1.8f);
                    return true;
                }
                if (!WaterFarmPlot(*plot, waterMinutes, maxWaterMinutes)) {
                    SetNotice("浇水失败", kNoticeFailColor, 1.5f);
                    return true;
                }
                if (items::RemoveItem(*container, m_homesteadConfig.farm.waterItem,
                                      1) == 1) {
                    ecs::ItemInstance empty{"empty_bottle", 1, 1.0f};
                    if (const items::ItemDef* itemDef =
                            m_ctx.items->Find(m_homesteadConfig.farm.waterItem);
                        itemDef != nullptr && !itemDef->use.replaceWith.empty()) {
                        empty.defId = itemDef->use.replaceWith;
                    }
                    ReturnOrDropItem(empty);
                }
                SetNotice("浇水完成：" + FarmStatusText(*plot, crop),
                          kNoticeOkColor, 2.0f);
                return true;
            }
            if (plot->stage == ecs::CropStage::Mature) {
                const CropDef* crop = findCrop(plot->cropId);
                const std::string produceItem =
                    crop != nullptr ? crop->produceItem
                                    : m_homesteadConfig.farm.produceItem;
                const std::string seedItem =
                    crop != nullptr ? crop->seedItem
                                    : m_homesteadConfig.farm.seedItem;
                const int seedYield =
                    crop != nullptr ? crop->seedYield
                                    : m_homesteadConfig.farm.seedYield;
                int harvested = 0;
                ecs::Container probe = *container;
                ecs::ItemInstance produce{produceItem,
                                          std::max(1, plot->harvestYield), 1.0f};
                const int allowed = items::AddItem(probe, *m_ctx.items, produce);
                if (allowed <= 0) {
                    SetNotice("背包空间不足，无法收??", kNoticeFailColor, 1.8f);
                    return true;
                }
                if (!HarvestFarmPlot(*plot, &harvested)) {
                    return true;
                }
                ecs::ItemInstance output{produceItem, harvested, 1.0f};
                const int added = items::AddItem(*container, *m_ctx.items, output);
                if (added < harvested) {
                    ecs::ItemInstance remainder = output;
                    remainder.count = harvested - added;
                    ReturnOrDropItem(remainder);
                }
                std::string harvestNotice =
                    "收获 " + itemName(produceItem) + " ×" +
                    std::to_string(harvested);
                if (seedYield > 0) {
                    ecs::ItemInstance seeds{seedItem, seedYield, 1.0f};
                    const int seedAdded =
                        items::AddItem(*container, *m_ctx.items, seeds);
                    if (seedAdded < seeds.count) {
                        ecs::ItemInstance remainder = seeds;
                        remainder.count = seeds.count - seedAdded;
                        ReturnOrDropItem(remainder);
                    }
                    harvestNotice += " + " + itemName(seedItem) + " ×" +
                                     std::to_string(seedYield);
                }
                SetNotice(std::move(harvestNotice), kNoticeOkColor, 2.0f);
                return true;
            }
            plot->cropId.clear();
            plot->growthMinutes = 0.0f;
            plot->waterMinutes = 0.0f;
            plot->dryMinutes = 0.0f;
            plot->stage = ecs::CropStage::Empty;
            plot->diseased = false;
            plot->fertilized = false;
            SetNotice("清理了枯萎作物", kNoticeInfoColor, 1.5f);
            return true;
        }
    }

    if (target.kind == HomesteadTargetKind::Floodlight) {
        auto* floodlight = registry.try_get<ecs::Floodlight>(target.entity);
        if (floodlight == nullptr) {
            return true;
        }
        floodlight->on = !floodlight->on;
        if (!floodlight->on) {
            floodlight->powered = false;
        }
        const bool night =
            m_ctx.gameTime != nullptr && m_ctx.gameTime->IsNight();
        SetNotice(FloodlightStatusText(
                      *floodlight, night,
                      m_homesteadConfig.power.deviceRequiresNight),
                  floodlight->on ? kNoticeOkColor : kNoticeInfoColor, 2.0f);
        return true;
    }

    if (target.kind == HomesteadTargetKind::Generator) {
        auto* generator = registry.try_get<ecs::Generator>(target.entity);
        if (generator == nullptr) {
            return true;
        }
        if (generator->running) {
            generator->running = false;
            SetNotice(GeneratorStatusText(*generator,
                                              m_homesteadConfig.power.baseLoad),
                      kNoticeInfoColor, 2.0f);
            return true;
        }
        if (generator->fuelMinutes <= 0.0f) {
            if (items::CountOf(*container, m_homesteadConfig.power.fuelItem) < 1) {
                SetNotice("发电机需要 " +
                              itemName(m_homesteadConfig.power.fuelItem),
                          kNoticeFailColor, 1.8f);
                return true;
            }
            if (items::RemoveItem(*container,
                                  m_homesteadConfig.power.fuelItem,
                                  1) != 1) {
                return true;
            }
            generator->fuelMinutes +=
                m_homesteadConfig.power.fuelMinutesPerCan;
        }
        generator->running = true;
        SetNotice(GeneratorStatusText(*generator,
                                          m_homesteadConfig.power.baseLoad),
                  kNoticeOkColor, 2.0f);
        return true;
    }

    if (target.kind == HomesteadTargetKind::Trap) {
        auto* trap = registry.try_get<ecs::SpikeTrap>(target.entity);
        if (trap == nullptr) {
            return true;
        }
        if (trap->uses <= 0) {
            SetNotice("陷阱已耗尽，需要拆除重建", kNoticeFailColor, 1.8f);
            return true;
        }
        trap->armed = !trap->armed;
        SetNotice(trap->armed ? "陷阱已布防" : "陷阱已解除",
                  trap->armed ? kNoticeOkColor : kNoticeInfoColor, 1.5f);
        return true;
    }
    return false;
}

FarmSeason WorldDemoState::CurrentFarmSeason() const {
    if (m_ctx.gameTime == nullptr) {
        return FarmSeason::Summer;
    }
    return FarmSeasonFromMonth(m_ctx.gameTime->Now().month);
}

const CropDef* WorldDemoState::SelectedFarmCrop() const {
    if (m_ctx.crops == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return nullptr;
    }
    const auto* container =
        m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (container == nullptr || m_ctx.crops->Size() == 0) {
        return nullptr;
    }
    const auto& crops = m_ctx.crops->All();
    if (!m_farmSeedCropId.empty()) {
        for (const CropDef& crop : crops) {
            if (crop.id == m_farmSeedCropId &&
                items::CountOf(*container, crop.seedItem) >= 1) {
                return &crop;
            }
        }
    }
    for (const CropDef& crop : crops) {
        if (items::CountOf(*container, crop.seedItem) >= 1) {
            return &crop;
        }
    }
    return nullptr;
}

void WorldDemoState::CycleFarmSeed() {
    if (m_ctx.crops == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return;
    }
    const HomesteadTarget target = FindNearestHomesteadTarget();
    if (target.kind != HomesteadTargetKind::Farm) {
        return;
    }
    const std::uint64_t owner = EntityOwnerStableId(target.entity);
    if (!CanEditLocalStructure(owner)) {
        SetNotice("该设施属于 " + StructureOwnerLabel(owner) +
                      "，只有所有者可操作",
                  kNoticeFailColor, 1.8f);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* plot = registry.try_get<ecs::FarmPlot>(target.entity);
    const auto* container = registry.try_get<ecs::Container>(m_player);
    if (plot == nullptr || container == nullptr) {
        return;
    }
    if (plot->stage != ecs::CropStage::Empty) {
        SetNotice("农田已有作物；空地才能选择种子", kNoticeInfoColor, 1.5f);
        return;
    }
    const auto& crops = m_ctx.crops->All();
    if (crops.empty()) {
        return;
    }
    std::size_t current = crops.size();
    if (!m_farmSeedCropId.empty()) {
        for (std::size_t i = 0; i < crops.size(); ++i) {
            if (crops[i].id == m_farmSeedCropId) {
                current = i;
                break;
            }
        }
    }
    for (std::size_t step = 1; step <= crops.size(); ++step) {
        const std::size_t index = (current + step) % crops.size();
        const CropDef& crop = crops[index];
        if (items::CountOf(*container, crop.seedItem) < 1) {
            continue;
        }
        m_farmSeedCropId = crop.id;
        SetNotice("已选择" + crop.name + "种子（E 种植??",
                  kNoticeInfoColor, 1.5f);
        return;
    }
    SetNotice("没有可种植的种子", kNoticeFailColor, 1.8f);
}

bool WorldDemoState::TryFarmCare() {
    if (m_runOutcome != RunOutcome::None || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr || m_player == entt::null) {
        return false;
    }
    const HomesteadTarget target = FindNearestHomesteadTarget();
    if (target.kind != HomesteadTargetKind::Farm) {
        return false;
    }
    const std::uint64_t owner = EntityOwnerStableId(target.entity);
    if (!CanEditLocalStructure(owner)) {
        SetNotice("该设施属于 " + StructureOwnerLabel(owner) +
                      "，只有所有者可操作",
                  kNoticeFailColor, 1.8f);
        return true;
    }
    if (m_networkMode) {
        net::Command command =
            MakePlayerCommand(net::CommandType::InteractFacility);
        command.payload.entityId = m_ctx.ecs->IdOf(target.entity).id;
        command.payload.flags = kFarmActionCare;
        SubmitMoveCommand(0.0f, 0.0f);
        return SubmitPlayerCommand(command);
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* plot = registry.try_get<ecs::FarmPlot>(target.entity);
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (plot == nullptr || container == nullptr) {
        return false;
    }
    const FarmConfig& config = m_homesteadConfig.farm;
    const auto itemName = [this](std::string_view defId) {
        const items::ItemDef* def = m_ctx.items->Find(std::string(defId));
        return def != nullptr ? def->name : std::string(defId);
    };
    if (plot->stage != ecs::CropStage::Growing) {
        SetNotice("只有生长中的作物需要施肥/杀菌",
                  kNoticeInfoColor, 1.5f);
        return true;
    }
    if (plot->diseased) {
        if (items::CountOf(*container, config.fungicideItem) < 1) {
            SetNotice("杀菌需??" + itemName(config.fungicideItem),
                      kNoticeFailColor, 1.8f);
            return true;
        }
        if (items::RemoveItem(*container, config.fungicideItem, 1) != 1 ||
            !CureFarmDisease(*plot)) {
            SetNotice("杀菌失??", kNoticeFailColor, 1.5f);
            return true;
        }
        SetNotice("已杀菌：" + FarmStatusText(
                      *plot, m_ctx.crops != nullptr
                                 ? m_ctx.crops->Find(plot->cropId)
                                 : nullptr,
                      FarmSeasonName(CurrentFarmSeason())),
                  kNoticeOkColor, 2.0f);
        return true;
    }
    if (plot->fertilized) {
        SetNotice("该农田已经施过肥", kNoticeInfoColor, 1.5f);
        return true;
    }
    if (items::CountOf(*container, config.fertilizerItem) < 1) {
        SetNotice("施肥需??" + itemName(config.fertilizerItem),
                  kNoticeFailColor, 1.8f);
        return true;
    }
    if (items::RemoveItem(*container, config.fertilizerItem, 1) != 1 ||
        !FertilizeFarmPlot(*plot, config.fertilizerYieldBonus)) {
        SetNotice("施肥失败", kNoticeFailColor, 1.5f);
        return true;
    }
    SetNotice("施肥完成：" + FarmStatusText(
                  *plot,
                  m_ctx.crops != nullptr ? m_ctx.crops->Find(plot->cropId)
                                         : nullptr,
                  FarmSeasonName(CurrentFarmSeason())),
              kNoticeOkColor, 2.0f);
    return true;
}

bool WorldDemoState::TryBuyTradeOffer(int index) {
    if (!m_tradeOpen || !m_homesteadConfig.trade.enabled || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr || m_player == entt::null || index < 0 ||
        index >= static_cast<int>(kMerchantOfferCount)) {
        return false;
    }
    const auto& offers = MerchantOffers();
    const TradeOffer& offer = offers[static_cast<std::size_t>(index)];
    const ecs::Entity merchant = FindNearbyMerchant();
    if (merchant == entt::null) {
        m_tradeOpen = false;
        return false;
    }
    if (m_networkMode) {
        net::Command command = MakePlayerCommand(net::CommandType::TradeOffer);
        command.payload.entityId = m_ctx.ecs->IdOf(merchant).id;
        command.payload.index = index;
        SubmitMoveCommand(0.0f, 0.0f);
        return SubmitPlayerCommand(command);
    }
    if (const auto* npc =
            m_ctx.ecs->Registry().try_get<SettlementNpc>(merchant);
        npc != nullptr) {
        bool fallen = false;
        bool raidActive = false;
        (void)SettlementDefenseBlocked(npc->settlementId, &fallen,
                                       &raidActive);
        if (fallen || raidActive) {
            m_tradeOpen = false;
            SetNotice(fallen ? "聚落已失守，交易暂停"
                             : "夜袭中，无法交易",
                      kNoticeFailColor, 2.0f);
            return false;
        }
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* stock = registry.try_get<ecs::MerchantStock>(merchant);
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (stock == nullptr || container == nullptr ||
        static_cast<std::size_t>(index) >= stock->stock.size()) {
        return false;
    }
    if (stock->stock[static_cast<std::size_t>(index)] <= 0) {
        SetNotice("商人库存不足", kNoticeInfoColor, 1.5f);
        return false;
    }
    if (items::CountOf(*container, std::string(offer.costDefId)) <
        offer.costCount) {
        SetNotice("物资不足：需要 " + std::string(offer.costDefId) + " ×" +
                      std::to_string(offer.costCount),
                  kNoticeFailColor, 1.8f);
        return false;
    }
    if (!offer.blueprint.empty() && RecipeKnown(offer.blueprint)) {
        SetNotice("已经学会该图纸", kNoticeInfoColor, 1.5f);
        return false;
    }
    if (!offer.rewardDefId.empty()) {
        ecs::Container probe = *container;
        ecs::ItemInstance reward{std::string(offer.rewardDefId),
                                 offer.rewardCount, 1.0f};
        if (items::AddItem(probe, *m_ctx.items, reward) < offer.rewardCount) {
            SetNotice("背包空间不足", kNoticeFailColor, 1.8f);
            return false;
        }
    }
    if (items::RemoveItem(*container, std::string(offer.costDefId),
                          offer.costCount) != offer.costCount) {
        return false;
    }
    if (!offer.rewardDefId.empty()) {
        ecs::ItemInstance reward{std::string(offer.rewardDefId),
                                 offer.rewardCount, 1.0f};
        const int added = items::AddItem(*container, *m_ctx.items, reward);
        if (added < offer.rewardCount) {
            ecs::ItemInstance cost{std::string(offer.costDefId),
                                   offer.costCount, 1.0f};
            ReturnOrDropItem(cost);
            return false;
        }
    }
    if (!offer.blueprint.empty()) {
        auto& known = registry.get_or_emplace<ecs::KnownRecipes>(m_player);
        known.recipeIds.emplace_back(offer.blueprint);
        std::sort(known.recipeIds.begin(), known.recipeIds.end());
        known.recipeIds.erase(
            std::unique(known.recipeIds.begin(), known.recipeIds.end()),
            known.recipeIds.end());
    }
    --stock->stock[static_cast<std::size_t>(index)];
    std::string notice = "交易完成：" + std::string(offer.name);
    if (!offer.blueprint.empty()) {
        notice += "（已学习图纸）";
    }
    SetNotice(std::move(notice), kNoticeOkColor, 2.0f);
    ZL_INFO("homestead", "商人交易：{}，库存 {}", offer.id,
            stock->stock[static_cast<std::size_t>(index)]);
    return true;
}

void WorldDemoState::HandleTradeInput(core::Input& input) {
    SubmitMoveCommand(0.0f, 0.0f);
    if (!m_tradeOpen) {
        return;
    }
    if (FindNearbyMerchant() == entt::null) {
        m_tradeOpen = false;
        return;
    }
    if (input.IsActionPressed("Quit") || input.IsActionPressed("Use")) {
        m_tradeOpen = false;
        SetNotice("结束交易", kNoticeInfoColor, 1.2f);
        return;
    }
    m_scrollAccum += input.ScrollDeltaY();
    if (m_scrollAccum >= 1.0f || m_scrollAccum <= -1.0f) {
        const int count = static_cast<int>(kMerchantOfferCount);
        m_tradeIndex =
            (m_tradeIndex + (m_scrollAccum > 0.0f ? 1 : count - 1)) % count;
        m_scrollAccum = 0.0f;
    }
    for (int i = 0; i < static_cast<int>(kMerchantOfferCount); ++i) {
        if (input.IsActionPressed(kHotbarActions[i])) {
            (void)TryBuyTradeOffer(i);
            return;
        }
    }
}

bool WorldDemoState::FindNearbySettlementNpc(float range,
                                             ecs::Entity* out) const {
    if (out != nullptr) {
        *out = entt::null;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null || range <= 0.0f) {
        return false;
    }
    const auto* playerPosition =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (playerPosition == nullptr) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const float limitSq = range * range;
    float bestSq = limitSq;
    ecs::Entity best = entt::null;
    registry.view<SettlementNpc, ecs::Position>().each(
        [&](const ecs::Entity entity, const SettlementNpc&,
            const ecs::Position& position) {
            if (position.z != playerPosition->z) {
                return;
            }
            const float dx = position.x - playerPosition->x;
            const float dy = position.y - playerPosition->y;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq <= bestSq) {
                bestSq = distanceSq;
                best = entity;
            }
        });
    if (out != nullptr) {
        *out = best;
    }
    return best != entt::null;
}

bool WorldDemoState::DialogueNpcInRange() const {
    if (m_dialogueNpc == entt::null || m_ctx.ecs == nullptr ||
        m_player == entt::null || !m_ctx.ecs->Alive(m_dialogueNpc)) {
        return false;
    }
    const auto* playerPosition =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    const auto* npcPosition =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_dialogueNpc);
    if (playerPosition == nullptr || npcPosition == nullptr ||
        npcPosition->z != playerPosition->z) {
        return false;
    }
    const float range = m_buildActionRange + 1.0f;
    const float dx = npcPosition->x - playerPosition->x;
    const float dy = npcPosition->y - playerPosition->y;
    return dx * dx + dy * dy <= range * range;
}

void WorldDemoState::CloseSettlementDialogue() {
    m_dialogueOpen = false;
    m_dialogueNpc = entt::null;
    m_dialogueSettlementId.clear();
    m_dialogueNodeId.clear();
    m_dialogueIndex = 0;
    m_dialogueTree = {};
}

void WorldDemoState::OpenSettlementDialogue(ecs::Entity npc) {
    if (m_ctx.ecs == nullptr || !m_ctx.ecs->Alive(npc)) {
        return;
    }
    const auto* component = m_ctx.ecs->Registry().try_get<SettlementNpc>(npc);
    if (component == nullptr) {
        return;
    }
    const SettlementDef* settlement =
        m_settlementConfig.Find(component->settlementId);
    if (settlement == nullptr) {
        return;
    }
    m_dialogueTree = MakeSettlementDialogue(*settlement, component->kind);
    const DialogueNode* root =
        m_dialogueTree.FindNode(m_dialogueTree.startNode);
    if (root == nullptr) {
        CloseSettlementDialogue();
        return;
    }
    m_dialogueOpen = true;
    m_dialogueNpc = npc;
    m_dialogueSettlementId = settlement->id;
    m_dialogueNodeId = root->id;
    m_dialogueIndex = 0;
    m_scrollAccum = 0.0f;
    m_tradeOpen = false;
    SubmitMoveCommand(0.0f, 0.0f);
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->CloseInventory();
    }
    SetNotice("交谈??" + std::string(DialogueSpeakerLabel(component->kind)) +
                  " · " + settlement->name,
              kNoticeInfoColor, 1.5f);
    ZL_INFO("dialogue", "打开对话：{}（{}）", settlement->id,
            DialogueSpeakerLabel(component->kind));
}

void WorldDemoState::EnterDialogueNode(std::string_view nodeId) {
    const DialogueNode* node = m_dialogueTree.FindNode(nodeId);
    if (node == nullptr) {
        return;
    }
    m_dialogueNodeId = node->id;
    m_dialogueIndex = 0;
    m_scrollAccum = 0.0f;
}

const DialogueNode* WorldDemoState::CurrentDialogueNode() const {
    return m_dialogueTree.FindNode(m_dialogueNodeId);
}

DialogueContext WorldDemoState::MakeDialogueContext() const {
    DialogueContext context;
    context.settlement = m_settlementConfig.Find(m_dialogueSettlementId);
    context.missions = &m_missionConfig;
    context.missionLog = PlayerMissions();
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        context.inventory =
            m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    }
    context.items = m_ctx.items;
    context.day = CurrentMissionDay();
    return context;
}

std::vector<const DialogueOption*> WorldDemoState::CurrentDialogueOptions()
    const {
    const DialogueNode* node = CurrentDialogueNode();
    if (node == nullptr) {
        return {};
    }
    return VisibleDialogueOptions(*node, MakeDialogueContext());
}

bool WorldDemoState::AcceptSettlementMissionFromDialogue() {
    const SettlementDef* settlement =
        m_settlementConfig.Find(m_dialogueSettlementId);
    if (settlement == nullptr || !m_missionConfig.enabled) {
        SetNotice("当前没有可接取的委托", kNoticeInfoColor, 1.8f);
        return false;
    }
    if (m_networkMode) {
        if (m_ctx.ecs == nullptr || m_dialogueNpc == entt::null ||
            !m_ctx.ecs->Alive(m_dialogueNpc)) {
            SetNotice("对话目标已失效", kNoticeInfoColor, 1.8f);
            return false;
        }
        net::Command command =
            MakePlayerCommand(net::CommandType::AcceptSettlementMission);
        command.payload.entityId = m_ctx.ecs->IdOf(m_dialogueNpc).id;
        SubmitMoveCommand(0.0f, 0.0f);
        if (!SubmitPlayerCommand(command)) {
            SetNotice("接取委托请求发送失败", kNoticeFailColor, 1.8f);
            return false;
        }
        SetNotice("已发送接取委托请求", kNoticeInfoColor, 1.8f);
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    ecs::MissionLog* log = PlayerMissions();
    if (log == nullptr) {
        log = &m_ctx.ecs->Registry().emplace<ecs::MissionLog>(m_player);
    }
    const MissionDef* def = FindAvailableSettlementMission(
        *settlement, m_missionConfig, *log, CurrentMissionDay());
    if (def == nullptr) {
        SetNotice("当前没有可接取的委托", kNoticeInfoColor, 1.8f);
        return false;
    }
    ActivateMission(*log, *def, CurrentMissionDay());
    m_missionFocusId = def->id;
    SetNotice("已接取委托：" + def->name, kNoticeOkColor, 2.5f);
    return true;
}

bool WorldDemoState::FocusSettlementMissionFromDialogue() {
    const SettlementDef* settlement =
        m_settlementConfig.Find(m_dialogueSettlementId);
    const ecs::MissionLog* log = PlayerMissions();
    if (settlement == nullptr || log == nullptr) {
        SetNotice("当前没有进行中的委托", kNoticeInfoColor, 1.8f);
        return false;
    }
    const MissionDef* def =
        FindActiveSettlementMission(*settlement, m_missionConfig, *log);
    if (def == nullptr) {
        SetNotice("当前没有进行中的委托", kNoticeInfoColor, 1.8f);
        return false;
    }
    m_missionFocusId = def->id;
    SetNotice("任务焦点：" + def->name, kNoticeInfoColor, 2.0f);
    return true;
}

void WorldDemoState::ExecuteDialogueOption(const DialogueOption& option) {
    switch (option.result.kind) {
        case DialogueResultKind::None:
            return;
        case DialogueResultKind::Goto:
            EnterDialogueNode(option.result.target);
            return;
        case DialogueResultKind::Close:
            CloseSettlementDialogue();
            return;
        case DialogueResultKind::OpenTrade:
            if (!m_homesteadConfig.trade.enabled) {
                SetNotice("交易未启用", kNoticeInfoColor, 1.8f);
                return;
            }
            CloseSettlementDialogue();
            m_tradeOpen = true;
            m_tradeIndex = 0;
            SetNotice("开始交易：幸存者商人", kNoticeInfoColor, 1.5f);
            return;
        case DialogueResultKind::AcceptSettlementMission:
            (void)AcceptSettlementMissionFromDialogue();
            CloseSettlementDialogue();
            return;
        case DialogueResultKind::FocusSettlementMission:
            (void)FocusSettlementMissionFromDialogue();
            CloseSettlementDialogue();
            return;
    }
}

void WorldDemoState::HandleDialogueInput(core::Input& input) {
    SubmitMoveCommand(0.0f, 0.0f);
    if (!m_dialogueOpen) {
        return;
    }
    if (!DialogueNpcInRange()) {
        CloseSettlementDialogue();
        SetNotice("离开了交谈范围", kNoticeInfoColor, 1.2f);
        return;
    }
    const std::vector<const DialogueOption*> options =
        CurrentDialogueOptions();
    const int count =
        std::min(static_cast<int>(options.size()),
                 static_cast<int>(ui::kMaxDialogueOptions));
    m_dialogueIndex = count > 0 ? std::clamp(m_dialogueIndex, 0, count - 1)
                                : 0;
    if (input.IsActionPressed("Quit")) {
        CloseSettlementDialogue();
        SetNotice("结束交谈", kNoticeInfoColor, 1.2f);
        return;
    }
    m_scrollAccum += input.ScrollDeltaY();
    if (count > 1 &&
        (m_scrollAccum >= 1.0f || m_scrollAccum <= -1.0f)) {
        m_dialogueIndex =
            (m_dialogueIndex + (m_scrollAccum > 0.0f ? 1 : count - 1)) %
            count;
        m_scrollAccum = 0.0f;
    } else if (std::abs(m_scrollAccum) >= 1.0f) {
        m_scrollAccum = 0.0f;
    }
    for (int i = 0; i < count; ++i) {
        if (input.IsActionPressed(kHotbarActions[i])) {
            m_dialogueIndex = i;
            ExecuteDialogueOption(*options[static_cast<std::size_t>(i)]);
            return;
        }
    }
    if (input.IsActionPressed("Use")) {
        if (count > 0) {
            ExecuteDialogueOption(
                *options[static_cast<std::size_t>(m_dialogueIndex)]);
        } else {
            CloseSettlementDialogue();
        }
    }
}

void WorldDemoState::SyncDialogueHud(ui::HudState& hud) {
    if (!m_dialogueOpen) {
        return;
    }
    const DialogueNode* node = CurrentDialogueNode();
    if (node == nullptr) {
        CloseSettlementDialogue();
        return;
    }
    const std::vector<const DialogueOption*> options =
        CurrentDialogueOptions();
    const int count =
        std::min(static_cast<int>(options.size()),
                 static_cast<int>(ui::kMaxDialogueOptions));
    m_dialogueIndex = count > 0 ? std::clamp(m_dialogueIndex, 0, count - 1)
                                : 0;
    hud.dialogueOpen = true;
    hud.dialogueSpeaker = node->speaker;
    hud.dialogueText = node->text;
    hud.dialogueOptionCount = count;
    hud.dialogueSelected = m_dialogueIndex;
    hud.dialogueOptions.fill({});
    for (int i = 0; i < count; ++i) {
        hud.dialogueOptions[static_cast<std::size_t>(i)] =
            "[" + std::to_string(i + 1) + "] " +
            options[static_cast<std::size_t>(i)]->text;
    }
    hud.dialogueHint = "1-5 选择 · 滚轮定位 · E 确认 · ESC 关闭";
    hud.actionHint.clear();
}

bool WorldDemoState::SettlementDefenseBlocked(
    std::string_view settlementId, bool* fallen, bool* raidActive) const {
    if (fallen != nullptr) {
        *fallen = false;
    }
    if (raidActive != nullptr) {
        *raidActive = false;
    }
    if (m_networkMode) {
        const auto it = m_networkSettlements.find(std::string(settlementId));
        if (it == m_networkSettlements.end()) {
            return false;
        }
        if (fallen != nullptr) {
            *fallen = it->second.fallen;
        }
        if (raidActive != nullptr) {
            *raidActive = it->second.raidActive;
        }
        return it->second.fallen || it->second.raidActive;
    }
    const SettlementNpcSystem* system =
        m_ctx.simulation != nullptr ? m_ctx.simulation->SettlementNpcs()
                                    : nullptr;
    if (system == nullptr) {
        return false;
    }
    const bool settlementFallen = system->IsFallen(settlementId);
    const bool settlementRaid = system->RaidActive(settlementId);
    if (fallen != nullptr) {
        *fallen = settlementFallen;
    }
    if (raidActive != nullptr) {
        *raidActive = settlementRaid;
    }
    return settlementFallen || settlementRaid;
}

std::string WorldDemoState::SettlementNpcHint() const {
    ecs::Entity npc = entt::null;
    if (!FindNearbySettlementNpc(m_buildActionRange, &npc) ||
        m_ctx.ecs == nullptr) {
        return {};
    }
    const auto* component =
        m_ctx.ecs->Registry().try_get<SettlementNpc>(npc);
    if (component == nullptr) {
        return {};
    }
    const SettlementDef* settlement =
        m_settlementConfig.Find(component->settlementId);
    bool fallen = false;
    bool raidActive = false;
    (void)SettlementDefenseBlocked(component->settlementId, &fallen,
                                   &raidActive);
    if (fallen) {
        return settlement != nullptr
                   ? settlement->name + "已失守 · 交易与委托暂停"
                   : "聚落已失??· 交易与委托暂??";
    }
    if (raidActive) {
        return settlement != nullptr
                   ? settlement->name + "夜袭中 · 守卫正在迎战"
                   : "夜袭??· 守卫正在迎战";
    }
    std::string hint = "??E 交谈??" +
                       std::string(DialogueSpeakerLabel(component->kind));
    if (settlement != nullptr) {
        hint += " · " + settlement->name;
    }
    if (component->kind == SettlementNpcKind::Merchant) {
        hint += " · 交易/消息";
    } else if (component->kind == SettlementNpcKind::Requester) {
        hint += " · 委托";
    } else {
        hint += " · 营地状??";
    }
    return hint;
}

bool WorldDemoState::TryInteractSettlementNpc() {
    ecs::Entity npc = entt::null;
    if (!FindNearbySettlementNpc(m_buildActionRange, &npc)) {
        return false;
    }
    const auto* component =
        m_ctx.ecs->Registry().try_get<SettlementNpc>(npc);
    if (component != nullptr) {
        bool fallen = false;
        bool raidActive = false;
        (void)SettlementDefenseBlocked(component->settlementId, &fallen,
                                       &raidActive);
        if (fallen) {
            SetNotice("聚落已失守，交易与委托暂停", kNoticeFailColor, 2.5f);
            return true;
        }
        if (raidActive) {
            SetNotice("夜袭中，无法进行对话", kNoticeFailColor, 2.0f);
            return true;
        }
    }
    OpenSettlementDialogue(npc);
    return m_dialogueOpen;
}

WorldDemoState::HomesteadTarget WorldDemoState::FindNearestHomesteadTarget() const {
    HomesteadTarget target;
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return target;
    }
    const auto* playerPosition =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (playerPosition == nullptr) {
        return target;
    }
    const auto consider = [&](HomesteadTargetKind kind, ecs::Entity entity) {
        if (entity == entt::null) {
            return;
        }
        const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(entity);
        if (position == nullptr) {
            return;
        }
        const float dx = position->x - playerPosition->x;
        const float dy = position->y - playerPosition->y;
        const float distance = dx * dx + dy * dy;
        if (target.kind == HomesteadTargetKind::None ||
            distance < target.distanceSq) {
            target.kind = kind;
            target.entity = entity;
            target.distanceSq = distance;
        }
    };
    if (m_homesteadConfig.trade.enabled) {
        consider(HomesteadTargetKind::Merchant, FindNearbyMerchant());
    }
    if (m_homesteadConfig.farm.enabled) {
        consider(HomesteadTargetKind::Farm, FindNearbyFarmPlot());
    }
    if (m_homesteadConfig.power.enabled) {
        consider(HomesteadTargetKind::Generator, FindNearbyGenerator());
        consider(HomesteadTargetKind::Floodlight, FindNearbyFloodlight());
    }
    if (m_homesteadConfig.trap.enabled) {
        consider(HomesteadTargetKind::Trap, FindNearbyTrap());
    }
    return target;
}

ecs::Entity WorldDemoState::FindNearbyFarmPlot() const {
    if (m_ctx.ecs == nullptr) {
        return entt::null;
    }
    return FindNearestComponentEntity<ecs::FarmPlot>(*m_ctx.ecs, m_player,
                                                       m_buildActionRange);
}

ecs::Entity WorldDemoState::FindNearbyGenerator() const {
    if (m_ctx.ecs == nullptr) {
        return entt::null;
    }
    return FindNearestComponentEntity<ecs::Generator>(*m_ctx.ecs, m_player,
                                                        m_buildActionRange);
}

ecs::Entity WorldDemoState::FindNearbyFloodlight() const {
    if (m_ctx.ecs == nullptr) {
        return entt::null;
    }
    return FindNearestComponentEntity<ecs::Floodlight>(*m_ctx.ecs, m_player,
                                                         m_buildActionRange);
}

ecs::Entity WorldDemoState::FindNearbyTrap() const {
    if (m_ctx.ecs == nullptr) {
        return entt::null;
    }
    return FindNearestComponentEntity<ecs::SpikeTrap>(*m_ctx.ecs, m_player,
                                                        m_buildActionRange);
}

ecs::Entity WorldDemoState::FindNearbyMerchant() const {
    if (m_ctx.ecs == nullptr) {
        return entt::null;
    }
    return FindNearestComponentEntity<ecs::MerchantStock>(
        *m_ctx.ecs, m_player, m_homesteadConfig.trade.range);
}

bool WorldDemoState::TryDemolishHomestead() {
    if (m_networkMode || !m_buildMode || m_runOutcome != RunOutcome::None ||
        m_ctx.ecs == nullptr || m_player == entt::null) {
        return false;
    }
    struct Candidate {
        ecs::Entity entity = entt::null;
        std::string id;
        float distance = 0.0f;
    };
    Candidate best;
    const auto consider = [&](ecs::Entity entity, std::string_view id) {
        if (entity == entt::null) {
            return;
        }
        const auto* position = m_ctx.ecs->Registry().try_get<ecs::Position>(entity);
        const auto* playerPosition =
            m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
        if (position == nullptr || playerPosition == nullptr) {
            return;
        }
        const float dx = position->x - playerPosition->x;
        const float dy = position->y - playerPosition->y;
        const float distance = dx * dx + dy * dy;
        if (best.entity == entt::null || distance < best.distance) {
            best = Candidate{entity, std::string(id), distance};
        }
    };
    consider(FindNearbyFarmPlot(), "farm_plot");
    consider(FindNearbyGenerator(), "generator");
    consider(FindNearbyFloodlight(), "floodlight");
    consider(FindNearbyTrap(), "spike_trap");
    if (best.entity == entt::null) {
        return false;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(best.entity);
    if (position == nullptr) {
        return false;
    }
    const std::uint64_t owner = EntityOwnerStableId(best.entity);
    if (!CanEditLocalStructure(owner)) {
        SetNotice("该设施属于 " + StructureOwnerLabel(owner) +
                      "，只有所有者可拆除",
                  kNoticeFailColor, 1.8f);
        return false;
    }
    const BuildableDef* def = BuildableById(best.id);
    const std::string name = def != nullptr ? def->name : best.id;
    const int wx = static_cast<int>(std::floor(position->x));
    const int wy = static_cast<int>(std::floor(position->y));
    const std::uint64_t token = m_ctx.ecs->IdOf(best.entity).id;
    if (!BeginOrConfirmDemolish(PendingDemolishKind::Entity, wx, wy, 0, token,
                                name)) {
        return true;
    }
    const int refund = def != nullptr
                           ? BuildWoodRefund(def->woodCost, m_demolishRefundRatio)
                           : 0;
    m_ctx.ecs->Destroy(best.entity);
    if (refund > 0) {
        ecs::ItemInstance wood{m_buildWoodItem, refund, 1.0f};
        ReturnOrDropItem(wood);
    }
    m_buildActionCooldown = m_buildActionCooldownSeconds;
    std::string notice = "拆除 " + (def != nullptr ? def->name : best.id);
    if (refund > 0) {
        notice += "（返??" + std::to_string(refund) + " 木材??";
    }
    SetNotice(std::move(notice), kNoticeOkColor, 2.0f);
    ZL_INFO("homestead", "拆除设施 {}", best.id);
    return true;
}

void WorldDemoState::UpdateDemolishConfirm(float tickDt) {
    if (m_pendingDemolish.kind == PendingDemolishKind::None) {
        return;
    }
    m_pendingDemolish.remaining =
        std::max(0.0f, m_pendingDemolish.remaining - tickDt);
    if (m_pendingDemolish.remaining <= 0.0f) {
        m_pendingDemolish = {};
    }
}

bool WorldDemoState::PendingDemolishMatches(PendingDemolishKind kind, int wx,
                                            int wy, int layer,
                                            std::uint64_t token) const {
    return m_pendingDemolish.kind == kind &&
           m_pendingDemolish.wx == wx && m_pendingDemolish.wy == wy &&
           m_pendingDemolish.layer == layer &&
           m_pendingDemolish.token == token &&
           m_pendingDemolish.remaining > 0.0f;
}

bool WorldDemoState::BeginOrConfirmDemolish(PendingDemolishKind kind, int wx,
                                            int wy, int layer,
                                            std::uint64_t token,
                                            const std::string& name) {
    if (PendingDemolishMatches(kind, wx, wy, layer, token)) {
        m_pendingDemolish = {};
        return true;
    }
    m_pendingDemolish = PendingDemolish{kind, wx, wy, layer, token, 3.0f};
    if (m_networkMode) {
        net::Command command =
            MakePlayerCommand(net::CommandType::DemolishStructure);
        command.payload.x = static_cast<float>(wx);
        command.payload.y = static_cast<float>(wy);
        command.payload.flags = net::kDemolishConfirmRequestFlag;
        if (!SubmitPlayerCommand(command)) {
            m_pendingDemolish = {};
            SetNotice("拆除确认请求发送失??", kNoticeFailColor, 1.5f);
            return false;
        }
    }
    SetNotice("再次按 X 确认拆除 " + name + "（3 秒内）",
              kNoticeInfoColor, 3.0f);
    return false;
}

std::uint64_t WorldDemoState::LocalPlayerStableId() const {
    if (m_networkMode && m_ctx.networkClient != nullptr) {
        return m_ctx.networkClient->PlayerStableId();
    }
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        return m_ctx.ecs->IdOf(m_player).id;
    }
    return 0;
}

std::uint64_t WorldDemoState::EntityOwnerStableId(ecs::Entity entity) const {
    if (m_ctx.ecs == nullptr || entity == entt::null) {
        return 0;
    }
    const auto* owner =
        m_ctx.ecs->Registry().try_get<ecs::StructureOwner>(entity);
    return owner != nullptr ? owner->ownerStableId : 0;
}

bool WorldDemoState::CanEditLocalStructure(
    std::uint64_t ownerStableId) const {
    return CanEditStructure(ownerStableId, LocalPlayerStableId());
}

std::string WorldDemoState::StructureOwnerLabel(
    std::uint64_t ownerStableId) const {
    if (ownerStableId == 0) {
        return "无主";
    }
    if (ownerStableId == LocalPlayerStableId()) {
        return "??";
    }
    if (m_ctx.networkClient != nullptr &&
        m_ctx.networkClient->HasSocialState()) {
        for (const net::SocialPlayer& player :
             m_ctx.networkClient->SocialState().players) {
            if (player.stableId == ownerStableId && !player.name.empty()) {
                return player.name;
            }
        }
    }
    return "玩家 " + std::to_string(ownerStableId);
}

std::string WorldDemoState::StructureOwnerSuffix(
    std::uint64_t ownerStableId) const {
    if (ownerStableId == 0 && !m_networkMode) {
        return {};
    }
    return " · 归属??" + StructureOwnerLabel(ownerStableId);
}

std::string WorldDemoState::HomesteadHint() const {
    if (m_dialogueOpen) {
        return {};
    }
    if (m_tradeOpen) {
        return TradePanelText();
    }
    if (m_buildMode) {
        return {};
    }
    const std::string npcHint = SettlementNpcHint();
    if (!npcHint.empty()) {
        return npcHint;
    }
    const HomesteadTarget target = FindNearestHomesteadTarget();
    if (target.kind == HomesteadTargetKind::Merchant) {
        return "按 E 交易：幸存者商人（可换药品/图纸/弹药/枪械）";
    }
    if (target.kind == HomesteadTargetKind::Farm) {
        if (m_ctx.ecs != nullptr) {
            if (const auto* plot =
                    m_ctx.ecs->Registry().try_get<ecs::FarmPlot>(target.entity);
                plot != nullptr) {
                const std::uint64_t owner =
                    EntityOwnerStableId(target.entity);
                const bool canEdit = CanEditLocalStructure(owner);
                const CropDef* crop =
                    m_ctx.crops != nullptr ? m_ctx.crops->Find(plot->cropId)
                                           : nullptr;
                std::string hint =
                    FarmStatusText(*plot, crop,
                                   FarmSeasonName(CurrentFarmSeason()));
                if (canEdit && plot->stage == ecs::CropStage::Empty) {
                    if (const CropDef* selected = SelectedFarmCrop();
                        selected != nullptr) {
                        hint += " · 当前种子：" + selected->name;
                    } else {
                        hint += " · 需要种??";
                    }
                }
                if (canEdit) {
                    hint += " · 种子/水瓶/肥料/杀菌剂";
                } else {
                    hint += " · 仅所有者可操作";
                }
                return hint + StructureOwnerSuffix(owner);
            }
        }
    }
    if (target.kind == HomesteadTargetKind::Generator) {
        if (m_ctx.ecs != nullptr) {
            if (const auto* generator =
                    m_ctx.ecs->Registry().try_get<ecs::Generator>(target.entity);
                generator != nullptr) {
                const std::uint64_t owner =
                    EntityOwnerStableId(target.entity);
                const bool canEdit = CanEditLocalStructure(owner);
                return GeneratorStatusText(*generator,
                                           m_homesteadConfig.power.baseLoad) +
                       (canEdit ? " · E 开??加燃??" : " · 仅所有者可操作") +
                       StructureOwnerSuffix(owner);
            }
        }
    }
    if (target.kind == HomesteadTargetKind::Floodlight) {
        if (m_ctx.ecs != nullptr) {
            if (const auto* floodlight =
                    m_ctx.ecs->Registry().try_get<ecs::Floodlight>(
                        target.entity);
                floodlight != nullptr) {
                const std::uint64_t owner =
                    EntityOwnerStableId(target.entity);
                const bool canEdit = CanEditLocalStructure(owner);
                const bool night =
                    m_ctx.gameTime != nullptr && m_ctx.gameTime->IsNight();
                const auto* health =
                    m_ctx.ecs->Registry().try_get<ecs::Health>(
                        target.entity);
                std::string text = FloodlightStatusText(
                    *floodlight, night,
                    m_homesteadConfig.power.deviceRequiresNight);
                if (health != nullptr) {
                    text += " · HP " +
                            std::to_string(static_cast<int>(
                                std::lround(health->current))) +
                            "/" +
                            std::to_string(static_cast<int>(
                                std::lround(health->max)));
                }
                text += " · 照明 " +
                        std::to_string(static_cast<int>(std::lround(
                            m_homesteadConfig.power.floodlightRadius))) +
                        " ??" +
                        (canEdit ? " · E 开关" : " · 仅所有者可操作");
                if (health != nullptr &&
                    health->current < health->max - 0.01f) {
                    text += canEdit ? " · H 维修" : " · 仅所有者可维修";
                }
                return text + StructureOwnerSuffix(owner);
            }
        }
    }
    if (target.kind == HomesteadTargetKind::Trap) {
        if (m_ctx.ecs != nullptr) {
            if (const auto* trap =
                    m_ctx.ecs->Registry().try_get<ecs::SpikeTrap>(target.entity);
                trap != nullptr) {
                const std::uint64_t owner =
                    EntityOwnerStableId(target.entity);
                const bool canEdit = CanEditLocalStructure(owner);
                return "陷阱：剩余 " + std::to_string(trap->uses) + " 次 · " +
                       (trap->armed ? "已布置" : "已解除") +
                       (canEdit ? " · E 切换" : " · 仅所有者可操作") +
                       StructureOwnerSuffix(owner);
            }
        }
    }
    if (m_ctx.ecs != nullptr) {
        PlayerBuildTarget fence;
        if (FindNearestPlayerBuiltStructure(m_buildActionRange, false, false,
                                            &fence) ||
            FindNearestPlayerBuiltStructure(m_buildActionRange, false, false,
                                            &fence, {}, true)) {
            if (fence.def != nullptr && fence.def->id == "electric_fence") {
                const bool powered = m_poweredFenceCells.contains(
                    BuildCellKey(fence.wx, fence.wy));
                const std::uint64_t owner = fence.ownerStableId;
                const bool canEdit = CanEditLocalStructure(owner);
                std::string text =
                    "电网??" + std::string(powered ? "供电??" : "断电") +
                    " · HP " + std::to_string(static_cast<int>(
                                      std::lround(fence.hp))) +
                    "/" + std::to_string(static_cast<int>(
                                std::lround(fence.def->maxHp)));
                if (fence.hp < fence.def->maxHp - 0.01f) {
                    text += canEdit ? " · H 维修" : " · 仅所有者可维修";
                }
                text += StructureOwnerSuffix(owner);
                return text;
            }
        }
    }
    return {};
}

std::string WorldDemoState::TradePanelText() const {
    if (!m_tradeOpen || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return {};
    }
    const ecs::Entity merchant = FindNearbyMerchant();
    if (merchant == entt::null) {
        return {};
    }
    const auto* stock = m_ctx.ecs->Registry().try_get<ecs::MerchantStock>(merchant);
    const auto* container = m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (stock == nullptr || container == nullptr) {
        return {};
    }
    const auto& offers = MerchantOffers();
    const int index =
        std::clamp(m_tradeIndex, 0, static_cast<int>(kMerchantOfferCount) - 1);
    const TradeOffer& offer = offers[static_cast<std::size_t>(index)];
    const auto itemName = [this](std::string_view defId) {
        const items::ItemDef* def = m_ctx.items->Find(std::string(defId));
        return def != nullptr ? def->name : std::string(defId);
    };
    std::ostringstream text;
    text << "商人 · " << itemName(offer.costDefId) << " "
         << items::CountOf(*container, std::string(offer.costDefId)) << " | ["
         << (index + 1) << "/" << kMerchantOfferCount << "] " << offer.name
         << " · 成本 " << offer.costCount << " "
         << itemName(offer.costDefId) << " · 库存 "
         << stock->stock[static_cast<std::size_t>(index)]
         << " | 滚轮选择 · 1-5 购买 · E 关闭";
    return text.str();
}

const BuildableDef* WorldDemoState::BuildableById(std::string_view id) const {
    for (const BuildableDef& def : m_buildables) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

bool WorldDemoState::FindNearbyBed(PlayerBuildTarget* out) const {
    return FindNearestPlayerBuiltStructure(m_buildActionRange, false, false, out,
                                           "bed");
}

bool WorldDemoState::FindNearbyCampfire(int* wx, int* wy) const {
    if (m_ctx.world == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null || m_stationRange <= 0.0f) {
        return false;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }
    return game::FindCampfireNearby(*m_ctx.world, position->x, position->y,
                                    position->z, m_stationRange, wx, wy);
}

bool WorldDemoState::FindNearbyActiveFire(FireTarget* out) const {
    if (m_ctx.simulation == nullptr || m_ctx.ecs == nullptr ||
        m_player == entt::null) {
        return false;
    }
    game::FireSystem* fire = m_ctx.simulation->Fire();
    if (fire == nullptr) {
        return false;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }
    return fire->FindNearestActiveFire(*m_ctx.ecs, position->x, position->y,
                                       position->z, fire->Params().extinguishRange,
                                       out);
}

bool WorldDemoState::TryExtinguishNearbyFire(const FireTarget& target) {
    if (m_networkMode || m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_ctx.simulation == nullptr || m_player == entt::null) {
        return false;
    }
    game::FireSystem* fire = m_ctx.simulation->Fire();
    auto* container = m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
    if (fire == nullptr || container == nullptr) {
        return false;
    }
    const game::FireParams& params = fire->Params();
    const game::FireExtinguishPlan plan =
        game::PlanFireExtinguish(*container, *m_ctx.items, params);
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    const bool success =
        game::FireExtinguishSucceeds(plan.chance, distribution(m_fireExtinguishRng));

    if (plan.usesItem) {
        (void)game::ConsumeFireExtinguishItem(*container, *m_ctx.items, plan);
    }
    if (!success && plan.bareHands && plan.selfDamage > 0.0f &&
        m_ctx.combat != nullptr) {
        ecs::DamageEvent event;
        event.type = ecs::DamageType::Fire;
        event.amount = plan.selfDamage;
        (void)m_ctx.combat->ApplyDamage(*m_ctx.ecs, m_player, event);
    }

    const std::string fireName =
        target.material != nullptr ? target.material->name : "火焰";
    const int chance = static_cast<int>(std::lround(
        std::clamp(plan.chance, 0.0f, 1.0f) * 100.0f));
    if (success) {
        (void)fire->ExtinguishAt(*m_ctx.ecs, target.x, target.y, target.z);
        std::string message =
            plan.bareHands ? "扑灭??" : "使用 " + plan.name + " 扑灭??";
        message += fireName;
        SetNotice(message, kNoticeOkColor, 2.0f);
        return true;
    }
    if (plan.bareHands) {
        SetNotice("徒手拍打失败，被火焰灼伤（成功率 " +
                      std::to_string(chance) + "%??",
                  kNoticeFailColor, 2.2f);
    } else {
        SetNotice("使用 " + plan.name + " 灭火失败（成功率 " +
                      std::to_string(chance) + "%??",
                  kNoticeFailColor, 2.2f);
    }
    return true;
}

bool WorldDemoState::TryToggleCampfireFire() {
    if (m_buildMode || m_ctx.simulation == nullptr || m_ctx.ecs == nullptr) {
        return false;
    }
    game::FireSystem* fire = m_ctx.simulation->Fire();
    if (fire == nullptr) {
        return false;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }

    if (m_networkMode) {
        FireTarget activeTarget;
        net::Command command;
        if (FindNearbyActiveFire(&activeTarget)) {
            command = MakePlayerCommand(net::CommandType::ExtinguishFire);
            command.payload.x = static_cast<float>(activeTarget.x);
            command.payload.y = static_cast<float>(activeTarget.y);
            command.payload.targetIndex = activeTarget.z;
        } else {
            int wx = 0;
            int wy = 0;
            if (!FindNearbyCampfire(&wx, &wy)) {
                return false;
            }
            command = MakePlayerCommand(net::CommandType::IgniteFire);
            command.payload.x = static_cast<float>(wx);
            command.payload.y = static_cast<float>(wy);
            command.payload.targetIndex = position->z;
        }
        if (!SubmitPlayerCommand(command)) {
            SetNotice("火焰操作发送失败", kNoticeFailColor, 1.8f);
        }
        return true;
    }

    FireTarget activeTarget;
    if (FindNearbyActiveFire(&activeTarget)) {
        return TryExtinguishNearbyFire(activeTarget);
    }
    int wx = 0;
    int wy = 0;
    if (!FindNearbyCampfire(&wx, &wy)) {
        return false;
    }
    std::string error;
    const game::WeatherState weather = m_ctx.simulation->CurrentWeather();
    const game::FireIgnitionResult result = fire->IgniteAt(
        *m_ctx.ecs, *m_ctx.world, wx, wy, position->z,
        weather.wetnessIntensity, &error);
    if (result == game::FireIgnitionResult::Ignited) {
        SetNotice("篝火点燃中", kNoticeOkColor, 1.8f);
    } else if (result == game::FireIgnitionResult::AlreadyActive) {
        SetNotice("篝火仍在燃烧", kNoticeInfoColor, 1.5f);
    } else {
        SetNotice("无法点燃篝火：" + error, kNoticeFailColor, 2.5f);
    }
    return true;
}

bool WorldDemoState::ZombieNearPlayer(float radius) const {
    if (m_ctx.ecs == nullptr || m_player == entt::null || radius <= 0.0f) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* playerPosition = registry.try_get<ecs::Position>(m_player);
    if (playerPosition == nullptr) {
        return false;
    }
    const float radiusSq = radius * radius;
    bool zombieNear = false;
    registry.view<ecs::Position, ecs::Health, ecs::AiState>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::Health& health, const ecs::AiState& ai) {
            if (zombieNear || entity == m_player || health.current <= 0.0f ||
                ai.status == ecs::AiStatus::Dead ||
                registry.any_of<ecs::Dying>(entity) || position.z != playerPosition->z) {
                return;
            }
            const float dx = position.x - playerPosition->x;
            const float dy = position.y - playerPosition->y;
            zombieNear = dx * dx + dy * dy <= radiusSq;
        });
    return zombieNear;
}

std::string WorldDemoState::SleepClockText() const {
    if (m_ctx.gameTime == nullptr) {
        return "--:--";
    }
    const core::DateTime& now = m_ctx.gameTime->Now();
    const int day =
        1 + static_cast<int>(m_ctx.gameTime->TotalGameMinutes() / (24.0 * 60.0));
    char text[32];
    std::snprintf(text, sizeof(text), "第 %d 天 %02d:%02d", day, now.hour, now.minute);
    return text;
}

std::string WorldDemoState::SleepStatusText() const {
    if (m_sleepSelecting) {
        const std::string duration =
            m_sleepSelectUntilDawn ? "睡到天亮" : std::to_string(m_sleepSelectedHours) + " 小时";
        const std::string hoursRange =
            std::to_string(m_sleepConfig.minHours) + "~" +
            std::to_string(m_sleepConfig.maxHours);
        return "睡眠：" + duration + " ｜ 滚轮选择 " + hoursRange +
               " 小时/天亮 ??E 开????ESC 取消";
    }
    if (!m_sleep.Active()) {
        return {};
    }
    const double elapsedHours = m_sleep.ElapsedMinutes() / 60.0;
    const double totalHours = m_sleep.DurationMinutes() / 60.0;
    char text[160];
    float fatigue = -1.0f;
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        if (const auto* needs =
                m_ctx.ecs->Registry().try_get<ecs::Needs>(m_player);
            needs != nullptr) {
            fatigue = needs->fatigue;
        }
    }
    if (fatigue >= 0.0f) {
        std::snprintf(text, sizeof(text),
                      "睡眠??%s ??%.1f/%.1f 小时 ??疲劳 %d%% ??ESC 取消",
                      SleepClockText().c_str(), elapsedHours, totalHours,
                      static_cast<int>(std::lround(
                          std::clamp(fatigue, 0.0f, 1.0f) * 100.0f)));
    } else {
        std::snprintf(text, sizeof(text), "睡眠??%s ??%.1f/%.1f 小时 ??ESC 取消",
                      SleepClockText().c_str(), elapsedHours, totalHours);
    }
    return text;
}

void WorldDemoState::HaltPlayerForSleep() {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (auto* move = registry.try_get<ecs::MoveInput>(m_player); move != nullptr) {
        move->x = 0.0f;
        move->y = 0.0f;
    }
    if (auto* intent = registry.try_get<ecs::MoveIntent>(m_player); intent != nullptr) {
        intent->x = 0.0f;
        intent->y = 0.0f;
    }
    if (auto* velocity = registry.try_get<ecs::Velocity>(m_player); velocity != nullptr) {
        velocity->x = 0.0f;
        velocity->y = 0.0f;
    }
    if (auto* request = registry.try_get<ecs::PathRequest>(m_player); request != nullptr) {
        request->newRequest = false;
        request->queryId = 0;
        request->state = ecs::PathStatus::None;
    }
    if (auto* follow = registry.try_get<ecs::PathFollow>(m_player); follow != nullptr) {
        follow->waypoints.clear();
        follow->index = 0;
        follow->status = ecs::PathStatus::None;
    }
    if (auto* attack = registry.try_get<ecs::AttackIntent>(m_player); attack != nullptr) {
        attack->pending = false;
    }
    m_blockInputDown = false;
    if (auto* stance = registry.try_get<ecs::CombatStance>(m_player);
        stance != nullptr) {
        stance->blocking = false;
        stance->blockElapsed = 0.0f;
        stance->dodgeLeft = 0.0f;
        stance->dodgeInvulnLeft = 0.0f;
        stance->dodgeDirX = 0.0f;
        stance->dodgeDirY = 0.0f;
    }
}

bool WorldDemoState::TryBeginSleep() {
    if (!m_sleepEnabled || m_sleep.Active() || m_sleepSelecting ||
        m_ctx.gameTime == nullptr || m_buildMode) {
        return false;
    }
    if (m_networkMode && m_networkSleep.phase != 0) {
        SetNotice("已有睡眠投票或睡眠进行中", kNoticeInfoColor, 1.8f);
        return true;
    }
    PlayerBuildTarget bed;
    if (!FindNearbyBed(&bed)) {
        return false;
    }
    m_sleepBedName = bed.def->name;
    if (m_baseSiege.Active()) {
        SetNotice("守夜事件进行中，无法入睡", kNoticeFailColor, 2.0f);
        return true;
    }
    if (m_crafting.active) {
        SetNotice("制作进行中，先完成或取消制作", kNoticeFailColor, 2.0f);
        return true;
    }
    if (m_ctx.gameTime->GetSpeed() == core::GameTime::Speed::Paused) {
        SetNotice("时间已暂停，先恢复时间再睡觉", kNoticeFailColor, 2.0f);
        return true;
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return true;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* health = registry.try_get<ecs::Health>(m_player);
    if (health == nullptr || health->current <= 0.0f) {
        SetNotice("当前无法入睡", kNoticeFailColor, 2.0f);
        return true;
    }
    if (m_sleepConfig.interruptOnZombie &&
        ZombieNearPlayer(m_sleepConfig.zombieRadius)) {
        SetNotice("附近有僵尸，无法入睡", kNoticeFailColor, 2.0f);
        return true;
    }
    m_sleepSelectedHours =
        std::clamp(m_sleepSelectedHours, m_sleepConfig.minHours, m_sleepConfig.maxHours);
    m_sleepSelectUntilDawn = false;
    m_sleepSelecting = true;
    m_scrollAccum = 0.0f;
    HaltPlayerForSleep();
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->CloseInventory();
    }
    SetNotice("睡眠：滚轮选择 " + std::to_string(m_sleepConfig.minHours) + "~" +
                  std::to_string(m_sleepConfig.maxHours) +
                  " 小时或天亮，E 开始，ESC 取消",
              kNoticeInfoColor, 2.5f);
    ZL_INFO("sleep", "打开睡眠选择：{}（{}）", m_sleepBedName, SleepStatusText());
    return true;
}

void WorldDemoState::ConfirmSleep() {
    if (!m_sleepSelecting) {
        return;
    }
    if (!m_sleepEnabled || m_ctx.gameTime == nullptr) {
        m_sleepSelecting = false;
        SetNotice("当前模式不支持睡眠", kNoticeFailColor, 2.0f);
        return;
    }
    PlayerBuildTarget bed;
    if (!FindNearbyBed(&bed)) {
        m_sleepSelecting = false;
        SetNotice("已经离开床边，取消睡眠", kNoticeFailColor, 2.0f);
        return;
    }
    if (m_baseSiege.Active()) {
        m_sleepSelecting = false;
        SetNotice("守夜事件进行中，无法入睡", kNoticeFailColor, 2.0f);
        return;
    }
    if (m_crafting.active) {
        m_sleepSelecting = false;
        SetNotice("制作进行中，先完成或取消制作", kNoticeFailColor, 2.0f);
        return;
    }
    if (m_sleepConfig.interruptOnZombie &&
        ZombieNearPlayer(m_sleepConfig.zombieRadius)) {
        m_sleepSelecting = false;
        SetNotice("附近有僵尸，无法入睡", kNoticeFailColor, 2.0f);
        return;
    }
    const double duration =
        m_sleepSelectUntilDawn
            ? m_ctx.gameTime->MinutesUntilHour(m_sleepConfig.dawnHour)
            : static_cast<double>(m_sleepSelectedHours) * 60.0;
    if (!(duration > 0.0)) {
        m_sleepSelecting = false;
        SetNotice("当前无法计算睡眠时长", kNoticeFailColor, 2.0f);
        return;
    }
    m_sleepBedName = bed.def->name;
    if (m_networkMode) {
        const bool untilDawn = m_sleepSelectUntilDawn;
        m_sleepSelecting = false;
        m_sleepSelectUntilDawn = false;
        if (m_ctx.ui != nullptr) {
            m_ctx.ui->CloseInventory();
        }
        SubmitNetworkSleepCommand(
            net::CommandType::SleepRequest,
            untilDawn ? net::kSleepUntilDawnFlag : 0,
            static_cast<float>(duration));
        SetNotice("已发起睡眠投票，等待队友响应", kNoticeInfoColor, 2.0f);
        return;
    }
    StartSleep(static_cast<float>(duration), m_sleepSelectUntilDawn);
}

void WorldDemoState::StartSleep(float durationMinutes, bool untilDawn) {
    if (m_ctx.gameTime == nullptr || m_ctx.ecs == nullptr || m_player == entt::null ||
        !(durationMinutes > 0.0f)) {
        m_sleepSelecting = false;
        return;
    }
    m_sleepSpeedBefore = m_ctx.gameTime->GetSpeed();
    if (m_sleepSpeedBefore == core::GameTime::Speed::Paused) {
        m_sleepSpeedBefore = core::GameTime::Speed::X1;
    }
    m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
    const double startMinutes = m_ctx.gameTime->TotalGameMinutes();
    m_sleep.Start(startMinutes, durationMinutes);
    m_sleepSelecting = false;
    m_sleepSelectUntilDawn = untilDawn;
    m_buildMode = false;
    m_scrollAccum = 0.0f;
    if (const auto* health =
            m_ctx.ecs->Registry().try_get<ecs::Health>(m_player);
        health != nullptr) {
        m_sleepLastHealth = health->current;
    } else {
        m_sleepLastHealth = 0.0f;
    }
    HaltPlayerForSleep();
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->CloseInventory();
    }
    char hoursText[32];
    std::snprintf(hoursText, sizeof(hoursText), "%.1f", durationMinutes / 60.0f);
    const std::string text =
        untilDawn ? "入睡，睡到天亮" : "入睡，" + std::string(hoursText) + " 小时后醒来";
    SetNotice(text, kNoticeInfoColor, 2.5f);
    ZL_INFO("sleep", "入睡：{}，时长 {:.1f} 小时，目标 {}，速度恢复 {}", m_sleepBedName,
            durationMinutes / 60.0f, SleepClockText(),
            core::GameTime::SpeedName(m_sleepSpeedBefore));
}

void WorldDemoState::FinishSleep(SleepStopReason reason, float damage) {
    if (!(m_sleep.DurationMinutes() > 0.0)) {
        m_sleepSelecting = false;
        return;
    }
    if (reason == SleepStopReason::Completed && m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetTotalGameMinutes(m_sleep.TargetMinutes());
    }
    const double elapsed = m_sleep.ElapsedMinutes();
    const double duration = m_sleep.DurationMinutes();
    m_sleep.Stop();
    m_sleepSelecting = false;
    m_sleepSelectUntilDawn = false;
    if (m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(m_sleepSpeedBefore);
    }
    HaltPlayerForSleep();
    float fatigue = -1.0f;
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        if (const auto* needs =
                m_ctx.ecs->Registry().try_get<ecs::Needs>(m_player);
            needs != nullptr) {
            fatigue = needs->fatigue;
        }
    }
    std::string text = reason == SleepStopReason::Completed ? "睡眠结束：" : "睡眠中断：";
    text += SleepClockText();
    char hoursText[48];
    std::snprintf(hoursText, sizeof(hoursText), "，已??%.1f/%.1f 小时",
                  elapsed / 60.0, duration / 60.0);
    text += hoursText;
    if (fatigue >= 0.0f) {
        text += "，疲??" +
                std::to_string(static_cast<int>(
                    std::lround(std::clamp(fatigue, 0.0f, 1.0f) * 100.0f))) +
                "%";
    }
    if (reason != SleepStopReason::Completed) {
        text += "??" + std::string(SleepStopText(reason)) + "??";
    }
    if (damage > 0.01f) {
        text += "，失??" +
                std::to_string(static_cast<int>(std::lround(damage))) + " HP";
    }
    const uint32_t color =
        reason == SleepStopReason::Completed
            ? kNoticeOkColor
            : reason == SleepStopReason::Cancelled ? kNoticeInfoColor : kNoticeFailColor;
    SetNotice(std::move(text), color, 4.0f);
    ZL_INFO("sleep", "睡眠结束：{}（{}，{:.1f}/{:.1f} 小时，疲劳 {}，伤害 {:.1f}）",
            SleepStopText(reason), SleepClockText(), elapsed / 60.0, duration / 60.0,
            fatigue, damage);
}

void WorldDemoState::UpdateSleep(float tickDt) {
    if (!m_sleep.Active()) {
        return;
    }
    if (m_ctx.gameTime == nullptr || m_ctx.ecs == nullptr || m_player == entt::null ||
        !m_ctx.ecs->Alive(m_player)) {
        FinishSleep(SleepStopReason::Died);
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* health = registry.try_get<ecs::Health>(m_player);
    if (health == nullptr || health->current <= 0.0f) {
        FinishSleep(SleepStopReason::Died);
        return;
    }
    if (m_sleepConfig.interruptOnDamage &&
        health->current + 0.01f < m_sleepLastHealth) {
        const float damage = m_sleepLastHealth - health->current;
        m_sleepLastHealth = health->current;
        FinishSleep(SleepStopReason::Damaged, damage);
        return;
    }
    m_sleepLastHealth = health->current;
    if (m_sleepConfig.interruptOnZombie &&
        ZombieNearPlayer(m_sleepConfig.zombieRadius)) {
        FinishSleep(SleepStopReason::ZombieNear);
        return;
    }
    HaltPlayerForSleep();
    const double currentMinutes = m_ctx.gameTime->TotalGameMinutes();
    const double remainingMinutes = m_sleep.TargetMinutes() - currentMinutes;
    if (remainingMinutes <= 1e-6) {
        FinishSleep(SleepStopReason::Completed);
        return;
    }
    const double step = std::min(
        static_cast<double>(m_sleepConfig.gameMinutesPerSecond) *
            static_cast<double>(std::max(0.0f, tickDt)),
        remainingMinutes);
    if (!(step > 0.0)) {
        return;
    }
    m_ctx.gameTime->AdvanceGameMinutes(step);
    if (auto* needs = registry.try_get<ecs::Needs>(m_player); needs != nullptr) {
        ApplySleepNeeds(*needs, m_sleepHungerPerDay, m_sleepThirstPerDay,
                        static_cast<float>(step), m_sleepConfig.fatigueRecoveryPerHour);
        if (needs->hunger >= 0.95f || needs->thirst >= 0.95f) {
            m_sleep.Advance(step);
            FinishSleep(SleepStopReason::NeedsCritical);
            return;
        }
    }
    m_sleep.Advance(step);
    if (!m_sleep.Active()) {
        FinishSleep(SleepStopReason::Completed);
    }
}

int WorldDemoState::CountPlayerStructuresNear(float x, float y, float radius,
                                              bool barricadesOnly) const {
    if (m_ctx.ecs == nullptr || m_ctx.world == nullptr || radius <= 0.0f) {
        return 0;
    }
    const int minX = static_cast<int>(std::floor(x - radius));
    const int maxX = static_cast<int>(std::ceil(x + radius));
    const int minY = static_cast<int>(std::floor(y - radius));
    const int maxY = static_cast<int>(std::ceil(y + radius));
    const float radiusSq = radius * radius;
    int count = 0;
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx = (static_cast<float>(wx) + 0.5f) - x;
            const float dy = (static_cast<float>(wy) + 0.5f) - y;
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            constexpr std::array<world::SquareLayer, 2> kLayers{
                world::SquareLayer::Wall, world::SquareLayer::Furniture};
            for (const world::SquareLayer layer : kLayers) {
                const auto& objects =
                    chunk->Square(local.lx, local.ly, 0).Layer(layer);
                for (const world::SquareObject& object : objects) {
                    if (!object.playerBuilt) {
                        continue;
                    }
                    const BuildableDef* def = BuildableForSprite(object.sprite);
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

std::vector<BaseSiegeStructure> WorldDemoState::ListPlayerStructuresNear(
    float x, float y, float radius) const {
    std::vector<BaseSiegeStructure> result;
    if (m_ctx.world == nullptr || radius <= 0.0f) {
        return result;
    }
    const int minX = static_cast<int>(std::floor(x - radius));
    const int maxX = static_cast<int>(std::ceil(x + radius));
    const int minY = static_cast<int>(std::floor(y - radius));
    const int maxY = static_cast<int>(std::ceil(y + radius));
    const float radiusSq = radius * radius;
    constexpr std::array<world::SquareLayer, 2> kLayers{
        world::SquareLayer::Wall, world::SquareLayer::Furniture};
    for (int wy = minY; wy <= maxY; ++wy) {
        for (int wx = minX; wx <= maxX; ++wx) {
            const float dx = (static_cast<float>(wx) + 0.5f) - x;
            const float dy = (static_cast<float>(wy) + 0.5f) - y;
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            const world::ChunkCoord coord = world::ChunkOf(wx, wy);
            const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
            if (chunk == nullptr ||
                chunk->State() != world::ChunkState::Loaded) {
                continue;
            }
            const world::SquareLocal local = world::SquareLocalOf(wx, wy);
            for (const world::SquareLayer layer : kLayers) {
                const auto& objects =
                    chunk->Square(local.lx, local.ly, 0).Layer(layer);
                for (const world::SquareObject& object : objects) {
                    if (!object.playerBuilt || !object.blocksMovement) {
                        continue;
                    }
                    const BuildableDef* def = BuildableForSprite(object.sprite);
                    if (def == nullptr || def->maxHp <= 0.0f) {
                        continue;
                    }
                    const float hp = ResolveBarricadeHp(object, def->maxHp);
                    if (hp <= 0.0f) {
                        continue;
                    }
                    BaseSiegeStructure structure;
                    structure.id = object.structureId != 0
                                       ? object.structureId
                                       : MakeStructureId(
                                             wx, wy, 0,
                                             static_cast<int>(layer));
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

bool WorldDemoState::IsBaseSiegeSpawnCell(int wx, int wy) const {
    if (m_ctx.world == nullptr) {
        return false;
    }
    const world::ChunkCoord coord = world::ChunkOf(wx, wy);
    const world::Chunk* chunk = FindChunk(coord.cx, coord.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return false;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    if (!chunk->Square(local.lx, local.ly, 0).HasFloor()) {
        return false;
    }
    return !IsSquareBlocked(*m_ctx.world, wx, wy, 0);
}

bool WorldDemoState::BaseSiegeApproachPoint(const BaseSiegeAttackPlan& plan,
                                            float* x, float* y) const {
    const BaseSiegePoint& center = m_baseSiege.Center();
    return game::FindBaseSiegeApproachPoint(
        plan, center.x, center.y,
        [this](int wx, int wy) { return IsBaseSiegeSpawnCell(wx, wy); }, x,
        y);
}

std::string WorldDemoState::GrantBaseSiegeReward(
    const BaseSiegeResult& result) {
    if (m_networkMode || m_ctx.items == nullptr) {
        return {};
    }
    const std::vector<game::BaseSiegeReward> rewards =
        game::BaseSiegeRewardsFor(result.rating, m_buildWoodItem);
    if (rewards.empty()) {
        return {};
    }
    std::string text;
    bool rewardDropped = false;
    for (const auto& [defId, count] : rewards) {
        const items::ItemDef* def = m_ctx.items->Find(defId);
        if (def == nullptr) {
            continue;
        }
        bool dropped = false;
        ReturnOrDropItem(ecs::ItemInstance{defId, count, 1.0f}, &dropped);
        rewardDropped = rewardDropped || dropped;
        if (!text.empty()) {
            text += "??";
        }
        text += def->name + "×" + std::to_string(count);
    }
    if (rewardDropped) {
        text += "（背包已满，奖励掉落脚下??";
    }
    ZL_INFO("siege", "守夜评级 {}，奖励：{}", result.rating,
            text.empty() ? "??" : text);
    return text;
}

std::uint64_t WorldDemoState::SpawnBaseSiegeZombie(
    int wx, int wy, ecs::ZombieSpecialKind special) {
    if (m_ctx.simulation == nullptr || m_ctx.ecs == nullptr) {
        return 0;
    }
    const float px = static_cast<float>(wx) + 0.5f;
    const float py = static_cast<float>(wy) + 0.5f;
    ecs::Entity zombie = ecs::EcsWorld::kNull;
    if (special == ecs::ZombieSpecialKind::Howler) {
        zombie = m_ctx.simulation->SpawnZombieAt(
            px, py, ecs::ZombieVariantKind::Fast);
    } else if (special == ecs::ZombieSpecialKind::Breaker) {
        zombie = m_ctx.simulation->SpawnZombieAt(
            px, py, ecs::ZombieVariantKind::Strong);
    } else {
        zombie = m_ctx.simulation->SpawnZombieAt(px, py);
    }
    if (zombie == ecs::EcsWorld::kNull) {
        return 0;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    if (!registry.valid(zombie)) {
        return 0;
    }
    registry.emplace_or_replace<ecs::BaseSiegeZombie>(zombie);
    if (special != ecs::ZombieSpecialKind::None) {
        ecs::ZombieSpecial value;
        value.kind = special;
        registry.emplace_or_replace<ecs::ZombieSpecial>(zombie, value);
    }
    const auto* playerPos =
        m_player != entt::null
            ? registry.try_get<ecs::Position>(m_player)
            : nullptr;
    const std::uint64_t playerId =
        m_player != entt::null ? m_ctx.ecs->IdOf(m_player).id : 0;
    if (auto* ai = registry.try_get<ecs::AiState>(zombie); ai != nullptr) {
        ai->status = ecs::AiStatus::Chase;
        ai->stateTime = 0.0f;
        ai->actionTimer = 0.0f;
        ai->targetId = playerId;
    }
    if (auto* perception =
            registry.try_get<ecs::PerceptionState>(zombie);
        perception != nullptr) {
        if (playerPos != nullptr) {
            perception->lastKnownX = playerPos->x;
            perception->lastKnownY = playerPos->y;
        }
        perception->alertness = 1.0f;
        perception->sinceSeen = 0.0f;
        perception->sinceHeard = 0.0f;
    }
    const std::uint64_t stableId = m_ctx.ecs->IdOf(zombie).id;
    ZL_INFO("siege", "生成事件僵尸 stableId={} at ({}, {}) special={}", stableId,
            wx, wy, static_cast<int>(special));
    return stableId;
}

void WorldDemoState::DespawnBaseSiegeZombie(std::uint64_t stableId) {
    if (m_ctx.ecs == nullptr || m_ctx.simulation == nullptr) {
        return;
    }
    const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
    if (entity != ecs::EcsWorld::kNull) {
        m_ctx.simulation->DespawnEntity(entity);
    }
}

std::string WorldDemoState::SpecialWarningText() const {
    if (m_ctx.ecs == nullptr || m_baseSiege.EventZombies().empty()) {
        return {};
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    int howling = 0;
    int breaking = 0;
    for (const std::uint64_t stableId : m_baseSiege.EventZombies()) {
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        if (entity == ecs::EcsWorld::kNull) {
            continue;
        }
        const auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
        if (special == nullptr || special->windup <= 0.0f) {
            continue;
        }
        if (special->kind == ecs::ZombieSpecialKind::Howler) {
            ++howling;
        } else if (special->kind == ecs::ZombieSpecialKind::Breaker) {
            ++breaking;
        }
    }
    std::string text;
    if (howling > 0) {
        text += "嚎叫蓄力×" + std::to_string(howling);
    }
    if (breaking > 0) {
        if (!text.empty()) {
            text += " ";
        }
        text += "破门蓄力×" + std::to_string(breaking);
    }
    return text;
}

bool WorldDemoState::FindFloodlightLure(float x, float y, float* lureX,
                                        float* lureY) const {
    if (m_ctx.ecs == nullptr || lureX == nullptr || lureY == nullptr ||
        !m_homesteadConfig.power.enabled) {
        return false;
    }
    if (m_homesteadConfig.power.deviceRequiresNight &&
        (m_ctx.gameTime == nullptr || !m_ctx.gameTime->IsNight())) {
        return false;
    }
    const float radius = m_homesteadConfig.power.attractRadius;
    const float radiusSq = radius * radius;
    bool found = false;
    float bestDistance = radiusSq;
    m_ctx.ecs->Registry()
        .view<ecs::Position, ecs::Floodlight>()
        .each([&](const ecs::Entity entity, const ecs::Position& position,
                  const ecs::Floodlight& floodlight) {
            if (!floodlight.on || !floodlight.powered) {
                return;
            }
            if (const auto* health =
                    m_ctx.ecs->Registry().try_get<ecs::Health>(entity);
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
            *lureX = position.x;
            *lureY = position.y;
        });
    return found;
}

bool WorldDemoState::FindGeneratorNoise(float x, float y, float* lureX,
                                        float* lureY) const {
    if (m_ctx.ecs == nullptr || lureX == nullptr || lureY == nullptr ||
        !m_homesteadConfig.power.enabled) {
        return false;
    }
    const float radius = m_homesteadConfig.power.attractRadius;
    const float radiusSq = radius * radius;
    bool found = false;
    float bestDistance = radiusSq;
    m_ctx.ecs->Registry()
        .view<ecs::Position, ecs::Generator>()
        .each([&](const ecs::Entity, const ecs::Position& position,
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
            *lureX = position.x;
            *lureY = position.y;
        });
    return found;
}

void WorldDemoState::DriveBaseSiegeZombies() {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* playerPos = registry.try_get<ecs::Position>(m_player);
    if (playerPos == nullptr) {
        return;
    }
    const std::uint64_t playerId = m_ctx.ecs->IdOf(m_player).id;
    const BaseSiegeAttackPlan& plan = m_baseSiege.AttackPlan();
    const bool director = m_baseSiege.DirectorActive();
    float approachX = 0.0f;
    float approachY = 0.0f;
    const bool hasApproach =
        director && BaseSiegeApproachPoint(plan, &approachX, &approachY);

    const BaseSiegePoint& center = m_baseSiege.Center();
    bool hasRear = false;
    float rearX = 0.0f;
    float rearY = 0.0f;
    if (director && plan.currentDirection >= 0) {
        const float angle = BaseSiegeDirectionAngle(plan.currentDirection);
        constexpr float kRearDistance = 11.0f;
        rearX = center.x + std::cos(angle) * kRearDistance;
        rearY = center.y - std::sin(angle) * kRearDistance;
        hasRear = true;
    }

    for (const std::uint64_t stableId : m_baseSiege.EventZombies()) {
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        if (entity == ecs::EcsWorld::kNull || !m_ctx.ecs->Alive(entity)) {
            continue;
        }
        auto* ai = registry.try_get<ecs::AiState>(entity);
        if (ai == nullptr || ai->status == ecs::AiStatus::Dead) {
            continue;
        }
        auto* perception = registry.try_get<ecs::PerceptionState>(entity);
        const auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
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
        bool hasLure = false;
        if (!seenPlayer && position != nullptr) {
            hasLure = FindFloodlightLure(position->x, position->y, &lureX,
                                         &lureY);
            if (!hasLure) {
                hasLure = FindGeneratorNoise(position->x, position->y, &lureX,
                                              &lureY);
            }
        }
        if (hasLure) {
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
                perception->alertness = std::max(perception->alertness, 0.8f);
                perception->sinceHeard = 0.0f;
            }
            continue;
        }

        registry.remove<ecs::SiegeAssaultTarget>(entity);
        if (perception != nullptr) {
            perception->lastKnownX = playerPos->x;
            perception->lastKnownY = playerPos->y;
            perception->alertness = std::max(perception->alertness, 0.8f);
            perception->sinceHeard = 0.0f;
        }
        if (ai->status == ecs::AiStatus::Idle ||
            ai->status == ecs::AiStatus::Wander ||
            ai->status == ecs::AiStatus::SearchLastKnown ||
            (ai->status == ecs::AiStatus::Chase && ai->targetId == 0)) {
            ai->status = ecs::AiStatus::Chase;
            ai->targetId = playerId;
            ai->stateTime = 0.0f;
            ai->actionTimer = 0.0f;
        }
    }
}

void WorldDemoState::ShowBaseSiegeNotice(const std::string& text,
                                         const std::string& tone) {
    const uint32_t color = tone == "fail"  ? kNoticeFailColor
                           : tone == "ok"  ? kNoticeOkColor
                                           : kNoticeInfoColor;
    SetNotice(text, color, 3.0f);
    ZL_INFO("siege", "{}", text);
    if (text.rfind("守夜结束", 0) == 0) {
        OpenSiegeReport(text, tone);
    }
}

void WorldDemoState::OpenSiegeReport(const std::string& text,
                                     const std::string& tone) {
    if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
        m_ctx.ui->CloseInventory();
    }
    m_characterOpen = false;
    m_siegeReportTitle = "守夜战报";
    m_siegeReportHint = "ESC 关闭";
    m_siegeReportRating.clear();
    m_siegeReportLines.fill({});
    (void)ui::BuildSiegeReportLines(text, &m_siegeReportRating,
                                    &m_siegeReportLines);
    m_siegeReportTone = m_siegeReportRating.empty()
                            ? tone
                            : ui::SiegeReportToneForRating(
                                  m_siegeReportRating);
    m_siegeReportOpen = true;
    ZL_INFO("siege", "打开战报面板：评级 {}", m_siegeReportRating.empty()
                                                  ? "?"
                                                  : m_siegeReportRating);
}

void WorldDemoState::AppendBuildGhost() {
    if (!m_buildMode || m_runOutcome != RunOutcome::None) {
        return;
    }
    const BuildableDef* def = SelectedBuildable();
    if (def == nullptr || m_ctx.world == nullptr) {
        return;
    }
    const world::ChunkCoord coord = world::ChunkOf(m_hoverX, m_hoverY);
    const world::Chunk* hoverChunk = FindChunk(coord.cx, coord.cy);
    if (hoverChunk == nullptr || hoverChunk->State() != world::ChunkState::Loaded) {
        return;
    }
    std::string reason;
    const bool valid = CanPlaceBuild(*def, m_hoverX, m_hoverY, &reason);
    const std::size_t before = m_items.size();
    PushObject(MakeBuiltObject(*def, m_buildOrientationVertical), m_hoverX,
               m_hoverY, 0, def->layer,
               RoomIdAt(m_hoverX, m_hoverY, 0));
    if (m_items.size() > before) {
        m_items.back().color = valid ? 0x9000ff00 : 0x900000ff;
        if (def->layer == world::SquareLayer::Floor) {
            m_items.back().stage = 1;
        }
    }
}

void WorldDemoState::FinishRun(bool victory) {
    if (m_runOutcome != RunOutcome::None) {
        return;
    }
    m_baseSiege.Abort({}, false);
    m_runOutcome = victory ? RunOutcome::Escaped : RunOutcome::Dead;
    m_buildMode = false;
    m_tradeOpen = false;
    CloseSettlementDialogue();
    // 结算后不再自动存档，避免把已结束/已死亡的一局写成可继续的快速档。
    m_autoSaveOnExit = false;
    if (!m_networkMode && m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::Paused);
    }
    const std::string title = victory ? "成功撤离" : "你已死亡";
    const std::string subtitle = victory ? "撤离成功" : "你已死亡";
    if (m_ctx.ui != nullptr) {
        m_ctx.ui->CloseInventory();
        m_ctx.ui->ShowEndScreen(victory, title, subtitle, BuildRunStats(),
                                !m_networkMode);
    }
    int day = 1;
    int hour = 0;
    int minute = 0;
    if (m_ctx.gameTime != nullptr) {
        day = 1 + static_cast<int>(m_ctx.gameTime->TotalGameMinutes() / (24.0 * 60.0));
        hour = m_ctx.gameTime->Now().hour;
        minute = m_ctx.gameTime->Now().minute;
    }
    const char* mode = m_networkMode ? "联机局" : "单机局";
    if (victory) {
        ZL_INFO("game", "{}结束：撤离成功（第 {} 天 {:02d}:{:02d}）", mode, day,
                hour, minute);
    } else {
        ZL_INFO("game", "{}结束：死亡（死因：{}；第 {} ??{:02d}:{:02d}??", mode,
                game::DeathCauseLabel(m_runDeathCause), day, hour, minute);
    }
}

void WorldDemoState::UpdateObjective() {
    if (m_networkMode || m_runOutcome != RunOutcome::None || m_ctx.ecs == nullptr ||
        m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* objective = registry.try_get<ecs::Objective>(m_player);
    if (objective == nullptr) {
        objective = &registry.emplace<ecs::Objective>(m_player);
    }
    const auto* position = registry.try_get<ecs::Position>(m_player);
    const bool hasPart = PlayerHasRadioPart();
    const auto before = objective->step;
    const bool atExtraction =
        position != nullptr &&
        (position->x - m_extractionX) * (position->x - m_extractionX) +
                (position->y - m_extractionY) * (position->y - m_extractionY) <=
            m_extractionRadius * m_extractionRadius;
    objective->step = game::AdvanceObjective(objective->step, hasPart, atExtraction);

    if (objective->step == ecs::ObjectiveStep::RepairRadio && !hasPart &&
        !RadioPartExistsInWorld()) {
        if (game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items,
                                  ecs::ItemInstance{m_radioPartDefId, 1, 1.0f},
                                  m_radioX + 0.6f, m_radioY + 0.6f, 0) != entt::null) {
            SetNotice("无线电零件掉落在电台旁", kNoticeInfoColor, 3.0f);
            ZL_INFO("objective", "玩家失去无线电零件，已在电台旁补充");
        }
    }

    if (objective->step != before) {
        SetNotice(game::ObjectiveText(objective->step, hasPart, m_extractionX,
                                      m_extractionY),
                  objective->step == ecs::ObjectiveStep::Complete ? kNoticeOkColor
                                                                  : kNoticeInfoColor,
                  objective->step == ecs::ObjectiveStep::Complete ? 5.0f : 3.0f);
        ZL_INFO("objective", "目标推进：{}", game::ObjectiveStepName(objective->step));
    }
    if (objective->step == ecs::ObjectiveStep::Complete &&
        before != ecs::ObjectiveStep::Complete) {
        FinishRun(true);
    }
}

bool WorldDemoState::TryInteractRadio() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr ||
        m_player == entt::null) {
        return false;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return false;
    }
    const float dx = position->x - m_radioX;
    const float dy = position->y - m_radioY;
    if (dx * dx + dy * dy > m_objectiveInteractRange * m_objectiveInteractRange) {
        return false;
    }

    if (m_networkMode) {
        net::Command command =
            MakePlayerCommand(net::CommandType::InteractRadio);
        if (SubmitPlayerCommand(command)) {
            SetNotice("已发送电台交互请求", kNoticeInfoColor, 1.5f);
        } else {
            SetNotice("电台交互请求发送失败", kNoticeFailColor, 1.5f);
        }
        return true;
    }

    auto* objective = registry.try_get<ecs::Objective>(m_player);
    if (objective == nullptr) {
        objective = &registry.emplace<ecs::Objective>(m_player);
    }
    if (objective->step == ecs::ObjectiveStep::FindRadioPart &&
        PlayerHasRadioPart()) {
        objective->step = ecs::ObjectiveStep::RepairRadio;
    }
    if (objective->step == ecs::ObjectiveStep::FindRadioPart) {
        SetNotice("电台缺少无线电零件，先搜刮屋内工具柜", kNoticeFailColor, 2.5f);
        return true;
    }
    if (objective->step == ecs::ObjectiveStep::RepairRadio) {
        if (!PlayerHasRadioPart()) {
            SetNotice("缺少无线电零件", kNoticeFailColor, 2.0f);
            return true;
        }
        auto* container = registry.try_get<ecs::Container>(m_player);
        if (container == nullptr ||
            items::RemoveItem(*container, m_radioPartDefId, 1) != 1) {
            SetNotice("无法使用无线电零件", kNoticeFailColor, 2.0f);
            return true;
        }
        objective->step = ecs::ObjectiveStep::ReachExtraction;
        RecordMissionEvent(MissionObjectiveKind::Interact, "radio", 1);
        SetNotice("电台修复完成！前往撤离点", kNoticeOkColor, 4.0f);
        ZL_INFO("objective", "电台已修复，目标推进：{}",
                game::ObjectiveStepName(objective->step));
        return true;
    }
    if (objective->step == ecs::ObjectiveStep::ReachExtraction) {
        SetNotice("电台已修复，前往撤离点", kNoticeInfoColor, 2.0f);
        return true;
    }
    SetNotice("目标已完成", kNoticeOkColor, 2.0f);
    return true;
}

void WorldDemoState::PickupNearestItem() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* pos = registry.try_get<ecs::Position>(m_player);
    auto* container = registry.try_get<ecs::Container>(m_player);
    if (pos == nullptr || container == nullptr) {
        return;
    }
    const game::PickupResult result = game::PickupNearestItem(
        *m_ctx.ecs, *m_ctx.items, *container, pos->x, pos->y, pos->z, m_pickupRadius);
    if (!result.found) {
        SetNotice("Nothing to pick up", kNoticeInfoColor, 1.2f);
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(result.defId);
    const std::string name = def != nullptr ? def->name : result.defId;
    if (result.picked > 0) {
        if (result.groundRemaining > 0) {
            SetNotice("拾取??" + name + " ×" + std::to_string(result.picked) + "（地上剩 " +
                          std::to_string(result.groundRemaining) + "??",
                      kNoticeOkColor, 2.0f);
        } else {
            SetNotice("拾取??" + name + " ×" + std::to_string(result.picked),
                      kNoticeOkColor, 2.0f);
        }
        ZL_INFO("items", "拾取：{} x{}（地面剩 {} 件；背包 {} 件）", name, result.picked,
                result.groundRemaining, items::CountOf(*container, result.defId));
        if (m_ctx.events != nullptr) {  // M7-T4：OnItemPickup（排队派发）
            m_ctx.events->EmitQueued(
                core::kOnItemPickup,
                core::ItemEvent{result.entityId, result.defId, result.picked});
        }
    } else {
        SetNotice("Bag full (" + result.defId + ")", kNoticeFailColor, 1.5f);
        ZL_INFO("items", "拾取失败：{}（背包已满）", name);
    }
}

void WorldDemoState::UseConsumableItem() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* health = registry.try_get<ecs::Health>(m_player);
    if (container == nullptr || health == nullptr) {
        return;
    }

    const auto* infectionState = registry.try_get<ecs::Infection>(m_player);
    const bool infected =
        infectionState != nullptr &&
        infectionState->stage != ecs::InfectionStage::None;
    int useIndex = -1;
    for (std::size_t i = 0; i < container->items.size(); ++i) {
        const items::ItemDef* candidate =
            m_ctx.items->Find(container->items[i].defId);
        if (candidate == nullptr || !candidate->use.Any()) {
            continue;
        }
        if (candidate->use.infectionCure > 0.0f && !infected) {
            continue;
        }
        useIndex = static_cast<int>(i);
        break;
    }
    if (useIndex < 0) {
        SetNotice("No usable item", kNoticeInfoColor, 1.2f);
        return;
    }
    const items::UseResult use =
        items::UseAt(*container, *m_ctx.items, static_cast<std::size_t>(useIndex));
    if (!use.applied) {
        SetNotice("No usable item", kNoticeInfoColor, 1.2f);
        return;
    }
    const items::ItemDef* def = m_ctx.items->Find(use.defId);
    const std::string name = def != nullptr ? def->name : use.defId;

    // 效果落地（纯函数：HP 钳到上限，返回实际回血；需求钳到 [0,1]）
    auto* needs = registry.try_get<ecs::Needs>(m_player);
    const float healed = items::ApplyUseEffect(
        use.effect, health->max, &health->current,
        needs != nullptr ? &needs->hunger : nullptr,
        needs != nullptr ? &needs->thirst : nullptr,
        needs != nullptr ? &needs->fatigue : nullptr);
    const float foodDamage = use.spoiled ? m_spoiledFoodDamage : 0.0f;
    const float totalDamage = use.effect.healthPenalty + foodDamage;
    if (totalDamage > 0.0f) {
        health->current = std::max(0.0f, health->current - totalDamage);
    }
    if (def != nullptr) {
        ApplyFoodUse(*def, use);
    }
    characters::WoundTreatment treatment;
    if (use.effect.bandage > 0.0f) {
        if (auto* wounds = registry.try_get<ecs::Wounds>(m_player); wounds != nullptr) {
            treatment = characters::TreatWounds(*wounds, use.effect.bandage);
        }
    }
    characters::InfectionTreatment infectionTreatment;
    if (use.effect.infectionCure > 0.0f || use.effect.infectionSlow > 0.0f) {
        if (auto* infection = registry.try_get<ecs::Infection>(m_player);
            infection != nullptr) {
            infectionTreatment = characters::TreatInfection(
                *infection, use.effect.infectionCure, use.effect.infectionSlow,
                use.effect.infectionSlowSeconds,
                registry.try_get<ecs::Wounds>(m_player));
        }
    }
    if (ApplyUseInfectionRisk(use.effect.infectionRisk, use.defId)) {
        SetNotice("Used " + use.defId + " (infected)", kNoticeFailColor, 2.0f);
        return;
    }
    if (treatment.wounds > 0) {
        std::string text = "Used " + use.defId + " (bandaged " +
                           std::to_string(treatment.wounds) + " wounds)";
        if (healed > 0.0f) {
            text += " (+" + std::to_string(static_cast<int>(healed)) + "HP)";
        }
        if (infectionTreatment.applied) {
            text += infectionTreatment.cured ? " (infection cured)"
                                             : " (infection slowed)";
        }
        SetNotice(std::move(text), kNoticeOkColor, 1.8f);
    } else if (infectionTreatment.applied) {
        SetNotice(infectionTreatment.cured
                      ? "Used " + use.defId + " (infection cured)"
                      : "Used " + use.defId + " (infection slowed)",
                  kNoticeOkColor, 1.8f);
    } else if (use.effect.heal > 0.0f) {
        SetNotice("Used " + use.defId + " (+" + std::to_string(static_cast<int>(healed)) +
                      "HP)",
                  kNoticeOkColor, 1.5f);
    } else if (totalDamage > 0.0f) {
        SetNotice("Used " + use.defId + " (-" +
                      std::to_string(static_cast<int>(std::lround(totalDamage))) +
                      "HP)",
                  kNoticeInfoColor, 1.5f);
    } else {
        SetNotice("Used " + use.defId, kNoticeOkColor, 1.5f);
    }
    ZL_INFO("items", "使用：{}（回血 {:.0f}，伤??{:.0f}，止血 {} 处，HP "
                     "{:.0f}/{:.0f}；剩??{} 件）",
            name, healed, totalDamage, treatment.wounds, health->current, health->max,
            use.remaining);
}

void WorldDemoState::ToggleEquipWeapon() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* equipment = registry.try_get<ecs::Equipment>(m_player);
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* pos = registry.try_get<ecs::Position>(m_player);
    if (equipment == nullptr || container == nullptr || pos == nullptr) {
        return;
    }
    m_rangedWeapon.CancelReload();
    constexpr std::size_t kPrimary = static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand);
    const items::ItemRegistry& reg = *m_ctx.items;

    // 已装????卸下（回背包；放不下则落地面??
    if (equipment->slots[kPrimary].has_value()) {
        ecs::ItemInstance out;
        items::Unequip(*equipment, ecs::EquipSlot::PrimaryHand, &out);
        const items::ItemDef* def = reg.Find(out.defId);
        const std::string name = def != nullptr ? def->name : out.defId;
        if (items::AddItem(*container, reg, out) > 0) {
            SetNotice("Unequipped " + out.defId, kNoticeInfoColor, 1.5f);
            ZL_INFO("items", "卸下：{}（回背包??", name);
        } else {
            game::SpawnGroundItem(*m_ctx.ecs, reg, out, pos->x, pos->y, pos->z);
            SetNotice("Unequipped " + out.defId + " (dropped)", kNoticeInfoColor, 1.5f);
            ZL_INFO("items", "卸下：{}（背包放不下 → 落地面）", name);
        }
        return;
    }

    // 空手 → 背包第一件武器上手；被换下的武器回背包（放不下落地面）
    for (std::size_t i = 0; i < container->items.size(); ++i) {
        const ecs::ItemInstance entry = container->items[i];  // 拷贝（RemoveItem 会失效引用）
        const items::ItemDef* def = reg.Find(entry.defId);
        if (def == nullptr || def->type != items::ItemType::Weapon) {
            continue;
        }
        ecs::ItemInstance swapped;
        if (!items::Equip(*equipment, reg, ecs::EquipSlot::PrimaryHand, entry, &swapped)) {
            continue;
        }
        items::RemoveItem(*container, entry.defId, 1);
        if (swapped.Valid()) {
            if (items::AddItem(*container, reg, swapped) <= 0) {
                game::SpawnGroundItem(*m_ctx.ecs, reg, swapped, pos->x, pos->y, pos->z);
            }
        }
        SetNotice("Equipped " + entry.defId, kNoticeOkColor, 1.5f);
        ZL_INFO("items", "装备：{} ??主手（背包剩 {} 件）", def->name,
                items::CountOf(*container, entry.defId));
        return;
    }
    SetNotice("No weapon in bag", kNoticeFailColor, 1.2f);
}

void WorldDemoState::DropFirstItem() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    auto* container = registry.try_get<ecs::Container>(m_player);
    auto* pos = registry.try_get<ecs::Position>(m_player);
    if (container == nullptr || pos == nullptr) {
        return;
    }
    if (container->items.empty()) {
        SetNotice("Bag empty", kNoticeInfoColor, 1.2f);
        return;
    }
    ecs::ItemInstance stack = container->items.front();
    const int removed = items::RemoveItem(*container, stack.defId, stack.count);
    if (removed <= 0) {
        return;
    }
    stack.count = removed;
    const ecs::Entity ground =
        game::SpawnGroundItem(*m_ctx.ecs, *m_ctx.items, stack, pos->x, pos->y, pos->z);
    if (m_ctx.events != nullptr && ground != entt::null) {  // M7-T4：OnItemDrop（排队派发）
        m_ctx.events->EmitQueued(core::kOnItemDrop,
                                 core::ItemEvent{m_ctx.ecs->IdOf(ground).id, stack.defId, removed});
    }
    const items::ItemDef* def = m_ctx.items->Find(stack.defId);
    SetNotice("Dropped " + stack.defId + " x" + std::to_string(removed), kNoticeInfoColor,
              1.5f);
    ZL_INFO("items", "丢弃：{} x{}（地面物??{} 件）", def != nullptr ? def->name : stack.defId,
            removed, game::CountGroundItems(*m_ctx.ecs));
}

void WorldDemoState::AppendActorPresentation() {
    if (m_ctx.ecs == nullptr || m_ctx.items == nullptr || m_ctx.assets == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const std::vector<AssetProvider::Entry>* pixelFrames =
        m_ctx.assets->FramesByHash(ecs::HashName("ui.pixel"));
    if (pixelFrames == nullptr || pixelFrames->empty() ||
        (*pixelFrames)[0].atlas == nullptr || (*pixelFrames)[0].frame == nullptr) {
        return;
    }
    const AssetProvider::Entry& pixel = (*pixelFrames)[0];

    auto pushIcon = [&](const ecs::Position& pos, std::uint64_t stableId,
                        std::string_view icon, int subLayer, float offsetX,
                        float offsetY, float scale, std::uint32_t tint) {
        const items::ItemDef* def = m_ctx.items->Find(icon);
        if (def == nullptr) {
            return;
        }
        const auto* frames = m_ctx.assets->FramesByHash(ecs::HashName(def->icon));
        if (frames == nullptr || frames->empty() || frames->front().atlas == nullptr ||
            frames->front().frame == nullptr) {
            return;
        }
        const AssetProvider::Entry& entry = frames->front();
        render::DrawItem item;
        RenderCollectSystem::FillEntityDrawItem(
            pos.x, pos.y, pos.z, tint, stableId, *entry.frame,
            entry.atlas->Texture(), subLayer, &item);
        const float centerX = item.rect.x + item.rect.w * 0.5f;
        const float centerY = item.rect.y + item.rect.h * 0.5f;
        item.rect.w *= scale;
        item.rect.h *= scale;
        item.rect.x = centerX + offsetX - item.rect.w * 0.5f;
        item.rect.y = centerY + offsetY - item.rect.h * 0.5f;
        m_items.push_back(item);
    };

    auto pushPixel = [&](const ecs::Position& pos, std::uint64_t stableId,
                         float offsetX, float offsetY, float width, float height,
                         std::uint32_t tint, int subLayer) {
        render::DrawItem item;
        RenderCollectSystem::FillEntityDrawItem(
            pos.x, pos.y, pos.z, tint, stableId, *pixel.frame,
            pixel.atlas->Texture(), subLayer, &item);
        const float centerX = item.rect.x + item.rect.w * 0.5f;
        const float centerY = item.rect.y + item.rect.h * 0.5f;
        item.rect.w = width;
        item.rect.h = height;
        item.rect.x = centerX + offsetX - width * 0.5f;
        item.rect.y = centerY + offsetY - height * 0.5f;
        m_items.push_back(item);
    };

    registry.view<ecs::Position, ecs::SpriteRef>().each(
        [&](const ecs::Entity entity, const ecs::Position& pos,
            const ecs::SpriteRef&) {
            const std::uint64_t stableId = m_ctx.ecs->IdOf(entity).id;
            const auto* action = registry.try_get<ecs::ActionState>(entity);
            const auto* facing = registry.try_get<ecs::Facing>(entity);
            const auto* anim = registry.try_get<ecs::AnimState>(entity);
            const auto* ai = registry.try_get<ecs::AiState>(entity);
            const auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
            const bool specialWindup =
                special != nullptr && special->windup > 0.0f;
            const bool windingUp =
                (ai != nullptr && ai->attackWindupLeft > 0.0f) ||
                (specialWindup &&
                 special->kind == ecs::ZombieSpecialKind::Breaker);
            if (specialWindup) {
                const std::uint32_t color =
                    special->kind == ecs::ZombieSpecialKind::Howler
                        ? 0xFFB060FFu
                        : 0xFF4050FFu;
                const float progress =
                    special->windupDuration > 0.0f
                        ? std::clamp(special->windup /
                                         special->windupDuration,
                                     0.0f, 1.0f)
                        : 1.0f;
                const float ring = 26.0f + 10.0f * (1.0f - progress);
                pushPixel(pos, stableId, 0.0f, -42.0f, ring, 4.0f, color, 4);
                pushPixel(pos, stableId, -ring * 0.5f, -42.0f, 4.0f,
                          ring * 0.5f, color, 4);
                pushPixel(pos, stableId, ring * 0.5f, -42.0f, 4.0f,
                          ring * 0.5f, color, 4);
            }
            if (const auto* equipment = registry.try_get<ecs::Equipment>(entity);
                equipment != nullptr) {
                const auto& primary =
                    equipment->slots[static_cast<std::size_t>(
                        ecs::EquipSlot::PrimaryHand)];
                if (primary.has_value()) {
                    const characters::WeaponSocketOffset socket =
                        characters::PrimaryHandSocket(
                            facing != nullptr ? facing->dir : ecs::Direction::Down,
                            action != nullptr ? action->action : ecs::ActorAction::Idle,
                            anim != nullptr ? anim->frame : 0);
                    pushIcon(pos, stableId, primary->defId, 1, socket.x, socket.y,
                             socket.scale, 0xFFFFFFFFu);
                }
                const auto& body =
                    equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
                if (body.has_value()) {
                    pushIcon(pos, stableId, body->defId, 0, 0.0f, -18.0f, 0.72f,
                             0xD8FFFFFFu);
                }
            }

            if (const auto* npc = registry.try_get<SettlementNpc>(entity);
                npc != nullptr) {
                const std::uint32_t color = NpcActivityColor(npc->activity);
                pushPixel(pos, stableId, 0.0f, -54.0f, 20.0f, 4.0f, color, 4);
                pushPixel(pos, stableId, 0.0f, -58.0f, 4.0f, 4.0f, color, 4);
            }
            if (action == nullptr || facing == nullptr) {
                return;
            }
            if (action->action == ecs::ActorAction::Attacking) {
                float offsetX = 24.0f;
                float offsetY = -34.0f;
                float width = 34.0f;
                float height = 8.0f;
                switch (facing->dir) {
                    case ecs::Direction::Down:
                        offsetX = 20.0f;
                        offsetY = -26.0f;
                        break;
                    case ecs::Direction::Up:
                        offsetX = -20.0f;
                        offsetY = -46.0f;
                        break;
                    case ecs::Direction::Left:
                        offsetX = -26.0f;
                        offsetY = -34.0f;
                        break;
                    case ecs::Direction::Right:
                        offsetX = 26.0f;
                        offsetY = -34.0f;
                        width = 8.0f;
                        height = 34.0f;
                        break;
                }
                if (facing->dir == ecs::Direction::Up ||
                    facing->dir == ecs::Direction::Down) {
                    width = 8.0f;
                    height = 34.0f;
                }
                const std::uint32_t attackColor =
                    windingUp ? 0xFF4040FFu : 0xFFE8A0FFu;
                pushPixel(pos, stableId, offsetX, offsetY, width, height,
                          attackColor, 3);
                pushPixel(pos, stableId, offsetX * 0.7f, offsetY - 8.0f,
                          width * 0.6f, height * 0.6f,
                          windingUp ? 0xFF8080FFu : 0xFFFFFFFFu, 3);
                if (windingUp && action->duration > 0.0f) {
                    const float progress =
                        std::clamp(action->elapsed / action->duration, 0.0f, 1.0f);
                    pushPixel(pos, stableId, 0.0f, -54.0f,
                              28.0f * (1.0f - progress), 5.0f, 0xFF4040FFu, 4);
                }
            } else if (action->action == ecs::ActorAction::Howling) {
                const float progress =
                    action->duration > 0.0f
                        ? std::clamp(action->elapsed / action->duration, 0.0f,
                                     1.0f)
                        : 1.0f;
                pushPixel(pos, stableId, 0.0f, -56.0f,
                          34.0f * (1.0f - progress), 5.0f, 0xFFB060FFu, 4);
                pushPixel(pos, stableId, -12.0f, -32.0f, 4.0f, 4.0f,
                          0xFFD090FFu, 3);
                pushPixel(pos, stableId, 8.0f, -38.0f, 3.0f, 3.0f,
                          0xFFFFD0FFu, 3);
            } else if (action->action == ecs::ActorAction::Crafting) {
                pushPixel(pos, stableId, -7.0f, -30.0f, 4.0f, 4.0f,
                          0xFFFFD060u, 3);
                pushPixel(pos, stableId, 5.0f, -37.0f, 3.0f, 3.0f,
                          0xFFFFF0A0u, 3);
                pushPixel(pos, stableId, 0.0f, -23.0f, 3.0f, 3.0f,
                          0xFFFFB040u, 3);
            }
        });
}

void WorldDemoState::DespawnGroundItems() {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    registry.view<ecs::ItemInstance>().each(
        [&](const ecs::Entity e, const ecs::ItemInstance&) {
            if (m_ctx.ecs->Alive(e)) {
                m_ctx.ecs->Destroy(e);
            }
        });
    m_ctx.ecs->Flush();
}

void WorldDemoState::OpenPauseMenu() {
    if (m_ctx.ui == nullptr || m_ctx.ui->IsMenuOpen()) {
        return;
    }
    m_ctx.ui->CloseInventory();
    m_speedBeforeMenu = m_ctx.gameTime->GetSpeed();
    if (m_speedBeforeMenu == core::GameTime::Speed::Paused) {
        m_speedBeforeMenu = core::GameTime::Speed::X1;
    }
    m_menuPaused = true;
    m_ctx.gameTime->SetSpeed(core::GameTime::Speed::Paused);
    m_ctx.ui->ShowPauseMenu();
    ZL_INFO("ui", "暂停菜单打开（ESC 继续）");
}

void WorldDemoState::ResumeFromMenu() {
    if (m_ctx.ui == nullptr) {
        return;
    }
    m_ctx.ui->HideMenu();
    if (!m_menuPaused) {
        return;
    }
    m_menuPaused = false;
    m_ctx.gameTime->SetSpeed(m_speedBeforeMenu);
    ZL_INFO("ui", "继续游戏：倍速 {}", core::GameTime::SpeedName(m_speedBeforeMenu));
}

void WorldDemoState::HandleMenuAction(ui::MenuActionType action) {
    switch (action) {
        case ui::MenuActionType::Continue:
        case ui::MenuActionType::Resume:
            ResumeFromMenu();
            break;
        case ui::MenuActionType::ReturnToMainMenu:
            if (m_networkMode) {
                if (m_ctx.leaveMultiplayer) {
                    m_ctx.leaveMultiplayer();
                }
                if (m_ctx.states != nullptr) {
                    m_ctx.states->SetState(std::make_unique<MainMenuState>(m_ctx));
                }
                ZL_INFO("ui", "离开联机会话，返回主菜单");
                break;
            }
            if (m_runOutcome != RunOutcome::None) {
                // 结算页不能把已结束的世界作为“继续”恢复；回到真正的主菜单。
                if (m_ctx.states != nullptr) {
                    m_ctx.states->SetState(std::make_unique<MainMenuState>(m_ctx));
                }
                ZL_INFO("ui", "结算结束，返回主菜单");
                break;
            }
            m_baseSiege.Abort({}, false);
            if (m_ctx.ui != nullptr) {
                m_ctx.ui->ShowMainMenu(true);
                ZL_INFO("ui", "返回主菜单（当前世界保留，可继续??");
            }
            break;
        case ui::MenuActionType::Multiplayer:
            break;
        case ui::MenuActionType::NewGame:
            if (m_ctx.gameTime != nullptr) {
                m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
            }
            if (m_ctx.states != nullptr) {
                m_ctx.states->SetState(std::make_unique<WorldDemoState>(m_ctx));
            }
            break;
        case ui::MenuActionType::Quit:
            if (m_ctx.window != nullptr) {
                m_ctx.window->SetShouldClose(true);
            }
            break;
    }
}

void WorldDemoState::OnExit() {
    // 退出前自动存档：先采集快照/提交后台，再销毁实体；GameApp 会在关闭时等待 zl-save
    m_baseSiege.Abort({}, false);
    m_ctx.executePlayerCommand = {};
    m_ctx.onSaveFinished = {};
    m_ctx.quickSave = {};
    if (m_autoSaveOnExit && m_runOutcome == RunOutcome::None &&
        m_player != entt::null && m_ctx.ecs != nullptr &&
        m_ctx.ecs->Alive(m_player) && m_ctx.saveService != nullptr) {
        if (m_screenshotPending) {
            m_screenshotPending = false;
            SubmitPendingSave();
        } else if (m_saveRequestPending) {
            SubmitPendingSave();
        } else if (!m_savePending) {
            RequestQuickSave(false);
        }
    }
    m_ctx.activeSaveId.clear();
    m_ctx.hudState = {};

    if (m_ctx.ui != nullptr) {
        m_ctx.ui->SetHudState(m_ctx.hudState);
        m_ctx.ui->CloseInventory();
        m_ctx.ui->SetInventoryActionHandler({});
        m_ctx.ui->SetSocialChatHandler({});
        m_ctx.ui->SetSocialState({});
        m_ctx.ui->HideMenu();
        m_ctx.ui->SetMenuActionHandler({});
    }
    if (m_menuPaused && m_ctx.gameTime != nullptr) {
        m_ctx.gameTime->SetSpeed(core::GameTime::Speed::X1);
    }
    m_menuPaused = false;
    // 先清演示实体（含物理体/地面物品），再汇报统计
    DespawnGroundItems();
    DespawnDemoEntities();
    if (m_ctx.ecs != nullptr) {
        entt::registry& registry = m_ctx.ecs->Registry();
        std::vector<ecs::Entity> remaining;
        registry.view<ecs::StableId>().each(
            [&](const ecs::Entity entity, const ecs::StableId&) {
                if (m_ctx.ecs->Alive(entity)) {
                    remaining.push_back(entity);
                }
            });
        for (const ecs::Entity entity : remaining) {
            if (m_ctx.physics != nullptr) {
                if (const auto* rigid = registry.try_get<ecs::RigidBody>(entity);
                    rigid != nullptr && rigid->body != nullptr) {
                    m_ctx.physics->DestroyBody(rigid->body);
                }
            }
            m_ctx.ecs->Destroy(entity);
        }
        m_ctx.ecs->Flush();
    }
    m_networkEntities.clear();
    m_pendingNetworkChunkDiffs.clear();
    m_forageAction = {};
    m_fishingAction = {};
    m_animalHarvestAction = {};
    m_forageNodes.clear();
    m_fishingSpots.clear();
    m_foragePlanValid = false;
    m_player = entt::null;
    StopVehicleAudio();
    m_vehicle = entt::null;
    m_vehicleMotion = {};
    m_localParkedVehicleMotions.clear();
    m_vehicleHeadingX = 0.0f;
    m_vehicleHeadingY = 1.0f;
    m_vehicleVisualDirection = VehicleVisualDirection::South;
    m_vehicleSmokePuffs.clear();
    m_vehicleSmokeRuntimes.clear();
    m_vehicleVisualDirections.clear();
    m_vehicleDamageVisuals.clear();
    m_vehicleSurface = VehicleSurface::Paved;
    m_lastVehicleSurface = VehicleSurface::Paved;
    m_driving = false;
    m_riding = false;
    m_vehicleNoiseCooldown = 0.0f;
    m_vehicleRamWearCooldown = 0.0f;
    m_vehicleBodyCollisionCooldown = 0.0f;
    m_vehicleHornCooldown = 0.0f;
    m_vehicleZombieAttackCooldown = 0.0f;
    m_networkVehicle = {};
    m_networkCombat = {};
    m_vehiclePredictionActive = false;
    m_vehiclePredictionTick = 0;
    m_networkHasConnected = false;
    m_networkWasDisconnected = false;
    m_networkDisconnectWasDriving = false;
    m_playerSpriteBackup.reset();
    const auto& stats = m_ctx.chunkManager->GetStats();
    ZL_INFO("render", "退出 WorldDemo：本状态共 {} tick；chunk 累计 加载 {} / 卸载 {} / 取消 {}",
            m_ticksInState, stats.totalLoaded, stats.totalUnloaded, stats.totalCancelled);
}

void WorldDemoState::UpdateAudio(double frameDt) {
    (void)frameDt;
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    const auto* anim = registry.try_get<ecs::AnimState>(m_player);
    if (position == nullptr || anim == nullptr) {
        return;
    }
    if (anim->eventSerial == m_lastPlayerAnimEventSerial) {
        return;
    }
    m_lastPlayerAnimEventSerial = anim->eventSerial;

    const bool paused = m_menuPaused ||
                        (m_ctx.ui != nullptr && m_ctx.ui->IsMenuOpen()) ||
                        (m_ctx.gameTime != nullptr && m_ctx.gameTime->SpeedScale() <= 0.0);
    if (paused || anim->lastEvent == ecs::AnimEvent::None) {
        return;
    }

    if (anim->lastEvent == ecs::AnimEvent::Footstep) {
        std::string_view floorSprite = "tile.floor.grass";
        if (m_ctx.world != nullptr) {
            const int wx = static_cast<int>(std::floor(position->x));
            const int wy = static_cast<int>(std::floor(position->y));
            if (const world::GridSquare* square = m_ctx.world->GetSquare(wx, wy, 0);
                square != nullptr) {
                if (const world::SquareObject* floorObject = square->Floor();
                    floorObject != nullptr && !floorObject->sprite.empty()) {
                    floorSprite = floorObject->sprite;
                }
            }
        }
        core::SoundEvent step;
        step.x = position->x;
        step.y = position->y;
        step.radius = 2.5f;
        step.type = core::SoundType::Footstep;
        step.clip = std::string(audio::FootstepClipForFloor(floorSprite));
        if (m_ctx.audio != nullptr) {
            m_ctx.audio->EmitWorldSound(std::move(step));
        } else if (m_ctx.events != nullptr) {
            m_ctx.events->EmitQueued(core::kOnSound, std::move(step));
        }
    } else if (anim->lastEvent == ecs::AnimEvent::Hit) {
        const auto* attack = registry.try_get<ecs::AttackIntent>(m_player);
        const bool heavy =
            attack != nullptr && attack->kind == ecs::AttackKind::Heavy;
        PlayCombatCue(heavy ? CombatCue::HeavySwing : CombatCue::LightSwing,
                      position->x, position->y);
        m_localSwing.active = true;
        m_localSwing.heavy = heavy;
        m_localSwing.remaining = 0.44f;
        m_localSwing.duration = 0.44f;
        m_localSwing.range =
            attack != nullptr && std::isfinite(attack->range)
                ? attack->range
                : 1.7f;
        float dirX = 0.0f;
        float dirY = 1.0f;
        if (attack != nullptr && std::isfinite(attack->dirX) &&
            std::isfinite(attack->dirY)) {
            const glm::vec2 screen =
                render::ToScreen(attack->dirX, attack->dirY, 0) -
                render::ToScreen(0.0f, 0.0f, 0);
            const float length =
                std::sqrt(screen.x * screen.x + screen.y * screen.y);
            if (std::isfinite(length) && length > 0.0001f) {
                dirX = screen.x / length;
                dirY = screen.y / length;
            }
        } else if (const auto* facing = registry.try_get<ecs::Facing>(m_player);
                   facing != nullptr) {
            switch (facing->dir) {
                case ecs::Direction::Down:
                    break;
                case ecs::Direction::Left:
                    dirX = -1.0f;
                    dirY = 0.0f;
                    break;
                case ecs::Direction::Right:
                    dirX = 1.0f;
                    dirY = 0.0f;
                    break;
                case ecs::Direction::Up:
                    dirX = 0.0f;
                    dirY = -1.0f;
                    break;
            }
        }
        m_localSwing.dirScreenX = dirX;
        m_localSwing.dirScreenY = dirY;
    }
}

void WorldDemoState::UpdateCombatFeedback(double frameDt) {
    if (m_localSwing.active) {
        m_localSwing.remaining -=
            static_cast<float>(std::max(0.0, frameDt));
        if (m_localSwing.remaining <= 0.0f) {
            m_localSwing.active = false;
            m_localSwing.remaining = 0.0f;
        }
    }
    if (m_player != m_feedbackPlayer) {
        m_feedbackPlayer = m_player;
        m_lastPlayerHitSerial = 0;
        m_threatRemaining = 0.0f;
        m_threatDirection.clear();
        m_threatStatus.clear();
    }
    if (m_threatRemaining > 0.0f) {
        m_threatRemaining =
            std::max(0.0f, m_threatRemaining - static_cast<float>(frameDt));
    }
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();

    // M30-T4：格??弹反/破防/闪避反馈。回调在固定 tick 内累积，这里按帧消费??
    bool playerDefenseFeedback = false;
    if (m_ctx.simulation != nullptr) {
        const auto feedbacks = m_ctx.simulation->TakeBlockFeedbacks();
        bool playedCue = false;
        for (const characters::CombatSystem::BlockFeedback& feedback :
             feedbacks) {
            const bool defending = feedback.defender == m_player;
            const bool attacking = feedback.attacker == m_player;
            if (!defending && !attacking) {
                continue;
            }
            if (defending) {
                playerDefenseFeedback = true;
            }
            if (feedback.outcome ==
                    characters::CombatSystem::BlockOutcome::Dodged &&
                !defending) {
                continue;  // 自己攻击被闪避暂不提示，避免刷屏
            }
            std::string text;
            const char* tone = "info";
            switch (feedback.outcome) {
                case characters::CombatSystem::BlockOutcome::Blocked:
                    text = defending
                               ? std::string("格挡成功（") +
                                     DamageTypeLabel(feedback.type) + "）"
                               : "攻击被格挡";
                    tone = defending ? "ok" : "warn";
                    break;
                case characters::CombatSystem::BlockOutcome::Parried:
                    text = defending
                               ? std::string("弹反！攻击者硬直（") +
                                     DamageTypeLabel(feedback.type) + "??"
                               : "攻击被弹??";
                    tone = defending ? "ok" : "fail";
                    break;
                case characters::CombatSystem::BlockOutcome::GuardBroken:
                    text = defending ? "破防！格挡被击溃" : "目标破防";
                    tone = "fail";
                    break;
                case characters::CombatSystem::BlockOutcome::Dodged:
                    text = "闪避：无敌帧";
                    tone = "ok";
                    break;
            }
            if (!text.empty()) {
                const std::uint32_t color =
                    std::string_view(tone) == "ok"
                        ? kNoticeOkColor
                        : std::string_view(tone) == "warn" ? kNoticeInfoColor
                                                           : kNoticeFailColor;
                SetNotice(text, color, 1.3f);
            }
            if (feedback.defender != entt::null &&
                m_ctx.ecs->Alive(feedback.defender)) {
                SpawnCombatFeedbackSparks(feedback.defender,
                                          feedback.outcome);
            }
            if (!playedCue && feedback.defender != entt::null &&
                m_ctx.ecs->Alive(feedback.defender)) {
                CombatCue cue = CombatCue::Block;
                switch (feedback.outcome) {
                    case characters::CombatSystem::BlockOutcome::Blocked:
                        cue = CombatCue::Block;
                        break;
                    case characters::CombatSystem::BlockOutcome::Parried:
                        cue = CombatCue::Parry;
                        break;
                    case characters::CombatSystem::BlockOutcome::GuardBroken:
                        cue = CombatCue::GuardBreak;
                        break;
                    case characters::CombatSystem::BlockOutcome::Dodged:
                        cue = CombatCue::Dodge;
                        break;
                }
                if (const auto* position =
                        registry.try_get<ecs::Position>(feedback.defender);
                    position != nullptr) {
                    PlayCombatCue(cue, position->x, position->y);
                    playedCue = true;
                }
            }
        }
    }

    // M30-T5：玩家真实近战命????火花/音效；破防额外提示??
    if (m_ctx.simulation != nullptr) {
        const auto hits = m_ctx.simulation->TakeHitFeedbacks();
        bool playedHitSound = false;
        for (const characters::CombatSystem::HitFeedback& hit : hits) {
            if (hit.attacker != m_player || hit.target == m_player ||
                !(hit.damage > 0.0f) || hit.target == entt::null ||
                !m_ctx.ecs->Alive(hit.target)) {
                continue;
            }
            SpawnMeleeHitSparks(hit.target, hit.poiseBroken);
            if (hit.poiseBroken) {
                SetNotice("破防！目标硬??", kNoticeOkColor, 1.2f);
            }
            if (!playedHitSound) {
                if (const auto* position =
                        registry.try_get<ecs::Position>(hit.target);
                    position != nullptr) {
                    PlayCombatCue(CombatCue::Hit, position->x, position->y);
                    playedHitSound = true;
                }
            }
        }
    }

    const auto* hit = registry.try_get<ecs::HitReaction>(m_player);
    if (hit == nullptr || hit->hitSerial == m_lastPlayerHitSerial) {
        return;
    }
    m_lastPlayerHitSerial = hit->hitSerial;
    if (playerDefenseFeedback) {
        return;  // 普通格挡仍会扣血：用格挡反馈替代同帧“受击”提??
    }
    std::string direction;
    std::string label;
    ThreatDirectionFor(hit->sourceDirX, hit->sourceDirY, &direction, &label);
    m_threatDirection = std::move(direction);
    m_threatStatus = "受击：" + label + "（" + DamageTypeLabel(hit->sourceType) + "）";
    m_threatDuration = kThreatFeedbackSeconds;
    m_threatRemaining = kThreatFeedbackSeconds;

    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    PlayCombatCue(CombatCue::Hit, position->x, position->y);
}

void WorldDemoState::SpawnCombatFeedbackSparks(
    ecs::Entity entity, characters::CombatSystem::BlockOutcome outcome) {
    if (m_ctx.ecs == nullptr || !m_ctx.ecs->Alive(entity)) {
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(entity);
    if (position == nullptr || !std::isfinite(position->x) ||
        !std::isfinite(position->y)) {
        return;
    }
    int count = 6;
    float lifetime = 0.30f;
    std::uint32_t tint = 0xFFFFD060u;
    switch (outcome) {
        case characters::CombatSystem::BlockOutcome::Blocked:
            count = 7;
            lifetime = 0.32f;
            tint = 0xFFFFD060u;
            break;
        case characters::CombatSystem::BlockOutcome::Parried:
            count = 12;
            lifetime = 0.46f;
            tint = 0xFFFFFFFFu;
            break;
        case characters::CombatSystem::BlockOutcome::GuardBroken:
            count = 9;
            lifetime = 0.38f;
            tint = 0xFFFF6060u;
            break;
        case characters::CombatSystem::BlockOutcome::Dodged:
            count = 4;
            lifetime = 0.22f;
            tint = 0xFF80E0FFu;
            break;
    }
    ++m_vehicleHitSparkSerial;
    for (int i = 0; i < count; ++i) {
        const float angle =
            static_cast<float>(m_vehicleHitSparkSerial + i) * 1.37f +
            6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        const float distance =
            0.08f + 0.24f * static_cast<float>(
                               (m_vehicleHitSparkSerial + i * 3u) % 7u) /
                         6.0f;
        VehicleHitSpark spark;
        spark.x = position->x + std::cos(angle) * distance;
        spark.y = position->y + std::sin(angle) * distance;
        spark.z = position->z;
        spark.lifetime = lifetime;
        spark.size = outcome == characters::CombatSystem::BlockOutcome::Parried
                         ? 7.0f
                         : 5.0f;
        spark.vx = std::cos(angle) * 0.85f;
        spark.vy = std::sin(angle) * 0.85f;
        spark.tint = tint;
        if (m_vehicleHitSparks.size() >= 64) {
            m_vehicleHitSparks.erase(m_vehicleHitSparks.begin());
        }
        m_vehicleHitSparks.push_back(spark);
    }
}

void WorldDemoState::SpawnMeleeHitSparks(ecs::Entity target, bool poiseBroken) {
    if (m_ctx.ecs == nullptr || !m_ctx.ecs->Alive(target)) {
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(target);
    if (position == nullptr || !std::isfinite(position->x) ||
        !std::isfinite(position->y)) {
        return;
    }
    const int count = poiseBroken ? 10 : 5;
    const float lifetime = poiseBroken ? 0.38f : 0.24f;
    const std::uint32_t tint =
        poiseBroken ? 0xFFFF6060u : 0xFFFFD060u;
    ++m_vehicleHitSparkSerial;
    for (int i = 0; i < count; ++i) {
        const float angle =
            static_cast<float>(m_vehicleHitSparkSerial + i) * 1.37f +
            6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        const float distance =
            0.08f + 0.24f * static_cast<float>(
                               (m_vehicleHitSparkSerial + i * 3u) % 7u) /
                          6.0f;
        VehicleHitSpark spark;
        spark.x = position->x + std::cos(angle) * distance;
        spark.y = position->y + std::sin(angle) * distance;
        spark.z = position->z;
        spark.lifetime = lifetime;
        spark.size = poiseBroken ? 7.0f : 4.5f;
        spark.vx = std::cos(angle) * (poiseBroken ? 1.1f : 0.75f);
        spark.vy = std::sin(angle) * (poiseBroken ? 1.1f : 0.75f);
        spark.tint = tint;
        if (m_vehicleHitSparks.size() >= 64) {
            m_vehicleHitSparks.erase(m_vehicleHitSparks.begin());
        }
        m_vehicleHitSparks.push_back(spark);
    }
}

void WorldDemoState::OnTick(double tickDt) {
    UpdateDemolishConfirm(static_cast<float>(tickDt));
    if (!m_networkMode &&
        (m_dynamicContentApplied != m_ctx.dynamicContentCount ||
         m_contentReloadApplied != m_ctx.contentReloadCount)) {
        RefreshDynamicContent();
    }
    // M15-T1：武器冷却按固定 tick 递减（单机权威；联机由服务器校验）。
    if (m_attackCooldownLeft > 0.0f) {
        m_attackCooldownLeft =
            std::max(0.0f, m_attackCooldownLeft - static_cast<float>(tickDt));
    }
    if (!m_networkMode) {
        UpdateRangedWeapon(static_cast<float>(tickDt));
        UpdateRangedPresentation(static_cast<float>(tickDt));
        UpdateRangedProjectiles(static_cast<float>(tickDt));
        UpdateVehicle(static_cast<float>(tickDt));
        UpdateCrafting(static_cast<float>(tickDt));
    } else if (m_ctx.networkClient != nullptr && m_ctx.networkClient->HasPlayer() &&
               m_moveCommandValid && !m_driving) {
        // 本地轻量预测：服务器快照到达后在 EntityReplicationClient 内校正/吸附。
        const glm::vec2 screen(m_lastMoveCommand.x, m_lastMoveCommand.y);
        if (glm::length(screen) > 0.0001f) {
            const glm::vec2 grid =
                glm::normalize(characters::InputSystem::ScreenDirToGridDir(
                    glm::normalize(screen)));
            (void)m_ctx.networkClient->AdvanceLocalPrediction(
                static_cast<float>(tickDt), grid.x, grid.y);
        }
    }
    if (m_networkMode) {
        if (m_ctx.networkClient == nullptr ||
            !m_ctx.networkClient->IsConnected()) {
            m_networkSleep = {};
        }
        RefreshSocialState();
    } else {
        UpdateRunStats(tickDt);
        UpdateBuilding(static_cast<float>(tickDt));
        UpdateHomestead(static_cast<float>(tickDt));
        UpdateSleep(static_cast<float>(tickDt));
        UpdateBaseSiege(static_cast<float>(tickDt));
        UpdateBaseHud(static_cast<float>(tickDt));
        UpdateProgression(static_cast<float>(tickDt));
        UpdateForage(static_cast<float>(tickDt));
        UpdateFishing(static_cast<float>(tickDt));
        UpdateAnimalHarvest(static_cast<float>(tickDt));
        if (m_sleep.Active() && m_baseSiege.Active()) {
            FinishSleep(SleepStopReason::SiegeStarted);
        }
    }
    ++m_ticksInState;
    // 单机：感知/AI/位移由固定链驱动；网络：服务器权威推进，客户端只应用副本。
}

void WorldDemoState::OnFrame(double frameDt, double alpha) {
    (void)alpha;
    using Clock = std::chrono::steady_clock;
    const auto frameStart = Clock::now();
    m_fireVisualTime += std::max(0.0f, static_cast<float>(frameDt));
    PollSaveScreenshot(frameDt);
    ConsumeSaveWarning();
    if (m_networkMode) {
        SyncNetworkReplicas(static_cast<float>(alpha));
        const bool connected =
            m_ctx.networkClient != nullptr &&
            m_ctx.networkClient->IsConnected();
        if (connected) {
            m_networkHasConnected = true;
            if (m_networkWasDisconnected &&
                m_ctx.networkClient->HasPlayerState()) {
                m_networkWasDisconnected = false;
                SetNotice(m_networkDisconnectWasDriving
                              ? "已重新连接：载具座位已释放，请重新上??"
                              : "已重新连接服务器",
                          kNoticeOkColor, 2.5f);
                m_networkDisconnectWasDriving = false;
            }
        } else if (m_networkHasConnected && !m_networkWasDisconnected) {
            m_networkWasDisconnected = true;
            const std::string reason =
                m_ctx.networkClient != nullptr
                    ? m_ctx.networkClient->LastError()
                    : std::string();
            SetNotice(reason.empty() ? "连接断开，等待服务器重连"
                                     : "连接断开??" + reason,
                      kNoticeFailColor, 2.5f);
        }
    }
    HandleFrameInput(frameDt);
    if (m_networkMode) {
        UpdateNetworkVehiclePrediction(static_cast<float>(frameDt));
    }
    if ((m_sleep.Active() || m_sleepSelecting) && m_ctx.ui != nullptr &&
        m_ctx.ui->IsInventoryOpen()) {
        m_ctx.ui->CloseInventory();
    }
    if (m_ctx.ui == nullptr || !m_ctx.ui->IsMenuOpen()) {
        UpdatePathFeedback(frameDt);  // M4-T7：点击移动反馈（提示文字/目标标记计时与状态跃迁）
        UpdateCruise(frameDt);
        UpdateCameraFollow(frameDt);  // M4-T5：跟随在巡航之后（巡航接管时不生效）
    }

    // 流式加载：相机中心（世界像素 → 世界格坐标）
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);
    m_ctx.chunkManager->Update(camGrid.x, camGrid.y);
    m_ctx.chunkManager->ProcessResults();
    if (m_networkMode) {
        ApplyNetworkChunkDiffs();
        ApplyNetworkWorldSounds();
        RefreshNetworkPowerDisplay(static_cast<float>(frameDt));
        UpdateBaseHud(static_cast<float>(frameDt));
    }

    // M4-T4：目标 chunk 加载完成后生成玩家（村庄中心附近找可走格）
    EnsurePlayerSpawned();
    EnsureVehicleSpawned();
    UpdateVehicleVisual();
    UpdateVehicleDamageSmoke(static_cast<float>(frameDt));
    UpdateVehicleDamageVisual();
    UpdateVehicleHitFeedback(static_cast<float>(frameDt));
    UpdateVehicleMetalDebris(static_cast<float>(frameDt));
    EnsureHomesteadRuntime();
    EnsureSettlementNpcs();
    PollSettlementDefenseNotices();
    // M6-T3：玩家就绪后散落地面物品（网络客户端不持有权威世界物品）
    if (!m_networkMode) {
        EnsureDemoItemsScattered();
        EnsureDemoLootContainers();
        EnsureHouseLootContainers();
        EnsurePoiContent();
    }
    // M25-T4 V4：驾驶时更新引擎循环音的音量/音高与位置??
    UpdateVehicleAudio();
    // M11-T3：脚步/地面材质 → OnSound（音频 + AI 听觉）
    UpdateAudio(frameDt);
    // M15-T3：玩家受击 → 方向/音效/屏幕边缘提示
    UpdateCombatFeedback(frameDt);
    const auto afterStreaming = Clock::now();

    CollectWorld();
    AppendBuildGhost();
    const auto afterWorldCollect = Clock::now();
    // M4-T2：ECS 实体 → 同列表的"高度精灵"通道；之后统一全局画家序排序
    if (m_ctx.ecs != nullptr && m_ctx.spatialIndex != nullptr) {
        RenderCollectSystem::Collect(*m_ctx.ecs, *m_ctx.spatialIndex, *m_ctx.assets, m_view,
                                     m_items);
    }
    const auto afterEntityCollect = Clock::now();
    // M6-T5：手持武器（装备槽 → 玩家旁图标；画在同格角色之上）
    AppendActorPresentation();
    std::sort(m_items.begin(), m_items.end(), render::DrawItemLess);
    ApplyWallOcclusion();
    const auto afterSort = Clock::now();
    DrawWorld();
    const auto afterWorldDraw = Clock::now();
    if (m_ctx.ui != nullptr && m_ctx.ui->IsMenuOpen()) {
        // 菜单是全屏模态：Debug 视图在 UI 之后合成，必须跳过世界调试叠层（含路径目标标记）。
        bgfx::touch(static_cast<bgfx::ViewId>(render::ViewId::Debug));
    } else {
        DrawDebugOverlay();
        DrawPhysicsDebug();
    }
    DrawDebugText();
    const auto afterDebugDraw = Clock::now();
    const auto elapsedMs = [](Clock::time_point begin, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    };

    m_statsTimer += static_cast<float>(frameDt);
    if (m_statsTimer >= kStatsLogInterval) {
        m_statsTimer = 0.0f;
        const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
        const auto& chunkStats = m_ctx.chunkManager->GetStats();
        ai::PathfindingService::Stats pathStats{};
        if (m_ctx.pathfinding != nullptr) {
            pathStats = m_ctx.pathfinding->GetStats();
        }
        audio::SoundManager::Stats audioStats{};
        if (m_ctx.audio != nullptr) {
            audioStats = m_ctx.audio->GetStats();
        }
        ai::PerceptionSystem::Stats perceptionStats{};
        if (m_ctx.perception != nullptr) {
            perceptionStats = m_ctx.perception->GetStats();
        }
        ZL_INFO("render",
                "世界统计: FPS={:.0f} | 实例={} | 四边形={} | 批次={} | drawcall={} | zoom={:.2f} | "
                "chunk 驻留={} 加载中={} | 相机格=({:.0f},{:.0f}) | 累计加载={} 卸载={} 取消={} 失败={} "
                "挂接={:.2f}ms/{}??| "
                "ECS={} 索引={} | 物理: 静??{} 动??{} step={:.2f}ms | 寻路: 查询={} 完成={} 平均={:.2f}ms | "
                "感知: ??{} ??{} ??{} | "
                "音频: 活动={} 播放={} 脚步={} 枪声={} 低吼={} 环境={}",
                stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                m_ctx.sprites->DrawCallsThisFrame(), stats.drawCalls, m_ctx.camera->Zoom(),
                chunkStats.loadedChunks, chunkStats.loadingChunks, camGrid.x, camGrid.y,
                chunkStats.totalLoaded, chunkStats.totalUnloaded, chunkStats.totalCancelled,
                chunkStats.totalFailed, chunkStats.lastAttachMs,
                static_cast<unsigned long long>(chunkStats.attachBudgetHits),
                m_ctx.ecs != nullptr ? m_ctx.ecs->AliveCount() : 0u,
                m_ctx.spatialIndex != nullptr ? m_ctx.spatialIndex->EntityCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->StaticBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->DynamicBodyCount() : 0u,
                m_ctx.physics != nullptr ? m_ctx.physics->AvgStepMs() : 0.0,
                static_cast<unsigned long long>(pathStats.submitted),
                static_cast<unsigned long long>(pathStats.completed), pathStats.avgMs,
                perceptionStats.seeing, perceptionStats.alerted,
                static_cast<unsigned long long>(perceptionStats.heard),
                audioStats.activeVoices,
                static_cast<unsigned long long>(audioStats.eventsPlayed),
                static_cast<unsigned long long>(audioStats.footsteps),
                static_cast<unsigned long long>(audioStats.gunshots),
                static_cast<unsigned long long>(audioStats.growls),
                static_cast<unsigned long long>(audioStats.ambienceStarts));
        if (m_ctx.ecs != nullptr) {
            std::ostringstream timings;
            timings << std::fixed << std::setprecision(2);
            bool first = true;
            for (const auto& entry : m_ctx.ecs->Systems().Entries()) {
                if (!entry.enabled) {
                    continue;
                }
                if (!first) {
                    timings << ' ';
                }
                first = false;
                timings << entry.name << '=' << entry.avgMs;
            }
            ZL_INFO("perf", "系统耗时(ms): {}", timings.str());
        }
        ZL_INFO("perf",
                "帧内耗时(ms): 流式={:.2f} 世界收集={:.2f} 实体收集={:.2f} 排序={:.2f} "
                "世界绘制={:.2f} 调试={:.2f} OnFrame={:.2f} Frame={:.2f} 条目={}",
                elapsedMs(frameStart, afterStreaming),
                elapsedMs(afterStreaming, afterWorldCollect),
                elapsedMs(afterWorldCollect, afterEntityCollect),
                elapsedMs(afterEntityCollect, afterSort),
                elapsedMs(afterSort, afterWorldDraw),
                elapsedMs(afterWorldDraw, afterDebugDraw),
                elapsedMs(frameStart, afterDebugDraw),
                m_ctx.loop != nullptr ? m_ctx.loop->LastFrameMs() : 0.0, m_items.size());
        if (m_ctx.commands != nullptr) {
            const net::CommandPipeline::Stats commandStats = m_ctx.commands->GetStats();
            const std::uint64_t serverTick =
                m_ctx.commands->HasSnapshot() ? m_ctx.commands->LastSnapshot().serverTick : 0;
            const std::uint64_t nowTick =
                m_ctx.loop != nullptr ? m_ctx.loop->TotalTicks() : 0;
            ZL_INFO("net",
                    "命令: 模式={} | 提交={} 执行={} 拒绝={} | ??????{}/{} | "
                    "队列={} 快照={} | 环回 tick 延迟={}",
                    net::TransportModeName(commandStats.mode),
                    static_cast<unsigned long long>(commandStats.commandsSubmitted),
                    static_cast<unsigned long long>(commandStats.commandsExecuted),
                    static_cast<unsigned long long>(commandStats.commandsRejected),
                    static_cast<unsigned long long>(commandStats.packetsSent),
                    static_cast<unsigned long long>(commandStats.packetsReceived),
                    commandStats.queuedCommands,
                    static_cast<unsigned long long>(commandStats.snapshotsReceived),
                    serverTick > 0 && nowTick >= serverTick ? nowTick - serverTick : 0);
        }
        if (m_ctx.networkClient != nullptr) {
            const NetworkClient::Stats netStats = m_ctx.networkClient->GetStats();
            ZL_INFO("net",
                    "客户?? 连接={} 玩家={} | 分配={} 快照={} 状??{} | 命令包发/??{}/{} | "
                    "会话 ????{}/{} 数据 ????{}/{} | 实体副本={}",
                    m_ctx.networkClient->IsConnected() ? "yes" : "no",
                    static_cast<unsigned long long>(m_ctx.networkClient->PlayerStableId()),
                    static_cast<unsigned long long>(netStats.assignments),
                    static_cast<unsigned long long>(netStats.snapshotsReceived),
                    static_cast<unsigned long long>(netStats.playerStatesReceived),
                    static_cast<unsigned long long>(netStats.commandPacketsSent),
                    static_cast<unsigned long long>(netStats.commandPacketsRejected),
                    static_cast<unsigned long long>(netStats.session.messagesSent),
                    static_cast<unsigned long long>(netStats.session.messagesReceived),
                    static_cast<unsigned long long>(netStats.session.dataSent),
                    static_cast<unsigned long long>(netStats.session.dataReceived),
                    m_ctx.networkClient->Replication().Actors().size());
        }
    }

    // 压测采样（巡航期间每 5s 一条 perf 行）
    m_perfTimer += static_cast<float>(frameDt);
    if (m_perfTimer >= kPerfLogInterval) {
        m_perfTimer = 0.0f;
        if (m_cruiseEnabled && !m_cruiseFinished) {
            LogPerfSample("cruise");
        }
    }
    RefreshHud();
    RefreshInventory();
}

void WorldDemoState::UpdateCruise(double frameDt) {
    if (!m_cruiseEnabled || m_cruiseFinished) {
        return;
    }
    m_cruiseElapsed += frameDt;
    m_cruiseAngle += static_cast<float>(frameDt) * (m_cruiseSpeed / m_cruiseRadius);
    if (m_cruiseAngle > 2.0f * 3.14159265f) {
        m_cruiseAngle -= 2.0f * 3.14159265f;
    }
    const float gx = m_cruiseCenterX + m_cruiseRadius * std::cos(m_cruiseAngle);
    const float gy = m_cruiseCenterY + m_cruiseRadius * std::sin(m_cruiseAngle);
    m_ctx.camera->SetCenter(render::ToScreen(gx, gy));

    if (m_cruiseElapsed >= m_cruiseMinutes * 60.0) {
        m_cruiseFinished = true;
        ApplyCameraMode();  // 巡航结束：回跟随模式恢复地图边界
        LogPerfSample("cruise-done");
        const auto& chunkStats = m_ctx.chunkManager->GetStats();
        ZL_INFO("perf",
                "巡航结束：{:.0f}s | chunk 累计 加载={} 卸载={} 取消={} 失败={} | 驻留={}（回到稳态）",
                m_cruiseElapsed, chunkStats.totalLoaded, chunkStats.totalUnloaded,
                chunkStats.totalCancelled, chunkStats.totalFailed, chunkStats.loadedChunks);
    }
}

void WorldDemoState::LogPerfSample(const char* tag) {
    const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
    const auto& chunkStats = m_ctx.chunkManager->GetStats();
    const double p50 = m_ctx.chunkManager->LoadMsPercentile(0.50);
    const double p95 = m_ctx.chunkManager->LoadMsPercentile(0.95);
    const double maxMs = m_ctx.chunkManager->LoadMsPercentile(1.0);
    ZL_INFO("perf",
            "[{}] t={:.0f}s | FPS={:.0f} | 实例={} 四边??{} | drawcall={} | chunk 驻留={} 加载??{} | "
            "单块解析 P50={:.2f}ms P95={:.2f}ms max={:.2f}ms（样本 {}）| 累计加载={} | "
            "物理 静??{} 动??{} step={:.2f}ms | 工作??{:.1f}MB",
            tag, m_cruiseElapsed, stats.fps, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
            stats.drawCalls, chunkStats.loadedChunks, chunkStats.loadingChunks, p50, p95, maxMs,
            m_ctx.chunkManager->RecentLoadMs().size(),
            static_cast<unsigned long long>(chunkStats.totalLoaded),
            m_ctx.physics != nullptr ? m_ctx.physics->StaticBodyCount() : 0u,
            m_ctx.physics != nullptr ? m_ctx.physics->DynamicBodyCount() : 0u,
            m_ctx.physics != nullptr ? m_ctx.physics->AvgStepMs() : 0.0,
            static_cast<double>(WorkingSetBytes()) / (1024.0 * 1024.0));
}

void WorldDemoState::HandleSleepInput(core::Input& input) {
    SubmitMoveCommand(0.0f, 0.0f);
    HaltPlayerForSleep();
    if (m_sleep.Active()) {
        if (input.IsActionPressed("Quit")) {
            FinishSleep(SleepStopReason::Cancelled);
        }
        return;
    }
    if (!m_sleepSelecting) {
        return;
    }
    if (input.IsActionPressed("Quit")) {
        m_sleepSelecting = false;
        m_sleepSelectUntilDawn = false;
        SetNotice("取消睡眠", kNoticeInfoColor, 1.5f);
        return;
    }
    if (input.IsActionPressed("Use")) {
        ConfirmSleep();
        return;
    }
    m_scrollAccum += input.ScrollDeltaY();
    if (m_scrollAccum >= 1.0f || m_scrollAccum <= -1.0f) {
        const int hoursCount =
            m_sleepConfig.maxHours - m_sleepConfig.minHours + 1;
        const int optionCount = hoursCount + 1;
        int index = m_sleepSelectUntilDawn
                        ? hoursCount
                        : m_sleepSelectedHours - m_sleepConfig.minHours;
        index = (index + (m_scrollAccum > 0.0f ? 1 : optionCount - 1)) %
                optionCount;
        if (index >= hoursCount) {
            m_sleepSelectUntilDawn = true;
        } else {
            m_sleepSelectUntilDawn = false;
            m_sleepSelectedHours = m_sleepConfig.minHours + index;
        }
        m_scrollAccum = 0.0f;
        SetNotice("睡眠??" +
                      (m_sleepSelectUntilDawn
                           ? std::string("睡到天亮")
                           : std::to_string(m_sleepSelectedHours) + " 小时"),
                  kNoticeInfoColor, 1.2f);
    }
}

void WorldDemoState::HandleFrameInput(double frameDt) {
    core::Input& input = *m_ctx.input;
    core::EventBus& events = *m_ctx.events;
    render::Camera& camera = *m_ctx.camera;

    if (m_ctx.ui != nullptr && m_ctx.ui->IsMenuOpen()) {
        m_characterOpen = false;
        SubmitMoveCommand(0.0f, 0.0f);
        return;
    }
    if (m_siegeReportOpen) {
        if (input.IsActionPressed("Quit")) {
            m_siegeReportOpen = false;
            ZL_INFO("siege", "关闭战报面板");
        }
        SubmitMoveCommand(0.0f, 0.0f);
        m_scrollAccum = 0.0f;
        return;
    }
    if (m_networkMode && m_networkSleep.phase != 0) {
        m_characterOpen = false;
        SubmitMoveCommand(0.0f, 0.0f);
        HaltPlayerForSleep();
        HandleNetworkSleepInput(input);
        return;
    }
    if (m_sleep.Active() || m_sleepSelecting) {
        m_characterOpen = false;
        HandleSleepInput(input);
        return;
    }
    if (m_dialogueOpen) {
        m_characterOpen = false;
        HandleDialogueInput(input);
        return;
    }
    if (m_tradeOpen) {
        m_characterOpen = false;
        HandleTradeInput(input);
        return;
    }
    if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
        m_characterOpen = false;
        if (m_forageAction.active) {
            CancelForageSearch("打开背包", true);
        }
        if (m_fishingAction.active) {
            CancelFishing("打开背包", true);
        }
        if (m_animalHarvestAction.active) {
            if (m_networkMode) {
                CancelNetworkAnimalHarvest();
            } else {
                CancelAnimalHarvest("打开背包", true);
            }
        }
    }

    if (input.IsActionPressed("CharacterPanel")) {
        if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
            m_ctx.ui->CloseInventory();
        }
        m_characterOpen = !m_characterOpen;
        SubmitMoveCommand(0.0f, 0.0f);
        m_scrollAccum = 0.0f;
        if (const ecs::Progression* progression = PlayerProgression();
            progression != nullptr) {
            ZL_INFO("progression", "角色面板：{}（Lv.{}，Perk {}，待选 {}）",
                    m_characterOpen ? "打开" : "关闭", progression->level,
                    progression->perks.size(), progression->perkPoints);
        } else {
            ZL_INFO("progression", "角色面板：{}（成长数据未就绪）",
                    m_characterOpen ? "打开" : "关闭");
        }
        return;
    }
    if (m_characterOpen) {
        if (input.IsActionPressed("Quit")) {
            m_characterOpen = false;
            return;
        }
        if (PendingPerkPoints() > 0) {
            for (int i = 0; i < 3; ++i) {
                if (input.IsActionPressed(kHotbarActions[i])) {
                    SelectPerk(i);
                    return;
                }
            }
        }
        SubmitMoveCommand(0.0f, 0.0f);
        return;
    }
    if (input.IsActionPressed("Vehicle")) {
        if (ToggleVehicle()) {
            SubmitMoveCommand(0.0f, 0.0f);
            return;
        }
    }
    if (m_riding && !m_driving) {
        if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
            return;
        }
        for (int i = 0; i < 5; ++i) {
            if (input.IsActionPressed(kHotbarActions[i]) &&
                m_hotbarSelected != i) {
                net::Command command =
                    MakePlayerCommand(net::CommandType::SelectHotbar);
                command.payload.index = i;
                SubmitPlayerCommand(command);
            }
        }
        if (input.IsActionPressed("Use")) {
            net::Command command =
                MakePlayerCommand(net::CommandType::UseBag);
            command.payload.index = -1;
            SubmitPlayerCommand(command);
        }
        if (input.IsActionPressed("Equip")) {
            net::Command command =
                MakePlayerCommand(net::CommandType::EquipBag);
            command.payload.index = -1;
            SubmitPlayerCommand(command);
        }
        if (input.IsActionPressed("Pickup") &&
            NetworkVehicleDriverSeatOpen()) {
            TakeDriverSeat();
            SubmitMoveCommand(0.0f, 0.0f);
            return;
        }
        if (input.IsActionPressed("Quit")) {
            OpenPauseMenu();
        }
        return;
    }
    if (m_driving) {
        if (m_ctx.ui != nullptr && m_ctx.ui->IsInventoryOpen()) {
            m_ctx.ui->CloseInventory();
        }
        glm::vec2 vehicleMove(0.0f, 0.0f);
        if (input.IsActionDown("MoveUp")) {
            vehicleMove.y -= 1.0f;
        }
        if (input.IsActionDown("MoveDown")) {
            vehicleMove.y += 1.0f;
        }
        if (input.IsActionDown("MoveLeft")) {
            vehicleMove.x -= 1.0f;
        }
        if (input.IsActionDown("MoveRight")) {
            vehicleMove.x += 1.0f;
        }
        SubmitMoveCommand(vehicleMove.x, vehicleMove.y);
        m_scrollAccum = 0.0f;
        if (input.IsActionPressed("Headlights")) {
            ToggleVehicleHeadlights();
        }
        if (input.IsActionPressed("RepairStructure")) {
            HonkVehicleHorn();
        }
        if (input.IsActionPressed("Pickup") &&
            NetworkVehicleHasPassenger() &&
            VehicleSpeed(m_vehicleMotion) <=
                m_vehicleTuning.seatHandoffMaxSpeed) {
            GiveDriverSeat();
        }
        if (input.IsActionPressed("Quit")) {
            OpenPauseMenu();
        }
        return;
    }
    if (PendingPerkPoints() > 0) {
        for (int i = 0; i < 3; ++i) {
            if (input.IsActionPressed(kHotbarActions[i])) {
                SelectPerk(i);
                return;
            }
        }
    }
    if (!m_networkMode && input.IsActionPressed("MissionFocus")) {
        CycleMissionFocus();
    }
    if (!m_networkMode && input.IsActionPressed("MissionAbandon")) {
        AbandonFocusedMission();
    }

    // M12-T1：WASD 也先变成命令（Move：屏幕方向）；InputSystem 在固定链里转成 MoveIntent。
    glm::vec2 moveScreen(0.0f, 0.0f);
    if (input.IsActionDown("MoveUp")) {
        moveScreen.y -= 1.0f;
    }
    if (input.IsActionDown("MoveDown")) {
        moveScreen.y += 1.0f;
    }
    if (input.IsActionDown("MoveLeft")) {
        moveScreen.x -= 1.0f;
    }
    if (input.IsActionDown("MoveRight")) {
        moveScreen.x += 1.0f;
    }
    if (m_forageAction.active || m_fishingAction.active ||
        m_animalHarvestAction.active) {
        moveScreen = glm::vec2(0.0f, 0.0f);
    }
    SubmitMoveCommand(moveScreen.x, moveScreen.y);

    for (int i = 0; i < 5; ++i) {
        if (input.IsActionPressed(kHotbarActions[i]) && m_hotbarSelected != i) {
            net::Command command = MakePlayerCommand(net::CommandType::SelectHotbar);
            command.payload.index = i;
            SubmitPlayerCommand(command);
        }
    }

    if (input.IsActionPressed("BuildMode")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("BuildMode"));
        ToggleBuildMode();
    }
    if (input.IsActionPressed("RepairStructure")) {
        events.Emit(EventProbe::kInputActionEvent,
                    std::string("RepairStructure"));
        TryRepairNearestBarricade();
    }
    if (input.IsActionPressed("RepairAllStructures")) {
        events.Emit(EventProbe::kInputActionEvent,
                    std::string("RepairAllStructures"));
        TryRepairAllStructures();
    }
    if (input.IsActionPressed("DemolishStructure")) {
        events.Emit(EventProbe::kInputActionEvent,
                    std::string("DemolishStructure"));
        TryDemolishNearestStructure();
    }
    if (input.IsActionPressed("Quit")) {
        if (m_buildMode) {
            m_buildMode = false;
            m_pendingDemolish = {};
            ZL_INFO("building", "退出建造模式（ESC）");
        SetNotice("退出建造模式", kNoticeInfoColor, 1.2f);
            return;
        }
        ZL_INFO("render", "ESC 打开暂停菜单");
        events.Emit(EventProbe::kInputActionEvent, std::string("Quit"));
        OpenPauseMenu();
        return;
    }
    if (input.IsActionPressed("Pause")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("Pause"));
        core::GameTime& time = *m_ctx.gameTime;
        if (time.GetSpeed() == core::GameTime::Speed::Paused) {
            time.SetSpeed(core::GameTime::Speed::X1);
        } else {
            time.SetSpeed(core::GameTime::Speed::Paused);
        }
        ZL_INFO("render", "F1 暂停切换 → 当前 {}", core::GameTime::SpeedName(time.GetSpeed()));
        events.Emit(EventProbe::kSpeedChangedEvent, time.SpeedScale());
    }
    if (input.IsActionPressed("CycleSpeed")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("CycleSpeed"));
        m_ctx.gameTime->CycleSpeed();
        ZL_INFO("render", "F2 倍速循????当前 {}",
                core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()));
        events.Emit(EventProbe::kSpeedChangedEvent, m_ctx.gameTime->SpeedScale());
    }
    if (input.IsActionPressed("DemoKill")) {
        KillNearestDemoEntity();  // M5-T1：演示击杀（HP 归零 ??尸体??
    }
    // M30-T4：格挡按??松开；闪避按一次。M30-T6 网络模式走服务器命令??
    const bool blockDown = input.IsActionDown("Block");
    const bool blockChanged = blockDown != m_blockInputDown;
    m_blockInputDown = blockDown;
    if (m_networkMode) {
        if (blockChanged) {
            net::Command command = MakePlayerCommand(net::CommandType::Block);
            if (blockDown) {
                command.payload.flags |= net::kBlockDownFlag;
            }
            SubmitPlayerCommand(command);
        }
    } else {
        PlayerBlock(blockDown, blockChanged);
    }
    if (input.IsActionPressed("Dodge")) {
        if (m_networkMode) {
            net::Command command = MakePlayerCommand(net::CommandType::Dodge);
            if (std::isfinite(moveScreen.x) && std::isfinite(moveScreen.y) &&
                (std::abs(moveScreen.x) > 0.0001f ||
                 std::abs(moveScreen.y) > 0.0001f)) {
                const glm::vec2 grid = glm::normalize(
                    characters::InputSystem::ScreenDirToGridDir(
                        glm::normalize(glm::vec2(moveScreen.x, moveScreen.y))));
                command.payload.x = grid.x;
                command.payload.y = grid.y;
            }
            SubmitPlayerCommand(command);
        } else {
            PlayerDodge(moveScreen.x, moveScreen.y);
        }
    }
    if (input.IsActionPressed("Attack") ||
        input.IsActionPressed("HeavyAttack")) {
        const bool heavy = input.IsActionPressed("HeavyAttack");
        if (m_networkMode &&
            (m_networkAnimalHarvest.active ||
             m_animalHarvestAction.active)) {
            CancelNetworkAnimalHarvest();
        } else if (m_networkMode &&
            (m_networkSurvival.mode != net::kPlayerSurvivalModeNone ||
             m_forageAction.active || m_fishingAction.active)) {
            const bool fishing =
                m_networkSurvival.mode == net::kPlayerSurvivalModeFishing ||
                (m_networkSurvival.mode == net::kPlayerSurvivalModeNone &&
                 m_fishingAction.active);
            SubmitPlayerCommand(MakePlayerCommand(
                fishing ? net::CommandType::FishingCancel
                        : net::CommandType::ForageCancel));
        } else if (m_forageAction.active) {
            CancelForageSearch("攻击", true);
        } else if (m_fishingAction.active) {
            CancelFishing("攻击", true);
        } else if (m_animalHarvestAction.active) {
            CancelAnimalHarvest("攻击", true);
        } else {
            net::Command command = MakePlayerCommand(net::CommandType::Attack);
            if (heavy) {
                command.payload.flags |= net::kAttackHeavyFlag;
            }
            SubmitPlayerCommand(command);
        }
    }
    if (input.IsActionPressed("Pickup")) {
        SubmitPlayerCommand(MakePlayerCommand(net::CommandType::PickupNearest));
    }
    if (input.IsActionPressed("FarmSeed")) {
        CycleFarmSeed();
    }
    if (input.IsActionPressed("FarmCare")) {
        TryFarmCare();
    }
    if (input.IsActionPressed("Use")) {
        if (m_networkMode &&
            (m_networkAnimalHarvest.active ||
             m_animalHarvestAction.active)) {
            CancelNetworkAnimalHarvest();
        } else if (m_networkMode &&
            (m_networkSurvival.mode != net::kPlayerSurvivalModeNone ||
             m_forageAction.active || m_fishingAction.active)) {
            const bool fishing =
                m_networkSurvival.mode == net::kPlayerSurvivalModeFishing ||
                (m_networkSurvival.mode == net::kPlayerSurvivalModeNone &&
                 m_fishingAction.active);
            if (!fishing) {
                SubmitPlayerCommand(
                    MakePlayerCommand(net::CommandType::ForageCancel));
            } else if (m_networkSurvival.phase ==
                       net::kPlayerSurvivalPhaseBiting) {
                SubmitPlayerCommand(
                    MakePlayerCommand(net::CommandType::FishingReel));
            } else {
                SubmitPlayerCommand(
                    MakePlayerCommand(net::CommandType::FishingCancel));
            }
        } else if (m_forageAction.active) {
            CancelForageSearch("主动取消", true);
        } else if (m_fishingAction.active) {
            if (m_fishingAction.biting) {
                const FishingSpot spot{
                    m_fishingAction.spotId, m_fishingAction.tableId,
                    m_fishingAction.x, m_fishingAction.y, m_fishingAction.z,
                    m_fishingAction.waterX, m_fishingAction.waterY,
                    m_fishingAction.waterZ};
                const bool caught = FishingAttemptSucceeds(
                    m_ctx.forage != nullptr ? m_ctx.forage->Seed() : 0u, spot,
                    m_fishingAction.catchChance);
                ResolveFishing(caught);
            } else {
                CancelFishing("主动取消", true);
            }
        } else if (m_animalHarvestAction.active) {
            CancelAnimalHarvest("主动取消", true);
        } else if (!TryServiceVehicle()) {
            PlayerBuildTarget bedTarget;
            PlayerBuildTarget doorTarget;
            const bool hasBed = FindNearbyBed(&bedTarget);
            const bool hasDoor = FindNearbyDoor(&doorTarget);
            bool handled = !m_buildMode && TryInteractSettlementNpc();
            if (!handled && hasDoor &&
                (!hasBed ||
                 BuildTargetDistanceSq(doorTarget) <
                     BuildTargetDistanceSq(bedTarget))) {
                handled = TryToggleNearestDoor();
            }
            if (!handled) {
                handled = TryBeginSleep();
            }
            if (!handled) {
                handled = TryToggleNearestDoor();
            }
            if (!handled) {
                handled = TryToggleCampfireFire();
            }
            if (!handled) {
                handled = TryInteractHomestead();
            }
            if (!handled) {
                handled = TryStartAnimalHarvest();
            }
            if (!handled) {
                handled = TryStartFishing();
            }
            if (!handled) {
                handled = TryStartForageSearch();
            }
            if (!handled && !TryInteractRadio()) {
                net::Command command =
                    MakePlayerCommand(net::CommandType::UseBag);
                command.payload.index = -1;
                SubmitPlayerCommand(command);
            }
        }
    }
    if (input.IsActionPressed("Equip")) {
        if (m_buildMode) {
            ToggleBuildOrientation();
        } else if (m_networkMode) {
            // M23-F1-T6：主手镜像显示远程武器时请求服务器换弹，否则沿用装备切换。
            if (TryGetEquippedRangedWeapon(nullptr, nullptr)) {
                SubmitPlayerCommand(
                    MakePlayerCommand(net::CommandType::ReloadRanged));
            } else {
                net::Command command =
                    MakePlayerCommand(net::CommandType::EquipBag);
                command.payload.index = -1;
                SubmitPlayerCommand(command);
            }
        } else if (TryStartRangedReload()) {
            // M23-F1-T3：主手为远程武器时 R 优先换弹；卸下走背包 UI。
        } else {
            net::Command command = MakePlayerCommand(net::CommandType::EquipBag);
            command.payload.index = -1;
            SubmitPlayerCommand(command);
        }
    }
    if (input.IsActionPressed("Drop")) {
        net::Command command = MakePlayerCommand(net::CommandType::DropBag);
        command.payload.index = -1;
        SubmitPlayerCommand(command);
    }
    if (input.IsActionPressed("QuickSave")) {
        RequestQuickSave(true);  // M10-T5：F12 快照 ??截图 ??后台原子写盘
    }
    if (input.IsActionPressed("DemoGunshot")) {
        SubmitPlayerCommand(MakePlayerCommand(net::CommandType::Gunshot));
    }
    if (input.IsActionPressed("DemoProjectile")) {
        if (!m_networkMode) {
            if (TryGetEquippedRangedWeapon(nullptr, nullptr)) {
                PlayerRangedAttack(glm::vec2(input.MouseX(), input.MouseY()));
            } else {
                SetNotice("fire: no ranged weapon equipped", kNoticeFailColor, 1.5f);
            }
        } else {
            float dx = 0.0f;
            float dy = 0.0f;
            if (!TryGetEquippedRangedWeapon(nullptr, nullptr)) {
                SetNotice("fire: no ranged weapon equipped", kNoticeFailColor, 1.5f);
            } else if (TryGetAimDirection(glm::vec2(input.MouseX(), input.MouseY()),
                                          &dx, &dy)) {
                net::Command command =
                    MakePlayerCommand(net::CommandType::FireRanged);
                command.payload.x = dx;
                command.payload.y = dy;
                SubmitPlayerCommand(command);
            }
        }
    }
    if (input.IsActionPressed("ReloadBindings")) {
        events.Emit(EventProbe::kInputActionEvent, std::string("ReloadBindings"));
        m_ctx.reloadConfigAndBindings();
    }
    if (input.IsActionPressed("ToggleDebugOverlay")) {
        m_showGrid = !m_showGrid;
        ZL_INFO("render", "F9 网格叠加: {}", m_showGrid ? "开" : "关");
    }
    if (input.IsActionPressed("TogglePhysicsDebug")) {
        m_physicsDebug = !m_physicsDebug;
        m_wireDiagCount = 0;
        if (m_ctx.physics != nullptr) {
            m_ctx.physics->SetDebugDraw(m_physicsDebug);
        }
        ZL_INFO("physics", "F10 物理线框: {}", m_physicsDebug ? "开" : "关");
    }
    if (input.IsActionPressed("ToggleFreeCam")) {
        m_followCamera = !m_followCamera;
        ApplyCameraMode();
        ZL_INFO("render", "F4 相机模式: {}（{}）", m_followCamera ? "跟随玩家" : "自由",
                m_followCamera ? "边界=地图 AABB" : "无边界，方向键/拖拽平移");
    }
    if (input.IsActionPressed("ToggleCruise")) {
        m_cruiseEnabled = !m_cruiseEnabled;
        m_cruiseFinished = false;
        m_cruiseElapsed = 0.0;
        m_cruiseAngle = 0.0f;
        if (m_cruiseEnabled) {
            // 从圆周起点开始：以当前相机位置为圆心
            const glm::vec2 camGrid = render::FromScreen(camera.Center(), 0);
            m_cruiseCenterX = camGrid.x;
            m_cruiseCenterY = camGrid.y;
        }
        ApplyCameraMode();  // 巡航=无边界；结束回跟??恢复地图边界
        ZL_INFO("perf", "F8 自动巡航: {}（{:.1f} 分钟，速度 {:.0f} 格/s，半径 {:.0f}）",
                m_cruiseEnabled ? "开" : "??", m_cruiseMinutes, m_cruiseSpeed, m_cruiseRadius);
    }

    // 平移：方向键（仅自由相机模式；M4-T5 跟随模式下由 UpdateCameraFollow 接管）
    glm::vec2 move(0.0f, 0.0f);
    if (!m_followCamera) {
        if (input.IsActionDown("PanUp")) {
            move.y -= 1.0f;
        }
        if (input.IsActionDown("PanDown")) {
            move.y += 1.0f;
        }
        if (input.IsActionDown("PanLeft")) {
            move.x -= 1.0f;
        }
        if (input.IsActionDown("PanRight")) {
            move.x += 1.0f;
        }
    }
    if (move.x != 0.0f || move.y != 0.0f) {
        camera.PanBy(glm::normalize(move) * (kPanSpeedPxPerSecond * static_cast<float>(frameDt) /
                                             camera.Zoom()));
    }

    // 拖拽平移（右键/中键；自由相机模式）
    const glm::vec2 mouse(input.MouseX(), input.MouseY());
    const bool dragging = !m_followCamera && (input.IsMouseButtonDown(1) || input.IsMouseButtonDown(2));
    if (dragging && m_prevMouseValid) {
        camera.PanBy(-(mouse - m_prevMouse) / camera.Zoom());
    }
    m_prevMouse = mouse;
    m_prevMouseValid = true;

    // 滚轮：建造模式切换建造项，否则缩放（挡位吸附）
    m_scrollAccum += input.ScrollDeltaY();
    if (m_buildMode && !m_buildables.empty()) {
        if (m_scrollAccum >= 1.0f || m_scrollAccum <= -1.0f) {
            const int count = static_cast<int>(m_buildables.size());
            m_buildIndex = (m_buildIndex + (m_scrollAccum > 0.0f ? 1 : count - 1)) % count;
            const BuildableDef* def = SelectedBuildable();
            ZL_INFO("building", "建造项切换：{}（{} 木材）", def->id, def->woodCost);
            SetNotice(BuildMenuText() + "??" +
                          std::to_string(def->woodCost) + " 木材??",
                      kNoticeInfoColor, 1.5f);
            m_scrollAccum = 0.0f;
        }
    } else if (m_scrollAccum >= 1.0f) {
        camera.ZoomStep(1);
        m_scrollAccum = 0.0f;
    } else if (m_scrollAccum <= -1.0f) {
        camera.ZoomStep(-1);
        m_scrollAccum = 0.0f;
    }

    // 悬停拾取（无限世界：hit 恒真）
    const render::GridPick pick = PickMouseCell(mouse);
    m_hoverHit = pick.hit;
    m_hoverX = pick.x;
    m_hoverY = pick.y;

    // 左键：建造模式放置，否则点击移动（MoveTo → 服务器校验 → PathRequest）。
    // HUD 的脚本文档是全屏的，UI 可能把同一次世界点击标记为“已消费”；
    // 建造模式在光标未悬停可交互 UI 元素时允许绕过该阻断，避免放置被吞。
    const bool buildClick =
        m_buildMode &&
        (input.IsMouseButtonPressed(0) ||
         (input.IsMouseButtonPressedRaw(0) &&
          (m_ctx.ui == nullptr || !m_ctx.ui->IsPointerOverUi())));
    if (buildClick) {
        ZL_INFO("building", "建造左键：格 ({}, {})", pick.x, pick.y);
        TryPlaceSelectedBuild(pick.x, pick.y);
    } else if (input.IsMouseButtonPressed(0) &&
               !(m_cruiseEnabled && !m_cruiseFinished) && m_player != entt::null &&
               !m_forageAction.active && !m_fishingAction.active &&
               !m_animalHarvestAction.active) {
        net::Command command = MakePlayerCommand(net::CommandType::MoveTo);
        command.payload.x = static_cast<float>(pick.x);
        command.payload.y = static_cast<float>(pick.y);
        SubmitPlayerCommand(command);
    }
}

glm::vec2 WorldDemoState::MouseToFramebuffer(const glm::vec2& mousePx) const {
    const int winW = std::max(1, m_ctx.window->Width());
    const int winH = std::max(1, m_ctx.window->Height());
    const float sx = static_cast<float>(m_ctx.renderer->FramebufferWidth()) /
                     static_cast<float>(winW);
    const float sy = static_cast<float>(m_ctx.renderer->FramebufferHeight()) /
                     static_cast<float>(winH);
    return {mousePx.x * sx, mousePx.y * sy};
}

render::GridPick WorldDemoState::PickMouseCell(const glm::vec2& mousePx) const {
    const glm::vec2 fb = MouseToFramebuffer(mousePx);
    const glm::vec2 world = m_ctx.camera->ScreenToWorld(fb);
    return render::PickCellUnbounded(world, 0);
}

world::Chunk* WorldDemoState::FindChunk(int chunkX, int chunkY) const {
    return FindChunkInWorld(*m_ctx.world, chunkX, chunkY);
}

void WorldDemoState::PushObject(const world::SquareObject& object, int wx, int wy, int z,
                                world::SquareLayer layer, int roomId) {
    if (m_cachedSprite.empty() || object.sprite != m_cachedSprite) {
        m_cachedSprite = object.sprite;
        m_cachedFrames = m_ctx.assets->Frames(object.sprite);
    }
    const std::vector<AssetProvider::Entry>* frames = m_cachedFrames;
    if (frames == nullptr || frames->empty()) {
        return;  // 逻辑名缺失（应在 manifest 检查，运行时静默跳过）
    }
    const AssetProvider::Entry& entry =
        (*frames)[render::HashPick(wx, wy, static_cast<uint32_t>(layer)) % frames->size()];
    if (entry.atlas == nullptr || entry.frame == nullptr) {
        return;
    }
    const render::AtlasFrame& frame = *entry.frame;

    render::DrawItem item;
    item.texture = entry.atlas->Texture();
    item.rect = render::SpriteDrawRect(static_cast<float>(wx), static_cast<float>(wy), z, frame.fx,
                                       frame.fy, frame.ox, frame.oy, frame.w, frame.h);
    item.rect.x += object.offsetX;
    item.rect.y += object.offsetY;
    item.u0 = frame.u0;
    item.v0 = frame.v0;
    item.u1 = frame.u1;
    item.v1 = frame.v1;
    item.color = z >= 1 ? kUpperZColor : 0xffffffff;
    // 两段式：仅 z=0 的地面进"地面批"；其余（含 z≥1 的一切）走全局画家序
    item.stage = (z == 0 && layer == world::SquareLayer::Floor) ? 0 : 1;
    item.z = z;
    item.diag = wx + wy;
    item.gx = wx;
    item.layer = static_cast<int>(layer);
    item.tieBreaker = 0;  // 世界对象同格同层视为无序????
    item.roomId = roomId;
    m_items.push_back(item);
}

void WorldDemoState::CollectWorld() {
    m_items.clear();
    m_cachedSprite = {};
    m_cachedFrames = nullptr;
    m_collectedChunks = 0;
    m_missingChunks = 0;

    render::Camera& camera = *m_ctx.camera;
    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(192.0f, 512.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;

    // 可见范围换算到格坐标（四角反解，留 1 格余量；含树高/墙高余量已在上面的 half 里）
    const glm::vec2 g0 = render::FromScreen(minW, 0);
    const glm::vec2 g1 = render::FromScreen(maxW, 0);
    const glm::vec2 g2 = render::FromScreen({minW.x, maxW.y}, 0);
    const glm::vec2 g3 = render::FromScreen({maxW.x, minW.y}, 0);
    m_view.gxMin = static_cast<int>(std::floor(std::min({g0.x, g1.x, g2.x, g3.x}))) - 1;
    m_view.gxMax = static_cast<int>(std::ceil(std::max({g0.x, g1.x, g2.x, g3.x}))) + 1;
    m_view.gyMin = static_cast<int>(std::floor(std::min({g0.y, g1.y, g2.y, g3.y}))) - 1;
    m_view.gyMax = static_cast<int>(std::ceil(std::max({g0.y, g1.y, g2.y, g3.y}))) + 1;
    const int gxMin = m_view.gxMin;
    const int gxMax = m_view.gxMax;
    const int gyMin = m_view.gyMin;
    const int gyMax = m_view.gyMax;

    const int cx0 = world::FloorDiv(gxMin, core::kChunkSize);
    const int cx1 = world::FloorDiv(gxMax, core::kChunkSize);
    const int cy0 = world::FloorDiv(gyMin, core::kChunkSize);
    const int cy1 = world::FloorDiv(gyMax, core::kChunkSize);

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            world::Chunk* chunk = FindChunk(cx, cy);
            if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
                ++m_missingChunks;  // 后台还在加载：本帧跳过（下帧补齐）
                continue;
            }
            ++m_collectedChunks;
            const std::uint8_t zMask = chunk->NonEmptyZMask();

            // 逐 z 层收集（z=0 地面起步；z≥1 楼上，空层快速跳过）
            for (int z = 0; z < core::kMaxZ; ++z) {
                if ((zMask & static_cast<std::uint8_t>(1u << z)) == 0) {
                    continue;
                }
                for (int ly = 0; ly < core::kChunkSize; ++ly) {
                    const int wy = cy * core::kChunkSize + ly;
                    if (wy < gyMin || wy > gyMax) {
                        continue;
                    }
                    for (int lx = 0; lx < core::kChunkSize; ++lx) {
                        const int wx = cx * core::kChunkSize + lx;
                        if (wx < gxMin || wx > gxMax) {
                            continue;
                        }
                        const world::GridSquare& square = chunk->Square(lx, ly, z);
                        if (square.IsEmpty()) {
                            continue;
                        }
                        // 所有层共用地面格的房间 ID；室内剖切据此只隐藏当前房间上方的楼层。
                        const int groundRoomId = chunk->Square(lx, ly, 0).RoomId();
                        for (int layerIndex = 0; layerIndex < world::kSquareLayerCount;
                             ++layerIndex) {
                            const auto layer = static_cast<world::SquareLayer>(layerIndex);
                            for (const world::SquareObject& object : square.Layer(layer)) {
                                PushObject(object, wx, wy, z, layer, groundRoomId);
                            }
                        }
                    }
                }
            }
        }
    }

    // 排序：地面批 + 对象批统一画家序（含 M4-T2 追加的实体高度精灵），见 OnFrame
}

int WorldDemoState::RoomIdAt(int wx, int wy, int z) const {
    if (m_ctx.world == nullptr) {
        return -1;
    }
    const world::ChunkCoord cc = world::ChunkOf(wx, wy);
    const world::Chunk* chunk = FindChunk(cc.cx, cc.cy);
    if (chunk == nullptr || chunk->State() != world::ChunkState::Loaded) {
        return -1;
    }
    const world::SquareLocal local = world::SquareLocalOf(wx, wy);
    return chunk->Square(local.lx, local.ly, z).RoomId();
}

int WorldDemoState::PlayerRoomId() const {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return -1;
    }
    const auto* pos = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (pos == nullptr || pos->z != 0 || !std::isfinite(pos->x) || !std::isfinite(pos->y)) {
        return -1;
    }
    const int cx = static_cast<int>(std::floor(pos->x));
    const int cy = static_cast<int>(std::floor(pos->y));
    const int direct = RoomIdAt(cx, cy, 0);
    if (direct >= 0) {
        return direct;
    }

    // 角色贴墙/卡在房间角落时，碰撞体会把中心推到墙格（地面格 RoomId=-1），
    // 但身体仍有一部分在房间里。此时在 3×3 格内找最近的有房间 ID 的格，
    // 距离限制约一个碰撞半径，避免站在屋外也误触发剖切。
    constexpr float kRoomProbeDistance = 0.75f;
    constexpr float kRoomProbeDistanceSq = kRoomProbeDistance * kRoomProbeDistance;
    int nearestRoom = -1;
    float nearestDistSq = kRoomProbeDistanceSq;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int wx = cx + dx;
            const int wy = cy + dy;
            if (RoomIdAt(wx, wy, 0) < 0) {
                continue;
            }
            const float nearestX =
                std::clamp(pos->x, static_cast<float>(wx), static_cast<float>(wx + 1));
            const float nearestY =
                std::clamp(pos->y, static_cast<float>(wy), static_cast<float>(wy + 1));
            const float ddx = pos->x - nearestX;
            const float ddy = pos->y - nearestY;
            const float distSq = ddx * ddx + ddy * ddy;
            if (distSq <= nearestDistSq) {
                nearestDistSq = distSq;
                nearestRoom = RoomIdAt(wx, wy, 0);
            }
        }
    }
    return nearestRoom;
}

void WorldDemoState::CutawayRoomWalls(int roomId, std::size_t playerIndex) {
    if (roomId < 0) {
        return;
    }
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        render::DrawItem& item = m_items[i];
        if (item.stage != 1) {
            continue;
        }
        if (item.z == 0) {
            // 地面层只切玩家前方（画家序之后）的边界墙。
            if (i <= playerIndex ||
                item.layer != static_cast<int>(world::SquareLayer::Wall)) {
                continue;
            }
        } else if (item.z >= 1) {
            // 楼上没有独立房间 ID：地基格是墙时取四邻接房间，剖掉房间边界上的楼上墙。
            if (item.layer != static_cast<int>(world::SquareLayer::Wall) ||
                item.roomId == roomId) {
                continue;  // 本房间上方楼板，已由 CutawayUpperLayersInRoom 处理
            }
        } else {
            continue;
        }
        const int wx = item.gx;
        const int wy = item.diag - item.gx;
        // 用 3×3 邻域而不是四邻接：房屋外墙的转角格只与房间对角相邻，
        // 站在房间角落时仍会挡住角色，必须一起剖掉。
        bool touchesRoom = false;
        for (int dy = -1; dy <= 1 && !touchesRoom; ++dy) {
            for (int dx = -1; dx <= 1 && !touchesRoom; ++dx) {
                touchesRoom = RoomIdAt(wx + dx, wy + dy, 0) == roomId;
            }
        }
        if (!touchesRoom) {
            continue;
        }
        item.color &= 0x00FFFFFFu;
    }
}

void WorldDemoState::ApplyWallOcclusion() {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    const auto* pos = m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (pos == nullptr || pos->z != 0) {
        return;
    }
    const std::uint64_t playerId = m_ctx.ecs->IdOf(m_player).id;
    std::size_t playerIndex = m_items.size();
    const render::DrawItem* playerItem = nullptr;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const render::DrawItem& item = m_items[i];
        if (item.layer >= RenderCollectSystem::kEntityLayerBase &&
            item.tieBreaker == playerId) {
            playerIndex = i;
            playerItem = &item;
            break;
        }
    }
    if (playerItem == nullptr) {
        return;
    }
    if (m_roomCutaway) {
        const int roomId = PlayerRoomId();
        if (roomId >= 0) {
            // 房间内：先剖掉本房间上方楼层，再移除玩家前方/相交的边界墙（含楼上边界墙）。
            RenderCollectSystem::CutawayUpperLayersInRoom(roomId, m_items);
            CutawayRoomWalls(roomId, playerIndex);
            RenderCollectSystem::FadeOccludingWalls(playerItem->rect, playerIndex, 0, m_items);
            return;
        }
    }
    const std::uint32_t alpha = static_cast<std::uint32_t>(
        std::clamp(m_wallOcclusionAlpha, 0.05f, 1.0f) * 255.0f + 0.5f);
    RenderCollectSystem::FadeOccludingWalls(playerItem->rect, playerIndex, alpha, m_items);
}

void WorldDemoState::DrawWorld() {
    render::Camera& camera = *m_ctx.camera;
    glm::mat4 view = camera.ViewMatrix();
    const VehicleScreenShake shake = CurrentVehicleScreenShake();
    view[3][0] -= shake.x;
    view[3][1] -= shake.y;
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::World), &view, &proj);

    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(192.0f, 512.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;

    render::SpriteBatch& batch = *m_ctx.sprites;
    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::World));
    for (const render::DrawItem& item : m_items) {
        const render::SpriteRect& rect = item.rect;
        if (rect.x + rect.w < minW.x || rect.x > maxW.x || rect.y + rect.h < minW.y ||
            rect.y > maxW.y) {
            continue;
        }
        batch.DrawSprite(item.texture, rect, item.u0, item.v0, item.u1, item.v1, item.color);
        if (item.stage == 1) {
            const auto found =
                m_vehicleDamageVisuals.find(item.tieBreaker);
            if (found != m_vehicleDamageVisuals.end()) {
                DrawVehicleDamageVisual(batch, item.rect, found->second);
            }
        }
    }
    DrawVehicleHeadlights(batch);
    DrawVehicleDamageSmoke(batch);
    DrawVehicleHitFeedback(batch);
    DrawVehicleMetalDebris(batch);
    DrawAnimalCorpseHighlight(batch);
    DrawCombatOverlay(batch);
    DrawFireOverlay(batch);
    batch.End();
}

void WorldDemoState::DrawVehicleHeadlights(render::SpriteBatch& batch) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const bool night = m_ctx.gameTime != nullptr && m_ctx.gameTime->IsNight();
    const float strength = night ? 1.0f : 0.28f;
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    constexpr int kRays = 18;
    constexpr float kHalfAngle = 0.38f;
    const std::uint32_t rayColor =
        withAlpha(0x3030E0FFu, 0.060f * strength);
    const std::uint32_t arcColor =
        withAlpha(0x3030E0FFu, 0.28f * strength);
    const std::uint32_t lampColor =
        withAlpha(0xFF30E0FFu, 0.55f + 0.45f * strength);
    const auto drawLights = [&](const ecs::Position& position,
                                float headingX, float headingY) {
        const float headingLength =
            std::sqrt(headingX * headingX + headingY * headingY);
        if (!std::isfinite(headingLength) || headingLength < 0.0001f) {
            headingX = 0.0f;
            headingY = 1.0f;
        } else {
            headingX /= headingLength;
            headingY /= headingLength;
        }
        const glm::vec2 origin =
            render::ToScreen(position.x + headingX * 0.55f,
                             position.y + headingY * 0.55f, position.z) -
            glm::vec2(0.0f, 18.0f);
        const glm::vec2 farPoint =
            render::ToScreen(position.x + headingX * 6.2f,
                             position.y + headingY * 6.2f, position.z) -
            glm::vec2(0.0f, 18.0f);
        const glm::vec2 centerDir = farPoint - origin;
        const float length = glm::length(centerDir);
        if (!std::isfinite(length) || length < 4.0f) {
            return;
        }
        const glm::vec2 dir = centerDir / length;
        const glm::vec2 side(-dir.y, dir.x);
        glm::vec2 previous = origin + centerDir;
        for (int i = 0; i < kRays; ++i) {
            const float t = static_cast<float>(i) /
                            static_cast<float>(kRays - 1);
            const float angle = -kHalfAngle + 2.0f * kHalfAngle * t;
            const glm::vec2 point =
                origin + dir * (length * std::cos(angle)) +
                side * (length * std::sin(angle));
            batch.DrawLine(origin, point, 10.0f, rayColor);
            if (i > 0) {
                batch.DrawLine(previous, point, 2.0f, arcColor);
            }
            previous = point;
        }
        const glm::vec2 lamp =
            render::ToScreen(position.x + headingX * 0.35f,
                             position.y + headingY * 0.35f, position.z) -
            glm::vec2(0.0f, 16.0f);
        for (const float offset : {-7.0f, 7.0f}) {
            const glm::vec2 dot = lamp + side * offset;
            batch.DrawRectFill(dot.x - 4.0f, dot.y - 3.0f, 8.0f, 6.0f,
                               lampColor);
        }
    };
    registry.view<ecs::Position, ecs::Vehicle>().each(
        [&](const ecs::Entity entity, const ecs::Position& position,
            const ecs::Vehicle& state) {
            if (!state.headlights ||
                !m_view.Contains(position.x, position.y)) {
                return;
            }
            float headingX = 0.0f;
            float headingY = 1.0f;
            bool hasHeading = false;
            if (entity == m_vehicle) {
                headingX = m_vehicleHeadingX;
                headingY = m_vehicleHeadingY;
                hasHeading =
                    std::hypot(headingX, headingY) > 0.05f;
            }
            if (const auto* velocity =
                    registry.try_get<ecs::Velocity>(entity);
                velocity != nullptr &&
                std::hypot(velocity->x, velocity->y) > 0.05f) {
                headingX = velocity->x;
                headingY = velocity->y;
                hasHeading = true;
            }
            if (!hasHeading) {
                const std::uint64_t stableId =
                    m_ctx.ecs->IdOf(entity).id;
                const auto found =
                    m_vehicleVisualDirections.find(stableId);
                VehicleVisualDirectionVector(
                    found != m_vehicleVisualDirections.end()
                        ? found->second
                        : VehicleVisualDirection::South,
                    &headingX, &headingY);
            }
            drawLights(position, headingX, headingY);
        });
}

void WorldDemoState::DrawAnimalCorpseHighlight(render::SpriteBatch& batch) {
    if (m_ctx.ecs == nullptr || m_ctx.animals == nullptr ||
        m_player == entt::null) {
        return;
    }
    if (m_ctx.ui != nullptr &&
        (m_ctx.ui->IsInventoryOpen() || m_ctx.ui->IsMenuOpen())) {
        return;
    }
    if (m_characterOpen) {
        return;
    }
    ecs::Entity corpseEntity = entt::null;
    const AnimalDef* def =
        FindNearestAnimalCorpse(m_inventoryRange, &corpseEntity);
    if (def == nullptr || corpseEntity == entt::null) {
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(corpseEntity);
    if (position == nullptr) {
        return;
    }
    const glm::vec2 center = render::ToScreen(position->x, position->y);
    const float pulse = 0.5f + 0.5f * std::sin(m_fireVisualTime * 5.0f);
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24);
    };
    const std::uint32_t base =
        def->kind == ecs::AnimalKind::Dangerous ? 0x003050FFu : 0x0030E0FFu;
    const std::uint32_t outer = withAlpha(base, 0.55f + 0.45f * pulse);
    const std::uint32_t inner = withAlpha(base, 0.18f + 0.22f * pulse);
    const auto diamond = [&](float halfX, float halfY, float thickness,
                             std::uint32_t color) {
        const glm::vec2 top = center + glm::vec2(0.0f, -halfY);
        const glm::vec2 right = center + glm::vec2(halfX, 0.0f);
        const glm::vec2 bottom = center + glm::vec2(0.0f, halfY);
        const glm::vec2 left = center + glm::vec2(-halfX, 0.0f);
        batch.DrawLine(top, right, thickness, color);
        batch.DrawLine(right, bottom, thickness, color);
        batch.DrawLine(bottom, left, thickness, color);
        batch.DrawLine(left, top, thickness, color);
    };
    diamond(34.0f, 18.0f, 3.0f, outer);
    diamond(24.0f, 12.0f, 1.5f, inner);
}

void WorldDemoState::DrawCombatOverlay(render::SpriteBatch& batch) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    DrawLocalCombatOverlay(batch);
    DrawRemoteCombatOverlay(batch);
}

void WorldDemoState::DrawLocalCombatOverlay(render::SpriteBatch& batch) {
    if (m_ctx.ecs == nullptr || m_player == entt::null) {
        return;
    }
    entt::registry& registry = m_ctx.ecs->Registry();
    const auto* position = registry.try_get<ecs::Position>(m_player);
    if (position == nullptr || !m_view.Contains(position->x, position->y)) {
        return;
    }
    if (const auto* health = registry.try_get<ecs::Health>(m_player);
        health != nullptr && health->current <= 0.0f) {
        return;
    }
    const characters::CombatRuleConfig rules =
        m_ctx.combat != nullptr ? m_ctx.combat->GetCombatRules()
                                : characters::DefaultCombatRuleConfig();
    const float parryWindow = characters::ParryWindowSeconds(rules);
    const float dodgeDuration = characters::DodgeDurationSeconds(rules);
    float faceX = 0.0f;
    float faceY = 1.0f;
    if (const auto* facing = registry.try_get<ecs::Facing>(m_player);
        facing != nullptr) {
        switch (facing->dir) {
            case ecs::Direction::Down:
                break;
            case ecs::Direction::Left:
                faceX = -1.0f;
                faceY = 0.0f;
                break;
            case ecs::Direction::Right:
                faceX = 1.0f;
                faceY = 0.0f;
                break;
            case ecs::Direction::Up:
                faceX = 0.0f;
                faceY = -1.0f;
                break;
        }
    }
    const glm::vec2 ground =
        render::ToScreen(position->x, position->y, position->z);
    if (const auto* stance = registry.try_get<ecs::CombatStance>(m_player);
        stance != nullptr) {
        const CombatPresentationState presentation =
            ResolveCombatPresentation(*stance, parryWindow, dodgeDuration);
        if (presentation.visible) {
            DrawCombatStanceOverlay(batch, ground, faceX, faceY, presentation);
        }
    }

    if (!m_localSwing.active) {
        return;
    }
    const float duration =
        std::isfinite(m_localSwing.duration) && m_localSwing.duration > 0.0f
            ? m_localSwing.duration
            : 0.44f;
    ecs::ActionState action;
    action.action = ecs::ActorAction::Attacking;
    action.elapsed = std::clamp(duration - m_localSwing.remaining, 0.0f,
                                duration);
    action.duration = duration;
    ecs::AttackIntent attack;
    attack.kind = m_localSwing.heavy ? ecs::AttackKind::Heavy
                                     : ecs::AttackKind::Light;
    attack.range = m_localSwing.range;
    const MeleeSwingPresentation swing =
        ResolveMeleeSwingPresentation(&action, &attack);
    if (swing.active) {
        DrawMeleeSwingOverlay(batch, ground, m_localSwing.dirScreenX,
                              m_localSwing.dirScreenY, swing);
    }
}

void WorldDemoState::DrawCombatStanceOverlay(
    render::SpriteBatch& batch, const glm::vec2& ground, float faceX,
    float faceY, const CombatPresentationState& presentation) {
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    const glm::vec2 center = ground + glm::vec2(0.0f, -28.0f);
    const std::uint32_t blockColor = 0xFFFFC040u;
    const std::uint32_t parryColor = 0xFFFFFFFFu;
    const std::uint32_t guardBreakColor = 0xFF4040FFu;
    const std::uint32_t dodgeColor = 0xFFFF8040u;

    if (presentation.blocking) {
        std::array<glm::vec2, 7> arc;
        for (std::size_t i = 0; i < arc.size(); ++i) {
            const float angle = -0.95f + static_cast<float>(i) * (1.9f / 6.0f);
            arc[i] = center +
                     glm::vec2(faceX * std::cos(angle) - faceY * std::sin(angle),
                               faceY * std::cos(angle) + faceX * std::sin(angle)) *
                         24.0f;
        }
        for (std::size_t i = 1; i < arc.size(); ++i) {
            batch.DrawLine(arc[i - 1], arc[i], 3.5f,
                           withAlpha(blockColor, presentation.blockAlpha));
        }
        batch.DrawRectFill(center.x - 5.0f, center.y - 26.0f, 10.0f, 4.0f,
                           withAlpha(blockColor, presentation.blockAlpha));
        if (presentation.parryActive) {
            std::array<glm::vec2, 4> inner;
            for (std::size_t i = 0; i < inner.size(); ++i) {
                const float angle =
                    -0.6f + static_cast<float>(i) * (1.2f / 3.0f);
                inner[i] =
                    center +
                    glm::vec2(faceX * std::cos(angle) - faceY * std::sin(angle),
                              faceY * std::cos(angle) + faceX * std::sin(angle)) *
                        15.0f;
            }
            for (std::size_t i = 1; i < inner.size(); ++i) {
                batch.DrawLine(inner[i - 1], inner[i], 2.0f,
                               withAlpha(parryColor, 0.95f));
            }
        }
    }

    if (presentation.guardBroken) {
        const glm::vec2 front = center + glm::vec2(faceX, faceY) * 22.0f;
        batch.DrawLine(front + glm::vec2(-6.0f, -6.0f),
                       front + glm::vec2(6.0f, 6.0f), 3.0f,
                       withAlpha(guardBreakColor, 0.95f));
        batch.DrawLine(front + glm::vec2(-6.0f, 6.0f),
                       front + glm::vec2(6.0f, -6.0f), 3.0f,
                       withAlpha(guardBreakColor, 0.95f));
        batch.DrawRectFill(center.x - 5.0f, center.y - 26.0f, 10.0f, 4.0f,
                           withAlpha(guardBreakColor, 0.95f));
    }

    if (presentation.dodging) {
        glm::vec2 direction =
            render::ToScreen(presentation.dodgeDirX, presentation.dodgeDirY, 0) -
            render::ToScreen(0.0f, 0.0f, 0);
        float length = std::sqrt(direction.x * direction.x +
                                 direction.y * direction.y);
        if (!std::isfinite(length) || length < 0.0001f) {
            direction = glm::vec2(faceX, faceY);
            length = std::sqrt(direction.x * direction.x +
                               direction.y * direction.y);
        }
        if (length > 0.0001f) {
            direction /= length;
        } else {
            direction = glm::vec2(0.0f, 1.0f);
        }
        const glm::vec2 perpendicular(-direction.y, direction.x);
        const glm::vec2 body = ground + glm::vec2(0.0f, -22.0f);
        for (int i = 1; i <= 4; ++i) {
            const float trail = 6.0f + static_cast<float>(i - 1) * 9.0f;
            const float jitter = (i % 2 == 0) ? 2.0f : -2.0f;
            const glm::vec2 from =
                body - direction * trail + perpendicular * jitter;
            const glm::vec2 to = body - direction * (trail + 9.0f);
            batch.DrawLine(
                from, to, std::max(1.5f, 4.5f - i * 0.6f),
                withAlpha(dodgeColor, presentation.dodgeAlpha *
                                          std::max(0.15f, 0.65f - i * 0.11f)));
        }
        std::array<glm::vec2, 5> dashArc;
        for (std::size_t i = 0; i < dashArc.size(); ++i) {
            const float angle = -0.8f + static_cast<float>(i) * 0.4f;
            dashArc[i] =
                center +
                glm::vec2(direction.x * std::cos(angle) -
                              direction.y * std::sin(angle),
                          direction.y * std::cos(angle) +
                              direction.x * std::sin(angle)) *
                    28.0f;
        }
        for (std::size_t i = 1; i < dashArc.size(); ++i) {
            batch.DrawLine(dashArc[i - 1], dashArc[i], 3.0f,
                           withAlpha(dodgeColor,
                                     presentation.dodgeAlpha * 0.85f));
        }
        batch.DrawLine(body, body + direction * 16.0f, 3.5f,
                       withAlpha(dodgeColor, presentation.dodgeAlpha));
        if (presentation.invulnerable) {
            batch.DrawLine(body + perpendicular * 7.0f,
                           body - perpendicular * 7.0f, 2.0f,
                           withAlpha(parryColor,
                                     presentation.dodgeAlpha * 0.9f));
        }
        batch.DrawRectFill(center.x - 5.0f, center.y - 38.0f, 10.0f, 4.0f,
                           withAlpha(dodgeColor, presentation.dodgeAlpha));
    }
}

void WorldDemoState::DrawMeleeSwingOverlay(
    render::SpriteBatch& batch, const glm::vec2& ground, float faceX,
    float faceY, const MeleeSwingPresentation& presentation) {
    if (!presentation.active || presentation.alpha <= 0.0f) {
        return;
    }
    float length = std::sqrt(faceX * faceX + faceY * faceY);
    if (!std::isfinite(length) || length < 0.0001f) {
        faceX = 0.0f;
        faceY = 1.0f;
        length = 1.0f;
    }
    faceX /= length;
    faceY /= length;
    const glm::vec2 center = ground + glm::vec2(0.0f, -26.0f);
    const float base = std::atan2(faceY, faceX);
    const float start = base - presentation.halfAngleRadians;
    const float end = base + presentation.halfAngleRadians;
    const float current =
        start + (end - start) * std::clamp(presentation.progress, 0.0f, 1.0f);
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    const std::uint32_t color =
        presentation.heavy ? 0xFF4060FFu : 0xFFA0E0FFu;
    const auto pointAt = [&](float angle, float radius) {
        return center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
    };
    constexpr int kSegments = 12;
    for (int i = 1; i <= kSegments; ++i) {
        const float t0 = static_cast<float>(i - 1) / kSegments;
        const float t1 = static_cast<float>(i) / kSegments;
        const glm::vec2 a = pointAt(start + (end - start) * t0,
                                    presentation.radius);
        const glm::vec2 b = pointAt(start + (end - start) * t1,
                                    presentation.radius);
        batch.DrawLine(a, b, std::max(1.0f, presentation.thickness * 0.5f),
                       withAlpha(color, presentation.alpha * 0.35f));
    }
    for (int i = 1; i <= kSegments; ++i) {
        const float t0 = static_cast<float>(i - 1) / kSegments;
        const float t1 = static_cast<float>(i) / kSegments;
        const glm::vec2 a = pointAt(start + (current - start) * t0,
                                    presentation.radius);
        const glm::vec2 b = pointAt(start + (current - start) * t1,
                                    presentation.radius);
        const float fade = 0.25f + 0.75f * t1;
        batch.DrawLine(a, b, presentation.thickness * fade,
                       withAlpha(color, presentation.alpha * fade));
    }
    if (presentation.heavy) {
        const glm::vec2 a = pointAt(start, presentation.radius + 6.0f);
        const glm::vec2 b = pointAt(current, presentation.radius + 6.0f);
        batch.DrawLine(a, b, 2.0f,
                       withAlpha(color, presentation.alpha * 0.35f));
    }
    const glm::vec2 tip = pointAt(current, presentation.radius);
    const glm::vec2 tangent(-std::sin(current), std::cos(current));
    const glm::vec2 normal(std::cos(current), std::sin(current));
    batch.DrawLine(tip - tangent * 5.0f, tip + tangent * 5.0f,
                   presentation.thickness * 0.6f,
                   withAlpha(0xFFFFFFFFu, presentation.alpha * 0.9f));
    batch.DrawLine(tip, tip + normal * 7.0f, 2.0f,
                   withAlpha(color, presentation.alpha * 0.7f));
}

void WorldDemoState::DrawRemoteCombatOverlay(render::SpriteBatch& batch) {
    if (!m_networkMode || m_ctx.ecs == nullptr ||
        m_ctx.networkClient == nullptr) {
        return;
    }

    entt::registry& registry = m_ctx.ecs->Registry();
    const characters::CombatRuleConfig rules =
        m_ctx.combat != nullptr ? m_ctx.combat->GetCombatRules()
                                : characters::DefaultCombatRuleConfig();
    const float parryWindow = characters::ParryWindowSeconds(rules);
    const float dodgeDuration = characters::DodgeDurationSeconds(rules);
    const std::uint64_t localPlayerId =
        m_ctx.networkClient->PlayerStableId();
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    const auto facingScreenDirection = [](ecs::Direction direction, float* x,
                                          float* y) {
        *x = 0.0f;
        *y = 1.0f;
        switch (direction) {
            case ecs::Direction::Down:
                break;
            case ecs::Direction::Left:
                *x = -1.0f;
                *y = 0.0f;
                break;
            case ecs::Direction::Right:
                *x = 1.0f;
                *y = 0.0f;
                break;
            case ecs::Direction::Up:
                *x = 0.0f;
                *y = -1.0f;
                break;
        }
    };
    for (const auto& [stableId, actor] :
         m_ctx.networkClient->Replication().Actors()) {
        if (stableId == 0 || stableId == localPlayerId ||
            !actor.hasCombatStance || !actor.hasPosition) {
            continue;
        }
        if (actor.hasHealth && actor.health.current <= 0.0f) {
            continue;
        }
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        if (entity == entt::null ||
            !registry.all_of<ecs::SpriteRef>(entity)) {
            continue;
        }
        const auto* pos = registry.try_get<ecs::Position>(entity);
        if (pos == nullptr || !m_view.Contains(pos->x, pos->y)) {
            continue;
        }
        const CombatPresentationState presentation =
            ResolveCombatPresentation(actor.combatStance, parryWindow,
                                      dodgeDuration);
        if (!presentation.visible) {
            continue;
        }
        float faceX = 0.0f;
        float faceY = 1.0f;
        facingScreenDirection(actor.hasFacing ? actor.facing.dir
                                              : ecs::Direction::Down,
                              &faceX, &faceY);
        const glm::vec2 ground = render::ToScreen(pos->x, pos->y, pos->z);
        const glm::vec2 center = ground + glm::vec2(0.0f, -28.0f);
        const std::uint32_t blockColor = 0xFFFFC040u;
        const std::uint32_t parryColor = 0xFFFFFFFFu;
        const std::uint32_t guardBreakColor = 0xFF4040FFu;
        const std::uint32_t dodgeColor = 0xFFFF8040u;

        if (presentation.blocking) {
            std::array<glm::vec2, 7> arc;
            for (std::size_t i = 0; i < arc.size(); ++i) {
                const float angle =
                    -0.95f + static_cast<float>(i) * (1.9f / 6.0f);
                arc[i] = center +
                         glm::vec2(faceX * std::cos(angle) -
                                       faceY * std::sin(angle),
                                   faceY * std::cos(angle) +
                                       faceX * std::sin(angle)) *
                             24.0f;
            }
            for (std::size_t i = 1; i < arc.size(); ++i) {
                batch.DrawLine(arc[i - 1], arc[i], 3.5f,
                               withAlpha(blockColor,
                                         presentation.blockAlpha));
            }
            batch.DrawRectFill(center.x - 5.0f, center.y - 26.0f, 10.0f,
                               4.0f,
                               withAlpha(blockColor,
                                         presentation.blockAlpha));
            if (presentation.parryActive) {
                std::array<glm::vec2, 4> inner;
                for (std::size_t i = 0; i < inner.size(); ++i) {
                    const float angle =
                        -0.6f + static_cast<float>(i) * (1.2f / 3.0f);
                    inner[i] = center +
                               glm::vec2(faceX * std::cos(angle) -
                                             faceY * std::sin(angle),
                                         faceY * std::cos(angle) +
                                             faceX * std::sin(angle)) *
                                   15.0f;
                }
                for (std::size_t i = 1; i < inner.size(); ++i) {
                    batch.DrawLine(inner[i - 1], inner[i], 2.0f,
                                   withAlpha(parryColor, 0.95f));
                }
            }
        }

        if (presentation.guardBroken) {
            const glm::vec2 front =
                center + glm::vec2(faceX, faceY) * 22.0f;
            batch.DrawLine(front + glm::vec2(-6.0f, -6.0f),
                           front + glm::vec2(6.0f, 6.0f), 3.0f,
                           withAlpha(guardBreakColor, 0.95f));
            batch.DrawLine(front + glm::vec2(-6.0f, 6.0f),
                           front + glm::vec2(6.0f, -6.0f), 3.0f,
                           withAlpha(guardBreakColor, 0.95f));
            batch.DrawRectFill(center.x - 5.0f, center.y - 26.0f, 10.0f,
                               4.0f,
                               withAlpha(guardBreakColor, 0.95f));
        }

        if (presentation.dodging) {
            glm::vec2 direction =
                render::ToScreen(presentation.dodgeDirX,
                                 presentation.dodgeDirY, 0) -
                render::ToScreen(0.0f, 0.0f, 0);
            float length = std::sqrt(direction.x * direction.x +
                                     direction.y * direction.y);
            if (!std::isfinite(length) || length < 0.0001f) {
                direction = glm::vec2(faceX, faceY);
                length = std::sqrt(direction.x * direction.x +
                                   direction.y * direction.y);
            }
            if (length > 0.0001f) {
                direction /= length;
            } else {
                direction = glm::vec2(0.0f, 1.0f);
            }
            const glm::vec2 perpendicular(-direction.y, direction.x);
            const glm::vec2 body = ground + glm::vec2(0.0f, -22.0f);
            for (int i = 1; i <= 4; ++i) {
                const float trail = 6.0f + static_cast<float>(i - 1) * 9.0f;
                const float jitter = (i % 2 == 0) ? 2.0f : -2.0f;
                const glm::vec2 from =
                    body - direction * trail + perpendicular * jitter;
                const glm::vec2 to = body - direction * (trail + 9.0f);
                batch.DrawLine(
                    from, to, std::max(1.5f, 4.5f - i * 0.6f),
                    withAlpha(dodgeColor,
                              presentation.dodgeAlpha *
                                  std::max(0.15f,
                                           0.65f - i * 0.11f)));
            }
            std::array<glm::vec2, 5> dashArc;
            for (std::size_t i = 0; i < dashArc.size(); ++i) {
                const float angle =
                    -0.8f + static_cast<float>(i) * 0.4f;
                dashArc[i] = center +
                             glm::vec2(direction.x * std::cos(angle) -
                                           direction.y * std::sin(angle),
                                       direction.y * std::cos(angle) +
                                           direction.x * std::sin(angle)) *
                                28.0f;
            }
            for (std::size_t i = 1; i < dashArc.size(); ++i) {
                batch.DrawLine(
                    dashArc[i - 1], dashArc[i], 3.0f,
                    withAlpha(dodgeColor,
                              presentation.dodgeAlpha * 0.85f));
            }
            batch.DrawLine(body, body + direction * 16.0f, 3.5f,
                           withAlpha(dodgeColor, presentation.dodgeAlpha));
            if (presentation.invulnerable) {
                batch.DrawLine(body + perpendicular * 7.0f,
                               body - perpendicular * 7.0f, 2.0f,
                               withAlpha(parryColor,
                                         presentation.dodgeAlpha * 0.9f));
            }
            batch.DrawRectFill(center.x - 5.0f, center.y - 38.0f, 10.0f,
                               4.0f,
                               withAlpha(dodgeColor,
                                         presentation.dodgeAlpha));
        }
    }
    for (const auto& [stableId, actor] :
         m_ctx.networkClient->Replication().Actors()) {
        if (stableId == 0 || stableId == localPlayerId ||
            !actor.hasAction || !actor.hasPosition) {
            continue;
        }
        if (actor.hasHealth && actor.health.current <= 0.0f) {
            continue;
        }
        const ecs::Entity entity = m_ctx.ecs->FindById(stableId);
        if (entity == entt::null) {
            continue;
        }
        const auto* position = registry.try_get<ecs::Position>(entity);
        const auto* action = registry.try_get<ecs::ActionState>(entity);
        if (position == nullptr || action == nullptr ||
            !m_view.Contains(position->x, position->y)) {
            continue;
        }
        const MeleeSwingPresentation swing =
            ResolveMeleeSwingPresentation(action, nullptr);
        if (!swing.active) {
            continue;
        }
        float faceX = 0.0f;
        float faceY = 1.0f;
        facingScreenDirection(actor.hasFacing ? actor.facing.dir
                                              : ecs::Direction::Down,
                              &faceX, &faceY);
        DrawMeleeSwingOverlay(batch,
                              render::ToScreen(position->x, position->y,
                                               position->z),
                              faceX, faceY, swing);
    }
}

void WorldDemoState::DrawFireOverlay(render::SpriteBatch& batch) {
    if (m_ctx.ecs == nullptr) {
        return;
    }
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    m_ctx.ecs->Registry().view<ecs::FireSource, ecs::FireAnchor>().each(
        [&](const ecs::Entity, const ecs::FireSource& source,
            const ecs::FireAnchor& anchor) {
            const float centerX = static_cast<float>(anchor.x) + 0.5f;
            const float centerY = static_cast<float>(anchor.y) + 0.5f;
            if (!m_view.Contains(centerX, centerY)) {
                return;
            }
            const glm::vec2 center =
                render::ToScreen(centerX, centerY, anchor.z);
            const float pulse =
                0.5f + 0.5f * std::sin(m_fireVisualTime * 7.0f +
                                       static_cast<float>(anchor.x) * 0.7f +
                                       static_cast<float>(anchor.y) * 0.3f);
            if (source.phase == ecs::FirePhase::Extinguished) {
                if (!(source.burnDuration > 0.0f)) {
                    return;
                }
                const float half = 6.0f + pulse * 2.0f;
                const std::uint32_t color = withAlpha(0xFF202830u, 0.35f);
                const glm::vec2 top = center + glm::vec2(0.0f, -half);
                const glm::vec2 right = center + glm::vec2(half * 1.5f, 0.0f);
                const glm::vec2 bottom = center + glm::vec2(0.0f, half);
                const glm::vec2 left = center + glm::vec2(-half * 1.5f, 0.0f);
                batch.DrawLine(top, right, 1.5f, color);
                batch.DrawLine(right, bottom, 1.5f, color);
                batch.DrawLine(bottom, left, 1.5f, color);
                batch.DrawLine(left, top, 1.5f, color);
                batch.DrawLine(center + glm::vec2(-half, -half * 0.5f),
                               center + glm::vec2(half, half * 0.5f), 1.5f,
                               color);
                return;
            }
            const std::uint32_t color =
                source.phase == ecs::FirePhase::Igniting
                    ? 0xFF40C0FFu
                    : source.phase == ecs::FirePhase::Burning ? 0xFF4070FFu
                                                              : 0xFF2040B0u;
            const float alpha =
                source.phase == ecs::FirePhase::Igniting ? 0.55f : 0.85f;
            const float half = 8.0f + pulse * 5.0f;
            const glm::vec2 top = center + glm::vec2(0.0f, -half);
            const glm::vec2 right = center + glm::vec2(half * 1.5f, 0.0f);
            const glm::vec2 bottom = center + glm::vec2(0.0f, half);
            const glm::vec2 left = center + glm::vec2(-half * 1.5f, 0.0f);
            batch.DrawLine(top, right, 3.0f, withAlpha(color, alpha));
            batch.DrawLine(right, bottom, 3.0f, withAlpha(color, alpha));
            batch.DrawLine(bottom, left, 3.0f, withAlpha(color, alpha));
            batch.DrawLine(left, top, 3.0f, withAlpha(color, alpha));
            const float inner = half * 0.55f;
            const glm::vec2 innerTop = center + glm::vec2(0.0f, -inner);
            const glm::vec2 innerRight =
                center + glm::vec2(inner * 1.5f, 0.0f);
            const glm::vec2 innerBottom = center + glm::vec2(0.0f, inner);
            const glm::vec2 innerLeft =
                center + glm::vec2(-inner * 1.5f, 0.0f);
            batch.DrawLine(innerTop, innerRight, 1.5f,
                           withAlpha(color, 0.45f));
            batch.DrawLine(innerRight, innerBottom, 1.5f,
                           withAlpha(color, 0.45f));
            batch.DrawLine(innerBottom, innerLeft, 1.5f,
                           withAlpha(color, 0.45f));
            batch.DrawLine(innerLeft, innerTop, 1.5f,
                           withAlpha(color, 0.45f));
            batch.DrawLine(center, center + glm::vec2(0.0f, -half * 2.2f),
                           2.0f, withAlpha(color, 0.55f));
            const float smoke = game::FireSmokePerSecond(source);
            if (smoke > 0.0f) {
                const int puffs =
                    std::clamp(static_cast<int>(std::ceil(smoke * 1.5f)), 1, 5);
                for (int i = 0; i < puffs; ++i) {
                    const float rise = std::fmod(
                        m_fireVisualTime * 0.32f +
                            static_cast<float>(i) /
                                static_cast<float>(puffs),
                        1.0f);
                    const glm::vec2 puffCenter =
                        center + glm::vec2(
                                     std::sin((m_fireVisualTime * 0.7f +
                                               static_cast<float>(i)) *
                                              3.1f) *
                                         (5.0f + rise * 7.0f),
                                     -26.0f - rise * 64.0f);
                    const float radius =
                        5.0f + rise * 8.0f + smoke;
                    const float smokeAlpha =
                        0.65f * (1.0f - rise) *
                        std::clamp(smoke, 0.35f, 1.2f);
                    for (int ring = 0; ring < 2; ++ring) {
                        const float ringRadius = radius + ring * 3.0f;
                        const std::uint32_t smokeColor = withAlpha(
                            0xFFE0E0E0u, smokeAlpha * (1.0f - ring * 0.55f));
                        glm::vec2 previous =
                            puffCenter + glm::vec2(ringRadius, 0.0f);
                        for (int side = 1; side <= 8; ++side) {
                            const float angle =
                                static_cast<float>(side) * 0.7853982f;
                            const glm::vec2 next =
                                puffCenter +
                                glm::vec2(std::cos(angle) * ringRadius,
                                          std::sin(angle) * ringRadius);
                            batch.DrawLine(previous, next, 2.0f, smokeColor);
                            previous = next;
                        }
                    }
                }
            }
        });
}

bool WorldDemoState::HasSiegeMarkers() const {
    if (m_networkMode) {
        const bool active =
            m_networkSiegePhase ==
                static_cast<std::uint8_t>(BaseSiegePhase::Warning) ||
            m_networkSiegePhase ==
                static_cast<std::uint8_t>(BaseSiegePhase::Waves);
        return (active && m_networkSiegeTargetValid) ||
               m_networkSiegeBreachValid;
    }
    const bool visible =
        m_baseSiege.Active() || m_baseSiege.Result().rating != '-';
    if (!visible) {
        return false;
    }
    const bool target = m_baseSiege.Active() && m_baseSiege.DirectorActive() &&
                        m_baseSiege.AttackPlan().hasTarget;
    return target || m_baseSiege.Breached();
}

void WorldDemoState::DrawSiegeOverlay(render::SpriteBatch& batch) {
    const float pulse = 0.5f + 0.5f * std::sin(m_powerVisualTime * 4.0f);
    const auto withAlpha = [](std::uint32_t color, float alpha) {
        const std::uint32_t a = static_cast<std::uint32_t>(
            std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (color & 0x00FFFFFFu) | (a << 24U);
    };
    const auto drawDiamond = [&batch, &withAlpha](float x, float y,
                                                  float half,
                                                  std::uint32_t color) {
        const glm::vec2 center = render::ToScreen(x, y);
        const glm::vec2 top = center + glm::vec2(0.0f, -half);
        const glm::vec2 right = center + glm::vec2(half * 1.5f, 0.0f);
        const glm::vec2 bottom = center + glm::vec2(0.0f, half);
        const glm::vec2 left = center + glm::vec2(-half * 1.5f, 0.0f);
        batch.DrawLine(top, right, 3.0f, withAlpha(color, 0.9f));
        batch.DrawLine(right, bottom, 3.0f, withAlpha(color, 0.9f));
        batch.DrawLine(bottom, left, 3.0f, withAlpha(color, 0.9f));
        batch.DrawLine(left, top, 3.0f, withAlpha(color, 0.9f));
    };
    const auto drawBreach = [&batch, &withAlpha](float x, float y, float pulse) {
        const glm::vec2 center = render::ToScreen(x, y);
        const float half = 16.0f + 5.0f * pulse;
        const std::uint32_t color =
            withAlpha(kSiegeBreachMarkerColor, 0.65f + 0.35f * pulse);
        batch.DrawLine(center + glm::vec2(-half, -half),
                       center + glm::vec2(half, half), 4.0f, color);
        batch.DrawLine(center + glm::vec2(-half, half),
                       center + glm::vec2(half, -half), 4.0f, color);
        constexpr int kSegments = 20;
        const float radius = 22.0f + 7.0f * pulse;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = static_cast<float>(i) * 6.2831853f /
                             static_cast<float>(kSegments);
            const float a1 = static_cast<float>(i + 1) * 6.2831853f /
                             static_cast<float>(kSegments);
            batch.DrawLine(center + glm::vec2(std::cos(a0) * radius,
                                               std::sin(a0) * radius * 0.5f),
                           center + glm::vec2(std::cos(a1) * radius,
                                               std::sin(a1) * radius * 0.5f),
                           2.0f, withAlpha(kSiegeBreachMarkerColor, 0.6f));
        }
    };

    bool targetValid = false;
    float targetX = 0.0f;
    float targetY = 0.0f;
    bool breachValid = false;
    float breachX = 0.0f;
    float breachY = 0.0f;
    if (m_networkMode) {
        const bool active =
            m_networkSiegePhase ==
                static_cast<std::uint8_t>(BaseSiegePhase::Warning) ||
            m_networkSiegePhase ==
                static_cast<std::uint8_t>(BaseSiegePhase::Waves);
        targetValid = active && m_networkSiegeTargetValid;
        targetX = m_networkSiegeTargetX;
        targetY = m_networkSiegeTargetY;
        breachValid = m_networkSiegeBreachValid;
        breachX = m_networkSiegeBreachX;
        breachY = m_networkSiegeBreachY;
    } else {
        const bool visible =
            m_baseSiege.Active() || m_baseSiege.Result().rating != '-';
        const BaseSiegeAttackPlan& plan = m_baseSiege.AttackPlan();
        targetValid = visible && m_baseSiege.Active() &&
                      m_baseSiege.DirectorActive() && plan.hasTarget;
        targetX = plan.targetX;
        targetY = plan.targetY;
        breachValid = visible && m_baseSiege.Breached();
        const BaseSiegePoint& breach = m_baseSiege.BreachPoint();
        breachX = breach.x;
        breachY = breach.y;
    }

    if (targetValid) {
        drawDiamond(targetX, targetY, 18.0f + 4.0f * pulse,
                    kSiegeTargetMarkerColor);
    }
    if (breachValid) {
        drawBreach(breachX, breachY, pulse);
    }
}

void WorldDemoState::DrawForageOverlay(render::SpriteBatch& batch) {
    if (m_networkMode || m_ctx.ecs == nullptr || m_player == entt::null ||
        (m_forageNodes.empty() && m_fishingSpots.empty()) ||
        m_ctx.ui == nullptr) {
        return;
    }
    if (m_ctx.ui->IsInventoryOpen() || m_ctx.ui->IsMenuOpen() ||
        m_characterOpen) {
        return;
    }
    const auto* position =
        m_ctx.ecs->Registry().try_get<ecs::Position>(m_player);
    if (position == nullptr) {
        return;
    }
    const auto* state =
        m_ctx.ecs->Registry().try_get<ecs::ForageState>(m_player);
    const double now = m_ctx.gameTime != nullptr
                           ? m_ctx.gameTime->TotalGameMinutes()
                           : 0.0;
    const float pulse = 0.5f + 0.5f * std::sin(m_powerVisualTime * 5.0f);
    const float maxDistanceSq = 8.0f * 8.0f;
    for (const ForageNode& node : m_forageNodes) {
        if (node.z != position->z) {
            continue;
        }
        const float dx = (static_cast<float>(node.x) + 0.5f) - position->x;
        const float dy = (static_cast<float>(node.y) + 0.5f) - position->y;
        if (dx * dx + dy * dy > maxDistanceSq) {
            continue;
        }
        const bool ready = state == nullptr || ForageNodeReady(*state, node, now);
        const bool target = m_forageAction.active &&
                            m_forageAction.zoneId == node.zoneId &&
                            m_forageAction.x == node.x &&
                            m_forageAction.y == node.y &&
                            m_forageAction.z == node.z;
        const std::uint32_t color =
            target ? kForageTargetColor
                   : ready ? kForageReadyColor : kForageEmptyColor;
        const float half = (target ? 9.0f : ready ? 6.0f : 4.0f) +
                           (ready ? 1.5f * pulse : 0.0f);
        const glm::vec2 center = render::ToScreen(
            static_cast<float>(node.x) + 0.5f,
            static_cast<float>(node.y) + 0.5f);
        batch.DrawLine(center + glm::vec2(0.0f, -half),
                       center + glm::vec2(half * 1.5f, 0.0f), 2.0f, color);
        batch.DrawLine(center + glm::vec2(half * 1.5f, 0.0f),
                       center + glm::vec2(0.0f, half), 2.0f, color);
        batch.DrawLine(center + glm::vec2(0.0f, half),
                       center + glm::vec2(-half * 1.5f, 0.0f), 2.0f, color);
        batch.DrawLine(center + glm::vec2(-half * 1.5f, 0.0f),
                       center + glm::vec2(0.0f, -half), 2.0f, color);
    }
    for (const FishingSpot& spot : m_fishingSpots) {
        if (spot.z != position->z) {
            continue;
        }
        const FishingSpotDef* def =
            m_ctx.forage != nullptr ? m_ctx.forage->FindSpot(spot.spotId)
                                    : nullptr;
        if (def == nullptr || m_ctx.world == nullptr ||
            !FishingSpotWaterValid(*def, *m_ctx.world, spot)) {
            continue;
        }
        const float dx = (static_cast<float>(spot.x) + 0.5f) - position->x;
        const float dy = (static_cast<float>(spot.y) + 0.5f) - position->y;
        if (dx * dx + dy * dy > maxDistanceSq) {
            continue;
        }
        const bool target = m_fishingAction.active &&
                            m_fishingAction.spotId == spot.spotId &&
                            m_fishingAction.x == spot.x &&
                            m_fishingAction.y == spot.y;
        const std::uint32_t color =
            target ? kFishingTargetColor : kFishingReadyColor;
        const float half = (target ? 9.0f : 5.0f) + 1.5f * pulse;
        const glm::vec2 center = render::ToScreen(
            static_cast<float>(spot.x) + 0.5f,
            static_cast<float>(spot.y) + 0.5f);
        batch.DrawLine(center + glm::vec2(0.0f, -half),
                       center + glm::vec2(half * 1.5f, 0.0f), 2.0f, color);
        batch.DrawLine(center + glm::vec2(half * 1.5f, 0.0f),
                       center + glm::vec2(0.0f, half), 2.0f, color);
        batch.DrawLine(center + glm::vec2(0.0f, half),
                       center + glm::vec2(-half * 1.5f, 0.0f), 2.0f, color);
        batch.DrawLine(center + glm::vec2(-half * 1.5f, 0.0f),
                       center + glm::vec2(0.0f, -half), 2.0f, color);
    }
}

void WorldDemoState::DrawDebugOverlay() {
    if (!m_showGrid && !m_hoverHit && m_poiConfig.defs.empty() &&
        m_poweredFloodlights.empty() && m_poweredFenceCells.empty() &&
        m_forageNodes.empty() && m_fishingSpots.empty() &&
        !HasSiegeMarkers()) {
        return;
    }
    render::Camera& camera = *m_ctx.camera;
    render::SpriteBatch& batch = *m_ctx.sprites;

    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    const bool poiOverlayVisible =
        m_ctx.ui == nullptr ||
        (!m_ctx.ui->IsInventoryOpen() && !m_ctx.ui->IsMenuOpen() && !m_characterOpen);
    const bool hasWorldMarkers =
        !m_poweredFloodlights.empty() || !m_poweredFenceCells.empty() ||
        HasSiegeMarkers() || m_markerPathActive || m_markerTimer > 0.0f ||
        ((!m_forageNodes.empty() || !m_fishingSpots.empty()) &&
         poiOverlayVisible) ||
        (!m_poiConfig.defs.empty() && poiOverlayVisible);

    if (hasWorldMarkers) {
        bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::World), &view,
                               &proj);
        batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::World));
        DrawPowerOverlay(batch);
        DrawForageOverlay(batch);
        DrawSiegeOverlay(batch);
        // M4-T7：寻路目标标记（??进行中常显；????到达/不可??限时??
        if (m_markerPathActive || m_markerTimer > 0.0f) {
            const glm::vec2 center =
                render::ToScreen(static_cast<float>(m_markerX), static_cast<float>(m_markerY));
            const glm::vec2 top = center + glm::vec2(0.0f, -22.0f);
            const glm::vec2 right = center + glm::vec2(44.0f, 0.0f);
            const glm::vec2 bottom = center + glm::vec2(0.0f, 22.0f);
            const glm::vec2 left = center + glm::vec2(-44.0f, 0.0f);
            batch.DrawLine(top, right, kMarkerLineThickness, m_markerColor);
            batch.DrawLine(right, bottom, kMarkerLineThickness, m_markerColor);
            batch.DrawLine(bottom, left, kMarkerLineThickness, m_markerColor);
            batch.DrawLine(left, top, kMarkerLineThickness, m_markerColor);
        }
        // M19-F2：POI 世界标记。视野外夹到屏幕边缘，作为地图指引；已完成显示灰色。
        if (!m_poiConfig.defs.empty() && poiOverlayVisible) {
            const ecs::PoiTasks* tasks = PlayerPoiTasks();
            const float fbW = static_cast<float>(
                std::max(1, m_ctx.renderer->FramebufferWidth()));
            const float fbH = static_cast<float>(
                std::max(1, m_ctx.renderer->FramebufferHeight()));
            constexpr float kEdgeMargin = 44.0f;
            const float invZoom = 1.0f / std::max(0.001f, camera.Zoom());
            for (const PoiDef& poi : m_poiConfig.defs) {
                bool completed =
                    tasks != nullptr && game::IsPoiCompleted(tasks->completed, poi.id);
                if (m_networkMode) {
                    const auto found = m_networkPoiProgress.find(poi.id);
                    if (found != m_networkPoiProgress.end()) {
                        completed = found->second.completed;
                    }
                }
                const uint32_t color =
                    completed ? kPoiMarkerCompletedColor
                    : poi.threat >= 3 ? kMarkerFailColor
                    : poi.threat == 2 ? kMarkerActiveColor
                                      : kMarkerArriveColor;
                const glm::vec2 worldPx = render::ToScreen(poi.markerX, poi.markerY, 0);
                glm::vec2 screen = camera.WorldToScreen(worldPx);
                screen.x = std::clamp(screen.x, kEdgeMargin, fbW - kEdgeMargin);
                screen.y = std::clamp(screen.y, kEdgeMargin, fbH - kEdgeMargin);
                const float markerHalfX = completed ? 14.0f : 20.0f;
                const float markerHalfY = completed ? 10.0f : 14.0f;
                const bool atBottom = screen.y >= fbH - kEdgeMargin - 0.5f;
                const bool atTop = screen.y <= kEdgeMargin + 0.5f;
                const bool atLeft = screen.x <= kEdgeMargin + 0.5f;
                const bool atRight = screen.x >= fbW - kEdgeMargin - 0.5f;
                if (atBottom) {
                    const bool blockedLeft =
                        screen.x - markerHalfX < 320.0f;
                    const bool blockedCenter =
                        screen.x + markerHalfX > fbW * 0.5f - 240.0f &&
                        screen.x - markerHalfX < fbW * 0.5f + 240.0f;
                    const bool blockedRight =
                        screen.x + markerHalfX > fbW - 120.0f;
                    if (blockedLeft || blockedCenter) {
                        screen.x = 340.0f;
                    } else if (blockedRight) {
                        screen.x = fbW - 160.0f;
                    }
                }
                if (atTop) {
                    const bool blockedClock =
                        screen.x + markerHalfX > fbW * 0.5f - 110.0f &&
                        screen.x - markerHalfX < fbW * 0.5f + 110.0f;
                    const bool blockedRight =
                        screen.x + markerHalfX > fbW - 330.0f;
                    if (blockedClock) {
                        screen.x = fbW * 0.5f - 160.0f;
                    } else if (blockedRight) {
                        screen.x = fbW - 350.0f;
                    }
                }
                if (atLeft && !atTop && !atBottom &&
                    screen.y + markerHalfY > fbH - 320.0f) {
                    screen.y = fbH - 330.0f;
                }
                if (atRight && !atTop && !atBottom) {
                    if (screen.y - markerHalfY < 270.0f) {
                        screen.y = 280.0f;
                    } else if (screen.y + markerHalfY > fbH - 80.0f) {
                        screen.y = fbH - 100.0f;
                    }
                }
                const glm::vec2 center = camera.ScreenToWorld(screen);
                const float halfX = (completed ? 14.0f : 20.0f) * invZoom;
                const float halfY = (completed ? 10.0f : 14.0f) * invZoom;
                const glm::vec2 top = center + glm::vec2(0.0f, -halfY);
                const glm::vec2 right = center + glm::vec2(halfX, 0.0f);
                const glm::vec2 bottom = center + glm::vec2(0.0f, halfY);
                const glm::vec2 left = center + glm::vec2(-halfX, 0.0f);
                const float thickness = (completed ? 2.0f : 3.0f) * invZoom;
                batch.DrawLine(top, right, thickness, color);
                batch.DrawLine(right, bottom, thickness, color);
                batch.DrawLine(bottom, left, thickness, color);
                batch.DrawLine(left, top, thickness, color);
            }
        }
        batch.End();
    }

    if (!m_showGrid && !m_hoverHit) {
        return;
    }

    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::Debug), &view, &proj);

    const glm::vec2 half = camera.VisibleSize() * 0.5f;
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;
    const glm::vec2 g0 = render::FromScreen(minW, 0);
    const glm::vec2 g1 = render::FromScreen(maxW, 0);
    const glm::vec2 g2 = render::FromScreen({minW.x, maxW.y}, 0);
    const glm::vec2 g3 = render::FromScreen({maxW.x, minW.y}, 0);
    const int gxMin = static_cast<int>(std::floor(std::min({g0.x, g1.x, g2.x, g3.x}))) - 1;
    const int gxMax = static_cast<int>(std::ceil(std::max({g0.x, g1.x, g2.x, g3.x}))) + 1;
    const int gyMin = static_cast<int>(std::floor(std::min({g0.y, g1.y, g2.y, g3.y}))) - 1;
    const int gyMax = static_cast<int>(std::ceil(std::max({g0.y, g1.y, g2.y, g3.y}))) + 1;

    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::Debug));
    if (m_showGrid) {
        for (int gx = gxMin; gx <= gxMax; ++gx) {
            const float f = static_cast<float>(gx) - 0.5f;
            batch.DrawLine(render::ToScreen(f, static_cast<float>(gyMin) - 0.5f),
                           render::ToScreen(f, static_cast<float>(gyMax) - 0.5f),
                           kGridLineThickness, kGridColor);
        }
        for (int gy = gyMin; gy <= gyMax; ++gy) {
            const float f = static_cast<float>(gy) - 0.5f;
            batch.DrawLine(render::ToScreen(static_cast<float>(gxMin) - 0.5f, f),
                           render::ToScreen(static_cast<float>(gxMax) - 0.5f, f),
                           kGridLineThickness, kGridColor);
        }
        batch.DrawLine(render::ToScreen(0.0f, 0.0f), render::ToScreen(6.0f, 0.0f), 3.0f,
                       kAxisXColor);
        batch.DrawLine(render::ToScreen(0.0f, 0.0f), render::ToScreen(0.0f, 6.0f), 3.0f,
                       kAxisYColor);
    }
    if (m_hoverHit) {
        const glm::vec2 center =
            render::ToScreen(static_cast<float>(m_hoverX), static_cast<float>(m_hoverY));
        const glm::vec2 top = center + glm::vec2(0.0f, -16.0f);
        const glm::vec2 right = center + glm::vec2(32.0f, 0.0f);
        const glm::vec2 bottom = center + glm::vec2(0.0f, 16.0f);
        const glm::vec2 left = center + glm::vec2(-32.0f, 0.0f);
        batch.DrawLine(top, right, kHoverLineThickness, kHoverColor);
        batch.DrawLine(right, bottom, kHoverLineThickness, kHoverColor);
        batch.DrawLine(bottom, left, kHoverLineThickness, kHoverColor);
        batch.DrawLine(left, top, kHoverLineThickness, kHoverColor);
    }
    batch.End();
}

void WorldDemoState::DrawPowerOverlay(render::SpriteBatch& batch) {
    if (m_poweredFloodlights.empty() && m_poweredFenceCells.empty()) {
        return;
    }
    constexpr uint32_t kGlowFill = 0x18A0E0FFu;
    constexpr uint32_t kGlowOutline = 0x78A0E0FFu;
    constexpr uint32_t kArcColor = 0xFF30E0FFu;
    constexpr int kSegments = 24;
    for (const PowerGlow& glow : m_poweredFloodlights) {
        const glm::vec2 center = render::ToScreen(glow.x, glow.y);
        for (int i = 0; i < kSegments; ++i) {
            const float a0 =
                static_cast<float>(i) * 6.2831853f /
                static_cast<float>(kSegments);
            const float a1 =
                static_cast<float>(i + 1) * 6.2831853f /
                static_cast<float>(kSegments);
            const glm::vec2 p0 =
                render::ToScreen(glow.x + std::cos(a0) * glow.radius,
                                 glow.y + std::sin(a0) * glow.radius);
            const glm::vec2 p1 =
                render::ToScreen(glow.x + std::cos(a1) * glow.radius,
                                 glow.y + std::sin(a1) * glow.radius);
            batch.DrawLine(p0, p1, 2.0f, kGlowOutline);
            if ((i % 2) == 0) {
                batch.DrawLine(center, p0, 1.0f, kGlowFill);
            }
        }
    }
    const bool night =
        m_ctx.gameTime != nullptr && m_ctx.gameTime->IsNight();
    const bool arcsActive =
        m_baseSiege.Active() &&
        (!m_homesteadConfig.power.deviceRequiresNight || night);
    int arcs = 0;
    for (const std::uint64_t key : m_poweredFenceCells) {
        if (!arcsActive || ++arcs > 64) {
            break;
        }
        const float wx = static_cast<float>(BuildCellX(key)) + 0.5f;
        const float wy = static_cast<float>(BuildCellY(key)) + 0.5f;
        const glm::vec2 center = render::ToScreen(wx, wy);
        const float phase = m_powerVisualTime * 8.0f +
                            static_cast<float>(key % 17u) * 0.7f;
        float x = center.x;
        float y = center.y - 14.0f;
        for (int i = 0; i < 3; ++i) {
            const float nx =
                center.x + std::sin(phase + static_cast<float>(i) * 2.1f) *
                               4.0f;
            const float ny = y - 8.0f;
            batch.DrawLine({x, y}, {nx, ny}, 2.0f, kArcColor);
            x = nx;
            y = ny;
        }
    }
}

void WorldDemoState::DrawPhysicsDebug() {
    if (m_ctx.physics == nullptr || !m_ctx.physics->DebugDrawEnabled()) {
        return;
    }
    const std::vector<physics::PhysicsWorld::DebugLine>& lines = m_ctx.physics->DebugLines();
    if (lines.empty()) {
        return;
    }
    const std::vector<AssetProvider::Entry>* frames = m_ctx.assets->Frames("ui.pixel");
    if (frames == nullptr || frames->empty() || (*frames)[0].atlas == nullptr) {
        return;
    }
    const AssetProvider::Entry& entry = (*frames)[0];
    const render::AtlasFrame& white = *entry.frame;

    render::Camera& camera = *m_ctx.camera;
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjMatrix();
    bgfx::setViewTransform(static_cast<bgfx::ViewId>(render::ViewId::Debug), &view, &proj);

    render::SpriteBatch& batch = *m_ctx.sprites;
    batch.Begin(static_cast<bgfx::ViewId>(render::ViewId::Debug));

    // 线段 → 采样点四边形的集合（SpriteBatch 只画轴对齐四边形，点步进对任意角度都成立）。
    // 注意：线框四边形数量是"线段像素长度 / 采样间距"量级（实测 Bullet 一次 ~7000 条线段），
    // 必须做可见剔除 + 帧预算，否则会打爆 SpriteBatch 的单批上限（16384 四边形/帧）。
    constexpr float kDotSize = 3.4f;   // 单个采样点（四边形）边长
    constexpr float kDotStep = 10.0f;  // 采样间距（线框呈虚线感，调试足够）
    constexpr int kMaxWireQuads = 13500;
    const glm::vec2 half = camera.VisibleSize() * 0.5f + glm::vec2(160.0f, 320.0f);
    const glm::vec2 minW = camera.Center() - half;
    const glm::vec2 maxW = camera.Center() + half;
    constexpr float kFloorPx = static_cast<float>(core::kFloorPx);

    int quads = 0;
    for (const physics::PhysicsWorld::DebugLine& line : lines) {
        if (quads >= kMaxWireQuads) {
            break;
        }
        const glm::vec2 a = render::ToScreen(line.x0, line.y0) - glm::vec2(0.0f, line.z0 * kFloorPx);
        const glm::vec2 b = render::ToScreen(line.x1, line.y1) - glm::vec2(0.0f, line.z1 * kFloorPx);
        // 屏幕 AABB 剔除（完全在视野外的线段直接跳过）
        if (std::min(a.x, b.x) > maxW.x || std::max(a.x, b.x) < minW.x ||
            std::min(a.y, b.y) > maxW.y || std::max(a.y, b.y) < minW.y) {
            continue;
        }
        const glm::vec2 delta = b - a;
        const float length = glm::length(delta);
        const int steps = static_cast<int>(length / kDotStep) + 1;
        for (int i = 0; i <= steps && quads < kMaxWireQuads; ++i, ++quads) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const glm::vec2 point = a + delta * t;
            batch.DrawSprite(entry.atlas->Texture(),
                             {point.x - kDotSize * 0.5f, point.y - kDotSize * 0.5f, kDotSize,
                              kDotSize},
                             white.u0, white.v0, white.u1, white.v1, line.color);
        }
    }
    batch.End();

    ++m_wireDiagCount;
    if (m_wireDiagCount <= 2) {
        ZL_INFO("physics", "线框诊断：线段 {} 条，绘制四边形 {} 个", lines.size(), quads);
    }
}

void WorldDemoState::DrawDebugText() {
    if (!m_showDebugText || (m_ctx.ui != nullptr &&
                             (m_ctx.ui->IsInventoryOpen() || m_ctx.ui->IsMenuOpen() ||
                              m_characterOpen || m_siegeReportOpen))) {
        bgfx::dbgTextClear();
        return;
    }
    const render::Renderer::Stats stats = m_ctx.renderer->GetStats();
    const auto& chunkStats = m_ctx.chunkManager->GetStats();
    const auto& now = m_ctx.gameTime->Now();
    const std::string timeText = core::FormatDateTime(now);
    const glm::vec2 camGrid = render::FromScreen(m_ctx.camera->Center(), 0);

    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 1, 0x0f, "ZomboidLike M3 | State: %s | backend: %s", Name(),
                        m_ctx.renderer->BackendName());
    bgfx::dbgTextPrintf(0, 2, 0x0f, "FPS: %.0f | TPS: %.0f | frame: %.1fms | drawcall: %u",
                        m_ctx.loop->Fps(), m_ctx.loop->Tps(), m_ctx.loop->LastFrameMs(),
                        stats.drawCalls);
    bgfx::dbgTextPrintf(0, 3, 0x0f, "tick: %llu | %s | speed: %s | %s",
                        static_cast<unsigned long long>(m_ctx.loop->TotalTicks()),
                        timeText.c_str(), core::GameTime::SpeedName(m_ctx.gameTime->GetSpeed()),
                        m_ctx.gameTime->IsNight() ? "night" : "day");
    bgfx::dbgTextPrintf(0, 4, 0x0f, "world: camGrid=(%.0f,%.0f) | instances: %zu | quads: %u | batches: %u",
                        camGrid.x, camGrid.y, m_items.size(), m_ctx.sprites->QuadsThisFrame(),
                        m_ctx.sprites->DrawCallsThisFrame());
    bgfx::dbgTextPrintf(0, 5, 0x0f, "chunks: resident=%d loading=%d | visible=%d missing=%d | total: +%llu -%llu cancel=%llu",
                        chunkStats.loadedChunks, chunkStats.loadingChunks, m_collectedChunks,
                        m_missingChunks, static_cast<unsigned long long>(chunkStats.totalLoaded),
                        static_cast<unsigned long long>(chunkStats.totalUnloaded),
                        static_cast<unsigned long long>(chunkStats.totalCancelled));
    bgfx::dbgTextPrintf(0, 6, 0x0f, "zoom: %.2f | cam: %s | hover: (%d, %d) | provider: %s",
                        m_ctx.camera->Zoom(),
                        m_followCamera ? (m_cruiseEnabled && !m_cruiseFinished ? "cruise" : "follow")
                                       : "free",
                        m_hoverX, m_hoverY, m_ctx.assets->Provider().c_str());
    // M4-T4：玩家状态（位置/朝向/动画帧/移动意图）
    if (m_ctx.ecs != nullptr && m_player != entt::null) {
        entt::registry& registry = m_ctx.ecs->Registry();
        const auto* playerPos = registry.try_get<ecs::Position>(m_player);
        const auto* playerFacing = registry.try_get<ecs::Facing>(m_player);
        const auto* playerAnim = registry.try_get<ecs::AnimState>(m_player);
        const auto* playerIntent = registry.try_get<ecs::MoveIntent>(m_player);
        if (playerPos != nullptr) {
            const auto* playerHealth = registry.try_get<ecs::Health>(m_player);
            bgfx::dbgTextPrintf(
                0, 7, 0x0f, "player: (%.1f, %.1f) dir=%u frame=%u intent=(%.1f, %.1f) hp=%.0f",
                playerPos->x, playerPos->y,
                playerFacing != nullptr ? static_cast<unsigned>(playerFacing->dir) : 0u,
                playerAnim != nullptr ? static_cast<unsigned>(playerAnim->frame) : 0u,
                playerIntent != nullptr ? playerIntent->x : 0.0f,
                playerIntent != nullptr ? playerIntent->y : 0.0f,
                playerHealth != nullptr ? playerHealth->current : 0.0f);
        }
    }
    // M4-T6：寻路统计（提交/完成/失败分解 + 后台耗时 + 位图缓存）
    if (m_ctx.pathfinding != nullptr && m_ctx.ecs != nullptr) {
        const ai::PathfindingService::Stats pathStats = m_ctx.pathfinding->GetStats();
        int following = 0;
        m_ctx.ecs->Registry().view<ecs::PathFollow>().each(
            [&](const ecs::Entity, const ecs::PathFollow& follow) {
                if (follow.status == ecs::PathStatus::Following) {
                    ++following;
                }
            });
        bgfx::dbgTextPrintf(
            0, 8, 0x0f,
            "path: query=%llu found=%llu unreach=%llu invalid=%llu budget=%llu | pending=%d avg=%.2fms | cached=%zu | following=%d",
            static_cast<unsigned long long>(pathStats.submitted),
            static_cast<unsigned long long>(pathStats.found),
            static_cast<unsigned long long>(pathStats.unreachable),
            static_cast<unsigned long long>(pathStats.invalid),
            static_cast<unsigned long long>(pathStats.budget), pathStats.pending,
            pathStats.avgMs, m_ctx.pathfinding->CachedChunks(), following);
    }
    // M5-T2/T3：AI 状态分布 + 感知统计（每 tick 统计；swings = 攻击脉冲累计）
    if (m_ctx.aiSystem != nullptr) {
        const ai::AiSystem::Stats aiStats = m_ctx.aiSystem->GetStats();
        const ai::PerceptionSystem::Stats percStats =
            m_ctx.perception != nullptr ? m_ctx.perception->GetStats()
                                        : ai::PerceptionSystem::Stats{};
        bgfx::dbgTextPrintf(
            0, 10, 0x0f,
            "ai: idle=%d wander=%d chase=%d search=%d attack=%d dead=%d | swings=%llu | "
            "seeing=%d alert=%d heard=%d",
            aiStats.idle, aiStats.wander, aiStats.chase, aiStats.search, aiStats.attack,
            aiStats.dead, static_cast<unsigned long long>(aiStats.swings), percStats.seeing,
            percStats.alerted, percStats.heard);
    }
    bgfx::dbgTextPrintf(0, 9, 0x07,
                        "WASD/LMB move | SPACE light | LSHIFT heavy | LCTRL block | Z dodge | F pickup | E use | R equip/reload | G drop | I bag | L task | K abandon | F6 demo-kill | F7 debug-gunshot | V fire (SP)/projectile (MP) | R reload (build rotate first) | F4 cam | F9 grid | F1 pause | F2 speed | F5 reload | ESC quit");
    // M5-T5：LOD 档位 + 虚拟群落统计（虚拟化 = 统计层，不入 registry）
    if (m_ctx.lod != nullptr) {
        const ai::LodSystem::Stats lodStats = m_ctx.lod->GetStats();
        ai::VirtualHordeStore::Stats vStats{};
        if (m_ctx.virtualHordes != nullptr) {
            vStats = m_ctx.virtualHordes->GetStats(camGrid.x, camGrid.y);
        }
        bgfx::dbgTextPrintf(0, 11, 0x0f,
                            "lod: full=%d (60Hz) mid=%d (30Hz) far=%d (10Hz) | virtual: hordes=%d zombies=%d nearest=%.0f",
                            lodStats.fullCount, lodStats.midCount, lodStats.farCount,
                            vStats.hordes, vStats.zombies, vStats.nearestDist);
    }
    if (m_ctx.animalSystem != nullptr) {
        const game::AnimalSystem::Stats animalStats =
            m_ctx.animalSystem->GetStats();
        bgfx::dbgTextPrintf(
            0, 12, 0x0f,
            "animals: live=%d seeded=%d pending=%d | spawned=%llu restored=%llu far=%llu | flee=%d retaliate=%d heard=%llu attacks=%llu",
            animalStats.live, animalStats.seededChunks, animalStats.pendingChunks,
            static_cast<unsigned long long>(animalStats.spawned),
            static_cast<unsigned long long>(animalStats.restored),
            static_cast<unsigned long long>(animalStats.skippedFar),
            animalStats.fleeing, animalStats.retaliating,
            static_cast<unsigned long long>(animalStats.heard),
            static_cast<unsigned long long>(animalStats.attacks));
    }
    // M9-T4：点击移动/拾取/战斗提示改由 RmlUi HUD 浮字显示（支持中文）。
    // M5-T6：战斗统计（挥击/命中/伤害/击杀累计 + 本帧硬直数）
    if (m_ctx.combat != nullptr) {
        const characters::CombatSystem::Stats combatStats = m_ctx.combat->GetStats();
        bgfx::dbgTextPrintf(
            0, 13, 0x0f, "combat: swings=%llu hits=%llu dmg=%.0f kills=%llu | staggered=%d",
            static_cast<unsigned long long>(combatStats.swings),
            static_cast<unsigned long long>(combatStats.hits), combatStats.damageDealt,
            static_cast<unsigned long long>(combatStats.kills), combatStats.staggered);
    }
    // M5-T7：刷怪/虚拟化统计（live/virtual 存量 + 本局累计 实体化/虚拟化/补给）
    if (m_ctx.spawnLod != nullptr) {
        const ai::SpawnLodSystem::Stats spawnStats = m_ctx.spawnLod->GetStats();
        bgfx::dbgTextPrintf(0, 14, 0x0f,
                            "spawn: live=%d virtual=%d hordes=%d | mat=%llu virt=%llu refill=%llu",
                            spawnStats.live, spawnStats.virtualZombies, spawnStats.hordes,
                            static_cast<unsigned long long>(spawnStats.materialized),
                            static_cast<unsigned long long>(spawnStats.virtualized),
                            static_cast<unsigned long long>(spawnStats.spawned));
    }
    // M6-T2/T3：背包（件数/重量/容量）+ 地面物品数（睡眠化：远处物品仍在场但不进渲染收集）
    if (m_ctx.ecs != nullptr && m_player != entt::null && m_ctx.items != nullptr) {
        const auto* container = m_ctx.ecs->Registry().try_get<ecs::Container>(m_player);
        if (container != nullptr) {
            int itemCount = 0;
            for (const ecs::ItemInstance& entry : container->items) {
                itemCount += entry.count;
            }
            bgfx::dbgTextPrintf(0, 15, 0x0f, "bag: %d item(s) %.1f/%.1fkg | ground=%d",
                                itemCount, items::TotalWeight(*container, *m_ctx.items),
                                container->capacityKg, game::CountGroundItems(*m_ctx.ecs));
        }
        // M6-T5：装备槽（装??动作??AppendActorPresentation 落世界；此处 HUD 展示??
        const auto* equipment = m_ctx.ecs->Registry().try_get<ecs::Equipment>(m_player);
        if (equipment != nullptr) {
            const auto& primary =
                equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
            const auto& body = equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::Body)];
            bgfx::dbgTextPrintf(0, 16, 0x0f, "equip: primary=%s body=%s",
                                primary.has_value() ? primary->defId.c_str() : "-",
                                body.has_value() ? body->defId.c_str() : "-");
        }
    }
}

}  // namespace zl::game
