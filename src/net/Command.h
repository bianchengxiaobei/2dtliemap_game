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
    InventoryAction = 11,  // payload.flags 低 8 位 = ui::InventoryActionType，高 8 位 = 批量件数
    Gunshot = 12,
    SelectHotbar = 13,  // payload.index = 0..4
    FireProjectile = 14,  // M23-F1-T7：旧客户端兼容别名，服务器转换为 FireRanged 并走权威校验
    ChoosePerk = 15,      // payload.index = 当前 Perk 选项索引 0..2（服务器权威）
    PlaceStructure = 16,  // payload.x/y = 格坐标；index = 建造项；flags bit0 = 竖向
    DemolishStructure = 17,  // payload.x/y = 格坐标
    RepairStructure = 18,    // payload.x/y = 格坐标
    ToggleDoor = 19,         // payload.x/y = 格坐标
    InteractFacility = 20,   // payload.entityId = 设施 StableId
    TradeOffer = 21,         // payload.entityId = 商人 StableId；index = 报价索引
    SearchLootContainer = 22,  // payload.entityId = 容器 StableId
    AbandonMission = 23,       // payload.index = m_missionConfig.defs 索引
    InteractRadio = 24,        // 无 payload；服务器校验电台距离/无线电零件
    SleepRequest = 25,         // payload.x = 分钟；flags bit0 = 睡到天亮
    SleepVote = 26,            // flags bit0 = 1 同意 / 0 拒绝
    SleepCancel = 27,          // 取消当前投票或全队睡眠
    FireRanged = 28,           // M23-F1-T6：payload.x/y = 世界方向；服务器权威扣弹/生成弹丸
    ReloadRanged = 29,         // M23-F1-T6：服务器权威换弹；无 payload
    ForageStart = 30,          // M23-F2a-T6：payload.x/y = 节点中心；targetIndex = z
    ForageCancel = 31,         // 取消服务器权威采集
    FishingStart = 32,         // M23-F2a-T6：payload.x/y = 钓点中心；targetIndex = z
    FishingReel = 33,          // 咬钩窗口内收杆，服务器判定鱼获
    FishingCancel = 34,        // 取消等待/收杆
    IgniteFire = 35,           // M23-F3-T6：payload.x/y = 篝火格；targetIndex = z
    ExtinguishFire = 36,       // M23-F3-T6：payload.x/y = 火源格；targetIndex = z
    AnimalHarvestStart = 37,   // M23-F2b-T6：payload.entityId = 动物尸体 StableId
    AnimalHarvestCancel = 38,  // 取消服务器权威剥取
    AcceptSettlementMission = 39,  // M23-F4-T6：payload.entityId = 求助者 StableId
    EnterVehicle = 40,             // M25-T3：payload.entityId = 车辆 StableId
    ExitVehicle = 41,              // M25-T3：离开当前车辆
    ServiceVehicle = 42,           // M25-T3：payload.entityId = 车辆 StableId
    ToggleHeadlights = 43,         // M25-T4-V2：驾驶中切换车灯
    HonkHorn = 44,                 // M25-T4-V14：驾驶中鸣笛，服务器权威噪音
    EnterVehiclePassenger = 45,    // M25-T4-V22：payload.entityId = 车辆 StableId
    TakeDriverSeat = 46,           // M25-T4-V23：乘客接管空驾驶位
    GiveDriverSeat = 47,           // M25-T4-V24：驾驶员停车时把驾驶位让给乘客
    Block = 48,                    // M30-T6：flags bit0 1=按下 0=松开
    Dodge = 49,                    // M30-T6：payload.x/y = 世界闪避方向
    ChooseWorldEvent = 50,         // M33-F2-T5：payload.index = 当前事件选项索引
};

inline constexpr std::uint64_t kNoEntity = 0;
inline constexpr std::uint16_t kDemolishConfirmRequestFlag = 1u << 0;
inline constexpr std::uint16_t kDemolishConfirmAcceptFlag = 1u << 1;
inline constexpr std::uint16_t kSleepUntilDawnFlag = 1u << 0;
inline constexpr std::uint16_t kSleepVoteYesFlag = 1u << 0;
inline constexpr std::uint16_t kAttackHeavyFlag = 1u << 0;  // M30-T3：Attack 重击标记
inline constexpr std::uint16_t kBlockDownFlag = 1u << 0;    // M30-T6：Block 按下/松开

// 固定字段 payload：网络编码不需要 variant/JSON；未使用字段保持默认值。
// flags 低 8 位保存 InventoryActionType，高 8 位保存批量制作件数（0 兼容旧命令，视为 1）。
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
    return type >= CommandType::Move && type <= CommandType::ChooseWorldEvent;
}

const char* CommandTypeName(CommandType type);

enum class TransportMode : std::uint8_t {
    Direct = 0,
    Loopback = 1,
};

const char* TransportModeName(TransportMode mode);

}  // namespace zl::net
