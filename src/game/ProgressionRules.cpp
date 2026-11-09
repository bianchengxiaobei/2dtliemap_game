#include "game/ProgressionRules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "characters/components/CharacterComponents.h"
#include "characters/systems/CombatSystem.h"
#include "core/Config.h"
#include "core/Log.h"
#include "ecs/EcsWorld.h"

namespace zl::game {
namespace {

constexpr const char* kLogModule = "progression";
constexpr float kMaxThreshold = 1000000000.0f;

bool Finite(float value) {
    return std::isfinite(value);
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!Finite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

int ClampInt(int value, int minimum, int maximum) {
    return std::clamp(value, minimum, maximum);
}

bool ValidPerkId(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

SkillRules MakeSkill(ecs::SkillKind kind) {
    SkillRules skill;
    skill.id = ecs::kSkillIds[ecs::SkillIndex(kind)];
    skill.name = ecs::kSkillNames[ecs::SkillIndex(kind)];
    switch (kind) {
        case ecs::SkillKind::Melee:
            skill.description = "近战伤害";
            skill.xpBase = 80.0f;
            skill.xpGrowth = 1.4f;
            skill.damagePerLevel = 0.03f;
            break;
        case ecs::SkillKind::Building:
            skill.description = "结构维修";
            skill.xpBase = 60.0f;
            skill.xpGrowth = 1.4f;
            skill.repairPerLevel = 0.08f;
            break;
        case ecs::SkillKind::Crafting:
            skill.description = "制作速度";
            skill.xpBase = 60.0f;
            skill.xpGrowth = 1.4f;
            skill.craftSpeedPerLevel = 0.03f;
            break;
        case ecs::SkillKind::Survival:
            skill.description = "生存与搜刮";
            skill.xpBase = 100.0f;
            skill.xpGrowth = 1.4f;
            skill.carryKgPerLevel = 0.5f;
            skill.lootXpPerLevel = 0.03f;
            skill.needsDecayPerLevel = 0.02f;
            break;
        case ecs::SkillKind::Count:
            break;
    }
    return skill;
}

PerkRules MakePerk(std::string id, std::string name, std::string description) {
    PerkRules perk;
    perk.id = std::move(id);
    perk.name = std::move(name);
    perk.description = std::move(description);
    return perk;
}

float PerkSum(const ecs::Progression& progression,
              const ProgressionConfig& config,
              float PerkRules::*member) {
    float sum = 0.0f;
    for (const std::string& id : progression.perks) {
        const PerkRules* perk = FindPerk(config, id);
        if (perk != nullptr) {
            sum += perk->*member;
        }
    }
    return sum;
}

float TotalCharacterXpBonus(const ecs::Progression& progression,
                            const ProgressionConfig& config) {
    return std::max(0.0f, PerkSum(progression, config, &PerkRules::characterXpBonus));
}

float TotalSkillXpBonus(const ecs::Progression& progression,
                        const ProgressionConfig& config) {
    return std::max(0.0f, PerkSum(progression, config, &PerkRules::skillXpBonus));
}

float PerkLootXpBonus(const ecs::Progression& progression,
                      const ProgressionConfig& config) {
    return std::max(0.0f, PerkSum(progression, config, &PerkRules::lootXpBonus));
}

float Number(const nlohmann::json& object, const char* key, float fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<float>();
}

int Integer(const nlohmann::json& object, const char* key, int fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<int>();
}

std::string Text(const nlohmann::json& object, const char* key,
                 std::string fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

std::uint32_t StableHash(std::string_view text) {
    std::uint32_t hash = 2166136261u;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

std::string Percent(float value) {
    return std::to_string(static_cast<int>(std::lround(value * 100.0f))) + "%";
}

}  // namespace

ProgressionConfig DefaultProgressionConfig() {
    ProgressionConfig config;
    for (std::size_t i = 0; i < ecs::kSkillCount; ++i) {
        config.skills[i] = MakeSkill(static_cast<ecs::SkillKind>(i));
    }

    PerkRules bruiser = MakePerk("bruiser", "猛击", "近战伤害 +15%");
    bruiser.meleeDamageBonus = 0.15f;
    config.perks.push_back(std::move(bruiser));

    PerkRules brawler = MakePerk("brawler", "连击", "近战攻击冷却 -10%");
    brawler.meleeCooldownScale = 0.90f;
    config.perks.push_back(std::move(brawler));

    PerkRules fleet = MakePerk("fleet", "疾行", "移动速度 +8%");
    fleet.moveSpeedBonus = 0.08f;
    config.perks.push_back(std::move(fleet));

    PerkRules packMule = MakePerk("pack_mule", "负重者", "背包容量 +5kg");
    packMule.carryKg = 5.0f;
    config.perks.push_back(std::move(packMule));

    PerkRules quickHands = MakePerk("quick_hands", "巧手", "制作速度 +15%");
    quickHands.craftSpeedBonus = 0.15f;
    config.perks.push_back(std::move(quickHands));

    PerkRules builder = MakePerk("builder", "工匠", "结构维修量 +25%");
    builder.repairBonus = 0.25f;
    config.perks.push_back(std::move(builder));

    PerkRules survivor = MakePerk("survivor", "生存者", "饥饿/口渴/疲劳增长 -10%");
    survivor.hungerDecayBonus = -0.10f;
    survivor.thirstDecayBonus = -0.10f;
    survivor.fatigueDecayBonus = -0.10f;
    config.perks.push_back(std::move(survivor));

    PerkRules ironStomach = MakePerk("iron_stomach", "铁胃", "饥饿增长 -15%");
    ironStomach.hungerDecayBonus = -0.15f;
    config.perks.push_back(std::move(ironStomach));

    PerkRules lightStep = MakePerk("light_step", "轻步", "疲劳增长 -15%");
    lightStep.fatigueDecayBonus = -0.15f;
    config.perks.push_back(std::move(lightStep));

    PerkRules scavenger = MakePerk("scavenger", "搜刮专家", "搜刮经验 +50%");
    scavenger.lootXpBonus = 0.50f;
    config.perks.push_back(std::move(scavenger));

    PerkRules veteran = MakePerk("veteran", "老手", "角色经验 +10%");
    veteran.characterXpBonus = 0.10f;
    config.perks.push_back(std::move(veteran));

    PerkRules learner = MakePerk("learner", "好学", "技能经验 +15%");
    learner.skillXpBonus = 0.15f;
    config.perks.push_back(std::move(learner));
    return config;
}

ProgressionConfig LoadProgressionConfig(const core::Config& config) {
    ProgressionConfig result = DefaultProgressionConfig();
    const auto& root = config.Raw();
    const auto section = root.find("progression");
    if (section == root.end() || !section->is_object()) {
        return result;
    }

    result.enabled = section->value("enabled", result.enabled);
    result.maxLevel =
        ClampInt(Integer(*section, "maxLevel", result.maxLevel), 2, 50);
    result.maxSkillLevel =
        ClampInt(Integer(*section, "maxSkillLevel", result.maxSkillLevel), 1, 50);
    result.xpBase = ClampFinite(Number(*section, "xpBase", result.xpBase), 1.0f,
                                kMaxThreshold, result.xpBase);
    result.xpGrowth =
        ClampFinite(Number(*section, "xpGrowth", result.xpGrowth), 1.01f, 10.0f,
                    result.xpGrowth);
    result.perkChoices =
        ClampInt(Integer(*section, "perkChoices", result.perkChoices), 2, 3);
    result.meleeXpPerHit =
        ClampFinite(Number(*section, "meleeXpPerHit", result.meleeXpPerHit), 0.0f,
                    10000.0f, result.meleeXpPerHit);
    result.meleeXpPerKill =
        ClampFinite(Number(*section, "meleeXpPerKill", result.meleeXpPerKill), 0.0f,
                    100000.0f, result.meleeXpPerKill);
    result.craftXpPerItem =
        ClampFinite(Number(*section, "craftXpPerItem", result.craftXpPerItem), 0.0f,
                    10000.0f, result.craftXpPerItem);
    result.buildXpPerStructure = ClampFinite(
        Number(*section, "buildXpPerStructure", result.buildXpPerStructure), 0.0f,
        10000.0f, result.buildXpPerStructure);
    result.lootXpPerContainer = ClampFinite(
        Number(*section, "lootXpPerContainer", result.lootXpPerContainer), 0.0f,
        10000.0f, result.lootXpPerContainer);
    result.survivalXpPerHour = ClampFinite(
        Number(*section, "survivalXpPerHour", result.survivalXpPerHour), 0.0f,
        10000.0f, result.survivalXpPerHour);

    const auto skills = section->find("skills");
    if (skills != section->end() && skills->is_array()) {
        for (const auto& item : *skills) {
            if (!item.is_object()) {
                continue;
            }
            const std::string id = Text(item, "id", {});
            const auto found = std::find_if(
                result.skills.begin(), result.skills.end(),
                [&id](const SkillRules& skill) { return skill.id == id; });
            if (found == result.skills.end()) {
                ZL_WARN(kLogModule, "忽略未知技能定义：{}", id);
                continue;
            }
            found->name = Text(item, "name", found->name);
            found->description = Text(item, "description", found->description);
            found->xpBase = ClampFinite(Number(item, "xpBase", found->xpBase), 1.0f,
                                        kMaxThreshold, found->xpBase);
            found->xpGrowth =
                ClampFinite(Number(item, "xpGrowth", found->xpGrowth), 1.01f, 10.0f,
                            found->xpGrowth);
            found->damagePerLevel =
                ClampFinite(Number(item, "damagePerLevel", found->damagePerLevel),
                            0.0f, 10.0f, found->damagePerLevel);
            found->repairPerLevel =
                ClampFinite(Number(item, "repairPerLevel", found->repairPerLevel),
                            0.0f, 10.0f, found->repairPerLevel);
            found->craftSpeedPerLevel = ClampFinite(
                Number(item, "craftSpeedPerLevel", found->craftSpeedPerLevel), 0.0f,
                10.0f, found->craftSpeedPerLevel);
            found->carryKgPerLevel = ClampFinite(
                Number(item, "carryKgPerLevel", found->carryKgPerLevel), 0.0f, 100.0f,
                found->carryKgPerLevel);
            found->lootXpPerLevel = ClampFinite(
                Number(item, "lootXpPerLevel", found->lootXpPerLevel), 0.0f, 10.0f,
                found->lootXpPerLevel);
            found->needsDecayPerLevel = ClampFinite(
                Number(item, "needsDecayPerLevel", found->needsDecayPerLevel), 0.0f,
                0.5f, found->needsDecayPerLevel);
        }
    }

    const auto perks = section->find("perks");
    if (perks != section->end() && perks->is_array()) {
        std::vector<PerkRules> parsed;
        std::unordered_set<std::string> seen;
        for (const auto& item : *perks) {
            if (!item.is_object()) {
                continue;
            }
            PerkRules perk;
            perk.id = Text(item, "id", {});
            perk.name = Text(item, "name", {});
            perk.description = Text(item, "description", {});
            if (!ValidPerkId(perk.id) || perk.name.empty() ||
                !seen.insert(perk.id).second) {
                ZL_WARN(kLogModule, "忽略非法/重复 Perk：{}", perk.id);
                continue;
            }
            if (perk.description.empty()) {
                perk.description = perk.name;
            }
            perk.meleeDamageBonus =
                ClampFinite(Number(item, "meleeDamageBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            perk.meleeCooldownScale =
                ClampFinite(Number(item, "meleeCooldownScale", 1.0f), 0.1f, 2.0f, 1.0f);
            perk.moveSpeedBonus =
                ClampFinite(Number(item, "moveSpeedBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            perk.carryKg =
                ClampFinite(Number(item, "carryKg", 0.0f), -100.0f, 100.0f, 0.0f);
            perk.craftSpeedBonus =
                ClampFinite(Number(item, "craftSpeedBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            perk.repairBonus =
                ClampFinite(Number(item, "repairBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            perk.hungerDecayBonus = ClampFinite(
                Number(item, "hungerDecayBonus", 0.0f), -1.0f, 1.0f, 0.0f);
            perk.thirstDecayBonus = ClampFinite(
                Number(item, "thirstDecayBonus", 0.0f), -1.0f, 1.0f, 0.0f);
            perk.fatigueDecayBonus = ClampFinite(
                Number(item, "fatigueDecayBonus", 0.0f), -1.0f, 1.0f, 0.0f);
            perk.lootXpBonus =
                ClampFinite(Number(item, "lootXpBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            perk.characterXpBonus = ClampFinite(
                Number(item, "characterXpBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            perk.skillXpBonus =
                ClampFinite(Number(item, "skillXpBonus", 0.0f), -1.0f, 10.0f, 0.0f);
            parsed.push_back(std::move(perk));
        }
        if (!parsed.empty()) {
            result.perks = std::move(parsed);
        } else {
            ZL_WARN(kLogModule, "progression.perks 为空或全部非法，使用内置 Perk");
        }
    }
    return result;
}

float LevelThreshold(float base, float growth, int level) {
    if (!(base > 0.0f) || !(growth > 1.0f)) {
        return 0.0f;
    }
    const int offset = std::max(0, level - 1);
    const float value = base * std::pow(growth, static_cast<float>(offset));
    if (!std::isfinite(value)) {
        return kMaxThreshold;
    }
    return std::clamp(value, 0.0f, kMaxThreshold);
}

float CharacterXpToNext(const ecs::Progression& progression,
                        const ProgressionConfig& config) {
    if (progression.level >= config.maxLevel) {
        return 0.0f;
    }
    return LevelThreshold(config.xpBase, config.xpGrowth, progression.level);
}

float SkillXpToNext(const SkillRules& skill, int level,
                    const ProgressionConfig& config) {
    if (level >= config.maxSkillLevel) {
        return 0.0f;
    }
    return LevelThreshold(skill.xpBase, skill.xpGrowth, level + 1);
}

float SkillXpToNext(const ecs::Progression& progression, ecs::SkillKind kind,
                    const ProgressionConfig& config) {
    const std::size_t index = ecs::SkillIndex(kind);
    const SkillRules* skill = FindSkill(config, kind);
    if (skill == nullptr || index >= progression.skills.size()) {
        return 0.0f;
    }
    return SkillXpToNext(*skill, progression.skills[index].level, config);
}

ProgressionAward AwardXp(ecs::Progression& progression, ecs::SkillKind kind,
                         float baseAmount, const ProgressionConfig& config) {
    ProgressionAward award;
    if (!config.enabled || !Finite(baseAmount) || !(baseAmount > 0.0f)) {
        return award;
    }

    const float characterXp =
        baseAmount * (1.0f + TotalCharacterXpBonus(progression, config));
    const float skillXp = baseAmount * (1.0f + TotalSkillXpBonus(progression, config));
    award.characterXp = characterXp;
    award.skillXp = skillXp;

    progression.xp += characterXp;
    while (progression.level < config.maxLevel) {
        const float threshold = CharacterXpToNext(progression, config);
        if (!(threshold > 0.0f) || progression.xp < threshold) {
            break;
        }
        progression.xp -= threshold;
        ++progression.level;
        ++award.characterLevels;
        ++award.perkPoints;
    }
    if (progression.level >= config.maxLevel) {
        const float threshold = LevelThreshold(config.xpBase, config.xpGrowth,
                                              config.maxLevel);
        progression.xp = std::min(progression.xp, std::max(0.0f, threshold));
    }
    if (!Finite(progression.xp) || progression.xp < 0.0f) {
        progression.xp = 0.0f;
    }
    progression.perkPoints =
        std::max(0, progression.perkPoints + award.perkPoints);

    const std::size_t skillIndex = ecs::SkillIndex(kind);
    if (skillIndex < progression.skills.size()) {
        ecs::SkillProgress& skill = progression.skills[skillIndex];
        const SkillRules* rules = FindSkill(config, kind);
        if (rules != nullptr) {
            skill.xp += skillXp;
            while (skill.level < config.maxSkillLevel) {
                const float threshold = SkillXpToNext(*rules, skill.level, config);
                if (!(threshold > 0.0f) || skill.xp < threshold) {
                    break;
                }
                skill.xp -= threshold;
                ++skill.level;
                ++award.skillLevels;
            }
            if (skill.level >= config.maxSkillLevel) {
                const float threshold =
                    LevelThreshold(rules->xpBase, rules->xpGrowth, config.maxSkillLevel);
                skill.xp = std::min(skill.xp, std::max(0.0f, threshold));
            }
            if (!Finite(skill.xp) || skill.xp < 0.0f) {
                skill.xp = 0.0f;
            }
        }
    }

    RefreshDerived(progression, config);
    return award;
}

void RefreshDerived(ecs::Progression& progression,
                    const ProgressionConfig& config) {
    const SkillRules* survival = FindSkill(config, ecs::SkillKind::Survival);
    const int survivalLevel =
        ecs::SkillLevel(progression, ecs::SkillKind::Survival);
    const float survivalDecay =
        survival != nullptr ? survival->needsDecayPerLevel : 0.0f;
    const float baseScale = std::max(0.0f, 1.0f - survivalDecay * survivalLevel);
    const float hungerBonus = PerkSum(progression, config, &PerkRules::hungerDecayBonus);
    const float thirstBonus = PerkSum(progression, config, &PerkRules::thirstDecayBonus);
    const float fatigueBonus =
        PerkSum(progression, config, &PerkRules::fatigueDecayBonus);
    progression.hungerDecayScale =
        std::clamp(baseScale * (1.0f + hungerBonus), 0.2f, 1.0f);
    progression.thirstDecayScale =
        std::clamp(baseScale * (1.0f + thirstBonus), 0.2f, 1.0f);
    progression.fatigueDecayScale =
        std::clamp(baseScale * (1.0f + fatigueBonus), 0.2f, 1.0f);
    progression.carryCapacityBonusKg =
        std::max(0.0f, (survival != nullptr ? survival->carryKgPerLevel : 0.0f) *
                              static_cast<float>(survivalLevel) +
                          PerkSum(progression, config, &PerkRules::carryKg));
}

std::array<std::string, 3> PerkChoiceIds(const ecs::Progression& progression,
                                         const ProgressionConfig& config) {
    std::array<std::string, 3> result{};
    if (!config.enabled || progression.perkPoints <= 0) {
        return result;
    }

    struct Candidate {
        std::string id;
        std::uint32_t hash = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(config.perks.size());
    const std::uint32_t seed =
        StableHash(std::to_string(progression.level) + ":" +
                   std::to_string(progression.perkPoints) + ":" +
                   std::to_string(progression.perks.size()));
    for (const PerkRules& perk : config.perks) {
        if (HasPerk(progression, perk.id)) {
            continue;
        }
        candidates.push_back(Candidate{perk.id, StableHash(perk.id) ^ seed});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.hash == b.hash ? a.id < b.id : a.hash < b.hash;
              });
    const int count = std::min<int>(
        config.perkChoices, static_cast<int>(candidates.size()));
    for (int i = 0; i < count; ++i) {
        result[static_cast<std::size_t>(i)] = candidates[static_cast<std::size_t>(i)].id;
    }
    return result;
}

int PerkChoiceCount(const std::array<std::string, 3>& choices) {
    int count = 0;
    for (const std::string& id : choices) {
        if (!id.empty()) {
            ++count;
        }
    }
    return count;
}

bool ChoosePerkById(ecs::Progression& progression, std::string_view perkId,
                    const ProgressionConfig& config) {
    if (!config.enabled || progression.perkPoints <= 0 ||
        !ValidPerkId(perkId) || HasPerk(progression, perkId)) {
        return false;
    }
    const PerkRules* perk = FindPerk(config, perkId);
    if (perk == nullptr) {
        return false;
    }
    progression.perks.push_back(std::string(perk->id));
    --progression.perkPoints;
    RefreshDerived(progression, config);
    return true;
}

bool ChoosePerkByIndex(ecs::Progression& progression, int index,
                       const ProgressionConfig& config) {
    if (index < 0 || index >= 3) {
        return false;
    }
    const std::array<std::string, 3> choices = PerkChoiceIds(progression, config);
    return !choices[static_cast<std::size_t>(index)].empty() &&
           ChoosePerkById(progression, choices[static_cast<std::size_t>(index)], config);
}

const PerkRules* FindPerk(const ProgressionConfig& config, std::string_view id) {
    for (const PerkRules& perk : config.perks) {
        if (perk.id == id) {
            return &perk;
        }
    }
    return nullptr;
}

const SkillRules* FindSkill(const ProgressionConfig& config, ecs::SkillKind kind) {
    const std::size_t index = ecs::SkillIndex(kind);
    return index < config.skills.size() ? &config.skills[index] : nullptr;
}

std::string SkillEffectSummary(const ProgressionConfig& config,
                               ecs::SkillKind kind) {
    const SkillRules* skill = FindSkill(config, kind);
    if (skill == nullptr) {
        return {};
    }
    switch (kind) {
        case ecs::SkillKind::Melee:
            return "伤害 +" + Percent(skill->damagePerLevel) + "/级";
        case ecs::SkillKind::Building:
            return "维修 +" + Percent(skill->repairPerLevel) + "/级";
        case ecs::SkillKind::Crafting:
            return "速度 +" + Percent(skill->craftSpeedPerLevel) + "/级";
        case ecs::SkillKind::Survival: {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer),
                          "负重 +%.1fkg/级 · 搜刮 +%s/级 · 需求 -%s/级",
                          static_cast<double>(skill->carryKgPerLevel),
                          Percent(skill->lootXpPerLevel).c_str(),
                          Percent(skill->needsDecayPerLevel).c_str());
            return buffer;
        }
        case ecs::SkillKind::Count:
            break;
    }
    return {};
}

std::string ProgressionSkillLine(const ecs::Progression& progression,
                                 const ProgressionConfig& config,
                                 ecs::SkillKind kind) {
    const SkillRules* skill = FindSkill(config, kind);
    const std::size_t index = ecs::SkillIndex(kind);
    if (skill == nullptr || index >= progression.skills.size()) {
        return {};
    }
    const ecs::SkillProgress& progress = progression.skills[index];
    std::string text = skill->name + " Lv." + std::to_string(progress.level);
    const std::string effect = SkillEffectSummary(config, kind);
    if (!effect.empty()) {
        text += " · " + effect;
    }
    const float next = SkillXpToNext(progression, kind, config);
    if (next > 0.0f) {
        text += "  " +
                std::to_string(static_cast<int>(std::lround(progress.xp))) + "/" +
                std::to_string(static_cast<int>(std::lround(next))) + " XP";
    } else {
        text += "  MAX";
    }
    return text;
}

std::string ProgressionPerkSummary(const ecs::Progression& progression,
                                   const ProgressionConfig& config) {
    if (progression.perks.empty()) {
        return "暂无";
    }
    std::string text;
    for (const std::string& id : progression.perks) {
        const PerkRules* perk = FindPerk(config, id);
        if (!text.empty()) {
            text += "、";
        }
        text += perk != nullptr ? perk->name : id;
    }
    return text;
}

std::string PerkChoiceLine(const PerkRules& perk, int index) {
    return std::to_string(index + 1) + ". " + perk.name + " — " + perk.description;
}

float MeleeDamageMultiplier(const ecs::Progression& progression,
                            const ProgressionConfig& config) {
    const SkillRules* skill = FindSkill(config, ecs::SkillKind::Melee);
    const float skillBonus =
        skill != nullptr
            ? skill->damagePerLevel *
                  static_cast<float>(
                      ecs::SkillLevel(progression, ecs::SkillKind::Melee))
            : 0.0f;
    return std::max(
        0.1f, 1.0f + skillBonus + PerkSum(progression, config,
                                          &PerkRules::meleeDamageBonus));
}

float MeleeCooldownMultiplier(const ecs::Progression& progression,
                              const ProgressionConfig& config) {
    float scale = 1.0f;
    for (const std::string& id : progression.perks) {
        const PerkRules* perk = FindPerk(config, id);
        if (perk != nullptr) {
            scale *= std::clamp(perk->meleeCooldownScale, 0.1f, 2.0f);
        }
    }
    return std::clamp(scale, 0.2f, 2.0f);
}

float MoveSpeedMultiplier(const ecs::Progression& progression,
                          const ProgressionConfig& config) {
    return std::clamp(
        1.0f + std::max(0.0f, PerkSum(progression, config,
                                      &PerkRules::moveSpeedBonus)),
        0.5f, 2.0f);
}

float CarryCapacityBonusKg(const ecs::Progression& progression,
                           const ProgressionConfig& config) {
    const SkillRules* survival = FindSkill(config, ecs::SkillKind::Survival);
    return std::max(
        0.0f,
        (survival != nullptr ? survival->carryKgPerLevel : 0.0f) *
                static_cast<float>(
                    ecs::SkillLevel(progression, ecs::SkillKind::Survival)) +
            PerkSum(progression, config, &PerkRules::carryKg));
}

float CraftSpeedMultiplier(const ecs::Progression& progression,
                           const ProgressionConfig& config) {
    const SkillRules* skill = FindSkill(config, ecs::SkillKind::Crafting);
    const float skillBonus =
        skill != nullptr
            ? skill->craftSpeedPerLevel *
                  static_cast<float>(
                      ecs::SkillLevel(progression, ecs::SkillKind::Crafting))
            : 0.0f;
    return std::max(
        0.1f, 1.0f + skillBonus +
                  std::max(0.0f, PerkSum(progression, config,
                                         &PerkRules::craftSpeedBonus)));
}

float CraftDurationScale(const ecs::Progression& progression,
                         const ProgressionConfig& config) {
    return 1.0f / CraftSpeedMultiplier(progression, config);
}

float RepairAmountMultiplier(const ecs::Progression& progression,
                             const ProgressionConfig& config) {
    const SkillRules* skill = FindSkill(config, ecs::SkillKind::Building);
    const float skillBonus =
        skill != nullptr
            ? skill->repairPerLevel *
                  static_cast<float>(
                      ecs::SkillLevel(progression, ecs::SkillKind::Building))
            : 0.0f;
    return std::clamp(
        0.1f, 1.0f + skillBonus +
                  std::max(0.0f, PerkSum(progression, config,
                                         &PerkRules::repairBonus)),
        10.0f);
}

float LootXpMultiplier(const ecs::Progression& progression,
                       const ProgressionConfig& config) {
    const SkillRules* skill = FindSkill(config, ecs::SkillKind::Survival);
    const float levelBonus =
        skill != nullptr
            ? skill->lootXpPerLevel *
                  static_cast<float>(
                      ecs::SkillLevel(progression, ecs::SkillKind::Survival))
            : 0.0f;
    return std::max(0.1f, 1.0f + levelBonus + PerkLootXpBonus(progression, config));
}

int AwardCombatHits(ecs::EcsWorld& world,
                    const characters::CombatSystem& combat,
                    const ProgressionConfig& config,
                    std::vector<ecs::Entity>* changedPlayers) {
    if (!config.enabled) {
        return 0;
    }
    entt::registry& registry = world.Registry();
    int awarded = 0;
    for (const characters::CombatSystem::HitInfo& hit : combat.GetHits()) {
        if (!world.Alive(hit.attacker) ||
            !registry.all_of<ecs::PlayerTag, ecs::Progression>(hit.attacker)) {
            continue;
        }
        ecs::Progression& progression =
            registry.get<ecs::Progression>(hit.attacker);
        const float amount =
            config.meleeXpPerHit + (hit.lethal ? config.meleeXpPerKill : 0.0f);
        const ProgressionAward result = AwardXp(
            progression, ecs::SkillKind::Melee, amount, config);
        if (result.characterXp > 0.0f) {
            ++awarded;
            if (changedPlayers != nullptr) {
                changedPlayers->push_back(hit.attacker);
            }
        }
    }
    return awarded;
}

}  // namespace zl::game
