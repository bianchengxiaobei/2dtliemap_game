#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zl::core {

// 事件表 v1（docs/03 §5）中除 OnGameStart/OnTick（EventBus.h）与 OnSound（SoundEvent.h）
// 之外的键；EventBus 与脚本共用同一命名。载荷中的 xxxId 一律是稳定 ID（StableId）。
inline constexpr std::string_view kOnWorldStart = "OnWorldStart";
inline constexpr std::string_view kOnWorldExit = "OnWorldExit";
inline constexpr std::string_view kOnPlayerMove = "OnPlayerMove";
inline constexpr std::string_view kOnPlayerDeath = "OnPlayerDeath";
inline constexpr std::string_view kOnDamage = "OnDamage";
inline constexpr std::string_view kOnZombieSpawn = "OnZombieSpawn";
inline constexpr std::string_view kOnZombieDead = "OnZombieDead";
inline constexpr std::string_view kOnAnimalKilled = "OnAnimalKilled";
inline constexpr std::string_view kOnItemPickup = "OnItemPickup";
inline constexpr std::string_view kOnItemDrop = "OnItemDrop";
inline constexpr std::string_view kOnItemUsed = "OnItemUsed";
inline constexpr std::string_view kOnLootSearched = "OnLootSearched";
inline constexpr std::string_view kOnCraftStart = "OnCraftStart";
inline constexpr std::string_view kOnCraftFinished = "OnCraftFinished";
inline constexpr std::string_view kOnBuildPlaced = "OnBuildPlaced";
inline constexpr std::string_view kOnBuildDemolished = "OnBuildDemolished";
inline constexpr std::string_view kOnBuildRepaired = "OnBuildRepaired";
inline constexpr std::string_view kOnChunkLoaded = "OnChunkLoaded";
inline constexpr std::string_view kOnChunkUnloaded = "OnChunkUnloaded";
inline constexpr std::string_view kOnSaveBegin = "OnSaveBegin";
inline constexpr std::string_view kOnSaveEnd = "OnSaveEnd";
inline constexpr std::string_view kOnModReload = "OnModReload";
inline constexpr std::string_view kOnUiFrame = "OnUiFrame";
inline constexpr std::string_view kOnUiReady = "OnUiReady";
inline constexpr std::string_view kOnUiEvent = "OnUiEvent";
inline constexpr std::string_view kOnMenuChanged = "OnMenuChanged";
inline constexpr std::string_view kOnInventoryChanged = "OnInventoryChanged";

// —— 多字段事件载荷（EventBus variant 扩充；"只加类型，不改派发机制"）——

// OnWorldStart：世界开局、存档实体与玩家已就绪后同步派发一次。
struct WorldStartEvent {
    std::string saveId;
    bool isNewGame = false;
    int day = 1;
    std::uint64_t playerId = 0;
};

// OnWorldExit：离开世界、自动存档快照前同步派发一次。
struct WorldExitEvent {
    std::string saveId;
};

// OnPlayerMove / OnPlayerDeath
struct PlayerEvent {
    std::uint64_t playerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
    // M35-F2-T1：仅 OnPlayerDeath 使用；M35 之前构造的 OnPlayerMove 保持默认值。
    std::uint64_t sourceId = 0;
    std::string cause = "unknown";
};

// OnDamage：伤害经 CombatSystem::ApplyDamage 落地且实际扣血 > 0。
// 闪避/弹反/完全格挡/零伤害不发；普通格挡的剩余伤害发，blocked=true。
struct DamageEvent {
    std::uint64_t targetId = 0;
    std::uint64_t sourceId = 0;
    float amount = 0.0f;
    std::string damageType = "blunt";
    std::string cause = "blunt";
    float remainingHp = 0.0f;
    bool lethal = false;
    bool blocked = false;
};

// OnZombieSpawn（killerId 不用）/ OnZombieDead（killerId：击杀者 StableId，未知 = 0）
struct ZombieEvent {
    std::uint64_t zombieId = 0;
    std::uint64_t killerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

// OnAnimalKilled：动物转尸体时；killerId 来自 LastDamage.sourceId，未知 = 0。
struct AnimalKilledEvent {
    std::uint64_t animalId = 0;
    std::string speciesId;
    std::uint64_t killerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

// OnItemPickup / OnItemDrop（itemId = 地面物品实体稳定 ID）
struct ItemEvent {
    std::uint64_t itemId = 0;
    std::string defId;
    int count = 0;
};

// OnItemUsed：玩家成功使用一件消耗品/工具后；remaining 为使用后槽位剩余数量。
struct ItemUsedEvent {
    std::uint64_t playerId = 0;
    std::string defId;
    bool spoiled = false;
    int remaining = 0;
};

// OnLootSearched：容器首次搜刮成功；containerId 为容器实体稳定 ID。
struct LootSearchedEvent {
    std::uint64_t playerId = 0;
    std::uint64_t containerId = 0;
    std::string tableId;
    int itemCount = 0;
};

// OnCraftStart：一个制作批次开始；batchCount 为实际开始的件数（可能被材料/空间截断）。
struct CraftStartEvent {
    std::uint64_t playerId = 0;
    std::string recipeId;
    std::string outputDefId;
    int outputCount = 0;
    int batchCount = 0;
};

// OnCraftFinished：批次完成或失败/中断时发一次；batchCompleted 为该批次已完成件数。
struct CraftFinishedEvent {
    std::uint64_t playerId = 0;
    std::string recipeId;
    std::string outputDefId;
    int outputCount = 0;
    int batchCompleted = 0;
    bool failed = false;
};

// OnBuildPlaced：单机建造成功后；x/y 为世界格坐标，vertical 为实际朝向。
struct BuildPlacedEvent {
    std::uint64_t playerId = 0;
    std::string defId;
    int x = 0;
    int y = 0;
    int z = 0;
    bool vertical = false;
};

// OnBuildDemolished：单机拆除成功后；x/y 为世界格坐标。
struct BuildDemolishedEvent {
    std::uint64_t playerId = 0;
    std::string defId;
    int x = 0;
    int y = 0;
    int z = 0;
};

// OnBuildRepaired：单机维修成功后；x/y 为世界格坐标。
struct BuildRepairedEvent {
    std::uint64_t playerId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
};

// OnChunkLoaded / OnChunkUnloaded
struct ChunkEvent {
    int cx = 0;
    int cy = 0;
};

// OnUiEvent：脚本 UI 文档内产生的 DOM 输入事件。
// modId/documentId/elementId 由 UI Host 填充；key/button 无对应输入时为默认值。
struct UiEvent {
    std::string modId;
    std::string documentId;
    std::string elementId;
    std::string type;
    std::string value;
    int key = 0;
    int button = -1;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
};

// OnMenuChanged：主菜单/暂停菜单开关状态。暂停时没有 tick，脚本依赖此同步事件
// 立即加载/显示/隐藏菜单文档。
struct MenuChangedEvent {
    bool open = false;
    bool pauseMode = false;
    bool canContinue = false;
};

// OnInventoryChanged：背包开关状态变化，或背包数据刷新。脚本据此同步背包文档；
// 具体内容通过 Inventory.GetState() 读取，避免事件载荷随 UI 字段膨胀。
struct InventoryChangedEvent {
    bool open = false;
};

}  // namespace zl::core
