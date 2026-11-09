#include "items/Spoilage.h"

#include <algorithm>

namespace zl::items {
namespace {

constexpr float kMinutesPerDay = 1440.0f;

}  // namespace

bool IsPerishable(const ItemDef& def) { return def.spoilDays > 0.0f; }

bool IsSpoiled(const ecs::ItemInstance& item, const ItemDef& def) {
    return IsPerishable(def) && item.condition <= kSpoiledThreshold;
}

void AdvanceSpoilage(ecs::Container& container, const ItemRegistry& registry,
                     float gameMinutes, float scale) {
    if (!(gameMinutes > 0.0f) || !(scale > 0.0f)) {
        return;
    }
    for (ecs::ItemInstance& item : container.items) {
        if (!item.Valid()) {
            continue;
        }
        const ItemDef* def = registry.Find(item.defId);
        if (def == nullptr || !IsPerishable(*def) || !(item.condition > 0.0f)) {
            continue;
        }
        const float life = std::max(def->spoilDays * kMinutesPerDay, 1.0f);
        item.condition =
            std::max(0.0f, item.condition - gameMinutes * scale / life);
    }
}

}  // namespace zl::items
