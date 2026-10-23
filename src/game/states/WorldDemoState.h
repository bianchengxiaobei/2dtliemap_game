#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "ai/components/AiComponents.h"
#include "core/AppState.h"
#include "core/GameTime.h"
#include "ecs/EcsWorld.h"
#include "game/AssetProvider.h"
#include "game/GameContext.h"
#include "game/Objectives.h"
#include "game/RenderCollectSystem.h"
#include "items/Crafting.h"
#include "items/ItemComponents.h"
#include "net/Command.h"
#include "render/DrawList.h"
#include "render/IsoMath.h"
#include "save/SaveService.h"
#include "ui/InventoryUi.h"
#include "ui/MenuUi.h"
#include "world/GridSquare.h"

namespace zl::world {
class Chunk;
}

namespace zl::items {
struct ItemDef;
}

namespace zl::game {

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
    // M13-T5：把 EntityReplicationClient 副本同步成渲染用 ECS 实体（不参与本地模拟）。
    void SyncNetworkReplicas(float alpha);
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
    void SpawnDemoEntities();
    void DespawnDemoEntities();
    // M6-T3：地面物品——村庄附近散落（首次帧、目标 chunk 就绪后）+ F 键拾取最近 + 退出清理
    void EnsureDemoItemsScattered();
    void PickupNearestItem();
    void DespawnGroundItems();
    // M14-T5：村庄演示可搜刮容器；打开背包时自动搜索最近容器并展示其内容
    void EnsureDemoLootContainers();
    void EnsureHouseLootContainers();
    void SearchNearbyLootContainer();
    void UpdateObjective();
    bool TryInteractRadio();
    bool PlayerHasRadioPart() const;
    bool RadioPartExistsInWorld() const;
    // M14-T8：单机局结束（撤离成功/死亡）→ 冻结世界并显示结算页。
    void FinishRun(bool victory);
    std::string BuildRunStats() const;
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
    void SyncCraftingJobToEcs();
    void RestoreCraftingJobFromEcs();
    // M12-T1：玩家操作统一走 Command（direct 立即执行 / loopback 环回执行）
    net::Command MakePlayerCommand(net::CommandType type) const;
    bool ExecutePlayerCommand(const net::Command& command, std::string& error);
    bool SubmitPlayerCommand(const net::Command& command);
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
    bool UseBlueprintItem(int index, const items::ItemDef& def);
    void RepairBagItem(int index);
    // recipes-v3：工作台/制作站范围（空 station = 任意地点）
    std::string StationLabel(std::string_view station) const;
    bool StationNearby(std::string_view station, float x, float y, int z) const;
    bool RecipeKnown(std::string_view recipeId) const;
    void DropBagItem(int index);
    void DropEquipmentItem(int index);
    void PickupLootItem(std::uint64_t stableId, int index = -1);
    void ReturnOrDropItem(const ecs::ItemInstance& item);
    // M5-T1：演示击杀——离参考点（玩家/相机中心）最近的存活游荡者 HP 归零 → DeathSystem 转尸体
    void KillNearestDemoEntity();
    // M5-T4：演示开枪——玩家位置发射大半径 SoundEvent（事件总线 OnSound）吸引僵尸
    void EmitGunshot();
    // M5-T6：玩家挥击（空格）——自动瞄准最近僵尸 → 写 AttackIntent → CombatSystem 伤害管线
    void PlayerAttack(std::uint64_t targetStableId = 0);
    // M4-T4：玩家（PlayerTag）——chunk 加载完成后在村庄中心附近找可走格生成
    void EnsurePlayerSpawned();
    bool IsSolidSquare(int wx, int wy) const;
    // M4-T7：点击移动——屏幕拾取 → 目标格校验（越界/未加载/就近可走格）→ 写 PathRequest
    void HandleClickToMove(const render::GridPick& pick);
    // M4-T7：寻路反馈——玩家路径状态跃迁（到达/失败）→ HUD 提示 + 目标格标记
    void UpdatePathFeedback(double frameDt);
    void SetNotice(std::string text, uint32_t color, float seconds);
    void RefreshHud();
    // M4-T5：相机跟随（follow=平滑跟随玩家+地图边界；自由=方向键/拖拽，无边界）
    void ApplyCameraMode();
    void UpdateCameraFollow(double frameDt);
    void UpdateCruise(double frameDt);
    void LogPerfSample(const char* tag);
    void PushObject(const world::SquareObject& object, int wx, int wy, int z,
                    world::SquareLayer layer, int roomId = -1);
    world::Chunk* FindChunk(int chunkX, int chunkY) const;
    int RoomIdAt(int wx, int wy, int z) const;
    int PlayerRoomId() const;
    // M14-T10：隐藏当前房间前方（画家序在玩家之后）的边界墙与边界上方楼墙；3×3 邻域覆盖转角
    void CutawayRoomWalls(int roomId, std::size_t playerIndex);
    // M14-T1：玩家被墙/门挡住时淡出遮挡项（只改本帧 DrawItem 颜色）
    void ApplyWallOcclusion();
    void DrawWorld();
    void DrawDebugOverlay();
    void DrawPhysicsDebug();
    void DrawDebugText();
    glm::vec2 MouseToFramebuffer(const glm::vec2& mousePx) const;
    render::GridPick PickMouseCell(const glm::vec2& mousePx) const;

    GameContext& m_ctx;

    std::vector<render::DrawItem> m_items;
    std::string_view m_cachedSprite;
    const std::vector<AssetProvider::Entry>* m_cachedFrames = nullptr;
    RenderCollectSystem::ViewRect m_view;  // 本帧可见范围（世界格坐标）
    int m_collectedChunks = 0;  // 本帧进入收集的已加载 chunk 数
    int m_missingChunks = 0;    // 本帧可见但尚未加载的 chunk 数

    // M4-T4：本地玩家（WASD 控制；实体由 WorldSimulation 生成）
    ecs::Entity m_player = entt::null;
    // M13-T5：网络客户端的视觉副本缓存（服务器 StableId → 本地 ECS 实体）。
    bool m_networkMode = false;
    std::unordered_map<std::uint64_t, ecs::Entity> m_networkEntities;
    std::uint32_t m_lastNetworkStatusSequence = 0;
    // M13-T8：死亡/复活期间本地视觉玩家可能暂时不存在，仍需用服务器状态驱动 HUD。
    bool m_networkPlayerAlive = true;
    float m_networkHealth = 100.0f;
    float m_networkHealthMax = 100.0f;
    float m_networkRespawnSeconds = 0.0f;
    // M5-T4：演示枪声基础可闻半径（格；config sounds.gunshotRadius）
    float m_gunshotRadius = 30.0f;
    // M14-T3：玩家动画事件序号（事件帧驱动脚步/命中音效）
    std::uint32_t m_lastPlayerAnimEventSerial = 0;
    // M12-T1：上次提交的 Move 屏幕方向（去重，避免每帧重复发包）
    glm::vec2 m_lastMoveCommand{0.0f, 0.0f};
    bool m_moveCommandValid = false;
    // M5-T6：玩家挥击参数（config combat.player*；演示自动瞄准射程固定 3 格）
    float m_playerAttackDamage = 40.0f;
    float m_playerAttackRange = 1.7f;
    float m_playerAttackKnockback = 3.0f;
    float m_playerAttackStagger = 0.35f;
    // M6-T2：玩家背包容量（读档补建 Container 时使用；生成参数由 WorldSimulation 读取）
    float m_playerCapacityKg = 20.0f;
    // M6-T3：地面物品拾取半径（config inventory.pickupRadius）与散落标记
    float m_pickupRadius = 1.6f;
    float m_inventoryRange = 3.0f;
    std::string m_repairKitDefId = "repair_kit";
    int m_repairKitCount = 1;
    float m_repairAmount = 0.5f;
    float m_stationRange = 2.5f;
    std::unordered_map<std::string, std::string> m_stationLabels;
    std::unordered_set<std::string> m_blueprintRecipes;
    bool m_demoItemsScattered = false;
    bool m_demoLootContainersSpawned = false;
    bool m_houseLootContainersSpawned = false;
    std::string m_radioPartDefId = "radio_part";
    float m_radioX = 72.5f;
    float m_radioY = 20.5f;
    float m_extractionX = 66.5f;
    float m_extractionY = 28.5f;
    float m_extractionRadius = 1.5f;
    float m_objectiveInteractRange = 2.2f;
    enum class RunOutcome { None, Escaped, Dead };
    RunOutcome m_runOutcome = RunOutcome::None;
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
    ecs::PathStatus m_lastPlayerPathState = ecs::PathStatus::None;
    // M14-T1：遮挡墙体透明度（config render.wallOcclusionAlpha）
    float m_wallOcclusionAlpha = 0.35f;
    // M14-T10：进入房间后剖掉本房间上层与前方边界墙（config render.roomCutaway）
    bool m_roomCutaway = true;
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
    bool m_autoSaveOnExit = true;
    bool m_savePending = false;         // 已交给 SaveService，等待结果
    bool m_saveRequestPending = false;  // 快照已采集，等待缩略图/提交
    bool m_screenshotPending = false;
    double m_screenshotTimer = 0.0;
    save::SaveRequest m_pendingSave;

    uint64_t m_ticksInState = 0;
};

}  // namespace zl::game
