#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zl::ecs {

// M19-F1：角色成长（经验/等级/技能/Perk）。
// 组件只存持久化数据；hungerDecayScale 等派生字段每次读档/升级后由
// game::ProgressionRules 重新计算，不进入存档，避免配置调整后旧档锁死。
enum class SkillKind : std::uint8_t {
    Melee = 0,
    Building = 1,
    Crafting = 2,
    Survival = 3,
    Count = 4,
};

inline constexpr std::size_t kSkillCount = 4;

inline constexpr const char* kSkillIds[kSkillCount] = {
    "melee", "building", "crafting", "survival"};
inline constexpr const char* kSkillNames[kSkillCount] = {
    "近战", "建造", "制作", "生存"};

inline std::size_t SkillIndex(SkillKind kind) {
    return static_cast<std::size_t>(kind);
}

struct SkillProgress {
    int level = 0;
    float xp = 0.0f;
};

struct Progression {
    int level = 1;
    float xp = 0.0f;
    int perkPoints = 0;
    std::array<SkillProgress, kSkillCount> skills{};
    std::vector<std::string> perks;

    // 派生运行时字段：NeedsSystem / 背包容量直接读取，不写入存档。
    float hungerDecayScale = 1.0f;
    float thirstDecayScale = 1.0f;
    float fatigueDecayScale = 1.0f;
    float carryCapacityBonusKg = 0.0f;
};

inline bool HasPerk(const Progression& progression, std::string_view perkId) {
    for (const std::string& owned : progression.perks) {
        if (owned == perkId) {
            return true;
        }
    }
    return false;
}

inline int SkillLevel(const Progression& progression, SkillKind kind) {
    const std::size_t index = SkillIndex(kind);
    return index < progression.skills.size() ? progression.skills[index].level : 0;
}

}  // namespace zl::ecs
