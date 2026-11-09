#pragma once

#include "items/ItemComponents.h"
#include "items/ItemRegistry.h"

namespace zl::items {

inline constexpr float kSpoiledThreshold = 0.05f;

bool IsPerishable(const ItemDef& def);
bool IsSpoiled(const ecs::ItemInstance& item, const ItemDef& def);

void AdvanceSpoilage(ecs::Container& container, const ItemRegistry& registry,
                     float gameMinutes, float scale = 1.0f);

}  // namespace zl::items
