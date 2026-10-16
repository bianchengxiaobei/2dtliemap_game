#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zl::core {

// 事件表 v1（docs/03 §5）中除 OnGameStart/OnTick（EventBus.h）与 OnSound（SoundEvent.h）
// 之外的键；EventBus 与脚本共用同一命名。载荷中的 xxxId 一律是稳定 ID（StableId）。
inline constexpr std::string_view kOnPlayerMove = "OnPlayerMove";
inline constexpr std::string_view kOnPlayerDeath = "OnPlayerDeath";
inline constexpr std::string_view kOnZombieSpawn = "OnZombieSpawn";
inline constexpr std::string_view kOnZombieDead = "OnZombieDead";
inline constexpr std::string_view kOnItemPickup = "OnItemPickup";
inline constexpr std::string_view kOnItemDrop = "OnItemDrop";
inline constexpr std::string_view kOnChunkLoaded = "OnChunkLoaded";
inline constexpr std::string_view kOnChunkUnloaded = "OnChunkUnloaded";
inline constexpr std::string_view kOnSaveBegin = "OnSaveBegin";
inline constexpr std::string_view kOnSaveEnd = "OnSaveEnd";
inline constexpr std::string_view kOnModReload = "OnModReload";

// —— 多字段事件载荷（EventBus variant 扩充；"只加类型，不改派发机制"）——

// OnPlayerMove / OnPlayerDeath
struct PlayerEvent {
    std::uint64_t playerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

// OnZombieSpawn（killerId 不用）/ OnZombieDead（killerId：击杀者玩家，未知 = 0）
struct ZombieEvent {
    std::uint64_t zombieId = 0;
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

// OnChunkLoaded / OnChunkUnloaded
struct ChunkEvent {
    int cx = 0;
    int cy = 0;
};

}  // namespace zl::core
