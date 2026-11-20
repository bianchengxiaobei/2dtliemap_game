#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "characters/components/CombatComponents.h"
#include "characters/systems/CombatRules.h"
#include "characters/systems/WoundSystem.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {

// M5-T6：战斗系统。固定链位置（docs/00 §6.1）：Boids 之后、Movement 之前——
//  1) 衰减受击反应（硬直计时 / 击退速度 / 闪红恢复），硬直实体的 MoveIntent
//     被击退速度接管（覆盖 AI / 输入本 tick 写的意图）；
//  2) 推进攻击前摇；前摇结束才扇形判定（range + arcDegrees）
//     → ApplyDamage 统一管线（可命中锥内多个目标）。
//
// 伤害管线 ApplyDamage（来源/类型解耦的单一入口；Update/演示/测试/未来脚本共用）：
//  闪避无敌 → 格挡/弹反 → Health 扣减 → HitReaction（硬直/击退/闪红）
//  → 硬直条破防 → 致死由 DeathSystem 接手（倒地窗口 → 尸体）。Stats 供 HUD / 测试。
class CombatSystem {
public:
    explicit CombatSystem(WoundParams woundParams = {},
                          CombatRuleConfig rules = {})
        : m_woundParams(woundParams),
          m_rules(SanitizeCombatRuleConfig(rules)) {}

    struct Stats {
        std::uint64_t swings = 0;   // 真正完成前摇并结算的挥击数（跨帧累计）
        std::uint64_t hits = 0;     // 未被格挡/弹反/闪避的真实命中（跨帧累计）
        std::uint64_t kills = 0;    // 击杀数（跨帧累计，HP 被打到 <=0）
        std::uint64_t blocks = 0;   // 普通格挡成功（跨帧累计）
        std::uint64_t parries = 0;  // 弹反成功（跨帧累计）
        std::uint64_t guardBreaks = 0;  // 格挡体力不足破防（跨帧累计）
        std::uint64_t dodges = 0;       // 无敌帧闪避伤害（跨帧累计）
        std::uint64_t poiseBreaks = 0;  // 硬直条破防（跨帧累计）
        std::uint64_t backstabs = 0;    // M34-F1-T4：背刺增伤命中（跨帧累计）
        float damageDealt = 0.0f;   // 累计伤害（跨帧累计）
        int staggered = 0;          // 本帧硬直中的实体数（每帧快照）
    };

    // M30-T2：ApplyDamage 的结构化结果，供 m_hits / 经验 / 反馈区分“真实命中”
    // 与格挡、弹反、闪避、破防。
    struct DamageResult {
        bool applied = false;
        bool blocked = false;
        bool parried = false;
        bool guardBroken = false;
        bool dodged = false;
        bool poiseBroken = false;
    };

    // M35-F2-T2：ApplyDamage 实际扣血后的脚本事件载荷（一次扣血一条）。
    // amount 是护甲/格挡减伤后的实际伤害；remainingHp 下限为 0。
    struct DamageApplied {
        std::uint64_t targetId = 0;
        std::uint64_t sourceId = 0;
        ecs::DamageType type = ecs::DamageType::Blunt;
        float amount = 0.0f;
        float remainingHp = 0.0f;
        bool lethal = false;
        bool blocked = false;
    };

    enum class BlockOutcome : std::uint8_t {
        Blocked = 0,
        Parried = 1,
        GuardBroken = 2,
        Dodged = 3,
    };

    // 格挡/弹反/闪避反馈：game 层据此播放占位音/火花/提示，并避免计为命中。
    struct BlockFeedback {
        ecs::Entity defender = ecs::EcsWorld::kNull;
        ecs::Entity attacker = ecs::EcsWorld::kNull;
        BlockOutcome outcome = BlockOutcome::Blocked;
        ecs::DamageType type = ecs::DamageType::Blunt;
    };

    // M15-T1：命中后的主手武器磨损请求。CombatSystem 只产生请求，
    // 由上层 game 侧系统落地到 Equipment（characters 不依赖 items 模块）。
    struct WeaponWearRequest {
        ecs::Entity attacker = ecs::EcsWorld::kNull;
        float amount = 0.0f;
    };

    // M30-T5：真实近战命中反馈（玩家攻击命中/破防时播放占位火花/音效）。
    struct HitFeedback {
        ecs::Entity attacker = ecs::EcsWorld::kNull;
        ecs::Entity target = ecs::EcsWorld::kNull;
        float damage = 0.0f;
        bool lethal = false;
        bool poiseBroken = false;
        bool backstab = false;
    };

    // M19-F1：本 tick 的实际命中记录（只由 AttackIntent 扇形判定产生）。
    // 上层据此发放近战经验：空挥/被格挡/弹反/闪避/未命中都不会出现在这里。
    struct HitInfo {
        ecs::Entity attacker = ecs::EcsWorld::kNull;
        ecs::Entity target = ecs::EcsWorld::kNull;
        float damage = 0.0f;
        bool lethal = false;
        bool poiseBroken = false;
        bool backstab = false;
    };

    // M19-F4：护甲磨损回调。CombatSystem 只读取 DamageResist 并通知上层，
    // 由 game 层落到 Equipment（characters 不依赖 items 模块）。
    using ArmorWearCallback = std::function<void(ecs::Entity, float)>;
    void SetArmorWearCallback(ArmorWearCallback callback) {
        m_armorWearCallback = std::move(callback);
    }

    // M20-F1：实际爪/咬命中回调（在写伤口之前调用，供上层按既有未包扎伤口
    // 计算暴露概率；characters 不依赖具体感染数值/随机源）。
    using InfectionExposureCallback =
        std::function<void(ecs::Entity, ecs::DamageType, float)>;
    void SetInfectionExposureCallback(InfectionExposureCallback callback) {
        m_infectionExposureCallback = std::move(callback);
    }

    // M30-T2：格挡/弹反/闪避反馈回调（普通格挡仍可能造成剩余伤害）。
    using BlockFeedbackCallback = std::function<void(const BlockFeedback&)>;
    void SetBlockFeedbackCallback(BlockFeedbackCallback callback) {
        m_blockFeedbackCallback = std::move(callback);
    }

    // M30-T5：真实命中反馈（不含空挥/格挡/弹反/闪避）。
    using HitFeedbackCallback = std::function<void(const HitFeedback&)>;
    void SetHitFeedbackCallback(HitFeedbackCallback callback) {
        m_hitFeedbackCallback = std::move(callback);
    }

    // M35-F2-T2：实际扣血回调（闪避/弹反/零伤害不触发）。
    using DamageAppliedCallback = std::function<void(const DamageApplied&)>;
    void SetDamageAppliedCallback(DamageAppliedCallback callback) {
        m_damageAppliedCallback = std::move(callback);
    }

    void SetCombatRules(CombatRuleConfig rules) {
        m_rules = SanitizeCombatRuleConfig(rules);
    }
    const CombatRuleConfig& GetCombatRules() const { return m_rules; }

    // 消耗战斗体力。失败（无组件/ NaN / 耗尽锁定 / 不足）时不扣体力。
    // 成功会重置回复延迟；剩余体力进入阈值时自动开启 exhausted 锁定。
    bool TrySpendStamina(ecs::EcsWorld& world, ecs::Entity entity, float amount);

    // 统一伤害管线：命中有效性（有 Health 且 HP>0）→ 闪避/格挡/弹反
    // → 护甲减伤 → 扣血 + 受击反应 + 硬直条 + 统计。
    // 返回是否实际造成伤害（已倒地/闪避/弹反返回 false；普通格挡仍返回 true）。
    bool ApplyDamage(ecs::EcsWorld& world, ecs::Entity target,
                     const ecs::DamageEvent& event,
                     DamageResult* result = nullptr);

    void Update(ecs::EcsWorld& world, float dt);
    const Stats& GetStats() const { return m_stats; }
    const std::vector<WeaponWearRequest>& GetWeaponWears() const { return m_weaponWears; }
    const std::vector<HitInfo>& GetHits() const { return m_hits; }

private:
    struct Candidate {
        ecs::Entity e = ecs::EcsWorld::kNull;
        float x = 0.0f;
        float y = 0.0f;
        bool ai = false;  // 带 AiState（M5-T6 阵营规则：僵尸不误伤同类）
    };

    std::vector<Candidate> m_candidates;  // 命中候选缓存（每 tick 收集一次）
    std::vector<WeaponWearRequest> m_weaponWears;  // 本 tick 命中后的磨损请求
    std::vector<HitInfo> m_hits;  // M19-F1：本 tick 实际命中记录
    ArmorWearCallback m_armorWearCallback;
    InfectionExposureCallback m_infectionExposureCallback;
    BlockFeedbackCallback m_blockFeedbackCallback;
    HitFeedbackCallback m_hitFeedbackCallback;
    DamageAppliedCallback m_damageAppliedCallback;
    WoundParams m_woundParams;
    CombatRuleConfig m_rules;
    Stats m_stats;
};

}  // namespace zl::characters
