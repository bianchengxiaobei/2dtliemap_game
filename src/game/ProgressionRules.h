#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "characters/components/ProgressionComponents.h"
#include "ecs/EcsWorld.h"

namespace zl::core {
class Config;
}

namespace zl::ecs {
class EcsWorld;
}

namespace zl::characters {
class CombatSystem;
}

namespace zl::game {

// M19-F1：可配置成长规则。技能每级效果与 Perk 效果都从 config.progression 读取，
// 默认值只用于缺省/测试；修改配置无需改代码。
struct SkillRules {
    std::string id;
    std::string name;
    std::string description;
    float xpBase = 80.0f;
    float xpGrowth = 1.4f;
    float damagePerLevel = 0.0f;      // 近战：伤害乘数 +X/级
    float repairPerLevel = 0.0f;      // 建造：维修量 +X/级
    float craftSpeedPerLevel = 0.0f;  // 制作：速度 +X/级（耗时 / (1+X)）
    float carryKgPerLevel = 0.0f;     // 生存：负重 +kg/级
    float lootXpPerLevel = 0.0f;      // 生存：搜刮经验 +X/级
    float needsDecayPerLevel = 0.0f;  // 生存：需求衰减 -X/级
};

struct PerkRules {
    std::string id;
    std::string name;
    std::string description;
    float meleeDamageBonus = 0.0f;
    float meleeCooldownScale = 1.0f;
    float moveSpeedBonus = 0.0f;
    float carryKg = 0.0f;
    float craftSpeedBonus = 0.0f;
    float repairBonus = 0.0f;
    float hungerDecayBonus = 0.0f;
    float thirstDecayBonus = 0.0f;
    float fatigueDecayBonus = 0.0f;
    float lootXpBonus = 0.0f;
    float characterXpBonus = 0.0f;
    float skillXpBonus = 0.0f;
};

struct ProgressionConfig {
    bool enabled = true;
    int maxLevel = 10;
    int maxSkillLevel = 10;
    float xpBase = 180.0f;
    float xpGrowth = 1.45f;
    int perkChoices = 3;
    std::array<SkillRules, ecs::kSkillCount> skills{};
    std::vector<PerkRules> perks;

    // 单次行为经验（完成行为才发放；失败/重复无效操作不调用）。
    float meleeXpPerHit = 5.0f;
    float meleeXpPerKill = 25.0f;
    float craftXpPerItem = 8.0f;
    float buildXpPerStructure = 20.0f;
    float lootXpPerContainer = 15.0f;
    float survivalXpPerHour = 12.0f;
};

struct ProgressionAward {
    int characterLevels = 0;
    int skillLevels = 0;
    int perkPoints = 0;
    float characterXp = 0.0f;
    float skillXp = 0.0f;

    bool LeveledUp() const {
        return characterLevels > 0 || skillLevels > 0;
    }
};

ProgressionConfig DefaultProgressionConfig();
ProgressionConfig LoadProgressionConfig(const core::Config& config);

// XP 门槛：从当前等级升到下一级需要多少。level 从 1 开始。
float LevelThreshold(float base, float growth, int level);
float CharacterXpToNext(const ecs::Progression& progression,
                        const ProgressionConfig& config);
float SkillXpToNext(const ecs::Progression& progression, ecs::SkillKind kind,
                    const ProgressionConfig& config);
float SkillXpToNext(const SkillRules& skill, int level,
                    const ProgressionConfig& config);

// 发放经验：角色与对应技能同时增长；角色升级每个等级给 1 点 Perk。
// 达到上限后 XP 钳制在门槛，不产生负经验/重复点。
ProgressionAward AwardXp(ecs::Progression& progression, ecs::SkillKind kind,
                         float baseAmount, const ProgressionConfig& config);

// 派生字段（需求衰减、负重）由配置和当前等级/Perk 重算。
void RefreshDerived(ecs::Progression& progression,
                    const ProgressionConfig& config);

// 确定性 3 选 1：仅从未拥有的 Perk 中按稳定哈希排序，客户端只展示服务器结果。
std::array<std::string, 3> PerkChoiceIds(const ecs::Progression& progression,
                                         const ProgressionConfig& config);
int PerkChoiceCount(const std::array<std::string, 3>& choices);
bool ChoosePerkById(ecs::Progression& progression, std::string_view perkId,
                    const ProgressionConfig& config);
bool ChoosePerkByIndex(ecs::Progression& progression, int index,
                       const ProgressionConfig& config);

const PerkRules* FindPerk(const ProgressionConfig& config, std::string_view id);
const SkillRules* FindSkill(const ProgressionConfig& config, ecs::SkillKind kind);

// UI/日志文案（中文；数值随配置变化）。
std::string SkillEffectSummary(const ProgressionConfig& config,
                               ecs::SkillKind kind);
std::string ProgressionSkillLine(const ecs::Progression& progression,
                                 const ProgressionConfig& config,
                                 ecs::SkillKind kind);
std::string ProgressionPerkSummary(const ecs::Progression& progression,
                                   const ProgressionConfig& config);
std::string PerkChoiceLine(const PerkRules& perk, int index);

// 数值效果（单机与服务器共用同一套乘数）。
float MeleeDamageMultiplier(const ecs::Progression& progression,
                            const ProgressionConfig& config);
float MeleeCooldownMultiplier(const ecs::Progression& progression,
                              const ProgressionConfig& config);
float MoveSpeedMultiplier(const ecs::Progression& progression,
                          const ProgressionConfig& config);
float CarryCapacityBonusKg(const ecs::Progression& progression,
                           const ProgressionConfig& config);
float CraftSpeedMultiplier(const ecs::Progression& progression,
                           const ProgressionConfig& config);
float CraftDurationScale(const ecs::Progression& progression,
                         const ProgressionConfig& config);
float RepairAmountMultiplier(const ecs::Progression& progression,
                             const ProgressionConfig& config);
float LootXpMultiplier(const ecs::Progression& progression,
                       const ProgressionConfig& config);

// 结算 CombatSystem 本 tick 的命中：只给实际命中/致死的玩家发经验。
// 返回本次获得经验的玩家实体（服务器用于标记会话状态脏）。
int AwardCombatHits(ecs::EcsWorld& world,
                    const characters::CombatSystem& combat,
                    const ProgressionConfig& config,
                    std::vector<ecs::Entity>* changedPlayers = nullptr);

}  // namespace zl::game
