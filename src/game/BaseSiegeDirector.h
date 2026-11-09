#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace zl::game {

struct BaseSiegeStructure {
    std::uint64_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float hp = 0.0f;
    float maxHp = 0.0f;
    bool door = false;
    std::string name;
};

struct BaseSiegeAttackPlan {
    int primaryDirection = -1;
    int secondaryDirection = -1;
    int currentDirection = -1;
    bool hasTarget = false;
    std::uint64_t targetId = 0;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetHp = 0.0f;
    float targetMaxHp = 0.0f;
    std::string targetName;
};

struct BaseSiegePlanInput {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float triggerRadius = 12.0f;
    int maxAxes = 2;
    int lastDirection = -1;
    float weakestWeight = 0.7f;
    std::uint64_t fallbackSeed = 1;
};

struct BaseSiegeResult {
    char rating = '-';
    bool wavesStarted = false;
    bool hadTarget = false;
    bool breached = false;
    bool baseLost = false;
    int kills = 0;
    int wavesCleared = 0;
    int waveCount = 0;
    int direction = -1;
    int breachDirection = -1;
    int initialStructures = 0;
    int currentStructures = 0;
    float survivingRatio = 0.0f;
    float breachX = 0.0f;
    float breachY = 0.0f;
    std::string rewardText;
};

struct BaseSiegeReward {
    std::string itemId;
    int count = 0;
};

inline constexpr int kBaseSiegeDirectionCount = 8;

inline const char* BaseSiegeDirectionName(int direction) {
    static constexpr std::array<const char*, kBaseSiegeDirectionCount> kNames{
        "东", "东北", "北", "西北", "西", "西南", "南", "东南"};
    return direction >= 0 && direction < kBaseSiegeDirectionCount
               ? kNames[static_cast<std::size_t>(direction)]
               : "未知";
}

inline const char* BaseSiegeDirectionSide(int direction) {
    static constexpr std::array<const char*, kBaseSiegeDirectionCount> kSides{
        "东侧", "东北侧", "北侧", "西北侧",
        "西侧", "西南侧", "南侧", "东南侧"};
    return direction >= 0 && direction < kBaseSiegeDirectionCount
               ? kSides[static_cast<std::size_t>(direction)]
               : "";
}

inline int BaseSiegeDirectionFromDelta(float dx, float dy) {
    if (std::fabs(dx) < 0.0001f && std::fabs(dy) < 0.0001f) {
        return -1;
    }
    float angle = std::atan2(-dy, dx);
    if (angle < 0.0f) {
        angle += 6.28318530718f;
    }
    const int index = static_cast<int>(
        std::lround(angle / 0.785398163397f));
    return index % kBaseSiegeDirectionCount;
}

inline float BaseSiegeDirectionAngle(int direction) {
    if (direction < 0 || direction >= kBaseSiegeDirectionCount) {
        return 0.0f;
    }
    return static_cast<float>(direction) * 0.785398163397f;
}

// 单机/服务器共用的破口接近点：从目标墙/门向外侧找最近的可行走格，
// 供 Breaker 走位，避免直接寻路到不可站立的墙格。找不到时返回 false，
// 调用方应退回追逐玩家，而不是让事件僵尸停在原地。
inline bool FindBaseSiegeApproachPoint(
    const BaseSiegeAttackPlan& plan, float centerX, float centerY,
    const std::function<bool(int, int)>& isWalkable, float* x, float* y) {
    if (!plan.hasTarget || !isWalkable || x == nullptr || y == nullptr) {
        return false;
    }
    float outwardX = plan.targetX - centerX;
    float outwardY = plan.targetY - centerY;
    const float length = std::sqrt(outwardX * outwardX + outwardY * outwardY);
    if (length <= 0.0001f) {
        return false;
    }
    outwardX /= length;
    outwardY /= length;

    bool found = false;
    float bestScore = -2.0f;
    for (int radius = 1; radius <= 2; ++radius) {
        for (int step = 0; step < 8; ++step) {
            const float angle = static_cast<float>(step) * 0.785398163397f;
            const int wx = static_cast<int>(
                std::floor(plan.targetX + std::cos(angle) * radius));
            const int wy = static_cast<int>(
                std::floor(plan.targetY + std::sin(angle) * radius));
            if ((wx == static_cast<int>(std::floor(plan.targetX)) &&
                 wy == static_cast<int>(std::floor(plan.targetY))) ||
                !isWalkable(wx, wy)) {
                continue;
            }
            const float cx = static_cast<float>(wx) + 0.5f;
            const float cy = static_cast<float>(wy) + 0.5f;
            const float dx = cx - plan.targetX;
            const float dy = cy - plan.targetY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= 0.0001f) {
                continue;
            }
            const float score =
                (dx * outwardX + dy * outwardY) / dist - dist * 0.01f;
            if (!found || score > bestScore) {
                found = true;
                bestScore = score;
                *x = cx;
                *y = cy;
            }
        }
    }
    return found;
}

inline float BaseSiegeStructureScore(const BaseSiegeStructure& structure,
                                     float centerX, float centerY,
                                     float triggerRadius, float weakestWeight) {
    const float ratio =
        structure.maxHp > 0.0f
            ? std::clamp(structure.hp / structure.maxHp, 0.0f, 1.0f)
            : 1.0f;
    const float radius = std::max(triggerRadius, 1.0f);
    const float distance =
        std::hypot(structure.x - centerX, structure.y - centerY) / radius;
    const float weight = std::clamp(weakestWeight, 0.0f, 1.0f);
    const float passageBonus = structure.door ? 0.05f : 0.0f;
    return ratio * weight + std::clamp(distance, 0.0f, 1.0f) * (1.0f - weight) -
           passageBonus;
}

inline bool BaseSiegeStructureUsable(const BaseSiegeStructure& structure) {
    return structure.maxHp > 0.0f && structure.hp > 0.0f;
}

inline std::optional<BaseSiegeStructure> FindBaseSiegeTargetInDirection(
    const std::vector<BaseSiegeStructure>& structures, float centerX,
    float centerY, int direction, float triggerRadius, float weakestWeight,
    std::uint64_t excludeId = 0) {
    if (direction < 0 || direction >= kBaseSiegeDirectionCount) {
        return std::nullopt;
    }
    std::optional<BaseSiegeStructure> best;
    float bestScore = 0.0f;
    for (const BaseSiegeStructure& structure : structures) {
        if (structure.id == excludeId || !BaseSiegeStructureUsable(structure)) {
            continue;
        }
        if (BaseSiegeDirectionFromDelta(structure.x - centerX,
                                        structure.y - centerY) != direction) {
            continue;
        }
        const float score = BaseSiegeStructureScore(
            structure, centerX, centerY, triggerRadius, weakestWeight);
        if (!best.has_value() || score < bestScore) {
            best = structure;
            bestScore = score;
        }
    }
    return best;
}

inline BaseSiegeAttackPlan MakeBaseSiegeAttackPlan(
    const std::vector<BaseSiegeStructure>& structures,
    const BaseSiegePlanInput& input) {
    BaseSiegeAttackPlan plan;
    const int axes = std::clamp(input.maxAxes, 1, 2);
    std::array<bool, kBaseSiegeDirectionCount> occupied{};
    std::array<std::optional<BaseSiegeStructure>, kBaseSiegeDirectionCount>
        best{};
    std::array<float, kBaseSiegeDirectionCount> scores{};
    for (std::size_t i = 0; i < scores.size(); ++i) {
        scores[i] = 2.0f;
    }
    for (const BaseSiegeStructure& structure : structures) {
        if (!BaseSiegeStructureUsable(structure)) {
            continue;
        }
        const int direction = BaseSiegeDirectionFromDelta(
            structure.x - input.centerX, structure.y - input.centerY);
        if (direction < 0) {
            continue;
        }
        const float score = BaseSiegeStructureScore(
            structure, input.centerX, input.centerY, input.triggerRadius,
            input.weakestWeight);
        const std::size_t index = static_cast<std::size_t>(direction);
        if (!occupied[index] || score < scores[index]) {
            occupied[index] = true;
            scores[index] = score;
            best[index] = structure;
        }
    }

    std::array<int, kBaseSiegeDirectionCount> ranked{};
    int rankedCount = 0;
    for (int direction = 0; direction < kBaseSiegeDirectionCount; ++direction) {
        if (occupied[static_cast<std::size_t>(direction)]) {
            ranked[static_cast<std::size_t>(rankedCount++)] = direction;
        }
    }
    for (int i = 0; i < rankedCount; ++i) {
        for (int j = i + 1; j < rankedCount; ++j) {
            const std::size_t a = static_cast<std::size_t>(ranked[i]);
            const std::size_t b = static_cast<std::size_t>(ranked[j]);
            if (scores[b] < scores[a]) {
                std::swap(ranked[i], ranked[j]);
            }
        }
    }

    if (rankedCount == 0) {
        plan.primaryDirection =
            static_cast<int>(input.fallbackSeed % kBaseSiegeDirectionCount);
        if (plan.primaryDirection == input.lastDirection) {
            plan.primaryDirection =
                (plan.primaryDirection + 3) % kBaseSiegeDirectionCount;
        }
        plan.secondaryDirection =
            axes >= 2 ? (plan.primaryDirection + 4) % kBaseSiegeDirectionCount
                      : -1;
        plan.currentDirection = plan.primaryDirection;
        return plan;
    }

    int primary = ranked[0];
    if (primary == input.lastDirection && rankedCount > 1) {
        primary = ranked[1];
    }
    plan.primaryDirection = primary;
    if (axes >= 2 && rankedCount > 1) {
        for (int i = 0; i < rankedCount; ++i) {
            if (ranked[i] != primary) {
                plan.secondaryDirection = ranked[i];
                break;
            }
        }
    }
    plan.currentDirection = primary;
    const std::size_t primaryIndex = static_cast<std::size_t>(primary);
    if (best[primaryIndex].has_value()) {
        const BaseSiegeStructure& target = *best[primaryIndex];
        plan.hasTarget = true;
        plan.targetId = target.id;
        plan.targetX = target.x;
        plan.targetY = target.y;
        plan.targetHp = target.hp;
        plan.targetMaxHp = target.maxHp;
        plan.targetName = target.name;
    }
    return plan;
}

inline char BaseSiegeRatingFor(const BaseSiegeResult& result) {
    if (!result.wavesStarted || result.baseLost || !result.hadTarget) {
        return 'C';
    }
    if (!result.breached && result.survivingRatio >= 0.9f &&
        result.waveCount > 0 && result.wavesCleared >= result.waveCount) {
        return 'S';
    }
    if (!result.breached && result.survivingRatio >= 0.7f) {
        return 'A';
    }
    if (result.breached && result.survivingRatio > 0.0f) {
        return 'B';
    }
    return result.survivingRatio >= 0.7f ? 'B' : 'C';
}

inline std::vector<BaseSiegeReward> BaseSiegeRewardsFor(
    char rating, std::string woodItemId = "wood") {
    switch (rating) {
        case 'S':
            return {{"fuel_can", 1}, {"repair_kit", 1}, {"antibiotics", 1}};
        case 'A':
            return {{"fuel_can", 1}, {"repair_kit", 1}};
        case 'B':
            return {{std::move(woodItemId), 6}, {"bandage", 1}};
        case 'C':
            return {{std::move(woodItemId), 3}};
        default:
            return {};
    }
}

}  // namespace zl::game
