#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "ecs/EcsWorld.h"
#include "save/BinaryFormat.h"
#include "save/ComponentRegistry.h"

namespace zl::save {

inline constexpr std::uint16_t kEntityStoreVersion = 1;
inline constexpr std::size_t kMaxComponentBytes =
    kSegmentHeaderSize + kMaxSegmentPayloadBytes;

struct SavedComponent {
    std::uint32_t type = 0;
    std::uint16_t version = 0;
    std::vector<std::uint8_t> bytes;
};

struct SavedEntity {
    std::uint64_t stableId = 0;
    std::vector<SavedComponent> components;
};

struct EntitySnapshot {
    std::vector<SavedEntity> entities;
};

using EntityFilter = std::function<bool(ecs::Entity)>;

bool CaptureEntity(const ecs::EcsWorld& world, ecs::Entity entity,
                   const ComponentRegistry& registry, SavedEntity* out,
                   std::string* error = nullptr);

bool CaptureEntities(const ecs::EcsWorld& world, const ComponentRegistry& registry,
                     EntitySnapshot* out, EntityFilter filter = {},
                     std::string* error = nullptr);

bool CapturePlayer(const ecs::EcsWorld& world, const ComponentRegistry& registry,
                   SavedEntity* out, std::string* error = nullptr);

bool ValidateEntitySnapshot(const EntitySnapshot& snapshot,
                            std::string* error = nullptr);

bool ApplyEntity(const SavedEntity& saved, ecs::EcsWorld& world,
                 ecs::Entity entity, const ComponentRegistry& registry,
                 std::string* error = nullptr);

std::vector<std::uint8_t> EncodeEntitySnapshot(const EntitySnapshot& snapshot,
                                               std::string* error = nullptr);

bool DecodeEntitySnapshot(std::span<const std::uint8_t> bytes,
                          EntitySnapshot* out, std::string* error = nullptr);

bool LoadEntities(const EntitySnapshot& snapshot, ecs::EcsWorld& world,
                  const ComponentRegistry& registry,
                  std::vector<ecs::Entity>* loaded = nullptr,
                  std::string* error = nullptr);

}  // namespace zl::save
