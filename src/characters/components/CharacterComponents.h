#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zl::ecs {

// M4-T4：角色领域组件（玩家控制 + 占位动画）。
// 组件 = 纯数据；行为在 characters::InputSystem / AnimationSystem / MovementSystem。

// 本地玩家标记（一局一个）。只有带 PlayerTag 的实体读键盘。
struct PlayerTag {};

// M13-T4：实体归属。0 = 服务器/环境所有；非 0 = 由对应玩家 StableId 控制。
// 玩家实体默认 owner = 自己；未来驯服/召唤物可复用本组件，复制层只读取元数据。
// 这是会话内元数据，不进入存档；读档玩家没有本组件时，复制层按 PlayerTag 回退为自身。
struct NetworkOwner {
    std::uint64_t stableId = 0;
};

// 玩家已学会的锁定配方 id（蓝图/杂志解锁）。保存时只记录额外学会的配方；
// 未锁定配方始终可用。容器保持排序去重，便于存档与比较。
struct KnownRecipes {
    std::vector<std::string> recipeIds;
};

// 跨存档保留的制作任务：活跃批次进度 + 后续 FIFO 队列。
// 运行时以 WorldDemoState 为准，保存前同步到本组件；读档后恢复，配方存在/已学会才继续，
// 制作站与材料在计时完成或队首开始时再校验（读档时 chunk 可能尚未加载）。
inline constexpr int kMaxCraftBatchCount = 99;
inline constexpr int kMaxCraftQueueEntries = 8;

struct CraftQueueItem {
    std::string recipeId;
    int count = 1;
};

struct CraftingJob {
    bool active = false;
    std::string recipeId;
    float elapsed = 0.0f;
    float duration = 0.0f;
    float toolEfficiency = 1.0f;
    int batchTotal = 0;
    int batchCompleted = 0;
    std::vector<CraftQueueItem> queue;
};

// 本地控制参数（玩家专属；AI 实体不挂）
struct LocalControl {
    float walkSpeed = 4.0f;  // 步行速度（格/秒）
};

// M12-T1：命令化的屏幕方向输入（-1..1，y 向下）。
// 玩家命令 Move 写入本组件，InputSystem 每 tick 做等距矫正并落到 MoveIntent；
// 组件保持纯数据，网络侧只传屏幕方向，不依赖相机/世界矩阵。
struct MoveInput {
    float x = 0.0f;
    float y = 0.0f;
};

// 期望速度（格/秒，世界格空间）：InputSystem / AI 写入，MovementSystem 消费。
// 语义 = "本 tick 想以这个速度移动"；写入方每 tick 全量重写（无输入 = 零向量）。
struct MoveIntent {
    float x = 0.0f;
    float y = 0.0f;
};

// 4 向朝向（屏幕语义，取值即帧序：Down/Left/Right/Up）。
// 由移动速度在屏幕坐标下的主轴导出（AnimationSystem）。
enum class Direction : std::uint8_t {
    Down = 0,
    Left = 1,
    Right = 2,
    Up = 3,
};

struct Facing {
    Direction dir = Direction::Down;
};

// M14-T2：角色动画片段。Legacy 保持旧 8 帧资源可用；v2 由 AnimationSystem
// 按移动/攻击/受击/死亡切换。片段名是 manifest 的稳定键，禁止靠文件名拼接。
enum class AnimClip : std::uint8_t {
    Legacy = 0,
    Idle = 1,
    Walk = 2,
    Attack = 3,
    Hit = 4,
    Death = 5,
    Count = 6,
};

// 动画事件：由片段事件帧触发。网络复制不传事件，远端从复制的 ActionState/
// 速度重新推导；事件只用于本地表现（脚步/武器命中音效）。
enum class AnimEvent : std::uint8_t {
    None = 0,
    Footstep = 1,
    Hit = 2,
};

// 动画状态（M4-T4，M14-T3 扩展）。
// 帧序约定：v2 片段 index = dir * framesPerDir + frame；旧资源按 4 向 × 2 帧回退。
// clip/lastEvent/eventSerial/eventMaskDone 是运行时字段：不进入存档，也不进
// M13 复制负载，保证旧快照仍能按 frame/framesPerDir/fps/time 解码。
struct AnimState {
    std::uint8_t frame = 0;         // [0, framesPerDir)
    std::uint8_t framesPerDir = 2;  // 每方向帧数
    float fps = 8.0f;               // 当前片段帧率
    float time = 0.0f;              // 帧计时累加器（循环取模，不无界增长）

    AnimClip clip = AnimClip::Legacy;
    AnimEvent lastEvent = AnimEvent::None;
    std::uint32_t eventSerial = 0;  // 每次事件帧自增；表现层比较后消费
    std::uint8_t eventMaskDone = 0; // 当前片段已触发的事件帧位（循环时清零）
};

enum class ZombieVariantKind : std::uint8_t {
    Normal = 0,
    Fast = 1,
    Strong = 2,
    Armored = 3,  // M23-F6-T3：军械守卫，重甲/抗击退（追加在末尾保持旧档兼容）
};

struct ZombieVariant {
    ZombieVariantKind kind = ZombieVariantKind::Normal;
    float speedScale = 1.0f;
    float damageScale = 1.0f;
    float visualScale = 1.0f;
};

// M23-F6-T3：重甲变体的伤害/击退规则。数值由 kind 派生，不新增存档或复制字段。
inline constexpr float kArmoredZombieDamageReduction = 0.35f;
inline constexpr float kArmoredZombieKnockbackScale = 0.35f;
// M30-T5：重甲只对达到该硬直条伤害的近战攻击产生基础硬直；
// 轻击（<=12）不能无脑打断，重击（>=18）可以。0 表示不参与破防。
inline constexpr float kArmoredZombieStaggerPoiseThreshold = 16.0f;

inline float ZombieVariantDamageReduction(ZombieVariantKind kind) {
    return kind == ZombieVariantKind::Armored ? kArmoredZombieDamageReduction
                                              : 0.0f;
}

inline float ZombieVariantKnockbackScale(ZombieVariantKind kind) {
    return kind == ZombieVariantKind::Armored ? kArmoredZombieKnockbackScale
                                              : 1.0f;
}

// 非破防轻击对重甲不产生基础硬直；破防长硬直由 CombatSystem 单独写入。
// 阈值由 CombatRuleConfig.armoredStaggerPoiseThreshold 提供，缺省保持 T5 数值。
inline float ZombieVariantStaggerScale(
    ZombieVariantKind kind, float poiseDamage,
    float armoredThreshold = kArmoredZombieStaggerPoiseThreshold) {
    if (kind == ZombieVariantKind::Armored && poiseDamage > 0.0f &&
        poiseDamage < armoredThreshold) {
        return 0.0f;
    }
    return 1.0f;
}

// M23-F3-T5：僵尸燃烧状态（运行时组件；T6 再接存档/联机）。
// remaining 是剩余燃烧时间；damageAccumulator/spreadAccumulator 分别驱动
// 持续伤害与脚下火源传播；panicRemaining 内 AI 会追向最近玩家并加速。
struct BurningZombie {
    float remaining = 0.0f;
    float damageAccumulator = 0.0f;
    float spreadAccumulator = 0.0f;
    float panicRemaining = 0.0f;
    float panicSpeedScale = 1.0f;
    float panicChaseRadius = 0.0f;
};

enum class ActorAction : std::uint8_t {
    Idle = 0,
    Moving = 1,
    Attacking = 2,
    Crafting = 3,
    Hit = 4,
    Dead = 5,
    Howling = 6,  // M20-F3：嚎叫预警/施放（追加在末尾，保持旧快照数值兼容）
};

struct ActionState {
    ActorAction action = ActorAction::Idle;
    float elapsed = 0.0f;
    float duration = 0.0f;
};

// M5-T1：生命值。current <= 0 = 死亡（DeathSystem 检测 → 生成尸体 + 延迟销毁实体）。
// 伤害管线（M5-T6 CombatSystem）只修改 current；本组件不携带来源/类型。
struct Health {
    float current = 100.0f;
    float max = 100.0f;
};

// M25-T4-V9：仅驾驶中的玩家持有；车辆对爪/咬提供减伤，不免疫其他伤害。
// 运行时组件，不写存档/复制；服务器与单机在上下车时同步。
struct VehicleOccupant {
    float damageReduction = 0.85f;
};

// M5-T1：需求（PZ 风格：0 = 完全满足，1 = 极限）。衰减与效果接后续里程碑，
// 本任务只定稿数据形态；数值含义固定为归一化 [0,1]，便于 HUD/脚本统一缩放。
struct Needs {
    float hunger = 0.0f;   // 饥饿
    float thirst = 0.0f;   // 口渴
    float fatigue = 0.0f;  // 疲劳

    float moveScale = 1.0f;
    float damageScale = 1.0f;
    float healthDrainPerSecond = 0.0f;
    std::uint8_t warning = 0;
};

// M23-F5-T3：食物营养与中毒。营养是临时饱腹缓冲（0~1），会按游戏时间衰减，
// 并降低饥饿增速/饥饿伤害；poisonRemaining 表示食物中毒剩余秒数。
// T3 只做运行时状态，存档/联机复制由 F5-T4 接入。
struct FoodState {
    float nutrition = 0.0f;
    float poisonRemaining = 0.0f;
    float poisonDamagePerSecond = 0.0f;
    float poisonTotal = 0.0f;
};

// M20-F2：体温/潮湿。bodyC 与 wetness 进入存档；ambient/effective/insulation/
// waterproof/moveScale/warning 每 tick 从天气、房间与篝火重新派生。
struct Climate {
    float bodyC = 37.0f;
    float wetness = 0.0f;
    float ambientC = 24.0f;
    float effectiveC = 24.0f;
    float insulation = 0.0f;
    float waterproof = 0.0f;
    float moveScale = 1.0f;
    bool indoor = false;
    bool nearFire = false;
    std::uint8_t warning = 0;
};

// M5-T1：尸体（DeathSystem 在死者位置生成：Position + SpriteRef("corpse.placeholder")
// + 本标记；无 Collider/RigidBody —— 不阻挡、不参与物理，只进渲染收集与空间索引）。
struct Corpse {
    std::uint64_t sourceId = 0;  // 死者 StableId（未来搜刮/存档溯源）
};

// M5-T6：倒地过渡（DeathSystem）：HP<=0 → 先入本组件（不可行动、速度冻结，
// 击退仍可滑行），remaining 递减到 0 才生成尸体并回收实体——
// 表现链"硬直（HitReaction）→ 倒地（Dying）→ 尸体（Corpse）"。
struct Dying {
    float remaining = 0.5f;  // 倒地剩余（秒）
};

}  // namespace zl::ecs
