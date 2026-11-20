#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zl::items {

// 物品类型（M6-T1）：JSON "type" 字段的取值集合。
enum class ItemType : std::uint8_t {
    Generic = 0,  // 杂物（无特殊用途，如破布）
    Weapon,       // 武器（M6-T5 装备闭环）
    Consumable,   // 消耗品（吃/喝/包扎，M6-T5 使用闭环）
    Clothing,     // 衣物（穿戴）
    Container,    // 容器（背包；容量规则 M6-T2）
};

const char* ItemTypeToString(ItemType type);

// 解析类型名（JSON 中的小写字符串）；失败返回 false 且不改 *out
bool ParseItemType(std::string_view text, ItemType* out);

// 使用效果（M6-T5；JSON "use" 可选对象）：
//   heal：立即回复 HP（0~100）；hunger/thirst/fatigue：降低对应需求量
//   （0~1 归一化，正值 = 减少需求）。无 use 字段 → 全 0（不可使用）。
struct UseEffect {
    float heal = 0.0f;
    float hunger = 0.0f;
    float thirst = 0.0f;
    float fatigue = 0.0f;
    float bandage = 0.0f;  // 停止流血并降低伤口严重度的比例（0~1）
    float painRelief = 0.0f;         // 短时止痛强度（0~1，不叠加）
    float painReliefSeconds = 0.0f;  // 止痛持续时间（秒）
    float healthPenalty = 0.0f;      // 使用后立即损失的生命值
    float infectionCure = 0.0f;      // 直接降低的感染病程（0~1）
    float infectionSlow = 0.0f;      // 抗生素抑制强度（0~1）
    float infectionSlowSeconds = 0.0f;  // 抑制持续时间（秒）
    float infectionRisk = 0.0f;      // 使用时的伤口感染基础概率（0~1）
    float extinguish = 0.0f;         // 灭火基础成功率（0~1；不参与普通 Use）
    float extinguishWear = 0.0f;     // 每次灭火消耗的耐久（0~1；水类用 replaceWith）
    std::string replaceWith;         // 使用后原位变成的物品 id（如水瓶 -> 空瓶）
    float nutrition = 0.0f;          // 食物营养值（0~1；提高饱腹并降低中毒风险）
    float foodPoisonChance = 0.0f;   // 食用时食物中毒基础概率（0~1）

    bool Any() const {
        return heal > 0.0f || hunger > 0.0f || thirst > 0.0f || fatigue > 0.0f ||
               bandage > 0.0f || painRelief > 0.0f || healthPenalty > 0.0f ||
               infectionCure > 0.0f || infectionSlow > 0.0f ||
               infectionRisk > 0.0f || nutrition > 0.0f ||
               foodPoisonChance > 0.0f;
        // extinguish 是 E 灭火专用；不进入 Any，避免“使用”背包时误消耗工具。
    }
};

// M30-T1：武器战斗参数（items.json 可选 "combat" 对象）。
// 每个字段可单独缺省；缺省字段由调用方提供的默认值补齐。
// stamina/windup/poise 是 M30 新字段，旧物品缺省时仍走原有默认值。
struct WeaponCombatFields {
    std::optional<float> damage;
    std::optional<float> range;
    std::optional<float> arcDegrees;
    std::optional<float> cooldownSeconds;
    std::optional<float> knockback;
    std::optional<float> staggerSeconds;
    std::optional<float> wearPerHit;
    std::optional<float> staminaCost;
    std::optional<float> windupSeconds;
    std::optional<float> poiseDamage;

    bool Any() const {
        return damage.has_value() || range.has_value() || arcDegrees.has_value() ||
               cooldownSeconds.has_value() || knockback.has_value() ||
               staggerSeconds.has_value() || wearPerHit.has_value() ||
               staminaCost.has_value() || windupSeconds.has_value() ||
               poiseDamage.has_value();
    }
};

// 重击覆盖数据：与轻击共用攻击字段，不含格挡字段。
struct WeaponHeavyCombat : WeaponCombatFields {};

struct WeaponCombat : WeaponCombatFields {
    std::optional<float> blockReduction;   // 0~0.95；0.65 = 伤害 ×0.35
    std::optional<float> blockStaminaCost;

    bool Any() const {
        return WeaponCombatFields::Any() || blockReduction.has_value() ||
               blockStaminaCost.has_value();
    }
};

struct RangedCombat {
    float damage = 0.0f;
    float range = 0.0f;
    float spreadDegrees = 0.0f;
    int pellets = 1;
    float cooldownSeconds = 0.0f;
    int magazine = 1;
    float reloadSeconds = 0.0f;
    std::string ammoDefId;
    float noiseRadius = 0.0f;
    float wearPerShot = 0.0f;
    float projectileSpeed = 0.0f;
    float falloffStart = 0.0f;
    float falloffEnd = 0.0f;
    float falloffScale = 1.0f;
};

// 解析后的攻击参数：基础值 = 空手/旧数据缺省，武器 combat 字段逐项覆盖。
struct WeaponAttackParams {
    float damage = 1.0f;
    float range = 1.0f;
    float arcDegrees = 100.0f;
    float cooldownSeconds = 0.35f;
    float knockback = 0.0f;
    float staggerSeconds = 0.0f;
    float wearPerHit = 0.0f;
    float staminaCost = 0.0f;
    float windupSeconds = 0.0f;
    float poiseDamage = 0.0f;
};

WeaponAttackParams ResolveWeaponAttack(const WeaponCombatFields& combat,
                                       const WeaponAttackParams& fallback);

// M34-F2：投掷落地火焰参数（items.json 的 throwable.fire）。
// 仅描述数据；实际点燃由 FireSystem 在投掷落地时执行。
struct ThrowableFire {
    float radius = 0.0f;   // 火焰半径（格，0~3）
    float minutes = 0.0f;  // 持续时间（游戏分钟，0~240）

    bool Any() const { return radius > 0.0f && minutes > 0.0f; }
};

// M34-F2：投掷物参数（items.json 可选 "throwable" 对象）。
// 未带该字段的物品保持旧行为，不能投掷。
struct ThrowableDef {
    float range = 0.0f;        // 最大射程（格，1~16）
    float damage = 0.0f;       // 落点伤害（0~80）
    float impactRadius = 0.0f; // 落点影响半径（格，0~3）
    float arcHeight = 0.0f;    // 表现弧高（格，0~3）
    float noiseRadius = 0.0f;  // 落地噪声半径（格，0~32）
    bool returnable = false;   // 未损坏时是否生成地面物品
    float breakChance = 0.0f;  // 落地损坏概率（0~1）
    std::optional<ThrowableFire> fire;  // 可选落地火焰
};

// M19-F4：衣物护甲参数（items.json 可选 "armor" 对象）。
// 减伤值为 0~1 的比例；condition 会按比例削弱实际减伤。insulation/waterproof
// 先作为数据契约保留，温度/潮湿结算由 M20-F2 接入。
struct ArmorStats {
    float clawReduction = 0.0f;
    float biteReduction = 0.0f;
    float bluntReduction = 0.0f;
    float wearPerHit = 0.0f;
    float moveScale = 1.0f;
    float insulation = 0.0f;
    float waterproof = 0.0f;

    bool Any() const {
        return clawReduction > 0.0f || biteReduction > 0.0f ||
               bluntReduction > 0.0f || wearPerHit > 0.0f ||
               moveScale < 0.999f || insulation > 0.0f || waterproof > 0.0f;
    }
};

// 当前耐久下的实际护甲效果；condition 钳制到 0~1。
struct ArmorProtection {
    float clawReduction = 0.0f;
    float biteReduction = 0.0f;
    float bluntReduction = 0.0f;
    float wearPerHit = 0.0f;
    float moveScale = 1.0f;
    float insulation = 0.0f;
    float waterproof = 0.0f;
};

ArmorProtection ResolveArmor(const ArmorStats& armor, float condition);

// 物品定义：不可变模板（数据来自 assets/items.json，M6-T1）。
// 实例的可变状态（耐久/剩余量等）在 ItemInstance 组件（M6-T2），
// 组件只引用定义 id，不复制 ItemDef。
struct ItemDef {
    std::string id;       // 唯一标识：小写字母/数字/下划线/点，1~64 字符
    std::string name;     // 显示名（可中文）
    std::string icon;     // 逻辑资源名（映射见 assets/asset_manifest.json）
    float weight = 0.0f;  // 单件重量（kg，0 ~ 1000）
    int maxStack = 1;     // 堆叠上限（1 ~ 9999）
    ItemType type = ItemType::Generic;
    UseEffect use;        // 使用效果（M6-T5；消耗品用）
    WeaponCombat combat;  // 战斗参数（M15-T1；weapon 专用）
    std::optional<WeaponHeavyCombat> heavy;  // 重击覆盖（M30-T1；weapon 专用）
    std::optional<RangedCombat> ranged;  // 远程武器参数（M23-F1；weapon 专用）
    std::optional<ThrowableDef> throwable;  // 投掷参数（M34-F2；显式标记才可投掷）
    ArmorStats armor;     // 护甲/保温参数（M19-F4；clothing 专用）
    float repairEfficiency = 1.0f;  // 维修包恢复量倍率（M19-F4）
    float spoilDays = 0.0f;  // >0 = 可腐败食物；ItemInstance.condition 当新鲜度
    std::vector<std::string> unlocksRecipes;  // 使用后学会的锁定配方 id（蓝图/杂志）
};

}  // namespace zl::items
