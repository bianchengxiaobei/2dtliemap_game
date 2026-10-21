#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace zl::net {

// M13-T7：服务器 → 单个客户端的背包/装备/配方/制作状态。
//
// 只发给拥有该玩家的会话，不进入全体可见的 ZLR1 实体快照；客户端用它在网络模式
// 构造只读 UI 镜像，所有修改仍必须通过 ZLC1 命令回服务器执行。
inline constexpr std::uint32_t kPlayerStateMagic = 0x31504C5Au;  // "ZLP1"
inline constexpr std::uint16_t kPlayerStateVersion = 2;
inline constexpr std::size_t kPlayerStateHeaderSize = 24;
inline constexpr std::size_t kMaxPlayerStateBytes = 32 * 1024;
inline constexpr std::size_t kMaxPlayerStateBagItems = 64;
inline constexpr std::size_t kMaxPlayerStateKnownRecipes = 256;
inline constexpr std::size_t kMaxPlayerStateCraftQueue = 16;
inline constexpr std::size_t kMaxPlayerStateStringBytes = 128;
inline constexpr std::size_t kMaxPlayerStateStatusBytes = 512;

struct PlayerStateItem {
    std::string defId;
    int count = 1;
    float condition = 1.0f;
};

struct PlayerStateEquipment {
    bool present = false;
    PlayerStateItem item;
};

struct PlayerStateCraftQueue {
    std::string recipeId;
    int count = 1;
};

struct PlayerState {
    std::uint64_t playerStableId = 0;
    std::uint64_t serverTick = 0;
    float capacityKg = 20.0f;

    // M13-T8：生命/死亡/复活。alive=false 时 health 必须为 0，
    // respawnSeconds 为服务器计算的剩余复活秒数（仅供 UI，不信任客户端）。
    float health = 100.0f;
    float healthMax = 100.0f;
    bool alive = true;
    float respawnSeconds = 0.0f;

    std::vector<PlayerStateItem> bag;
    std::array<PlayerStateEquipment, 2> equipment{};
    std::vector<std::string> knownRecipes;

    bool craftingActive = false;
    std::string craftingRecipeId;
    float craftingElapsed = 0.0f;
    float craftingDuration = 0.0f;
    float craftingToolEfficiency = 1.0f;
    int batchTotal = 0;
    int batchCompleted = 0;
    std::vector<PlayerStateCraftQueue> craftQueue;

    std::uint32_t statusSequence = 0;
    std::uint8_t statusTone = 0;  // 0=info, 1=ok, 2=fail
    std::string status;
};

std::vector<std::uint8_t> EncodePlayerState(const PlayerState& state,
                                            std::string* error = nullptr);
bool DecodePlayerState(std::span<const std::uint8_t> bytes, PlayerState* state,
                       std::string* error = nullptr);

}  // namespace zl::net
