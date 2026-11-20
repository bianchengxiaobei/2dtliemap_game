#pragma once

#include <array>
#include <cstdint>

namespace zl::ecs {

// M5-T6：战斗领域组件（攻击意图 / 受击反应 / 伤害事件）。
// 组件 = 纯数据；行为在 characters::CombatSystem
// （固定链位置：Boids 之后、Movement 之前）。

// 伤害类型：伤害管线"来源/类型解耦"的类型侧（Health 不携带来源/类型）。
// M6 装备/护甲在 ApplyDamage 处按类型扩展减伤钩子。
enum class DamageType : std::uint8_t {
    Blunt = 0,  // 钝击（徒手/枪托）
    Bite = 1,   // 撕咬（僵尸）
    Debug = 2,  // 演示/调试（F6 等）
    Claw = 3,   // 爪击（M15-T2，普通/快速僵尸）
    Fire = 4,   // 火焰持续伤害（M23-F3-T4；不写伤口）
};

// M19-F4：由装备派生的伤害抗性（不进入存档/复制，装备变化后由 game 层重算）。
// 减伤值为 0~1；wearPerHit 是每次实际命中的护甲耐久消耗；moveScale 钳制在 0.5~1。
struct DamageResist {
    float clawReduction = 0.0f;
    float biteReduction = 0.0f;
    float bluntReduction = 0.0f;
    float wearPerHit = 0.0f;
    float moveScale = 1.0f;
};

inline float DamageResistFor(const DamageResist& resist, DamageType type) {
    switch (type) {
        case DamageType::Claw:
            return resist.clawReduction;
        case DamageType::Bite:
            return resist.biteReduction;
        case DamageType::Blunt:
            return resist.bluntReduction;
        case DamageType::Debug:
            return 0.0f;
        case DamageType::Fire:
            return 0.0f;
    }
    return 0.0f;
}

// 伤害事件（管线输入）：来源 = 稳定 ID（跨模块约定，绝不存 entt 句柄）；
// 击退/硬直是"表现参数"，由管线统一落到受害者的 HitReaction。
struct DamageEvent {
    std::uint64_t sourceId = 0;   // 攻击者稳定 ID（0 = 环境/未知）
    DamageType type = DamageType::Blunt;
    float amount = 0.0f;          // 伤害量（HP 扣减）
    float dirX = 0.0f;            // 击退方向（世界格空间；零向量 = 无击退）
    float dirY = 0.0f;
    float knockback = 0.0f;       // 击退初速（格/秒）
    float staggerSeconds = 0.0f;  // 硬直时长（秒）
    float poiseDamage = 0.0f;     // M30-T2：对目标硬直条的伤害（0 = 不破防）
};

// M30-T2：轻击/重击。CombatSystem 只消费，不决定输入。
enum class AttackKind : std::uint8_t {
    Light = 0,
    Heavy = 1,
};

// 攻击意图（挥击申请）：攻击者（AiSystem / 玩家输入）写 pending = true；
// CombatSystem 本 tick 消费：清 pending → 扇形判定（range + arcDegrees）→ 伤害管线。
// 判定不依赖 targetId（它是"想打谁"的提示，供未来弹道/日志用）。
// windupSeconds=0 保持旧 AI/测试的“落下即命中”行为。
struct AttackIntent {
    std::uint64_t targetId = 0;      // 期望目标稳定 ID（0 = 只按方向横扫）
    float dirX = 1.0f;               // 攻击方向（单位向量，世界格空间）
    float dirY = 0.0f;
    float range = 1.0f;              // 触及距离（格，中心距）
    float arcDegrees = 100.0f;       // 扇形总张角（以 dir 为轴）
    float damage = 10.0f;
    float knockback = 2.5f;          // 命中击退初速（格/秒）
    float staggerSeconds = 0.35f;    // 命中硬直时长（秒）
    float weaponWearPerHit = 0.0f;   // M15-T1：命中后主手武器耐久消耗（0 = 不磨损）
    DamageType type = DamageType::Blunt;
    AttackKind kind = AttackKind::Light;  // M30-T2：轻/重击
    float windupSeconds = 0.0f;      // 前摇总时长（0 = 旧行为，立即命中）
    float windupLeft = 0.0f;         // 剩余前摇（运行时）
    float poiseDamage = 0.0f;        // 对目标硬直条的伤害
    float staminaCost = 0.0f;        // 动作开始时由输入/服务器扣除
    // M34-F1-T4：1 = 不启用背刺；>1 时只对首个“未警觉且在背后”的 AI 目标
    // 乘该倍率。判定发生在命中结算时，倍率由 game 层按 stealth 配置写入。
    float backstabMultiplier = 1.0f;
    float backstabHalfAngleDegrees = 60.0f;
    bool pending = false;            // true = 有攻击申请待消费
};

// 受击反应（CombatSystem 写/衰减）：
//  - staggerLeft > 0：硬直中——AiSystem 冻结转移/计时，MoveIntent 被击退速度接管；
//  - vx/vy：击退速度（指数衰减）；flashLeft：受击闪红剩余（渲染读 SpriteRef.tint）；
//  - sourceDir：从受害者指向攻击者的单位方向（屏幕边缘/方向提示）；
//  - hitSerial：每次实际受击自增，HUD 用它去重（同一帧多目标不重复提示）。
struct HitReaction {
    float staggerLeft = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float flashLeft = 0.0f;
    float sourceDirX = 0.0f;
    float sourceDirY = 0.0f;
    DamageType sourceType = DamageType::Blunt;
    std::uint32_t hitSerial = 0;
    std::uint32_t restoreTint = 0xFFFFFFFFu;
};

// M30-T2：战斗体力（运行时，不存档/不复制）。current/max 由 CombatSystem
// 按 Needs.fatigue 派生有效上限；exhausted 为耗尽锁定剩余秒数（0 = 未锁定）。
struct CombatStamina {
    float current = 100.0f;
    float max = 100.0f;
    float regenDelay = 0.0f;
    float exhausted = 0.0f;
};

// M30-T2：格挡/弹反/闪避姿态（运行时，不存档/不复制）。
// blockReduction/blockStaminaCost 是装备派生的可选覆盖；<0 表示使用全局配置。
struct CombatStance {
    bool blocking = false;
    float blockElapsed = 0.0f;
    float parryCooldown = 0.0f;
    float guardBreakLeft = 0.0f;
    float blockReduction = -1.0f;
    float blockStaminaCost = -1.0f;
    float dodgeLeft = 0.0f;
    float dodgeInvulnLeft = 0.0f;
    float dodgeDirX = 0.0f;
    float dodgeDirY = 0.0f;
    float dodgeCooldown = 0.0f;
};

// M30-T2：硬直条（运行时，不存档/不复制；只给 AI/动物等可硬直目标）。
// current <= 0 且 max > 0 表示已破防，等待 regenDelay 后回复。
struct Poise {
    float current = 0.0f;
    float max = 0.0f;
    float regenDelay = 0.0f;
};

struct Projectile {
    std::uint64_t ownerStableId = 0;
    float lifetime = 3.0f;
    float radius = 0.2f;
    float damage = 25.0f;
    float knockback = 1.0f;
    float staggerSeconds = 0.15f;
    bool hitPlayers = false;
    // M23-F1-T4：本地弹道按飞行距离结算；maxDistance<=0 时只受 lifetime 限制。
    float startX = 0.0f;
    float startY = 0.0f;
    float maxDistance = 0.0f;
    float falloffStart = 0.0f;
    float falloffEnd = 0.0f;
    float falloffScale = 1.0f;
};

struct TransientEffect {
    float remaining = 0.35f;
};

// M15-T2：伤口/流血（纯数据）。每个角色最多保留 kMaxWounds 处伤口；
// severity 0~1 供绷带/疼痛/HUD 共用一个归一化尺度，bleedRate 为起始失血 HP/s，
// 实际失血随 bleedRemaining / bleedDuration 线性衰减。bandaged=true 表示已包扎，
// 不再流血（severity 仍保留，供后续疼痛/治疗使用）。
inline constexpr std::uint8_t kMaxWounds = 4;

struct WoundEntry {
    DamageType source = DamageType::Blunt;
    float severity = 0.0f;
    float bleedRate = 0.0f;
    float bleedRemaining = 0.0f;
    float bleedDuration = 0.0f;
    bool bandaged = false;
};

struct Wounds {
    std::array<WoundEntry, kMaxWounds> entries{};
    std::uint8_t count = 0;
};

// M15-T5：致死来源。直接命中类型之外，还记录持续失血与需求耗尽，
// 由 DeathSystem/结算页映射为中文死因。
enum class DamageCause : std::uint8_t {
    Unknown = 0,
    Blunt = 1,
    Bite = 2,
    Claw = 3,
    Debug = 4,
    Bleed = 5,
    Starvation = 6,
    Dehydration = 7,
    Infection = 8,
    Hypothermia = 9,
    Fire = 10,
    Poison = 11,
};

inline DamageCause DamageCauseFromType(DamageType type) {
    switch (type) {
        case DamageType::Blunt:
            return DamageCause::Blunt;
        case DamageType::Bite:
            return DamageCause::Bite;
        case DamageType::Claw:
            return DamageCause::Claw;
        case DamageType::Debug:
            return DamageCause::Debug;
        case DamageType::Fire:
            return DamageCause::Fire;
    }
    return DamageCause::Unknown;
}

// 脚本事件 OnDamage.damageType 的稳定小写标识。
inline const char* DamageTypeName(DamageType type) {
    switch (type) {
        case DamageType::Blunt:
            return "blunt";
        case DamageType::Bite:
            return "bite";
        case DamageType::Claw:
            return "claw";
        case DamageType::Debug:
            return "debug";
        case DamageType::Fire:
            return "fire";
    }
    return "unknown";
}

// 脚本事件用的稳定小写标识；不要本地化（HUD/日志才用中文标签）。
inline const char* DamageCauseName(DamageCause cause) {
    switch (cause) {
        case DamageCause::Blunt:
            return "blunt";
        case DamageCause::Bite:
            return "bite";
        case DamageCause::Claw:
            return "claw";
        case DamageCause::Debug:
            return "debug";
        case DamageCause::Bleed:
            return "bleed";
        case DamageCause::Starvation:
            return "starvation";
        case DamageCause::Dehydration:
            return "dehydration";
        case DamageCause::Infection:
            return "infection";
        case DamageCause::Hypothermia:
            return "hypothermia";
        case DamageCause::Fire:
            return "fire";
        case DamageCause::Poison:
            return "poison";
        case DamageCause::Unknown:
        default:
            return "unknown";
    }
}


struct LastDamage {
    DamageCause cause = DamageCause::Unknown;
    float amount = 0.0f;
    std::uint64_t sourceId = 0;
};

// M15-T4：疼痛（纯数据）。baseLevel 由伤口严重度汇总，level 是止痛后的有效值；
// moveScale/attackCooldownScale 是已经按当前有效疼痛算好的乘数，供移动与挥击直接使用。
// relief* 表示短时止痛：强度 0~1，remaining/duration 线性衰减到 0 后疼痛恢复。
struct Pain {
    float baseLevel = 0.0f;
    float level = 0.0f;
    float moveScale = 1.0f;
    float attackCooldownScale = 1.0f;
    float reliefStrength = 0.0f;
    float reliefRemaining = 0.0f;
    float reliefDuration = 0.0f;
    float maxMoveSlow = 0.35f;
    float maxAttackCooldownIncrease = 0.5f;
};

// M20-F1：感染。progress 为累计病程（0~1），阶段按阈值派生；
// slow* 表示抗生素抑制窗口，timeToNextStage 是 HUD 用的派生剩余秒数。
enum class InfectionStage : std::uint8_t {
    None = 0,
    Incubating = 1,
    Fever = 2,
    Severe = 3,
};

struct Infection {
    InfectionStage stage = InfectionStage::None;
    float progress = 0.0f;
    float slowStrength = 0.0f;
    float slowRemaining = 0.0f;
    float slowDuration = 0.0f;
    float timeToNextStage = 0.0f;
};

}  // namespace zl::ecs
