#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "ai/components/AiComponents.h"
#include "characters/components/CharacterComponents.h"
#include "characters/components/CombatComponents.h"
#include "characters/components/ProgressionComponents.h"
#include "characters/systems/CombatSystem.h"
#include "characters/systems/StealthRules.h"
#include "core/AppState.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "game/AssetProvider.h"
#include "game/BaseSiege.h"
#include "game/CombatAudio.h"
#include "game/Companion.h"
#include "game/Dialogue.h"
#include "game/BuildingRules.h"
#include "game/GameContext.h"
#include "game/FoodSafety.h"
#include "game/Forage.h"
#include "game/ForageRuntime.h"
#include "game/Homestead.h"
#include "game/Sleeping.h"
#include "game/VehicleComponents.h"
#include "game/VehicleMods.h"
#include "game/VehicleSpike.h"
#include "game/Missions.h"
#include "game/Objectives.h"
#include "game/Poi.h"
#include "game/PoiComponents.h"
#include "game/WorldLifecycle.h"
#include "game/WorldMap.h"
#include "game/ProgressionRules.h"
#include "game/RenderCollectSystem.h"
#include "game/Settlement.h"
#include "game/Throwing.h"
#include "items/Crafting.h"
#include "items/ItemComponents.h"
#include "items/RangedWeapon.h"
#include "net/Command.h"
#include "net/PlayerState.h"
#include "render/CutawayLogic.h"
#include "render/DrawList.h"
#include "render/BuildingOcclusion.h"
#include "render/WindowPeek.h"
#include "render/IsoMath.h"
#include "save/SaveService.h"
#include "ui/InventoryUi.h"
#include "ui/MenuUi.h"
#include "ui/VehicleModsUi.h"
#include "world/GridSquare.h"

namespace zl::world {
class Chunk;
}

namespace zl::render {
class SpriteBatch;
}

namespace zl::items {
struct ItemDef;
}

namespace zl::game {

struct AnimalDef;
struct CombatPresentationState;
struct FireTarget;
struct MeleeSwingPresentation;

// M3-T5：世界渲染接入（World + ChunkManager 流式加载 → 逐格收集精灵实例 → SpriteBatch）。
// 收集分两段（对齐 PZ SpriteRenderer 的批处理思路）：
//   0) 地面层（Floor）整批先画：地面块之间互不遮挡，按纹理分批即 1~3 个 drawcall；
//   1) 其余层：全局画家序（z → x+y → x → 格内层序）——墙/树/草丛互相遮挡必须保序。
//   实体高度精灵（M4-T2）由 RenderCollectSystem 追加进同一份列表（render::DrawItem）。
// 本状态只读 ChunkManager 已挂接（Loaded）的 chunk；加载/卸载全在后台+预算内完成。
class WorldDemoState final : public core::AppState {
public:
    explicit WorldDemoState(GameContext& context, std::string loadSaveId = {})
        : m_ctx(context), m_loadSaveId(std::move(loadSaveId)) {}

    const char* Name() const override { return "WorldDemo"; }

    void OnEnter() override;
    void OnExit() override;
    void OnTick(double tickDt) override;
    void OnFrame(double frameDt, double alpha) override;

private:
    void HandleFrameInput(double frameDt);
    bool CanToggleStealth() const;
    bool IsGatheringActionActive() const;
    void ToggleStealth();
    void SetLocalStealth(bool crouched);
    void ExitStealth(characters::StealthExitReason reason);
    void UpdateStealthState();
    // M13-T5：把 EntityReplicationClient 副本同步成渲染用 ECS 实体（不参与本地模拟）。
    void SyncNetworkReplicas(float alpha);
    // X-F1：把服务器下发的单格 chunk diff 应用到已加载的客户端 chunk。
    void ApplyNetworkChunkDiffs();
    // M23-F1-T11：播放服务器广播的远端枪声并生成枪口效果。
    void ApplyNetworkWorldSounds();
    // M13-T7：把服务器下发到本会话的背包/装备/配方/制作状态镜像到本地玩家实体。
    void ApplyNetworkPlayerState();
    void RefreshSocialState();
    void SendChat(const std::string& text, std::string& error);
    // M10-T5：会话初始化/读档与后台存档（快照只在主线程采集）
    bool LoadFromSave();
    void RestoreRuntimeComponents(const std::vector<ecs::Entity>& loaded);
    void RequestQuickSave(bool withThumbnail);
    bool CaptureSaveChunks(std::vector<save::ChunkDiff>* out, std::string* error);
    void SubmitPendingSave();
    void PollSaveScreenshot(double frameDt);
    void HandleSaveFinished(bool ok, bool created, const std::string& id,
                            const std::string& error);
    void ConsumeSaveWarning();
    void CollectWorld();
    // M11-T3：脚步（按地面材质）与听者更新；世界声音走 OnSound（音频 + AI 共用）
    void UpdateAudio(double frameDt);
    // M15-T3：玩家受击 → 方向/音效/HUD 边缘提示（按 HitReaction.hitSerial 去重）
    void UpdateCombatFeedback(double frameDt);
    void SpawnCombatFeedbackSparks(
        ecs::Entity entity, characters::CombatSystem::BlockOutcome outcome);
    void SpawnMeleeHitSparks(ecs::Entity target, bool poiseBroken);
    void SpawnDemoEntities();
    void DespawnDemoEntities();
    // M6-T3：地面物品——村庄附近散落（首次帧、目标 chunk 就绪后）+ F 键拾取最近 + 退出清理
    void EnsureDemoItemsScattered();
    void PickupNearestItem();
    void DespawnGroundItems();
    // M14-T5：村庄演示可搜刮容器；打开背包时自动搜索最近容器并展示其内容
    void EnsureDemoLootContainers();
    void EnsureHouseLootContainers();
    // M19-F2：POI 容器/守军/任务与 HUD 指引（单机）。
    void InitializePoiRuntime();
    void EnsurePoiContent();
    void UpdatePoiTasks(float dt);
    ecs::PoiWorldProgress* PoiWorldProgressState();
    void UpdateStagedPoi(const PoiDef& poi, ecs::PoiWorldProgress* world,
                         ecs::PoiTasks* tasks, float dt);
    bool FindPoiStartCandidate(const PoiDef& poi, float x, float y,
                               const PoiEntryDef** entry) const;
    bool PlayerHasPoiKeyItem(const PoiKeyDef& key) const;
    bool ConsumePoiKeyItem(const PoiKeyDef& key);
    bool CheckPoiStageKeys(const PoiDef& poi, const PoiProgress& progress,
                           const PoiStageDef& stage,
                           std::string* reason) const;
    bool ConsumePoiStageKeys(const PoiDef& poi, PoiProgress* progress,
                             const PoiStageDef& stage);
    void GrantPoiStageReward(const PoiDef& poi, const PoiStageDef& stage,
                             ecs::PoiTasks* tasks);
    void GrantPoiStageKeys(const PoiDef& poi, const PoiStageDef& stage);
    void ApplyPoiStageUpdate(const PoiDef& poi, const PoiStageUpdate& update,
                             ecs::PoiTasks* tasks, int attempts);
    void ShowPoiResult(std::string title, std::string body, std::string tone,
                       float seconds = 10.0f);
    bool TryInteractPoi();
    bool TryChoosePoiBranchInput(core::Input& input);
    void RefreshPoiStageGuide(ui::HudState& hud, bool* handled);
    void RefreshPoiStageHint(ui::HudState& hud);
    bool PoiTargetSearched(const PoiDef& poi) const;
    int CountPoiGuards(std::string_view poiId) const;
    bool FindPoiGuardCell(const PoiDef& poi, int index, int* wx, int* wy) const;
    bool FindPoiStageEnemyCell(const PoiDef& poi, const PoiStageDef& stage,
                               int index, float originX, float originY,
                               int* wx, int* wy) const;
    ecs::Entity SpawnPoiStageEnemy(const PoiDef& poi, const PoiStageDef& stage,
                                   PoiStageEnemyRole role, PoiGuardKind kind,
                                   int index, float originX, float originY);
    void EnsurePoiStageEnemies(const PoiDef& poi, PoiProgress* progress);
    void UpdatePoiBossMechanics(const PoiDef& poi, const PoiProgress& progress,
                                float dt);
    void ClearPoiStageEnemies(std::string_view poiId,
                              std::string_view stageId = {});
    bool PoiPlayerNearStart(const PoiDef& poi, float x, float y) const;
    const ecs::PoiTasks* PlayerPoiTasks() const;
    void RefreshPoiGuide(ui::HudState& hud);
    bool IsM31World() const;
    void RefreshObjectiveHud(ui::HudState& hud);
    void InitializeMissionRuntime();
    void UpdateMissions();
    void UpdateMissionAvailability(ecs::MissionLog& log, int day);
    void TryActivateRefugeeMission(ecs::MissionLog& log, int day);
    void EnsureRandomEventContentForActiveMissions(ecs::MissionLog& log,
                                                   int day);
    void PruneRandomEventActors();
    void ActivateMission(ecs::MissionLog& log, const MissionDef& def, int day,
                         std::string_view settlementId = {});
    void CompleteMission(ecs::MissionRunState& state, const MissionDef& def);
    void FailMission(ecs::MissionRunState& state, const MissionDef& def,
                     std::string_view reason, int day);
    bool GrantMissionReward(const MissionDef& def);
    void RecordMissionEvent(MissionObjectiveKind kind, std::string_view target,
                            int amount);
    void CycleMissionFocus();
    void AbandonFocusedMission();
    void RefreshMissionGuide(ui::HudState& hud);
    void RefreshEventGuide(ui::HudState& hud);
    void RefreshRegionEventGuide(ui::HudState& hud);
    void RefreshRegionGuide(ui::HudState& hud);
    void RefreshStrategicGuide(ui::HudState& hud);
    void UpdateWorldEventUi(float frameDt);
    void RefreshWorldEventHud(ui::HudState& hud);
    void RefreshNetworkWorldEventHud(ui::HudState& hud);
    bool TryInteractWorldEvent();
    bool TryChooseWorldEvent(int index);
    void PushWorldEventLog(std::string line);
    void UpdateWorldMapDiscovery();
    void RefreshWorldMapState();
    void ToggleWorldMap();
    void OpenWorldMap();
    void CloseWorldMap();
    void HandleWorldMapSelect(const std::string& poiId);
    void HandleWorldMapStrategicAction(
        const std::string& regionId, game::WorldMapStrategicAction action);
    void PollStrategicActionResult();
    std::vector<const MissionDef*> SortedActiveMissions() const;
    bool MissionTargetPoint(const MissionDef& def,
                            const ecs::MissionRunState& state, float* x,
                            float* y) const;
    ecs::MissionLog* PlayerMissions();
    const ecs::MissionLog* PlayerMissions() const;
    ecs::MissionRunState* FocusedMissionState();
    std::string FocusedMissionId() const;
    int CurrentMissionDay() const;
    std::string CurrentSaveId() const;
    void SearchNearbyLootContainer();
    void UpdateForage(float tickDt);
    void RefreshForageNodes(float tickDt);
    void EnsureForageState();
    bool TryStartForageSearch();
    void UpdateForageSearch(float tickDt);
    void CompleteForageSearch();
    void CancelForageSearch(std::string_view reason, bool notice);
    const ForageNode* NearestForageNode(float range) const;
    bool HasForageTool(const ForageZoneDef& zone, std::string* toolName) const;
    bool WearForageTool(const ForageZoneDef& zone);
    std::string ForageToolName(const ForageZoneDef& zone) const;
    void UpdateFishing(float tickDt);
    void UpdateFishingSearch(float tickDt);
    bool TryStartFishing();
    void ResolveFishing(bool caught);
    void CancelFishing(std::string_view reason, bool notice);
    const FishingSpot* NearestFishingSpot(float range) const;
    bool HasFishingRod(const FishingSpotDef& spot) const;
    int FishingBaitCount(const FishingSpotDef& spot) const;
    bool ConsumeFishingBait(const FishingSpotDef& spot);
    bool WearFishingRod(const FishingSpotDef& spot);
    std::string FishingItemName(std::string_view defId) const;
    const AnimalDef* FindNearestAnimalCorpse(float range,
                                             ecs::Entity* corpse = nullptr) const;
    bool TryStartAnimalHarvest();
    void UpdateAnimalHarvest(float tickDt);
    void CompleteAnimalHarvest();
    void CancelAnimalHarvest(std::string_view reason, bool notice);
    void CancelNetworkAnimalHarvest();
    bool HasAnimalHarvestTool(const AnimalDef& def,
                              std::string* toolName = nullptr) const;
    std::string AnimalCorpseName(const AnimalDef& def) const;
    void UpdateObjective();
    bool TryInteractRadio();
    bool PlayerHasRadioPart() const;
    bool RadioPartExistsInWorld() const;
    // M14-T8：单机局结束（撤离成功/死亡）→ 冻结世界并显示结算页。
    void FinishRun(bool victory);
    void UpdateRunStats(double tickDt);
    std::string BuildRunStats() const;
    // M16-T1：单机建造模式与木栅栏防御。
    struct PlayerBuildTarget {
        int wx = 0;
        int wy = 0;
        std::size_t objectIndex = 0;
        world::SquareLayer layer = world::SquareLayer::Furniture;
        const BuildableDef* def = nullptr;
        float hp = 0.0f;
        std::string sprite;
        std::uint64_t structureId = 0;
        std::uint64_t ownerStableId = 0;
    };
    enum class PendingDemolishKind : std::uint8_t {
        None,
        Map,
        Entity,
    };
    struct PendingDemolish {
        PendingDemolishKind kind = PendingDemolishKind::None;
        int wx = 0;
        int wy = 0;
        int layer = 0;
        std::uint64_t token = 0;
        float remaining = 0.0f;
    };
    struct ForageAction {
        bool active = false;
        std::string zoneId;
        std::string zoneName;
        std::string tableId;
        int x = 0;
        int y = 0;
        int z = 0;
        float elapsed = 0.0f;
        float duration = 0.0f;
        std::uint32_t startHitSerial = 0;
    };
    struct AnimalHarvestAction {
        bool active = false;
        std::uint64_t corpseId = 0;
        std::string speciesId;
        std::string corpseName;
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
        float elapsed = 0.0f;
        float duration = 0.0f;
        std::uint32_t startHitSerial = 0;
    };
    struct VehicleModAction {
        bool active = false;
        bool install = true;
        ecs::Entity vehicle = entt::null;
        std::string defId;
        VehicleModSlot slot = VehicleModSlot::Armor;
        float elapsed = 0.0f;
        float duration = 0.0f;
    };
    struct FishingAction {
        bool active = false;
        bool biting = false;
        std::string spotId;
        std::string spotName;
        std::string tableId;
        std::string trashTableId;
        std::string rodDefId;
        std::string baitDefId;
        int x = 0;
        int y = 0;
        int z = 0;
        int waterX = 0;
        int waterY = 0;
        int waterZ = 0;
        float elapsed = 0.0f;
        float waitDuration = 0.0f;
        float biteElapsed = 0.0f;
        float biteDuration = 0.0f;
        float catchChance = 0.0f;
        std::uint32_t startHitSerial = 0;
    };
    struct BaseHudSnapshot {
        bool active = false;
        int storageCount = 0;
        float storageWeight = 0.0f;
        float storageCapacity = 0.0f;
        int beds = 0;
        int campfires = 0;
        int workbenches = 0;
        int intactDefenses = 0;
        int damagedDefenses = 0;
        int farms = 0;
        int farmGrowing = 0;
        int farmMature = 0;
        int farmDiseased = 0;
        int farmThirsty = 0;
        int farmFertilized = 0;
        int generators = 0;
        int generatorsOnline = 0;
        int fridges = 0;
        int fridgesPowered = 0;
        int traps = 0;
        int trapsArmed = 0;
        int floodlights = 0;
        int floodlightsPowered = 0;
        int electricFences = 0;
        int electricFencesPowered = 0;
        float powerFuelMinutes = 0.0f;
        float powerLoad = 0.0f;
        bool powerLowFuel = false;
        bool powerOutage = false;
    };
    struct PowerGlow {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
    };
    void UpdateBaseHud(float frameDt);
    void ScanBaseHud();
    void InitializeHomestead();
    void EnsureHomesteadRuntime();
    void EnsureSettlementNpcs();
    void StoreSettlementState();
    void ApplySettlementState();
    bool SettlementDefenseBlocked(std::string_view settlementId, bool* fallen,
                                  bool* raidActive) const;
    void UpdateHomestead(float tickDt);
    bool TryInteractHomestead();
    bool TryInteractStrategicRecovery();
    bool TryFarmCare();
    void CycleFarmSeed();
    const CropDef* SelectedFarmCrop() const;
    FarmSeason CurrentFarmSeason() const;
    bool OpenTrade(ecs::Entity merchant);
    void CloseTrade();
    bool TradeNpcInRange() const;
    bool TryBuyTradeOffer(int index);
    void HandleTradeInput(core::Input& input);
    bool TryInteractSettlementNpc();
    bool FindNearbySettlementNpc(float range, ecs::Entity* out) const;
    bool DialogueNpcInRange() const;
    void OpenSettlementDialogue(ecs::Entity npc);
    void CloseSettlementDialogue();
    void EnterDialogueNode(std::string_view nodeId);
    const DialogueNode* CurrentDialogueNode() const;
    DialogueContext MakeDialogueContext() const;
    std::vector<const DialogueOption*> CurrentDialogueOptions() const;
    void HandleDialogueInput(core::Input& input);
    void ExecuteDialogueOption(const DialogueOption& option);
    void SyncDialogueHud(ui::HudState& hud);
    void SyncSettlementHud(ui::HudState& hud) const;
    const SettlementDef* CurrentSettlementForHud() const;
    int SettlementReputationForHud(std::string_view settlementId,
                                   int fallback) const;
    void PollSettlementDefenseNotices();
    std::string SettlementNpcHint() const;
    bool AcceptSettlementMissionFromDialogue();
    bool FocusSettlementMissionFromDialogue();
    bool RecruitCompanionFromDialogue();
    bool DismissCompanionFromDialogue();
    // M33-F3-T7：同伴面板、HUD、对话/赠礼/委托/救援交互。
    bool FindOwnedCompanionEntity(ecs::Entity* out) const;
    bool BuildCompanionUiSnapshot(ui::CompanionUiState* out) const;
    void SyncCompanionHud(ui::HudState& hud) const;
    void ToggleCompanionPanel();
    void CloseCompanionPanel();
    void HandleCompanionPanelInput(core::Input& input);
    bool OpenCompanionDialogue(ecs::Entity companion);
    bool TryRescueDownedCompanion();
    bool TryInteractCompanion();
    bool RescueOwnedCompanion();
    bool SetOwnedCompanionCommand(CompanionCommand command);
    bool GiftToOwnedCompanionFromUi();
    bool AcceptCompanionQuestFromDialogue();
    bool AbandonCompanionQuestFromDialogue();
    std::string CompanionRecruitRequirementText() const;
    enum class HomesteadTargetKind {
        None,
        Farm,
        Generator,
        Floodlight,
        Trap,
        PlacedTrap,
        Merchant,
    };
    struct HomesteadTarget {
        HomesteadTargetKind kind = HomesteadTargetKind::None;
        ecs::Entity entity = entt::null;
        float distanceSq = 0.0f;
    };
    HomesteadTarget FindNearestHomesteadTarget() const;
    ecs::Entity FindNearbyFarmPlot() const;
    ecs::Entity FindNearbyGenerator() const;
    ecs::Entity FindNearbyFloodlight() const;
    ecs::Entity FindNearbyTrap() const;
    ecs::Entity FindNearbyPlacedTrap() const;
    const BuildableDef* PlacedTrapBuildable(ecs::Entity entity) const;
    ecs::Entity FindNearbyMerchant() const;
    bool TryDemolishHomestead();
    void UpdateDemolishConfirm(float tickDt);
    bool BeginOrConfirmDemolish(PendingDemolishKind kind, int wx, int wy,
                                int layer, std::uint64_t token,
                                const std::string& name);
    bool PendingDemolishMatches(PendingDemolishKind kind, int wx, int wy,
                                int layer, std::uint64_t token) const;
    std::uint64_t LocalPlayerStableId() const;
    std::uint64_t EntityOwnerStableId(ecs::Entity entity) const;
    bool CanEditLocalStructure(std::uint64_t ownerStableId) const;
    std::string StructureOwnerLabel(std::uint64_t ownerStableId) const;
    std::string StructureOwnerSuffix(std::uint64_t ownerStableId) const;
    std::string HomesteadHint(std::string* tone = nullptr) const;
    std::string TradePanelText() const;
    const BuildableDef* BuildableById(std::string_view id) const;
    void RebuildBuildables();
    void RefreshDynamicContent();
    void ToggleBuildMode();
    void UpdateBuilding(float tickDt);
    void TrackNearbyBarricades();
    const BuildableDef* BuildableForSprite(std::string_view sprite) const;
    bool FindNearestPlayerBuiltStructure(float range, bool barricadesOnly,
                                         bool damagedOnly, PlayerBuildTarget* out,
                                         std::string_view requiredId = {},
                                         bool includeFloors = false,
                                         bool editableOnly = false) const;
    ecs::Entity FindNearestFloodlight(float range, bool damagedOnly,
                                      float* outDistance = nullptr,
                                      bool editableOnly = false) const;
    bool TryRepairNearestBarricade();
    bool TryRepairAllStructures();
    bool TryDemolishNearestStructure();
    int CountDamagedPlayerStructuresNear(int* woodCost) const;
    bool FindNearbyDoor(PlayerBuildTarget* out,
                        bool editableOnly = false) const;
    bool TryToggleNearestDoor();
    bool IsCellOccupiedByLiving(int wx, int wy) const;
    float BuildTargetDistanceSq(const PlayerBuildTarget& target) const;
    void ToggleBuildOrientation();
    std::string BuildMenuText() const;
    ecs::Entity FindNearbyStorage(bool includeVehicle = true) const;
    bool CanAccessLocalStorage(ecs::Entity storage);
    const BuildableDef* StorageBuildable(ecs::Entity storage) const;
    std::string StorageName(ecs::Entity storage) const;
    void StoreBagItemInStorage(int index);
    void StoreAllBagInStorage();
    void TakeAllFromStorage(std::uint64_t fromEntityId = 0);
    void TakeAllFromLootContainer(ecs::Entity containerEntity);
    void SortNearbyStorage();
    bool TryDemolishStorage(ecs::Entity storage);
    bool CanPlaceBuild(const BuildableDef& def, int wx, int wy,
                       std::string* reason) const;
    bool TryPlaceSelectedBuild(int wx, int wy);
    // M35-F3-T5：按格坐标的脚本建造动作（不走建造模式/最近目标）。
    bool BuildCellInActionRange(int wx, int wy, std::string& error) const;
    bool FindPlayerBuiltStructureAt(int wx, int wy,
                                    PlayerBuildTarget* out) const;
    bool FindPlayerBuiltEntityAt(int wx, int wy, ecs::Entity* out,
                                 const BuildableDef** def) const;
    bool TryPlaceBuildAt(const BuildableDef& def, int wx, int wy, bool vertical,
                         std::string& error);
    bool TryDemolishBuildAt(int wx, int wy, std::string& error);
    bool TryRepairBuildAt(int wx, int wy, std::string& error);
    bool TryToggleDoorAt(int wx, int wy, std::string& error);
    void RemoveBarricade(std::uint64_t key);
    void SyncBarricadeHp(std::uint64_t key);
    void RebuildChunkCollision(int chunkX, int chunkY);
    void AppendBuildGhost();
    const BuildableDef* SelectedBuildable() const;
    void InitializeBaseSiege();
    void UpdateBaseSiege(float tickDt);
    void UpdateTraps(float tickDt);
    void UpdateElectricFences(float tickDt);
    void UpdatePowerDevices(float gameMinutes, float tickDt);
    bool FindFloodlightLure(float x, float y, float* lureX,
                            float* lureY) const;
    bool FindGeneratorNoise(float x, float y, float* lureX,
                            float* lureY) const;
    void DriveBaseSiegeZombies();
    int CountPlayerStructuresNear(float x, float y, float radius,
                                  bool barricadesOnly) const;
    std::vector<BaseSiegeStructure> ListPlayerStructuresNear(
        float x, float y, float radius) const;
    bool BaseSiegeApproachPoint(const BaseSiegeAttackPlan& plan, float* x,
                                float* y) const;
    std::string GrantBaseSiegeReward(const BaseSiegeResult& result);
    bool IsBaseSiegeSpawnCell(int wx, int wy) const;
    std::uint64_t SpawnBaseSiegeZombie(int wx, int wy,
                                       ecs::ZombieSpecialKind special);
    void DespawnBaseSiegeZombie(std::uint64_t stableId);
    std::string SpecialWarningText() const;
    void ShowBaseSiegeNotice(const std::string& text,
                             const std::string& tone);
    void PollLocalRegionalSiegeReport();
    void OpenSiegeReport(const std::string& text, const std::string& tone);
    bool HasSiegeMarkers() const;
    void DrawSiegeOverlay(render::SpriteBatch& batch);
    void DrawCombatOverlay(render::SpriteBatch& batch);
    void DrawLocalCombatOverlay(render::SpriteBatch& batch);
    void DrawRemoteCombatOverlay(render::SpriteBatch& batch);
    void DrawRemoteStealthOverlay(render::SpriteBatch& batch);
    void DrawCombatStanceOverlay(render::SpriteBatch& batch,
                                 const glm::vec2& ground, float faceX,
                                 float faceY,
                                 const CombatPresentationState& presentation);
    void DrawMeleeSwingOverlay(render::SpriteBatch& batch,
                               const glm::vec2& ground, float faceX,
                               float faceY,
                               const MeleeSwingPresentation& presentation);
    void DrawFireOverlay(render::SpriteBatch& batch);
    void DrawAnimalCorpseHighlight(render::SpriteBatch& batch);
    void HandleSleepInput(core::Input& input);
    void UpdateSleep(float tickDt);
    bool TryBeginSleep();
    void ConfirmSleep();
    void StartSleep(float durationMinutes, bool untilDawn);
    void FinishSleep(SleepStopReason reason, float damage = 0.0f);
    void HaltPlayerForSleep();
    bool FindNearbyBed(PlayerBuildTarget* out) const;
    bool FindNearbyCampfire(int* wx, int* wy) const;
    bool FindNearbyActiveFire(FireTarget* out) const;
    bool TryExtinguishNearbyFire(const FireTarget& target);
    bool TryToggleCampfireFire();
    bool ZombieNearPlayer(float radius) const;
    std::string SleepStatusText() const;
    std::string SleepClockText() const;
    // M19-F1：经验/技能/Perk（单机权威；联机从 PlayerState 只读镜像）。
    void InitializeProgressionRuntime();
    void UpdateProgression(float tickDt);
    void ApplyProgressionRuntime();
    void GrantProgressionXp(ecs::SkillKind kind, float baseAmount,
                            const char* reason);
    bool SelectPerk(int index);
    ecs::Progression* PlayerProgression();
    const ecs::Progression* PlayerProgression() const;
    int PendingPerkPoints() const;
    std::array<std::string, 3> CurrentPerkChoices() const;
    float RepairAmountForPlayer() const;
    float CraftSecondsForPlayer(const items::RecipeDef& recipe,
                                float efficiency) const;
    // M6-T5：装备/使用闭环（键盘快捷键；M9-T5 起背包 UI 走同一套容器/装备规则）
    void UseConsumableItem();       // E：使用背包中第一个带效果的物品（消耗 1 件）
    void ToggleEquipWeapon();       // R：主手武器 ↔ 背包第一件武器（换装/卸下）
    void DropFirstItem();           // G：丢弃背包第一堆（整堆落地面）
    void AppendActorPresentation();  // 装备/动作表现：收集后追加 DrawItem（排序前）
    void RefreshInventory();
    void HandleInventoryAction(const ui::InventoryAction& action);
    void UpdateCrafting(float tickDt);
    bool StartCraftBatch(const items::RecipeDef& recipe, int requested,
                         std::string& error, int& actual);
    bool StartNextQueuedCraft(std::string_view prefix);
    std::string QueuedCraftLabel(std::string_view recipeId) const;
    void EmitCraftStart(const items::RecipeDef& recipe, int batchCount);
    void EmitCraftFinished(std::string_view recipeId, std::string_view outputDefId,
                           int outputCount, int batchCompleted, bool failed);
    void SyncCraftingJobToEcs();
    void RestoreCraftingJobFromEcs();
    // M12-T1：玩家操作统一走 Command（direct 立即执行 / loopback 环回执行）
    net::Command MakePlayerCommand(net::CommandType type) const;
    bool ExecutePlayerCommand(const net::Command& command, std::string& error);
    bool SubmitPlayerCommand(const net::Command& command);
    bool SubmitPlayerCommand(const net::Command& command, bool& queued,
                             std::string& error);
    void SubmitMoveCommand(float screenX, float screenY);
    void SubmitInventoryAction(const ui::InventoryAction& action);
    // M9-T6：Esc 暂停菜单（保留当前世界状态；返回主菜单后“继续”可恢复）
    void OpenPauseMenu();
    void ResumeFromMenu();
    void HandleMenuAction(ui::MenuActionType action);
    void EquipBagItem(int index, ecs::EquipSlot slot);
    void EquipLootItem(std::uint64_t stableId, ecs::EquipSlot slot, int index = -1);
    void UnequipEquipment(int index);
    void UseBagItem(int index);
    bool ApplyUseInfectionRisk(float risk, std::string_view defId);
    void ApplyFoodUse(const items::ItemDef& def, const items::UseResult& use);
    void EmitItemUsed(std::string_view defId, bool spoiled, int remaining);
    bool UseBlueprintItem(int index, const items::ItemDef& def);
    void RepairBagItem(int index);
    // recipes-v3：工作台/制作站范围（空 station = 任意地点）
    std::string StationLabel(std::string_view station) const;
    bool StationNearby(std::string_view station, float x, float y, int z) const;
    bool RecipeKnown(std::string_view recipeId) const;
    void DropBagItem(int index);
    void DropEquipmentItem(int index);
    void PickupLootItem(std::uint64_t stableId, int index = -1);
    void ReturnOrDropItem(const ecs::ItemInstance& item, bool* dropped = nullptr);
    // M5-T1：演示击杀——离参考点（玩家/相机中心）最近的存活游荡者 HP 归零 → DeathSystem 转尸体
    void KillNearestDemoEntity();
    // M5-T4：演示开枪——玩家位置发射大半径 SoundEvent（事件总线 OnSound）吸引僵尸
    void EmitGunshot();
    // M5-T6/M30-T3：玩家挥击——自动瞄准最近目标；无目标时按朝向空挥。
    // heavy 使用 T1 派生/武器 heavy 覆盖，并在起手时扣战斗体力。
    void PlayerAttack(std::uint64_t targetStableId = 0, bool heavy = false);
    // M30-T4：按住格挡/闪避（单机；联机命令留 T6）。
    void PlayerBlock(bool down, bool announce);
    void PlayerDodge(float screenX, float screenY);
    // M23-F1-T3：远程武器单机状态机（V 朝鼠标开火、R 换弹）。
    bool TryGetEquippedRangedWeapon(const items::ItemDef** outDef,
                                    ecs::ItemInstance** outWeapon);
    bool TryGetAimDirection(const glm::vec2& mousePx, float* dirX,
                            float* dirY) const;
    void PlayerRangedAttack(const glm::vec2& mousePx);
    bool TryStartRangedReload();
    void UpdateRangedWeapon(float tickDt);
    // M34-F2-T2：投掷优先的瞄准上下文、射程夹取与预览（不消耗物品）。
    bool TryGetThrowableAimSource(const items::ItemDef** outDef,
                                  ecs::ItemInstance** outItem,
                                  int* outSourceIndex = nullptr);
    bool ThrowTargetCellValid(int cellX, int cellY) const;
    ThrowAimBlockReason CurrentThrowAimBlock();
    ThrowPathResult ClipThrowPathToWorld(float startX, float startY, float targetX,
                                         float targetY, int z) const;
    void ApplyThrowAimPath(const ThrowAimEvaluation& evaluation, int z);
    bool TryBeginThrowAim(const glm::vec2& mousePx);
    void UpdateThrowAimTarget(const glm::vec2& mousePx, bool announce);
    void ReleaseThrowAim();
    void CancelThrowAim(std::string_view reason, bool notice);
    void DrawThrowAimOverlay(render::SpriteBatch& batch);
    void UpdateThrownProjectiles(float tickDt);
    void SettleThrownProjectiles();
    void ResolveThrownProjectile(const ThrownProjectile& projectile);
    void DrawThrownProjectiles(render::SpriteBatch& batch);
    // M23-F1-T4：单机弹道、枪口/命中特效与投射物生命周期。
    void SpawnRangedProjectile(float startX, float startY, int z, float dirX,
                               float dirY, const items::RangedCombat& ranged,
                               std::uint64_t ownerStableId);
    void UpdateRangedProjectiles(float tickDt);
    void UpdateRangedPresentation(float tickDt);
    void SpawnRangedEffect(float x, float y, int z, float lifetime, int layer,
                           std::uint32_t tint);
    void PlayRangedSound(const char* clip, float x, float y, float maxDistance);
    void PlayCombatCue(CombatCue cue, float x, float y);
    // M4-T4：玩家（PlayerTag）——chunk 加载完成后在村庄中心附近找可走格生成
    void EnsurePlayerSpawned();
    void EnsureVehicleSpawned();
    void BindNearestLocalVehicle();
    ecs::Entity SpawnLocalVehicle(int cellX, int cellY);
    void AdoptVehicleEntity(ecs::Entity entity);
    void SyncVehicleStateToEcs();
    VehicleModStats VehicleModStatsForEntity(ecs::Entity entity) const;
    void SyncLocalVehicleModStorage(ecs::Entity entity);
    void ApplyLocalVehicleVehicleCollision(
        float impactSpeed, ecs::Entity target,
        const VehicleMotion& previousMotion,
        const VehicleModStats& activeModStats);
    float PushLocalVehicleOnCollision(ecs::Entity target, float impactSpeed);
    void UpdateLocalParkedVehicleMotions(float tickDt);
    void UpdateVehicle(float tickDt);
    void UpdateVehicleVisual();
    void SyncVehicleVisualDirectionFromSprite(ecs::Entity entity);
    void UpdateVehicleDamageSmoke(float frameDt);
    void DrawVehicleDamageSmoke(render::SpriteBatch& batch);
    void UpdateVehicleDamageVisual();
    void DrawVehicleDamageVisual(render::SpriteBatch& batch,
                                 const render::SpriteRect& rect,
                                 VehicleDamageVisualLevel level);
    VehicleSurface VehicleSurfaceAt(float x, float y, int z) const;
    bool VehicleBlockedAt(int x, int y, int z) const;
    void ApplyLocalVehicleRam(
        const VehicleMotion* previousMotion,
        const VehicleModStats& modStats);
    void ApplyLocalVehicleAttacks(const VehicleModStats& modStats);
    void SpawnVehicleHitFeedback(float wear, float x, float y, int z,
                                 bool shake = true);
    void UpdateVehicleHitFeedback(float frameDt);
    void DrawVehicleHitFeedback(render::SpriteBatch& batch);
    void SpawnVehicleMetalDebris(float strength, float x, float y, int z);
    void UpdateVehicleMetalDebris(float frameDt);
    void DrawVehicleMetalDebris(render::SpriteBatch& batch);
    void TriggerVehicleScreenShake(float strength);
    VehicleScreenShake CurrentVehicleScreenShake() const;
    void PlayVehicleHitSound(float x, float y);
    void EmitLocalVehicleNoise();
    void UpdateVehicleAudio();
    void StopVehicleAudio();
    void PlayLocalVehicleRamSound();
    void HonkVehicleHorn();
    void ToggleVehicleHeadlights();
    bool ToggleVehicle();
    bool NetworkVehicleDriverSeatOpen() const;
    bool NetworkVehicleHasPassenger() const;
    void TakeDriverSeat();
    void GiveDriverSeat();
    void EnterVehicle();
    void ExitVehicle();
    void SyncPlayerToVehicle();
    bool FindVehicleExitCell(float* x, float* y) const;
    ecs::Entity FindNearestVehicle(float range,
                                   float* outDistance = nullptr) const;
    bool VehicleNearPlayer(float range) const;
    std::string VehicleHint() const;
    bool TryServiceVehicle();
    bool OpenVehicleModsPanel(ecs::Entity vehicle);
    void CloseVehicleModsPanel();
    void RefreshVehicleModsPanel();
    void HandleVehicleModsAction(const ui::VehicleModPanelAction& action);
    void SubmitNetworkVehicleModAction(bool install, int slot);
    void StartVehicleModAction(bool install, int slot);
    void UpdateVehicleModAction(float tickDt);
    void CancelVehicleModAction(std::string_view reason, bool notice);
    bool IsVehicleModTargetValid(ecs::Entity vehicle) const;
    ecs::Entity FindVehicleForMods() const;
    const VehicleModDef* SelectedVehicleModCandidate(int slot) const;
    void SyncNetworkVehicleBinding();
    void UpdateNetworkVehiclePrediction(float frameDt);
    void ApplyNetworkDriverVisibility();
    bool IsSolidSquare(int wx, int wy) const;
    // M4-T7：点击移动——屏幕拾取 → 目标格校验（越界/未加载/就近可走格）→ 写 PathRequest
    void HandleClickToMove(const render::GridPick& pick);
    // M4-T7：寻路反馈——玩家路径状态跃迁（到达/失败）→ HUD 提示 + 目标格标记
    void UpdatePathFeedback(double frameDt);
    void SetNotice(std::string text, uint32_t color, float seconds);
    void RefreshRangedHud(ui::HudState& hud);
    void RefreshStealthHud(ui::HudState& hud, bool networkState);
    void RefreshTrapHud(ui::HudState& hud, bool suppressed);
    void RefreshHud(double frameDt);
    // M4-T5：相机跟随（follow=平滑跟随玩家+地图边界；自由=方向键/拖拽，无边界）
    void ApplyCameraMode();
    void UpdateCameraFollow(double frameDt);
    glm::vec2 StreamingVelocity() const;
    void UpdateCruise(double frameDt);
    void LogPerfSample(const char* tag);
    void PushObject(const world::SquareObject& object, int wx, int wy, int z,
                    world::SquareLayer layer, int roomId = -1);
    world::Chunk* FindChunk(int chunkX, int chunkY) const;
    int RoomIdAt(int wx, int wy, int z) const;
    int PlayerRoomId() const;
    // M14-T10：对齐 PZ SetCutawayRoomsForPlayer —— 收集"当前房间 + 玩家 ±1.5 格内、
    // 位于朝向前方且视线可达的相邻房间"，作为本帧剖切房间集合。
    std::vector<int> ComputeCutawayRoomTargets() const;
    // M36-T4：室外正对窗格时的扒看房间（无则 -1）。
    int FindWindowPeekRoom(float px, float py, float facingX, float facingY) const;
    bool RoomLineOfSight(float px, float py, int wx, int wy) const;
    // M36-T1：按格剖切。输入变化（玩家格/房间/朝向/chunk 计数/结构重建）才重算，
    // 每格带 750ms 锁；上层按地面格隐藏，边界墙按自身与 S/E 邻格决定裁切。
    bool CutawayInputsChanged();
    void RebuildCutawaySquares(const std::vector<int>& rooms);
    // M36-T3：房间矩形按 z 聚成建筑，筛选屏幕遮挡建筑并做 750ms 迟滞。
    void RebuildBuildings();
    void UpdateOccluderBuildings(const std::vector<int>& rooms);
    const render::BuildingInfo* FindBuilding(int id) const;
    bool SquareIsCutaway(int gx, int gy, int z) const;
    // M14-T10：隐藏剖切格的上层；前方边界墙裁切（只留墙顶），而非整块消失。
    // 返回本帧是否存在启用的剖切格。
    bool ApplyRoomCutaway(const render::SpriteRect& playerRect, std::size_t playerIndex,
                          double frameDt);
    // M14-T1：玩家被墙/门挡住时淡出遮挡项（只改本帧 DrawItem 颜色）
    void ApplyWallOcclusion(double frameDt);
    void DrawWorld();
    void DrawVehicleHeadlights(render::SpriteBatch& batch);
    void DrawDebugOverlay();
    void DrawPowerOverlay(render::SpriteBatch& batch);
    void DrawForageOverlay(render::SpriteBatch& batch);
    void DrawPhysicsDebug();
    void DrawDebugText();
    glm::vec2 MouseToFramebuffer(const glm::vec2& mousePx) const;
    render::GridPick PickMouseCell(const glm::vec2& mousePx) const;

    GameContext& m_ctx;

    std::vector<render::DrawItem> m_items;
    std::string_view m_cachedSprite;
    const std::vector<AssetProvider::Entry>* m_cachedFrames = nullptr;
    const AssetCutawayMeta* m_cachedCutaway = nullptr;
    bool m_cachedFoliage = false;
    RenderCollectSystem::ViewRect m_view;  // 本帧可见范围（世界格坐标）
    int m_collectedChunks = 0;  // 本帧进入收集的已加载 chunk 数
    int m_missingChunks = 0;    // 本帧可见但尚未加载的 chunk 数

    // M4-T4：本地玩家（WASD 控制；实体由 WorldSimulation 生成）
    ecs::Entity m_player = entt::null;
    ecs::Entity m_vehicle = entt::null;
    VehicleMotion m_vehicleMotion;
    VehicleTuning m_vehicleTuning = kVehicleDefaultTuning;
    int m_localVehicleCount = 3;
    std::string m_vehicleRepairItem = "repair_kit";
    std::string m_vehicleFuelItem = "fuel_can";
    bool m_vehicleModsOpen = false;
    ecs::Entity m_vehicleModsVehicle = entt::null;
    std::array<int, ecs::kVehicleModSlotCount> m_vehicleModCandidates{};
    VehicleModAction m_vehicleModAction;
    std::string m_vehicleModsStatus;
    std::string m_vehicleModsStatusTone = "info";
    bool m_driving = false;
    bool m_riding = false;
    bool m_vehicleFuelWarned = false;
    bool m_vehicleLowConditionWarned = false;
    float m_vehicleNoiseCooldown = 0.0f;
    float m_vehicleRamWearCooldown = 0.0f;
    float m_vehicleBodyCollisionCooldown = 0.0f;
    float m_vehicleHornCooldown = 0.0f;
    float m_vehicleZombieAttackCooldown = 0.0f;
    struct VehicleEngineAudioVoice {
        std::uint64_t soundId = 0;
    };
    std::unordered_map<std::uint64_t, VehicleEngineAudioVoice>
        m_vehicleEngineVoices;
    float m_vehicleHeadingX = 0.0f;
    float m_vehicleHeadingY = 1.0f;
    VehicleVisualDirection m_vehicleVisualDirection =
        VehicleVisualDirection::South;
    struct VehicleSmokePuff {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
        float age = 0.0f;
        float lifetime = 0.0f;
        float size = 0.0f;
        float driftX = 0.0f;
        float driftY = 0.0f;
        VehicleDamageSmokeLevel level = VehicleDamageSmokeLevel::None;
    };
    std::vector<VehicleSmokePuff> m_vehicleSmokePuffs;
    struct VehicleSmokeRuntime {
        float timer = 0.0f;
        VehicleDamageSmokeLevel level = VehicleDamageSmokeLevel::None;
    };
    std::unordered_map<std::uint64_t, VehicleSmokeRuntime>
        m_vehicleSmokeRuntimes;
    std::uint32_t m_vehicleSmokeSerial = 0;
    bool m_vehicleSmokeSoftLogged = false;
    std::unordered_map<std::uint64_t, VehicleVisualDirection>
        m_vehicleVisualDirections;
    std::unordered_map<std::uint64_t, VehicleDamageVisualLevel>
        m_vehicleDamageVisuals;
    struct VehicleHitSpark {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
        float age = 0.0f;
        float lifetime = 0.0f;
        float size = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        std::uint32_t tint = 0xFFFFC050u;
    };
    std::vector<VehicleHitSpark> m_vehicleHitSparks;
    std::uint32_t m_vehicleHitSparkSerial = 0;
    struct VehicleMetalDebris {
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
        float age = 0.0f;
        float lifetime = 0.0f;
        float size = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        std::uint32_t tint = 0xFFB0B0B0u;
    };
    std::vector<VehicleMetalDebris> m_vehicleMetalDebris;
    std::uint32_t m_vehicleMetalDebrisSerial = 0;
    float m_vehicleShakeStrength = 0.0f;
    float m_vehicleShakeAge = 0.0f;
    float m_vehicleShakeDuration = 0.0f;
    VehicleSurface m_vehicleSurface = VehicleSurface::Paved;
    VehicleSurface m_lastVehicleSurface = VehicleSurface::Paved;
    std::unordered_map<std::uint64_t, float> m_vehicleRamCooldowns;
    std::unordered_map<std::uint64_t, VehicleMotion>
        m_localParkedVehicleMotions;
    std::optional<ecs::SpriteRef> m_playerSpriteBackup;
    // M13-T5：网络客户端的视觉副本缓存（服务器 StableId → 本地 ECS 实体）。
    bool m_networkMode = false;
    std::unordered_map<std::uint64_t, ecs::Entity> m_networkEntities;
    // X-F1：chunk 未就绪时暂存单格补丁；键为 (chunkX, chunkY, z, lx, ly)。
    std::map<std::tuple<int, int, int, int, int>, save::ChunkDiff>
        m_pendingNetworkChunkDiffs;
    std::uint32_t m_lastNetworkStatusSequence = 0;
    // M13-T8：死亡/复活期间本地视觉玩家可能暂时不存在，仍需用服务器状态驱动 HUD。
    bool m_networkPlayerAlive = true;
    float m_networkHealth = 100.0f;
    float m_networkHealthMax = 100.0f;
    float m_networkRespawnSeconds = 0.0f;
    ecs::Infection m_networkInfection;
    ecs::Climate m_networkClimate;
    net::PlayerFoodState m_networkFood;
    // M21-F1b：服务器权威守夜 HUD 文本（只读镜像）。
    std::string m_networkSiegeStatus;
    std::string m_networkSiegeTone = "info";
    // M21-F1c：服务器下发的预计破口/实际破口坐标（世界格中心）。
    std::uint8_t m_networkSiegePhase = 0;
    bool m_networkSiegeTargetValid = false;
    float m_networkSiegeTargetX = 0.0f;
    float m_networkSiegeTargetY = 0.0f;
    bool m_networkSiegeBreachValid = false;
    float m_networkSiegeBreachX = 0.0f;
    float m_networkSiegeBreachY = 0.0f;
    // M21-F2b：服务器权威电力摘要；设备状态来自 ZLR1 副本。
    net::PlayerPowerState m_networkPower;
    // M22-F1：服务器权威睡眠/时间投票镜像；phase 0/1/2。
    net::PlayerSleepState m_networkSleep;
    // M25-T3：服务器权威载具绑定态/燃料/车况镜像（owner-only）。
    net::PlayerVehicleState m_networkVehicle;
    // M25-T4-V22：隐藏乘车玩家精灵时保留原始外观，下车后恢复。
    std::unordered_map<std::uint64_t, ecs::SpriteRef> m_hiddenNetworkSprites;
    // M25-T3-V5：驾驶客户端本地位置预测；只在收到新快照 tick 时校正。
    bool m_vehiclePredictionActive = false;
    std::uint64_t m_vehiclePredictionTick = 0;
    bool m_networkHasConnected = false;
    bool m_networkWasDisconnected = false;
    bool m_networkDisconnectWasDriving = false;
    // M23-F1-T7：服务器权威弹匣/备弹/换弹进度镜像（owner-only）。
    net::PlayerRangedState m_networkRanged;
    // M30-T6：服务器权威战斗体力/姿态镜像（owner-only）。
    net::PlayerCombatState m_networkCombat;
    // M34-F1-T5：服务器权威潜行摘要（owner-only）。
    net::PlayerStealthState m_networkStealth;
    net::PlayerSurvivalState m_networkSurvival;
    net::PlayerAnimalHarvestState m_networkAnimalHarvest;
    void HandleNetworkSleepInput(core::Input& input);
    void SubmitNetworkSleepCommand(net::CommandType type,
                                   std::uint16_t flags = 0,
                                   float durationMinutes = 0.0f);
    void RefreshNetworkPowerDisplay(float frameDt);
    // M19-F2/F3：服务器权威 POI/任务状态镜像。
    struct NetworkPoiProgress {
        bool completed = false;
        bool targetSearched = false;
        int liveGuards = 0;
        std::uint8_t stageIndex = 0;
        std::uint8_t stageCount = 0;
        std::uint8_t completedStageCount = 0;
        std::uint8_t stageFlags = 0;
        std::string currentStage;
        std::string chosenBranch;
    };
    std::unordered_map<std::string, NetworkPoiProgress> m_networkPoiProgress;
    struct NetworkSettlementStatus {
        bool fallen = false;
        bool raidActive = false;
        float guardHealth = 0.0f;
        float guardHealthMax = 0.0f;
    };
    std::unordered_map<std::string, NetworkSettlementStatus>
        m_networkSettlements;
    std::unordered_map<std::string, int> m_networkSettlementReputation;
    std::uint32_t m_networkSettlementNoticeSerial = 0;
    std::string m_networkSettlementNotice;
    std::string m_networkSettlementNoticeTone = "info";
    void ApplyNetworkPoiAndMissions(const net::PlayerState& state);
    std::uint64_t m_lastNetworkLootSearchEntity = 0;
    // M5-T4：演示枪声基础可闻半径（格；config sounds.gunshotRadius）
    float m_gunshotRadius = 30.0f;
    // M30-T10：本地近战挥击残留轨迹；独立于 ActionState，保证截图/网络
    // 摄像机在事件后几帧内仍能看到前摇弧形。
    struct LocalSwingVisual {
        bool active = false;
        bool heavy = false;
        float remaining = 0.0f;
        float duration = 0.44f;
        float dirScreenX = 0.0f;
        float dirScreenY = 1.0f;
        float range = 1.7f;
    };
    LocalSwingVisual m_localSwing;
    // M14-T3：玩家动画事件序号（事件帧驱动脚步/命中音效）
    std::uint32_t m_lastPlayerAnimEventSerial = 0;
    // M15-T3：玩家受击反馈（hitSerial 去重；边缘提示按剩余时间淡出）
    ecs::Entity m_feedbackPlayer = entt::null;
    std::uint32_t m_lastPlayerHitSerial = 0;
    float m_threatRemaining = 0.0f;
    float m_threatDuration = 1.0f;
    std::string m_threatDirection;
    std::string m_threatStatus;
    // M12-T1：上次提交的 Move 屏幕方向（去重，避免每帧重复发包）
    glm::vec2 m_lastMoveCommand{0.0f, 0.0f};
    bool m_moveCommandValid = false;
    // M5-T6：玩家挥击参数（config combat.player*；M15-T1 起由主手武器覆盖）
    float m_playerAttackDamage = 40.0f;
    float m_playerAttackRange = 1.7f;
    float m_playerAttackArc = 100.0f;
    float m_playerAttackCooldown = 0.5f;
    float m_playerAttackKnockback = 3.0f;
    float m_playerAttackStagger = 0.35f;
    float m_playerAttackWear = 0.0f;
    float m_attackCooldownLeft = 0.0f;
    bool m_blockInputDown = false;
    items::RangedWeaponRuntime m_rangedWeapon;
    std::uint32_t m_rangedReloadHitSerial = 0;
    std::uint32_t m_rangedShotSerial = 0;
    game::ThrowingConfig m_throwingConfig;
    struct ThrowAimRuntime {
        bool active = false;
        int sourceIndex = -1;
        std::string defId;
        std::string name;
        float itemRange = 0.0f;
        float range = 0.0f;
        float impactRadius = 0.0f;
        float arcHeight = 0.0f;
        float damage = 0.0f;
        float noiseRadius = 0.0f;
        float fireRadius = 0.0f;
        float fireMinutes = 0.0f;
        float breakChance = 0.0f;
        bool returnable = false;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float distance = 0.0f;
        float directionX = 1.0f;
        float directionY = 0.0f;
        bool clamped = false;
        bool wallClipped = false;
        int targetCellX = 0;
        int targetCellY = 0;
    };
    ThrowAimRuntime m_throwAim;
    std::vector<ThrownProjectile> m_thrownProjectiles;
    float m_throwCooldownLeft = 0.0f;
    std::string m_lastThrowResult;
    std::string m_lastThrowTone = "info";
    float m_lastThrowRemaining = 0.0f;
    std::mt19937 m_throwRng{20261118u};
    // M6-T2：玩家背包容量（读档补建 Container 时使用；生成参数由 WorldSimulation 读取）
    float m_playerCapacityKg = 20.0f;
    // M6-T3：地面物品拾取半径（config inventory.pickupRadius）与散落标记
    float m_pickupRadius = 1.6f;
    float m_inventoryRange = 3.0f;
    ForageAction m_forageAction;
    FishingAction m_fishingAction;
    AnimalHarvestAction m_animalHarvestAction;
    std::vector<ForageNode> m_forageNodes;
    std::vector<FishingSpot> m_fishingSpots;
    float m_forageInteractRange = 2.2f;
    float m_fishingInteractRange = 2.2f;
    float m_fishingPlanRadius = 12.0f;
    float m_foragePlanRadius = 10.0f;
    float m_forageScanSeconds = 1.0f;
    float m_foragePlanTimer = 0.0f;
    float m_foragePlanX = 0.0f;
    float m_foragePlanY = 0.0f;
    bool m_foragePlanValid = false;
    std::string m_repairKitDefId = "repair_kit";
    int m_repairKitCount = 1;
    float m_repairAmount = 0.5f;
    float m_stationRange = 2.5f;
    float m_spoiledFoodDamage = 4.0f;
    float m_foodWarningThreshold = 0.2f;
    FoodSafetyParams m_foodSafety;
    std::uint64_t m_foodUseSerial = 0;
    std::unordered_map<std::string, std::string> m_stationLabels;
    std::unordered_set<std::string> m_blueprintRecipes;
    bool m_demoItemsScattered = false;
    bool m_demoLootContainersSpawned = false;
    bool m_houseLootContainersSpawned = false;
    PoiConfig m_poiConfig;
    struct PoiRuntime {
        bool containersSpawned = false;
        bool guardsSpawned = false;
    };
    std::unordered_map<std::string, PoiRuntime> m_poiRuntime;
    std::unordered_map<std::string, float> m_poiBossResetTimers;
    std::string m_poiNearId;
    // M33-F5-T5：阶段完成/失败面板与叙事阅读状态。
    std::string m_poiResultTitle;
    std::string m_poiResultBody;
    std::string m_poiResultTone = "info";
    float m_poiResultTimer = 0.0f;
    float m_poiEntryRange = 3.5f;
    SettlementConfig m_settlementConfig;
    MissionConfig m_missionConfig;
    std::string m_missionFocusId;
    std::uint64_t m_lastMissionKills = 0;
    bool m_missionKillsInitialized = false;
    bool m_refugeeMissionTriggered = false;
    struct RandomEventRuntime {
        float siteX = 0.0f;
        float siteY = 0.0f;
    };
    std::unordered_map<std::string, RandomEventRuntime> m_randomEventRuntime;
    std::unordered_set<std::string> m_randomEventReady;
    std::unordered_set<std::uint64_t> m_randomEventKilled;
    std::string m_coldSnapEventId;
    std::string m_radioPartDefId = "radio_part";
    float m_radioX = 72.5f;
    float m_radioY = 20.5f;
    float m_extractionX = 66.5f;
    float m_extractionY = 28.5f;
    float m_extractionRadius = 1.5f;
    float m_objectiveInteractRange = 2.2f;
    enum class RunOutcome { None, Escaped, Dead };
    RunOutcome m_runOutcome = RunOutcome::None;
    ecs::Entity m_runStatsPlayer = entt::null;
    double m_runSurvivalSeconds = 0.0;
    std::uint64_t m_runInjuries = 0;
    float m_runDamageTaken = 0.0f;
    float m_runLastHealth = -1.0f;
    std::uint32_t m_runHitSerial = 0;
    ecs::DamageCause m_runDeathCause = ecs::DamageCause::Unknown;
    std::vector<BuildableDef> m_buildables;
    int m_dynamicContentApplied = 0;
    int m_contentReloadApplied = 0;
    std::string m_buildWoodItem = "wood";
    int m_buildIndex = 0;
    bool m_buildMode = false;
    bool m_buildOrientationVertical = false;
    float m_buildRange = 3.5f;
    float m_buildZombieDamage = 12.0f;
    float m_buildZombieCooldown = 1.0f;
    float m_breakerDamage = 28.0f;
    float m_breakerCooldown = 1.2f;
    float m_breakerRange = 2.2f;
    float m_breakerWindup = 0.9f;
    float m_buildActionRange = 2.5f;
    float m_buildActionCooldown = 0.0f;
    float m_buildActionCooldownSeconds = 0.5f;
    PendingDemolish m_pendingDemolish;
    int m_repairWoodCost = 1;
    float m_structureRepairAmount = 30.0f;
    float m_demolishRefundRatio = 0.5f;
    float m_barricadeScanTimer = 0.0f;
    std::unordered_map<std::uint64_t, BuiltBarricade> m_barricades;
    std::unordered_map<std::uint32_t, float> m_barricadeAttackCooldowns;
    std::unordered_map<std::uint64_t, float> m_floodlightAttackCooldowns;
    std::unordered_map<std::uint64_t, float> m_electricFenceCooldowns;
    std::unordered_set<std::uint64_t> m_poweredFenceCells;
    std::vector<std::pair<int, int>> m_electricFenceCells;
    float m_powerScanTimer = 0.0f;
    float m_electricNoticeCooldown = 0.0f;
    std::vector<PowerGlow> m_poweredFloodlights;
    float m_powerVisualTime = 0.0f;
    float m_fireVisualTime = 0.0f;
    bool m_powerLowFuelWarned = false;
    bool m_powerWasOnline = false;
    float m_baseHudScanTimer = 0.0f;
    BaseHudSnapshot m_baseHud;
    BaseSiege m_baseSiege;
    HomesteadConfig m_homesteadConfig;
    bool m_tradeOpen = false;
    ecs::Entity m_tradeNpc = entt::null;
    std::string m_tradeSettlementId;
    int m_tradeIndex = 0;
    bool m_companionOpen = false;
    bool m_dialogueOpen = false;
    bool m_dialogueCompanion = false;
    ecs::Entity m_dialogueNpc = entt::null;
    std::string m_dialogueSettlementId;
    CompanionState m_dialogueCompanionState;
    const CompanionDef* m_dialogueCompanionDef = nullptr;
    ecs::Health m_dialogueCompanionHealth;
    DialogueTree m_dialogueTree;
    std::string m_dialogueNodeId;
    int m_dialogueIndex = 0;
    std::uint32_t m_lastSettlementNoticeSerial = 0;
    double m_lastHomesteadMinute = -1.0;
    bool m_homesteadRuntimeReady = false;
    std::string m_farmSeedCropId;
    SleepConfig m_sleepConfig;
    SleepSession m_sleep;
    bool m_sleepEnabled = true;
    bool m_sleepSelecting = false;
    bool m_sleepSelectUntilDawn = false;
    int m_sleepSelectedHours = 6;
    float m_sleepLastHealth = 0.0f;
    float m_sleepHungerPerDay = 1.4f;
    float m_sleepThirstPerDay = 1.8f;
    core::GameTime::Speed m_sleepSpeedBefore = core::GameTime::Speed::X1;
    std::string m_sleepBedName;
    ProgressionConfig m_progressionConfig;
    characters::StealthRuleConfig m_stealthRules;
    ecs::Progression m_networkProgression;
    std::array<std::string, 3> m_networkPerkChoices{};
    bool m_networkProgressionValid = false;
    double m_lastSurvivalXpMinute = -1.0;
    ecs::Entity m_progressionPlayer = entt::null;
    bool m_characterOpen = false;
    // M21-F1c：战后独立战报面板（单机/联机共用，只读）。
    bool m_siegeReportOpen = false;
    std::string m_siegeReportTitle;
    std::string m_siegeReportRating;
    std::string m_siegeReportTone = "info";
    std::string m_siegeReportHint;
    std::array<std::string, 8> m_siegeReportLines{};
    std::string m_lastRegionalSiegeResultKey;
    float m_baseWalkSpeed = 4.0f;
    // M4-T7：点击移动反馈（notice 文本 + 目标格标记）
    std::string m_noticeText;
    std::string m_noticeTone = "info";
    float m_noticeTimer = 0.0f;
    float m_noticeDuration = 0.0f;
    int m_markerX = 0;
    int m_markerY = 0;
    float m_markerTimer = 0.0f;
    bool m_markerPathActive = false;  // 寻路中（Pending/Following）：目标标记常显
    uint32_t m_markerColor = 0xFFFFFFFFu;
    // M31-T7: world map, fog of war and non-fast-travel navigation.
    bool m_worldMapOpen = false;
    bool m_worldMapPausedByMap = false;
    core::GameTime::Speed m_worldMapSpeedBefore = core::GameTime::Speed::X1;
    float m_worldMapDiscoveryRadius = 36.0f;
    std::unordered_set<std::string> m_discoveredRegions;
    std::unordered_set<std::string> m_discoveredPois;
    WorldMapState m_worldMapState;
    WorldMapRoute m_worldMapRoute;
    std::string m_worldMapSelectedPoi;
    std::string m_worldEventUiId;
    int m_worldEventUiStatus = -1;
    int m_worldEventUiStage = -1;
    bool m_worldEventUiNear = false;
    bool m_worldEventChoiceOpen = false;
    std::array<std::string, 6> m_worldEventLog{};
    int m_worldEventLogCount = 0;
    std::string m_worldEventResult;
    std::string m_worldEventResultTone = "ok";
    float m_worldEventResultTimer = 0.0f;
    std::string m_worldEventSettledId;
    std::string m_worldEventSettleError;
    std::uint64_t m_lastStrategicResultSequence = 0;
    ecs::PathStatus m_lastPlayerPathState = ecs::PathStatus::None;
    // M14-T1：遮挡墙体透明度（config render.wallOcclusionAlpha）
    float m_wallOcclusionAlpha = 0.35f;
    // M14-T10：进入房间后剖掉本房间上层与前方边界墙（config render.roomCutaway）
    bool m_roomCutaway = true;
    // M14-T10 对齐 PZ：剖切墙体保留的顶部高度比例（config render.cutawayWallKeepRatio）
    float m_cutawayWallKeepRatio = 0.45f;
    // M36-T1：按地面格记录剖切状态（每格 750ms 锁），不再按房间整体开关。
    render::CutawaySquareMap m_cutawaySquares;
    std::vector<int> m_cutawayRoomIds;
    double m_cutawayClock = 0.0;      // 累计秒（锁计时）
    double m_cutawayLockSeconds = 0.75;  // config render.cutawayLockMs
    bool m_cutawayPerSquare = true;   // config render.cutawayPerSquare
    bool m_cutawayDebug = false;      // config render.cutawayDebug（重算日志摘要）
    bool m_cutawayTrimEdges = true;   // config render.cutawayTrimMode=edges（否则回退 ratio）
    // M36-T7：剖切更新计时与射线预算统计（每次重算 / 当前帧）
    mutable std::uint32_t m_cutawayRaysThisFrame = 0;  // const RoomLineOfSight 内统计
    std::uint32_t m_cutawayRaysLastRebuild = 0;
    std::uint32_t m_cutawayMaxRaysPerRebuild = 0;
    double m_cutawayLastUpdateMs = 0.0;
    // M36-T3：室外建筑遮挡（config render.buildingOcclusion / occluderBuildingsMax / occluderLockMs）
    bool m_buildingOcclusion = true;
    std::size_t m_occluderBuildingsMax = 8;
    double m_occluderLockSeconds = 0.75;
    std::vector<render::BuildingInfo> m_buildings;
    bool m_buildingsDirty = true;
    std::uint64_t m_buildingsChunkEpoch = 0;
    std::unordered_map<int, render::CutawaySquareState> m_occluderStates;
    std::vector<int> m_occluders;
    render::SpriteRect m_cutawayPlayerRect;  // 本帧玩家屏幕矩形（供建筑遮挡筛选）
    // M36-T4：窗口扒看（config render.windowPeek / windowPeekRange）
    bool m_windowPeek = true;
    float m_windowPeekRange = 0.9f;
    // M36-T5：屋顶归属（config render.roofCull；false 回退按格剖切）
    bool m_roofCull = true;
    // M36-T6：植被压平/淡化（config render.foliageFlatten/foliageAlpha/foliageKeepRatio）
    bool m_foliageFlatten = true;
    float m_foliageAlpha = 0.4f;
    float m_foliageKeepRatio = 0.55f;
    bool m_cutawayDirty = true;
    std::uint64_t m_cutawayChunkEpoch = 0;
    int m_cutawayPlayerX = 0;
    int m_cutawayPlayerY = 0;
    int m_cutawayRoomId = -1;
    int m_cutawayFacingDir = -1;
    // M4-T5：相机模式（F4 切换；follow 平滑跟随玩家并夹取到地图边界）
    bool m_followCamera = true;
    bool m_followSnapped = false;  // 下次跟随直接落位（刚切换/生成时不做缓动）
    float m_followRate = 8.0f;     // 指数趋近速率（1/s）
    bool m_physicsDebug = false;  // F10：Bullet 线框
    int m_wireDiagCount = 0;      // 线框诊断日志计数（开启后头几帧）

    glm::vec2 m_prevMouse{0.0f, 0.0f};
    bool m_prevMouseValid = false;
    float m_scrollAccum = 0.0f;
    float m_statsTimer = 0.0f;
    std::mt19937 m_fireExtinguishRng{20261029u};

    // 自动巡航 + 压测采样（M3-T7）：绕 (cx,cy) 圆周巡航，每 5s 打一条 perf 统计
    bool m_cruiseEnabled = false;
    bool m_cruiseFinished = false;
    double m_cruiseElapsed = 0.0;
    float m_cruiseAngle = 0.0f;
    float m_cruiseSpeed = 30.0f;     // 世界格/秒
    float m_cruiseRadius = 40.0f;    // 世界格
    float m_cruiseCenterX = 500.0f;  // 世界格
    float m_cruiseCenterY = 500.0f;
    double m_cruiseMinutes = 5.0;
    float m_perfTimer = 0.0f;

    int m_hoverX = 0;
    int m_hoverY = 0;
    bool m_hoverHit = true;
    bool m_showGrid = false;  // 世界地图默认关网格（F9 开）
    bool m_showDebugText = true;
    int m_hotbarSelected = 0;  // M9-T4：快捷栏选中槽（数字键 1~5）
    items::CraftingProcess m_crafting;
    int m_craftBatchTotal = 0;
    int m_craftBatchCompleted = 0;
    std::vector<ecs::CraftQueueItem> m_craftQueue;
    // M9-T6：暂停/主菜单覆盖层；暂停前倍速用于恢复
    bool m_menuPaused = false;
    core::GameTime::Speed m_speedBeforeMenu = core::GameTime::Speed::X1;

    // M10-T5：当前存档与后台保存状态
    std::string m_loadSaveId;
    std::string m_saveId;
    bool m_loadedFromSave = false;
    WorldLifecycle m_worldLifecycle;
    bool m_autoSaveOnExit = true;
    bool m_savePending = false;         // 已交给 SaveService，等待结果
    bool m_saveRequestPending = false;  // 快照已采集，等待缩略图/提交
    bool m_screenshotPending = false;
    double m_screenshotTimer = 0.0;
    save::SaveRequest m_pendingSave;

    uint64_t m_ticksInState = 0;
};

}  // namespace zl::game
