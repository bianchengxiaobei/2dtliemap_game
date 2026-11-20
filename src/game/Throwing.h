#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "items/ItemComponents.h"
#include "items/ItemDef.h"

namespace zl::core {
class Config;
}

namespace zl::game {

struct ThrowingConfig {
    float cooldownSeconds = 0.7f;
    float maxRange = 12.0f;
    float minRange = 1.0f;
};

ThrowingConfig DefaultThrowingConfig();
ThrowingConfig SanitizeThrowingConfig(const ThrowingConfig& config);
ThrowingConfig LoadThrowingConfig(const core::Config& config);

enum class ThrowAimSource {
    None,
    PrimaryHand,
    Hotbar,
};

enum class ThrowAimAction {
    None,
    Throwable,
    Ranged,
};

enum class ThrowAimBlockReason {
    None,
    NoThrowable,
    UiPointer,
    BuildMode,
    InventoryOpen,
    MenuOpen,
    OtherUi,
    Dead,
    InVehicle,
    InvalidTarget,
    TooClose,
};

struct ThrowAimRequest {
    bool hasThrowable = false;
    bool uiPointerOver = false;
    bool buildMode = false;
    bool inventoryOpen = false;
    bool menuOpen = false;
    bool modalOpen = false;
    bool dead = false;
    bool inVehicle = false;
    bool targetValid = false;
    float playerX = 0.0f;
    float playerY = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float itemRange = 0.0f;
    ThrowingConfig config{};
};

struct ThrowAimEvaluation {
    bool valid = false;
    bool clamped = false;
    ThrowAimBlockReason block = ThrowAimBlockReason::None;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float directionX = 1.0f;
    float directionY = 0.0f;
    float distance = 0.0f;
    float range = 0.0f;
};

ThrowAimSource ResolveThrowAimSource(bool primaryThrowable,
                                     bool hotbarThrowable);
ThrowAimAction ResolveThrowAimAction(bool primaryThrowable,
                                     bool hotbarThrowable, bool hasRanged);
ThrowAimBlockReason ThrowAimBlockReasonFor(const ThrowAimRequest& request);
ThrowAimEvaluation EvaluateThrowAim(const ThrowAimRequest& request);

using ThrowCellBlocked = std::function<bool(int, int)>;

struct ThrowPathResult {
    bool valid = false;
    bool hitWall = false;
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
};

ThrowPathResult ClipThrowPath(float startX, float startY, float targetX,
                              float targetY, const ThrowCellBlocked& blocked);

struct ThrowTargetCandidate {
    std::uint64_t stableId = 0;
    float x = 0.0f;
    float y = 0.0f;
    int z = 0;
};

std::uint64_t SelectThrowTarget(const std::vector<ThrowTargetCandidate>& candidates,
                                float x, float y, int z, float radius,
                                std::uint64_t excludeId = 0);

enum class ThrowLandingOutcome {
    Consumed,
    Recovered,
    Broken,
};

ThrowLandingOutcome ResolveThrowLanding(bool returnable, float breakChance,
                                        float roll);
float ThrowFlightSeconds(float distance);

struct ThrowFireCell {
    int cellX = 0;
    int cellY = 0;
};

std::vector<ThrowFireCell> ThrowFireAreaCells(float x, float y, float radius);
float ThrowFireBurnSeconds(float minutes, float gameMinutesPerRealSecond);

// M34-F2-T6：物品/HUD 共用的紧凑投掷提示，避免三处各自格式化。
std::string ThrowableHotbarTag(const items::ThrowableDef& throwable);
std::string ThrowableInventoryTag(const items::ThrowableDef& throwable);

bool ConsumeThrowableSource(ecs::Equipment* equipment, ecs::Container* container,
                            int sourceIndex, std::string_view expectedDefId,
                            ecs::ItemInstance* outItem);

struct ThrownProjectile {
    std::uint64_t ownerStableId = 0;
    int sourceIndex = -1;
    std::string defId;
    std::string name;
    int z = 0;
    float startX = 0.0f;
    float startY = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float directionX = 1.0f;
    float directionY = 0.0f;
    float arcHeight = 0.0f;
    float elapsed = 0.0f;
    float duration = 0.3f;
    float damage = 0.0f;
    float impactRadius = 0.0f;
    float noiseRadius = 0.0f;
    float fireRadius = 0.0f;
    float fireMinutes = 0.0f;
    bool returnable = false;
    float breakChance = 0.0f;
    float itemCondition = 1.0f;
    bool wallClipped = false;
    bool landed = false;
    bool visualOnly = false;
};

bool AdvanceThrownProjectile(ThrownProjectile& projectile, float dt);
bool ForceSettleThrownProjectile(ThrownProjectile& projectile);

}  // namespace zl::game
