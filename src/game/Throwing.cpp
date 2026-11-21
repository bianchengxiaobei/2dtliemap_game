#include "game/Throwing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

#include "core/Config.h"
#include "core/Log.h"

namespace zl::game {
namespace {

constexpr float kWallProbeStep = 0.125f;
constexpr float kFlightBaseSeconds = 0.18f;
constexpr float kFlightSecondsPerCell = 0.07f;
constexpr float kFlightMaxSeconds = 1.2f;
constexpr float kDefaultGameMinutesPerRealSecond = 0.4f;
constexpr float kMaxThrowFireRadius = 3.0f;
constexpr float kMaxThrowFireSeconds = 1.0e7f;

float ValidOrDefault(float value, float fallback, float minimum, float maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        return fallback;
    }
    return value;
}

}  // namespace

ThrowingConfig DefaultThrowingConfig() {
    return {};
}

ThrowingConfig SanitizeThrowingConfig(const ThrowingConfig& config) {
    const ThrowingConfig defaults;
    ThrowingConfig out;
    out.cooldownSeconds =
        ValidOrDefault(config.cooldownSeconds, defaults.cooldownSeconds, 0.1f, 10.0f);
    out.maxRange =
        ValidOrDefault(config.maxRange, defaults.maxRange, 1.0f, 64.0f);
    out.minRange =
        ValidOrDefault(config.minRange, defaults.minRange, 0.0f, 16.0f);
    if (out.minRange >= out.maxRange) {
        out.minRange = defaults.minRange;
        out.maxRange = defaults.maxRange;
    }
    return out;
}

ThrowingConfig LoadThrowingConfig(const core::Config& config) {
    ThrowingConfig rules = DefaultThrowingConfig();
    const auto read = [&config](const char* key, float fallback) {
        return static_cast<float>(
            config.Get<double>(std::string("throwing.") + key, fallback));
    };
    rules.cooldownSeconds = read("cooldownSeconds", rules.cooldownSeconds);
    rules.maxRange = read("maxRange", rules.maxRange);
    rules.minRange = read("minRange", rules.minRange);

    const ThrowingConfig safe = SanitizeThrowingConfig(rules);
    if (safe.cooldownSeconds != rules.cooldownSeconds ||
        safe.maxRange != rules.maxRange ||
        safe.minRange != rules.minRange) {
        ZL_WARN("throwing", "throwing.* 存在非法数值，已回退默认值");
    }
    return safe;
}

ThrowAimSource ResolveThrowAimSource(bool primaryThrowable,
                                     bool hotbarThrowable) {
    if (primaryThrowable) {
        return ThrowAimSource::PrimaryHand;
    }
    if (hotbarThrowable) {
        return ThrowAimSource::Hotbar;
    }
    return ThrowAimSource::None;
}

ThrowAimAction ResolveThrowAimAction(bool primaryThrowable,
                                     bool hotbarThrowable, bool hasRanged) {
    if (ResolveThrowAimSource(primaryThrowable, hotbarThrowable) !=
        ThrowAimSource::None) {
        return ThrowAimAction::Throwable;
    }
    return hasRanged ? ThrowAimAction::Ranged : ThrowAimAction::None;
}

ThrowAimBlockReason ThrowAimBlockReasonFor(const ThrowAimRequest& request) {
    if (!request.hasThrowable) {
        return ThrowAimBlockReason::NoThrowable;
    }
    if (request.uiPointerOver) {
        return ThrowAimBlockReason::UiPointer;
    }
    if (request.buildMode) {
        return ThrowAimBlockReason::BuildMode;
    }
    if (request.inventoryOpen) {
        return ThrowAimBlockReason::InventoryOpen;
    }
    if (request.menuOpen) {
        return ThrowAimBlockReason::MenuOpen;
    }
    if (request.modalOpen) {
        return ThrowAimBlockReason::OtherUi;
    }
    if (request.dead) {
        return ThrowAimBlockReason::Dead;
    }
    if (request.inVehicle) {
        return ThrowAimBlockReason::InVehicle;
    }
    if (request.gathering) {
        return ThrowAimBlockReason::Gathering;
    }
    if (!request.targetValid) {
        return ThrowAimBlockReason::InvalidTarget;
    }
    return ThrowAimBlockReason::None;
}

ThrowAimEvaluation EvaluateThrowAim(const ThrowAimRequest& request) {
    ThrowAimEvaluation out;
    out.block = ThrowAimBlockReasonFor(request);
    if (out.block != ThrowAimBlockReason::None) {
        return out;
    }

    const ThrowingConfig config = SanitizeThrowingConfig(request.config);
    const float itemRange =
        std::isfinite(request.itemRange)
            ? std::clamp(request.itemRange, 1.0f, 64.0f)
            : config.maxRange;
    out.range = std::min(itemRange, config.maxRange);
    if (out.range < config.minRange) {
        out.range = config.minRange;
    }

    const float dx = request.targetX - request.playerX;
    const float dy = request.targetY - request.playerY;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length < config.minRange) {
        out.block = ThrowAimBlockReason::TooClose;
        return out;
    }
    out.directionX = dx / length;
    out.directionY = dy / length;
    out.targetX = request.targetX;
    out.targetY = request.targetY;
    out.distance = length;
    if (length > out.range) {
        out.clamped = true;
        out.distance = out.range;
        out.targetX = request.playerX + out.directionX * out.range;
        out.targetY = request.playerY + out.directionY * out.range;
    }
    out.valid = true;
    return out;
}

ThrowPathResult ClipThrowPath(float startX, float startY, float targetX,
                              float targetY, const ThrowCellBlocked& blocked) {
    ThrowPathResult out;
    if (!std::isfinite(startX) || !std::isfinite(startY) ||
        !std::isfinite(targetX) || !std::isfinite(targetY)) {
        return out;
    }
    const float dx = targetX - startX;
    const float dy = targetY - startY;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length <= 0.0001f) {
        return out;
    }

    out.valid = true;
    out.x = targetX;
    out.y = targetY;
    out.distance = length;
    if (!blocked) {
        return out;
    }

    const int startCellX = static_cast<int>(std::floor(startX));
    const int startCellY = static_cast<int>(std::floor(startY));
    const int steps =
        std::max(1, static_cast<int>(std::ceil(length / kWallProbeStep)));
    for (int i = 1; i <= steps; ++i) {
        const float t =
            static_cast<float>(i) / static_cast<float>(steps);
        const float px = startX + dx * t;
        const float py = startY + dy * t;
        const int wx = static_cast<int>(std::floor(px));
        const int wy = static_cast<int>(std::floor(py));
        if (wx == startCellX && wy == startCellY) {
            continue;
        }
        if (!blocked(wx, wy)) {
            continue;
        }
        const float safeT =
            static_cast<float>(i - 1) / static_cast<float>(steps);
        out.hitWall = true;
        out.distance = length * safeT;
        out.x = startX + dx * safeT;
        out.y = startY + dy * safeT;
        return out;
    }
    return out;
}

std::uint64_t SelectThrowTarget(
    const std::vector<ThrowTargetCandidate>& candidates, float x, float y,
    int z, float radius, std::uint64_t excludeId) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        radius < 0.0f) {
        return 0;
    }
    const float limit = radius * radius + 0.0001f;
    float bestDistanceSq = limit;
    std::uint64_t bestId = 0;
    for (const ThrowTargetCandidate& candidate : candidates) {
        if (candidate.stableId == 0 || candidate.stableId == excludeId ||
            candidate.z != z || !std::isfinite(candidate.x) ||
            !std::isfinite(candidate.y)) {
            continue;
        }
        const float dx = candidate.x - x;
        const float dy = candidate.y - y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq > limit) {
            continue;
        }
        if (bestId == 0 || distanceSq < bestDistanceSq - 0.000001f ||
            (std::abs(distanceSq - bestDistanceSq) <= 0.000001f &&
             candidate.stableId < bestId)) {
            bestDistanceSq = distanceSq;
            bestId = candidate.stableId;
        }
    }
    return bestId;
}

ThrowLandingOutcome ResolveThrowLanding(bool returnable, float breakChance,
                                        float roll) {
    if (!returnable) {
        return ThrowLandingOutcome::Consumed;
    }
    if (!std::isfinite(breakChance) || !std::isfinite(roll)) {
        return ThrowLandingOutcome::Broken;
    }
    if (breakChance <= 0.0f) {
        return ThrowLandingOutcome::Recovered;
    }
    if (breakChance >= 1.0f) {
        return ThrowLandingOutcome::Broken;
    }
    return roll < breakChance ? ThrowLandingOutcome::Broken
                              : ThrowLandingOutcome::Recovered;
}

float ThrowFlightSeconds(float distance) {
    const float safeDistance =
        std::isfinite(distance) ? std::max(0.0f, distance) : 0.0f;
    return std::clamp(kFlightBaseSeconds + safeDistance * kFlightSecondsPerCell,
                      kFlightBaseSeconds, kFlightMaxSeconds);
}

std::vector<ThrowFireCell> ThrowFireAreaCells(float x, float y, float radius) {
    std::vector<ThrowFireCell> cells;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius)) {
        return cells;
    }
    const int centerX = static_cast<int>(std::floor(x));
    const int centerY = static_cast<int>(std::floor(y));
    const float safeRadius =
        std::clamp(radius, 0.0f, kMaxThrowFireRadius);
    cells.push_back(ThrowFireCell{centerX, centerY});
    const int extent = static_cast<int>(std::ceil(safeRadius));
    for (int ring = 1; ring <= extent; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) {
                    continue;
                }
                const float distance =
                    std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (distance <= safeRadius + 0.0001f) {
                    cells.push_back(ThrowFireCell{centerX + dx, centerY + dy});
                }
            }
        }
    }
    return cells;
}

namespace {

std::string FormatThrowNumber(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f",
                  static_cast<double>(std::isfinite(value) ? value : 0.0f));
    std::string out = text;
    if (out.size() >= 2 && out.substr(out.size() - 2) == ".0") {
        out.resize(out.size() - 2);
    }
    return out;
}

int ThrowRecoveryPercent(float breakChance) {
    const float safe = std::isfinite(breakChance)
                           ? std::clamp(breakChance, 0.0f, 1.0f)
                           : 0.0f;
    return static_cast<int>(std::lround((1.0f - safe) * 100.0f));
}

}  // namespace

std::string ThrowableHotbarTag(const items::ThrowableDef& throwable) {
    const std::string range = FormatThrowNumber(throwable.range);
    if (throwable.fire.has_value()) {
        return "投" + range + " 火" +
               FormatThrowNumber(throwable.fire->radius);
    }
    if (throwable.returnable) {
        return "投" + range + " 回" +
               std::to_string(ThrowRecoveryPercent(throwable.breakChance)) + "%";
    }
    return "投" + range + " 消耗";
}

std::string ThrowableInventoryTag(const items::ThrowableDef& throwable) {
    const std::string range = FormatThrowNumber(throwable.range);
    if (throwable.fire.has_value()) {
        return "可投" + range + " 火" +
               FormatThrowNumber(throwable.fire->radius);
    }
    if (throwable.returnable) {
        return "可投" + range + " 回" +
               std::to_string(ThrowRecoveryPercent(throwable.breakChance)) + "%";
    }
    return "可投" + range + " 消耗";
}

float ThrowFireBurnSeconds(float minutes, float gameMinutesPerRealSecond) {
    if (!std::isfinite(minutes) || !(minutes > 0.0f)) {
        return 0.0f;
    }
    const float minutesPerSecond =
        std::isfinite(gameMinutesPerRealSecond) &&
                (gameMinutesPerRealSecond > 0.0f)
            ? gameMinutesPerRealSecond
            : kDefaultGameMinutesPerRealSecond;
    const float seconds = minutes / minutesPerSecond;
    return std::clamp(seconds, 0.0f, kMaxThrowFireSeconds);
}

bool ConsumeThrowableSource(ecs::Equipment* equipment, ecs::Container* container,
                            int sourceIndex, std::string_view expectedDefId,
                            ecs::ItemInstance* outItem) {
    if (expectedDefId.empty() || sourceIndex < -1) {
        return false;
    }
    ecs::ItemInstance removed;
    if (sourceIndex == -1) {
        if (equipment == nullptr) {
            return false;
        }
        auto& slot =
            equipment->slots[static_cast<std::size_t>(ecs::EquipSlot::PrimaryHand)];
        if (!slot.has_value() || slot->defId != expectedDefId ||
            slot->count <= 0) {
            return false;
        }
        removed = ecs::ItemInstance{slot->defId, 1, slot->condition,
                                    slot->loadedAmmo};
        if (slot->count > 1) {
            --slot->count;
        } else {
            slot.reset();
        }
    } else {
        if (container == nullptr ||
            static_cast<std::size_t>(sourceIndex) >= container->items.size()) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(sourceIndex);
        ecs::ItemInstance& entry = container->items[index];
        if (entry.defId != expectedDefId || entry.count <= 0) {
            return false;
        }
        removed = ecs::ItemInstance{entry.defId, 1, entry.condition,
                                    entry.loadedAmmo};
        if (entry.count > 1) {
            --entry.count;
        } else {
            container->items.erase(container->items.begin() +
                                   static_cast<std::ptrdiff_t>(index));
        }
    }
    if (outItem != nullptr) {
        *outItem = removed;
    }
    return true;
}

bool AdvanceThrownProjectile(ThrownProjectile& projectile, float dt) {
    if (projectile.landed || !std::isfinite(dt) || dt < 0.0f) {
        return false;
    }
    projectile.elapsed += dt;
    if (!std::isfinite(projectile.duration) || projectile.duration <= 0.0f ||
        projectile.elapsed >= projectile.duration) {
        projectile.elapsed = std::max(0.0f, projectile.duration);
        projectile.landed = true;
        return true;
    }
    return false;
}

bool ForceSettleThrownProjectile(ThrownProjectile& projectile) {
    if (projectile.landed) {
        return false;
    }
    projectile.elapsed = std::max(projectile.elapsed, projectile.duration);
    projectile.landed = true;
    return true;
}

}  // namespace zl::game
