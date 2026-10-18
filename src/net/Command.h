#pragma once

#include <cstdint>

namespace zl::net {

// M12-T1：玩家操作命令（联机预留）。
//
// 所有会改变玩家/世界状态的操作都先产生命令，再由 CommandPipeline 执行：
//   direct   = 本地直接执行（默认，行为与 M11 一致）
//   loopback = Client 编码成包 → 队列 → Server 解码执行（单进程，占位联机管线）
//
// 实体引用只允许 StableId（playerStableId / entityId / targetEntityId），
// 绝不携带 entt::entity 句柄；tick 记录命令生成时的固定 tick，便于服务器重放/排序。
enum class CommandType : std::uint16_t {
    Move = 1,          // payload.x/y = 屏幕方向（-1..1；InputSystem 做等距矫正）
    MoveTo = 2,        // payload.x/y = 目标格坐标
    Attack = 3,        // payload.entityId = 目标 StableId（0 = 自动瞄准最近）
    PickupNearest = 4,
    PickupEntity = 5,  // payload.entityId = 地面物品 StableId
    UseBag = 6,        // payload.index = 背包索引（-1 = 第一个可用消耗品）
    EquipBag = 7,      // payload.index = 背包索引（-1 = 切换主手武器）；targetIndex = 装备槽
    Unequip = 8,       // payload.index = 装备槽
    DropBag = 9,       // payload.index = 背包索引（-1 = 第一堆）
    DropEquipment = 10,  // payload.index = 装备槽
    InventoryAction = 11,  // payload.flags = ui::InventoryActionType，area/index/entity 见 UI
    Gunshot = 12,
    SelectHotbar = 13,  // payload.index = 0..4
};

inline constexpr std::uint64_t kNoEntity = 0;

// 固定字段 payload：网络编码不需要 variant/JSON；未使用字段保持默认值。
// flags 低 8 位保存 InventoryActionType，其余位预留给后续协议。
struct CommandPayload {
    float x = 0.0f;
    float y = 0.0f;
    std::int32_t index = -1;
    std::int32_t targetIndex = -1;
    std::uint8_t area = 0;
    std::uint8_t targetArea = 0;
    std::uint16_t flags = 0;
    std::uint64_t entityId = kNoEntity;
    std::uint64_t targetEntityId = kNoEntity;
};

struct Command {
    CommandType type = CommandType::Move;
    CommandPayload payload;
    std::uint64_t tick = 0;
    std::uint64_t playerStableId = 0;
};

constexpr bool IsValidCommandType(CommandType type) {
    return type >= CommandType::Move && type <= CommandType::SelectHotbar;
}

const char* CommandTypeName(CommandType type);

enum class TransportMode : std::uint8_t {
    Direct = 0,
    Loopback = 1,
};

const char* TransportModeName(TransportMode mode);

}  // namespace zl::net
