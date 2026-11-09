#include "save/EntityStore.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace zl::save {
namespace {

constexpr std::uint32_t kMinEntityBytes = 12;
constexpr std::uint32_t kMinComponentRecordBytes = 4;

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool ValidateComponentBytes(const SavedComponent& component,
                            std::string* error) {
    if (component.type == 0) {
        return Fail(error, "组件类型 ID 为 0");
    }
    if (component.version == 0) {
        return Fail(error, "组件版本为 0");
    }
    if (component.bytes.size() < kSegmentHeaderSize) {
        return Fail(error, "组件数据截断");
    }
    if (component.bytes.size() > kMaxComponentBytes) {
        return Fail(error, "组件数据超过大小上限");
    }

    BinaryReader reader(component.bytes);
    SegmentHeader header;
    if (!reader.BeginSegment(header)) {
        return Fail(error, reader.Error().empty() ? "组件段头损坏" : reader.Error());
    }
    if (header.magic != component.type) {
        return Fail(error, "组件类型与段头不一致");
    }
    if (header.version != component.version) {
        return Fail(error, "组件版本与段头不一致");
    }
    if (component.bytes.size() !=
        kSegmentHeaderSize + static_cast<std::size_t>(header.payloadSize)) {
        return Fail(error, "组件数据长度与段头不一致");
    }
    return true;
}

bool ParseComponentRecord(std::vector<std::uint8_t> bytes,
                          SavedComponent* component, std::string* error) {
    if (component == nullptr) {
        return Fail(error, "组件输出为空");
    }
    if (bytes.size() < kSegmentHeaderSize) {
        return Fail(error, "组件数据截断");
    }
    if (bytes.size() > kMaxComponentBytes) {
        return Fail(error, "组件数据超过大小上限");
    }

    BinaryReader reader(bytes);
    SegmentHeader header;
    if (!reader.BeginSegment(header)) {
        return Fail(error, reader.Error().empty() ? "组件段头损坏" : reader.Error());
    }
    if (header.magic == 0) {
        return Fail(error, "组件类型 ID 为 0");
    }
    if (bytes.size() !=
        kSegmentHeaderSize + static_cast<std::size_t>(header.payloadSize)) {
        return Fail(error, "组件数据长度与段头不一致");
    }

    SavedComponent parsed;
    parsed.type = header.magic;
    parsed.version = header.version;
    parsed.bytes = std::move(bytes);
    *component = std::move(parsed);
    return true;
}

void DestroyCreated(ecs::EcsWorld& world,
                    const std::vector<ecs::Entity>& entities) {
    for (const ecs::Entity entity : entities) {
        world.Destroy(entity);
    }
    world.Flush();
}

}  // namespace

bool CaptureEntity(const ecs::EcsWorld& world, ecs::Entity entity,
                   const ComponentRegistry& registry, SavedEntity* out,
                   std::string* error) {
    if (out == nullptr) {
        return Fail(error, "实体输出为空");
    }
    if (!world.Alive(entity)) {
        return Fail(error, "实体不存在或已进入销毁队列");
    }
    const std::uint64_t stableId = world.IdOf(entity).id;
    if (stableId == 0) {
        return Fail(error, "实体缺少 StableId");
    }

    SavedEntity saved;
    saved.stableId = stableId;
    const entt::registry& ecsRegistry = world.Registry();
    for (const ComponentCodec& codec : registry.All()) {
        if (!codec.has(ecsRegistry, entity)) {
            continue;
        }

        BinaryWriter writer;
        if (!writer.BeginSegment(codec.type, codec.version)) {
            return Fail(error, "组件 " + codec.name + " 写入失败: " +
                                   writer.Error());
        }
        if (!codec.write(ecsRegistry, entity, writer)) {
            return Fail(error, "组件 " + codec.name + " 数据无效: " +
                                   writer.Error());
        }
        if (!writer.EndSegment()) {
            return Fail(error, "组件 " + codec.name + " 收尾失败: " +
                                   writer.Error());
        }

        SavedComponent component;
        component.type = codec.type;
        component.version = codec.version;
        const auto bytes = writer.Data();
        component.bytes.assign(bytes.begin(), bytes.end());
        saved.components.push_back(std::move(component));
    }

    std::sort(saved.components.begin(), saved.components.end(),
              [](const SavedComponent& left, const SavedComponent& right) {
                  return left.type < right.type;
              });
    *out = std::move(saved);
    return true;
}

bool CaptureEntities(const ecs::EcsWorld& world,
                     const ComponentRegistry& registry, EntitySnapshot* out,
                     EntityFilter filter, std::string* error) {
    if (out == nullptr) {
        return Fail(error, "实体快照输出为空");
    }

    EntitySnapshot snapshot;
    std::string captureError;
    bool failed = false;
    world.Registry().view<ecs::StableId>().each(
        [&](const ecs::Entity entity, const ecs::StableId&) {
            if (failed || !world.Alive(entity)) {
                return;
            }
            if (filter && !filter(entity)) {
                return;
            }
            SavedEntity saved;
            if (!CaptureEntity(world, entity, registry, &saved, &captureError)) {
                failed = true;
                return;
            }
            snapshot.entities.push_back(std::move(saved));
        });
    if (failed) {
        return Fail(error, std::move(captureError));
    }

    std::sort(snapshot.entities.begin(), snapshot.entities.end(),
              [](const SavedEntity& left, const SavedEntity& right) {
                  return left.stableId < right.stableId;
              });
    *out = std::move(snapshot);
    return true;
}

bool CapturePlayer(const ecs::EcsWorld& world,
                   const ComponentRegistry& registry, SavedEntity* out,
                   std::string* error) {
    ecs::Entity player = entt::null;
    int playerCount = 0;
    world.Registry().view<ecs::PlayerTag>().each([&](const ecs::Entity entity) {
        if (!world.Alive(entity)) {
            return;
        }
        ++playerCount;
        if (player == entt::null) {
            player = entity;
        }
    });
    if (playerCount == 0) {
        return Fail(error, "未找到玩家实体");
    }
    if (playerCount != 1) {
        return Fail(error, "玩家实体数量无效: " + std::to_string(playerCount));
    }
    return CaptureEntity(world, player, registry, out, error);
}

bool ValidateEntitySnapshot(const EntitySnapshot& snapshot,
                            std::string* error) {
    if (snapshot.entities.size() > kMaxContainerElements) {
        return Fail(error, "实体数量超过上限");
    }

    std::unordered_set<std::uint64_t> ids;
    for (const SavedEntity& entity : snapshot.entities) {
        if (entity.stableId == 0) {
            return Fail(error, "实体 StableId 为 0");
        }
        if (entity.stableId == std::numeric_limits<std::uint64_t>::max()) {
            return Fail(error, "实体 StableId 超出可分配范围");
        }
        if (!ids.insert(entity.stableId).second) {
            return Fail(error, "实体 StableId 重复: " +
                                   std::to_string(entity.stableId));
        }
        if (entity.components.size() > kMaxContainerElements) {
            return Fail(error, "组件数量超过上限");
        }

        std::unordered_set<std::uint32_t> types;
        for (const SavedComponent& component : entity.components) {
            if (!types.insert(component.type).second) {
                return Fail(error, "实体组件类型重复: " +
                                       std::to_string(component.type));
            }
            if (!ValidateComponentBytes(component, error)) {
                return false;
            }
        }
    }
    return true;
}

bool ApplyEntity(const SavedEntity& saved, ecs::EcsWorld& world,
                 ecs::Entity entity, const ComponentRegistry& registry,
                 std::string* error) {
    if (!world.Alive(entity)) {
        return Fail(error, "目标实体不存在或已进入销毁队列");
    }
    if (saved.stableId == 0) {
        return Fail(error, "实体 StableId 为 0");
    }
    if (world.IdOf(entity).id != saved.stableId) {
        return Fail(error, "目标实体 StableId 与存档不一致");
    }
    if (saved.components.size() > kMaxContainerElements) {
        return Fail(error, "组件数量超过上限");
    }

    std::unordered_set<std::uint32_t> types;
    for (const SavedComponent& component : saved.components) {
        if (!types.insert(component.type).second) {
            return Fail(error, "实体组件类型重复: " +
                                   std::to_string(component.type));
        }

        const ComponentCodec* codec = registry.Find(component.type);
        if (codec == nullptr) {
            if (!ValidateComponentBytes(component, error)) {
                return false;
            }
            continue;
        }
        if (component.version < codec->minimumVersion ||
            component.version > codec->version) {
            return Fail(error, "组件 " + codec->name + " 版本不兼容: 读取到 " +
                                   std::to_string(component.version) +
                                   "，支持版本 " +
                                   std::to_string(codec->minimumVersion) + "–" +
                                   std::to_string(codec->version));
        }

        BinaryReader reader(component.bytes);
        SegmentHeader header;
        if (!reader.BeginSegment(header, component.type,
                                 codec->minimumVersion, codec->version)) {
            return Fail(error, "组件 " + codec->name + " 段头无效: " +
                                   reader.Error());
        }
        if (component.bytes.size() !=
            kSegmentHeaderSize + static_cast<std::size_t>(header.payloadSize)) {
            return Fail(error, "组件 " + codec->name + " 数据长度不一致");
        }
        if (!codec->read(reader, world.Registry(), entity)) {
            return Fail(error, "组件 " + codec->name + " 数据无效: " +
                                   reader.Error());
        }
        if (!reader.EndSegment()) {
            return Fail(error, "组件 " + codec->name + " 尾部数据未读完: " +
                                   reader.Error());
        }
    }
    return true;
}

std::vector<std::uint8_t> EncodeEntitySnapshot(const EntitySnapshot& snapshot,
                                               std::string* error) {
    if (!ValidateEntitySnapshot(snapshot, error)) {
        return {};
    }

    BinaryWriter writer;
    if (!writer.BeginSegment(kMagicEntities, kEntityStoreVersion)) {
        Fail(error, writer.Error());
        return {};
    }
    if (!writer.Write(static_cast<std::uint32_t>(snapshot.entities.size()))) {
        Fail(error, writer.Error());
        return {};
    }
    for (const SavedEntity& entity : snapshot.entities) {
        if (!writer.Write(entity.stableId) ||
            !writer.Write(static_cast<std::uint32_t>(entity.components.size()))) {
            Fail(error, writer.Error());
            return {};
        }
        for (const SavedComponent& component : entity.components) {
            if (!writer.Write(static_cast<std::uint32_t>(component.bytes.size())) ||
                !writer.WriteBytes(component.bytes)) {
                Fail(error, writer.Error());
                return {};
            }
        }
    }
    if (!writer.EndSegment()) {
        Fail(error, writer.Error());
        return {};
    }
    const auto bytes = writer.Data();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

bool DecodeEntitySnapshot(std::span<const std::uint8_t> bytes,
                          EntitySnapshot* out, std::string* error) {
    if (out == nullptr) {
        return Fail(error, "实体快照输出为空");
    }

    BinaryReader reader(bytes);
    SegmentHeader header;
    if (!reader.BeginSegment(header, kMagicEntities, kEntityStoreVersion)) {
        return Fail(error, reader.Error().empty() ? "实体段缺失或截断"
                                                  : reader.Error());
    }

    std::uint32_t entityCount = 0;
    if (!reader.Read(entityCount)) {
        return Fail(error, reader.Error());
    }
    if (entityCount > kMaxContainerElements ||
        entityCount > reader.RemainingInSegment() / kMinEntityBytes) {
        return Fail(error, "实体数量超过上限或数据截断");
    }

    EntitySnapshot snapshot;
    snapshot.entities.reserve(std::min<std::uint32_t>(entityCount, 4096));
    std::unordered_set<std::uint64_t> ids;
    for (std::uint32_t i = 0; i < entityCount; ++i) {
        SavedEntity entity;
        if (!reader.Read(entity.stableId)) {
            return Fail(error, reader.Error());
        }
        if (entity.stableId == 0 ||
            entity.stableId == std::numeric_limits<std::uint64_t>::max() ||
            !ids.insert(entity.stableId).second) {
            return Fail(error, "实体 StableId 非法或重复");
        }

        std::uint32_t componentCount = 0;
        if (!reader.Read(componentCount)) {
            return Fail(error, reader.Error());
        }
        if (componentCount > kMaxContainerElements ||
            componentCount > reader.RemainingInSegment() /
                                 kMinComponentRecordBytes) {
            return Fail(error, "组件数量超过上限或数据截断");
        }

        entity.components.reserve(std::min<std::uint32_t>(componentCount, 16));
        std::unordered_set<std::uint32_t> types;
        for (std::uint32_t c = 0; c < componentCount; ++c) {
            std::uint32_t size = 0;
            if (!reader.Read(size)) {
                return Fail(error, reader.Error());
            }
            if (size < kSegmentHeaderSize || size > kMaxComponentBytes) {
                return Fail(error, "组件数据长度非法");
            }
            std::vector<std::uint8_t> componentBytes(size);
            if (!reader.ReadBytes(componentBytes.data(), componentBytes.size())) {
                return Fail(error, reader.Error());
            }

            SavedComponent component;
            if (!ParseComponentRecord(std::move(componentBytes), &component,
                                      error)) {
                return false;
            }
            if (!types.insert(component.type).second) {
                return Fail(error, "实体组件类型重复");
            }
            entity.components.push_back(std::move(component));
        }
        snapshot.entities.push_back(std::move(entity));
    }

    if (!reader.EndSegment()) {
        return Fail(error, reader.Error());
    }
    *out = std::move(snapshot);
    return true;
}

bool LoadEntities(const EntitySnapshot& snapshot, ecs::EcsWorld& world,
                  const ComponentRegistry& registry,
                  std::vector<ecs::Entity>* loaded, std::string* error) {
    if (loaded != nullptr) {
        loaded->clear();
    }
    if (!ValidateEntitySnapshot(snapshot, error)) {
        return false;
    }

    std::vector<ecs::Entity> created;
    created.reserve(snapshot.entities.size());
    for (const SavedEntity& saved : snapshot.entities) {
        const ecs::Entity entity = world.CreateWithStableId(saved.stableId);
        if (entity == ecs::EcsWorld::kNull) {
            DestroyCreated(world, created);
            return Fail(error, "无法重建实体 StableId: " +
                                   std::to_string(saved.stableId));
        }
        created.push_back(entity);
    }

    for (std::size_t i = 0; i < snapshot.entities.size(); ++i) {
        if (!ApplyEntity(snapshot.entities[i], world, created[i], registry,
                         error)) {
            DestroyCreated(world, created);
            return false;
        }
    }

    if (loaded != nullptr) {
        *loaded = std::move(created);
    }
    return true;
}

}  // namespace zl::save
