#include "game/EntityReplication.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>
#include <utility>

#include "core/Constants.h"

namespace zl::game {
namespace {

constexpr std::uint16_t kReplicationComponentVersion = 1;
constexpr float kMaxLocalPredictionStep = 0.25f;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Finite(float value) {
    return std::isfinite(value);
}

void PutU8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFu));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFu));
    }
}

void PutI32(std::vector<std::uint8_t>& out, std::int32_t value) {
    PutU32(out, static_cast<std::uint32_t>(value));
}

void PutF32(std::vector<std::uint8_t>& out, float value) {
    PutU32(out, std::bit_cast<std::uint32_t>(value));
}

class PayloadReader {
public:
    explicit PayloadReader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

    bool U8(std::uint8_t* value) {
        if (!Has(1)) {
            return false;
        }
        *value = m_bytes[m_offset++];
        return true;
    }

    bool U16(std::uint16_t* value) {
        if (!Has(2)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint16_t>(m_bytes[m_offset + 1]) << 8U);
        m_offset += 2;
        return true;
    }

    bool U32(std::uint32_t* value) {
        if (!Has(4)) {
            return false;
        }
        *value = static_cast<std::uint32_t>(m_bytes[m_offset]) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 1]) << 8U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 2]) << 16U) |
                 (static_cast<std::uint32_t>(m_bytes[m_offset + 3]) << 24U);
        m_offset += 4;
        return true;
    }

    bool I32(std::int32_t* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool F32(float* value) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) {
            return false;
        }
        *value = std::bit_cast<float>(raw);
        return true;
    }

    bool String(std::string* value, std::size_t maxBytes) {
        std::uint16_t size = 0;
        if (!U16(&size) || size > maxBytes || size == 0 || !Has(size)) {
            return false;
        }
        value->assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                      m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + size));
        m_offset += size;
        return true;
    }

    bool AtEnd() const { return m_offset == m_bytes.size(); }

private:
    bool Has(std::size_t count) const {
        return count <= m_bytes.size() - m_offset;
    }

    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

net::ReplicatedComponent MakeComponent(std::uint32_t type,
                                       std::vector<std::uint8_t> data) {
    net::ReplicatedComponent component;
    component.type = type;
    component.version = kReplicationComponentVersion;
    component.data = std::move(data);
    return component;
}

bool CaptureComponents(entt::registry& registry, entt::entity entity,
                       std::vector<net::ReplicatedComponent>* components,
                       std::string* error) {
    const auto* position = registry.try_get<ecs::Position>(entity);
    if (position == nullptr || !Finite(position->x) || !Finite(position->y) ||
        position->z < 0 || position->z >= core::kMaxZ) {
        SetError(error, "复制实体位置非法");
        return false;
    }
    std::vector<std::uint8_t> positionBytes;
    PutF32(positionBytes, position->x);
    PutF32(positionBytes, position->y);
    PutI32(positionBytes, position->z);
    components->push_back(MakeComponent(kReplicationPosition, std::move(positionBytes)));

    if (const auto* velocity = registry.try_get<ecs::Velocity>(entity);
        velocity != nullptr) {
        if (!Finite(velocity->x) || !Finite(velocity->y)) {
            SetError(error, "复制实体速度非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutF32(bytes, velocity->x);
        PutF32(bytes, velocity->y);
        components->push_back(MakeComponent(kReplicationVelocity, std::move(bytes)));
    }

    if (const auto* health = registry.try_get<ecs::Health>(entity); health != nullptr) {
        if (!Finite(health->current) || !Finite(health->max) || health->max <= 0.0f) {
            SetError(error, "复制实体生命值非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutF32(bytes, health->current);
        PutF32(bytes, health->max);
        components->push_back(MakeComponent(kReplicationHealth, std::move(bytes)));
    }

    if (const auto* sprite = registry.try_get<ecs::SpriteRef>(entity);
        sprite != nullptr) {
        std::vector<std::uint8_t> bytes;
        PutU32(bytes, sprite->sprite);
        PutI32(bytes, sprite->layer);
        PutU32(bytes, sprite->tint);
        components->push_back(MakeComponent(kReplicationSprite, std::move(bytes)));
    }

    if (const auto* facing = registry.try_get<ecs::Facing>(entity); facing != nullptr) {
        if (static_cast<std::uint8_t>(facing->dir) >
            static_cast<std::uint8_t>(ecs::Direction::Up)) {
            SetError(error, "复制实体朝向非法");
            return false;
        }
        components->push_back(MakeComponent(
            kReplicationFacing, {static_cast<std::uint8_t>(facing->dir)}));
    }

    if (const auto* anim = registry.try_get<ecs::AnimState>(entity); anim != nullptr) {
        if (anim->framesPerDir == 0 ||
            anim->frame >= anim->framesPerDir ||
            !Finite(anim->fps) || anim->fps < 0.0f ||
            !Finite(anim->time) || anim->time < 0.0f) {
            SetError(error, "复制实体动画状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, anim->frame);
        PutU8(bytes, anim->framesPerDir);
        PutF32(bytes, anim->fps);
        PutF32(bytes, anim->time);
        components->push_back(MakeComponent(kReplicationAnim, std::move(bytes)));
    }

    if (const auto* item = registry.try_get<ecs::ItemInstance>(entity); item != nullptr) {
        if (!item->Valid() || item->defId.size() > 128 || item->count > 9999 ||
            !Finite(item->condition) || item->condition < 0.0f ||
            item->condition > 1.0f) {
            SetError(error, "复制实体物品状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(item->defId.size()));
        bytes.insert(bytes.end(), item->defId.begin(), item->defId.end());
        PutU16(bytes, static_cast<std::uint16_t>(item->count));
        PutF32(bytes, item->condition);
        components->push_back(MakeComponent(kReplicationItem, std::move(bytes)));
    }

    if (const auto* equipment = registry.try_get<ecs::Equipment>(entity);
        equipment != nullptr) {
        std::vector<std::uint8_t> bytes;
        for (const auto& slot : equipment->slots) {
            if (!slot.has_value()) {
                PutU8(bytes, 0);
                continue;
            }
            const ecs::ItemInstance& item = *slot;
            if (!item.Valid() || item.defId.size() > 128 || item.count > 9999 ||
                !Finite(item.condition) || item.condition < 0.0f ||
                item.condition > 1.0f) {
                SetError(error, "复制实体装备状态非法");
                return false;
            }
            PutU8(bytes, 1);
            PutU16(bytes, static_cast<std::uint16_t>(item.defId.size()));
            bytes.insert(bytes.end(), item.defId.begin(), item.defId.end());
            PutU16(bytes, static_cast<std::uint16_t>(item.count));
            PutF32(bytes, item.condition);
        }
        components->push_back(
            MakeComponent(kReplicationEquipment, std::move(bytes)));
    }

    if (const auto* action = registry.try_get<ecs::ActionState>(entity);
        action != nullptr) {
        if (static_cast<std::uint8_t>(action->action) >
                static_cast<std::uint8_t>(ecs::ActorAction::Dead) ||
            !Finite(action->elapsed) || !Finite(action->duration) ||
            action->elapsed < 0.0f || action->duration < 0.0f ||
            action->duration > 3600.0f ||
            action->elapsed > action->duration + 1.0f) {
            SetError(error, "复制实体动作状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, static_cast<std::uint8_t>(action->action));
        PutF32(bytes, action->elapsed);
        PutF32(bytes, action->duration);
        components->push_back(MakeComponent(kReplicationAction, std::move(bytes)));
    }

    std::sort(components->begin(), components->end(),
              [](const net::ReplicatedComponent& lhs,
                 const net::ReplicatedComponent& rhs) {
                  return lhs.type < rhs.type;
              });
    return true;
}

bool SameComponent(const net::ReplicatedComponent& lhs,
                   const net::ReplicatedComponent& rhs) {
    return lhs.type == rhs.type && lhs.version == rhs.version &&
           lhs.data == rhs.data;
}

bool HasComponentChange(const std::vector<net::ReplicatedComponent>& baseline,
                        const std::vector<net::ReplicatedComponent>& current,
                        std::vector<net::ReplicatedComponent>* changed,
                        bool* structuralChange) {
    *structuralChange = false;
    for (const net::ReplicatedComponent& component : current) {
        const auto it = std::find_if(
            baseline.begin(), baseline.end(),
            [&](const net::ReplicatedComponent& candidate) {
                return candidate.type == component.type;
            });
        if (it == baseline.end()) {
            *structuralChange = true;
            changed->push_back(component);
        } else if (!SameComponent(*it, component)) {
            changed->push_back(component);
        }
    }
    for (const net::ReplicatedComponent& component : baseline) {
        const auto it = std::find_if(
            current.begin(), current.end(),
            [&](const net::ReplicatedComponent& candidate) {
                return candidate.type == component.type;
            });
        if (it == current.end()) {
            *structuralChange = true;
        }
    }
    return !changed->empty() || *structuralChange;
}

}  // namespace

bool RegisterEntityReplicationComponents(net::ReplicationRegistry& registry,
                                         std::string* error) {
    if (!registry.Register(kReplicationPosition, "Position", 12,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationVelocity, "Velocity", 8,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationHealth, "Health", 8,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationSprite, "SpriteRef", 12,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationFacing, "Facing", 1,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationItem, "ItemInstance", 144,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationAnim, "AnimState", 10,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationEquipment, "Equipment", 274,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    return registry.Register(kReplicationAction, "ActionState", 9,
                             kReplicationComponentVersion, error);
}

EntityReplicationProducer::EntityReplicationProducer(
    ecs::EcsWorld& world, const net::ReplicationRegistry& registry, Options options)
    : m_world(world), m_registry(registry), m_options(options) {
    if (!Finite(m_options.aoiRadius) || m_options.aoiRadius <= 0.0f) {
        m_options.aoiRadius = 48.0f;
    }
    m_options.maxEntities =
        std::clamp<std::size_t>(m_options.maxEntities, 1, net::kMaxReplicationEntities);
}

bool EntityReplicationProducer::Capture(
    float focusX, float focusY, std::vector<net::ReplicatedEntity>* entities,
    std::string* error) const {
    if (entities == nullptr) {
        SetError(error, "复制输出实体列表为空");
        return false;
    }
    entities->clear();
    if (!Finite(focusX) || !Finite(focusY)) {
        SetError(error, "复制焦点坐标非法");
        return false;
    }
    if (m_registry.Find(kReplicationPosition) == nullptr ||
        m_registry.Find(kReplicationVelocity) == nullptr ||
        m_registry.Find(kReplicationHealth) == nullptr ||
        m_registry.Find(kReplicationSprite) == nullptr ||
        m_registry.Find(kReplicationFacing) == nullptr ||
        m_registry.Find(kReplicationAnim) == nullptr ||
        m_registry.Find(kReplicationItem) == nullptr ||
        m_registry.Find(kReplicationEquipment) == nullptr ||
        m_registry.Find(kReplicationAction) == nullptr) {
        SetError(error, "复制组件注册表不完整");
        return false;
    }

    entt::registry& registry = m_world.Registry();
    struct Candidate {
        net::ReplicatedEntity entity;
        float distanceSq = 0.0f;
    };
    std::vector<Candidate> candidates;
    const float radiusSq = m_options.aoiRadius * m_options.aoiRadius;
    std::string failure;

    registry.view<ecs::StableId, ecs::Position, ecs::SpriteRef>().each(
        [&](const entt::entity entity, const ecs::StableId& stableId,
            const ecs::Position& position, const ecs::SpriteRef&) {
            if (!failure.empty()) {
                return;
            }
            const bool player = registry.all_of<ecs::PlayerTag>(entity);
            const bool item = registry.all_of<ecs::ItemInstance>(entity);
            const bool effect = registry.all_of<ecs::TransientEffect>(entity);
            const bool projectile = registry.all_of<ecs::Projectile>(entity);
            if (!player && !item && !effect && !projectile &&
                !registry.all_of<ecs::Health>(entity)) {
                return;
            }
            if (stableId.id == 0) {
                return;
            }
            const float dx = position.x - focusX;
            const float dy = position.y - focusY;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq > radiusSq) {
                return;
            }

            net::ReplicatedEntity replicated;
            replicated.stableId = stableId.id;
            replicated.archetype =
                player       ? kReplicationArchetypePlayer
                : item       ? kReplicationArchetypeItem
                : projectile ? kReplicationArchetypeProjectile
                : effect     ? kReplicationArchetypeEffect
                             : kReplicationArchetypeCharacter;
            replicated.ownerStableId = player ? stableId.id : 0;
            if (const auto* projectileComponent =
                    registry.try_get<ecs::Projectile>(entity);
                projectileComponent != nullptr) {
                replicated.ownerStableId = projectileComponent->ownerStableId;
            }
            if (const auto* owner = registry.try_get<ecs::NetworkOwner>(entity);
                owner != nullptr) {
                replicated.ownerStableId = owner->stableId;
            }
            if (!CaptureComponents(registry, entity, &replicated.components,
                                   &failure)) {
                return;
            }
            candidates.push_back(Candidate{std::move(replicated), distanceSq});
        });

    if (!failure.empty()) {
        SetError(error, failure);
        return false;
    }
    if (candidates.size() > m_options.maxEntities) {
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& lhs, const Candidate& rhs) {
                      if (lhs.distanceSq != rhs.distanceSq) {
                          return lhs.distanceSq < rhs.distanceSq;
                      }
                      return lhs.entity.stableId < rhs.entity.stableId;
                  });
        candidates.resize(m_options.maxEntities);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs) {
                  return lhs.entity.stableId < rhs.entity.stableId;
              });
    entities->reserve(candidates.size());
    for (Candidate& candidate : candidates) {
        entities->push_back(std::move(candidate.entity));
    }
    return true;
}

net::ReplicationSnapshot EntityReplicationProducer::BuildFull(
    std::uint64_t serverTick, float focusX, float focusY, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (serverTick == 0) {
        SetError(error, "复制快照 serverTick 不能为 0");
        return {};
    }
    std::vector<net::ReplicatedEntity> entities;
    if (!Capture(focusX, focusY, &entities, error)) {
        return {};
    }

    m_baseline.clear();
    for (const net::ReplicatedEntity& entity : entities) {
        BaselineEntity baseline;
        baseline.archetype = entity.archetype;
        baseline.ownerStableId = entity.ownerStableId;
        baseline.components = entity.components;
        m_baseline.emplace(entity.stableId, std::move(baseline));
    }
    m_lastBuiltTick = serverTick;
    m_hasBaseline = true;
    ++m_stats.fullSnapshots;
    m_stats.entitiesSent += entities.size();

    net::ReplicationSnapshot snapshot;
    snapshot.sequence = ++m_sequence;
    snapshot.serverTick = serverTick;
    snapshot.full = true;
    snapshot.upserts = std::move(entities);
    return snapshot;
}

bool EntityReplicationProducer::BuildDelta(
    std::uint64_t serverTick, float focusX, float focusY,
    net::ReplicationSnapshot* snapshot, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (snapshot == nullptr) {
        SetError(error, "输出复制快照为空");
        return false;
    }
    if (!m_hasBaseline) {
        SetError(error, "复制增量缺少全量基线");
        return false;
    }
    if (serverTick <= m_lastBuiltTick) {
        SetError(error, "复制快照 tick 未前进");
        return false;
    }

    std::vector<net::ReplicatedEntity> entities;
    if (!Capture(focusX, focusY, &entities, error)) {
        return false;
    }

    std::unordered_map<std::uint64_t, const net::ReplicatedEntity*> current;
    current.reserve(entities.size());
    for (const net::ReplicatedEntity& entity : entities) {
        current.emplace(entity.stableId, &entity);
    }

    net::ReplicationSnapshot delta;
    delta.sequence = ++m_sequence;
    delta.serverTick = serverTick;
    delta.baseTick = m_lastBuiltTick;
    delta.full = false;

    for (const net::ReplicatedEntity& entity : entities) {
        const auto baselineIt = m_baseline.find(entity.stableId);
        if (baselineIt == m_baseline.end()) {
            delta.upserts.push_back(entity);
            continue;
        }
        const BaselineEntity& baseline = baselineIt->second;
        if (baseline.archetype != entity.archetype ||
            baseline.ownerStableId != entity.ownerStableId) {
            // 归属/archetype 变化时连同组件一起发，避免同 tick 移动被元数据更新覆盖。
            delta.upserts.push_back(entity);
            continue;
        }

        std::vector<net::ReplicatedComponent> changed;
        bool structuralChange = false;
        if (HasComponentChange(baseline.components, entity.components, &changed,
                               &structuralChange)) {
            net::ReplicatedEntity update;
            update.stableId = entity.stableId;
            update.archetype = entity.archetype;
            update.ownerStableId = entity.ownerStableId;
            update.components = structuralChange ? entity.components : std::move(changed);
            delta.upserts.push_back(std::move(update));
        }
    }

    for (const auto& entry : m_baseline) {
        if (current.find(entry.first) == current.end()) {
            delta.removals.push_back(entry.first);
        }
    }
    std::sort(delta.removals.begin(), delta.removals.end());

    m_baseline.clear();
    for (const net::ReplicatedEntity& entity : entities) {
        BaselineEntity baseline;
        baseline.archetype = entity.archetype;
        baseline.ownerStableId = entity.ownerStableId;
        baseline.components = entity.components;
        m_baseline.emplace(entity.stableId, std::move(baseline));
    }
    m_lastBuiltTick = serverTick;
    ++m_stats.deltaSnapshots;
    m_stats.entitiesSent += delta.upserts.size();
    m_stats.entitiesRemoved += delta.removals.size();
    *snapshot = std::move(delta);
    return true;
}

void EntityReplicationProducer::Reset() {
    m_baseline.clear();
    m_lastBuiltTick = 0;
    m_hasBaseline = false;
    m_sequence = 0;
    m_stats = Stats{};
}

EntityReplicationClient::EntityReplicationClient(Options options)
    : m_options(options) {
    if (!Finite(m_options.localSnapDistance) || m_options.localSnapDistance < 0.0f) {
        m_options.localSnapDistance = 1.0f;
    }
    m_options.localCorrectionBlend =
        std::clamp(m_options.localCorrectionBlend, 0.0f, 1.0f);
}

bool EntityReplicationClient::DecodePatch(const net::ReplicatedEntity& entity,
                                          ActorPatch* patch,
                                          std::string* error) const {
    if (patch == nullptr) {
        SetError(error, "复制实体输出为空");
        return false;
    }
    if (entity.stableId == 0 || entity.archetype == 0) {
        SetError(error, "复制实体元数据非法");
        return false;
    }

    ActorPatch decoded;
    decoded.stableId = entity.stableId;
    decoded.archetype = entity.archetype;
    decoded.ownerStableId = entity.ownerStableId;
    std::unordered_set<std::uint32_t> types;
    for (const net::ReplicatedComponent& component : entity.components) {
        if (!types.insert(component.type).second) {
            SetError(error, "复制实体组件重复");
            return false;
        }
        if (component.version != kReplicationComponentVersion) {
            SetError(error, "复制实体组件版本不兼容");
            return false;
        }
        PayloadReader reader(component.data);
        if (component.type == kReplicationPosition) {
            if (!reader.F32(&decoded.position.x) || !reader.F32(&decoded.position.y) ||
                !reader.I32(&decoded.position.z) || !reader.AtEnd() ||
                !Finite(decoded.position.x) || !Finite(decoded.position.y) ||
                decoded.position.z < 0 || decoded.position.z >= core::kMaxZ) {
                SetError(error, "复制 Position 负载非法");
                return false;
            }
            decoded.hasPosition = true;
        } else if (component.type == kReplicationVelocity) {
            if (!reader.F32(&decoded.velocity.x) || !reader.F32(&decoded.velocity.y) ||
                !reader.AtEnd() || !Finite(decoded.velocity.x) ||
                !Finite(decoded.velocity.y)) {
                SetError(error, "复制 Velocity 负载非法");
                return false;
            }
            decoded.hasVelocity = true;
        } else if (component.type == kReplicationHealth) {
            if (!reader.F32(&decoded.health.current) ||
                !reader.F32(&decoded.health.max) || !reader.AtEnd() ||
                !Finite(decoded.health.current) || !Finite(decoded.health.max) ||
                decoded.health.max <= 0.0f) {
                SetError(error, "复制 Health 负载非法");
                return false;
            }
            decoded.hasHealth = true;
        } else if (component.type == kReplicationSprite) {
            if (!reader.U32(&decoded.sprite.sprite) ||
                !reader.I32(&decoded.sprite.layer) ||
                !reader.U32(&decoded.sprite.tint) || !reader.AtEnd()) {
                SetError(error, "复制 SpriteRef 负载非法");
                return false;
            }
            decoded.hasSprite = true;
        } else if (component.type == kReplicationFacing) {
            std::uint8_t facing = 0;
            if (!reader.U8(&facing) || !reader.AtEnd() ||
                facing > static_cast<std::uint8_t>(ecs::Direction::Up)) {
                SetError(error, "复制 Facing 负载非法");
                return false;
            }
            decoded.facing.dir = static_cast<ecs::Direction>(facing);
            decoded.hasFacing = true;
        } else if (component.type == kReplicationAnim) {
            if (!reader.U8(&decoded.anim.frame) ||
                !reader.U8(&decoded.anim.framesPerDir) ||
                !reader.F32(&decoded.anim.fps) || !reader.F32(&decoded.anim.time) ||
                !reader.AtEnd() || decoded.anim.framesPerDir == 0 ||
                decoded.anim.frame >= decoded.anim.framesPerDir ||
                !Finite(decoded.anim.fps) || decoded.anim.fps < 0.0f ||
                !Finite(decoded.anim.time) || decoded.anim.time < 0.0f) {
                SetError(error, "复制 AnimState 负载非法");
                return false;
            }
            decoded.hasAnim = true;
        } else if (component.type == kReplicationItem) {
            std::uint16_t count = 0;
            if (!reader.String(&decoded.item.defId, 128) ||
                !reader.U16(&count) || !reader.F32(&decoded.item.condition) ||
                !reader.AtEnd() || count == 0 || count > 9999 ||
                !Finite(decoded.item.condition) ||
                decoded.item.condition < 0.0f ||
                decoded.item.condition > 1.0f) {
                SetError(error, "复制 ItemInstance 负载非法");
                return false;
            }
            decoded.item.count = static_cast<int>(count);
            decoded.hasItem = true;
        } else if (component.type == kReplicationEquipment) {
            decoded.equipment = {};
            for (auto& slot : decoded.equipment) {
                std::uint8_t present = 0;
                if (!reader.U8(&present) || present > 1) {
                    SetError(error, "复制 Equipment 负载非法");
                    return false;
                }
                if (present == 0) {
                    continue;
                }
                ecs::ItemInstance item;
                std::uint16_t count = 0;
                if (!reader.String(&item.defId, 128) || !reader.U16(&count) ||
                    !reader.F32(&item.condition) || count == 0 || count > 9999 ||
                    !Finite(item.condition) || item.condition < 0.0f ||
                    item.condition > 1.0f) {
                    SetError(error, "复制 Equipment 负载非法");
                    return false;
                }
                item.count = static_cast<int>(count);
                slot = std::move(item);
            }
            if (!reader.AtEnd()) {
                SetError(error, "复制 Equipment 负载非法");
                return false;
            }
            decoded.hasEquipment = true;
        } else if (component.type == kReplicationAction) {
            std::uint8_t action = 0;
            if (!reader.U8(&action) || !reader.F32(&decoded.action.elapsed) ||
                !reader.F32(&decoded.action.duration) || !reader.AtEnd() ||
                action > static_cast<std::uint8_t>(ecs::ActorAction::Dead) ||
                !Finite(decoded.action.elapsed) ||
                !Finite(decoded.action.duration) ||
                decoded.action.elapsed < 0.0f ||
                decoded.action.duration < 0.0f ||
                decoded.action.duration > 3600.0f ||
                decoded.action.elapsed > decoded.action.duration + 1.0f) {
                SetError(error, "复制 ActionState 负载非法");
                return false;
            }
            decoded.action.action = static_cast<ecs::ActorAction>(action);
            decoded.hasAction = true;
        } else {
            SetError(error, "复制实体包含未知组件类型");
            return false;
        }
    }
    *patch = decoded;
    return true;
}

void EntityReplicationClient::ApplyPatch(const ActorPatch& patch) {
    const auto [it, inserted] = m_actors.try_emplace(patch.stableId);
    ReplicatedActor& actor = it->second;
    actor.stableId = patch.stableId;
    actor.archetype = patch.archetype;
    actor.ownerStableId = patch.ownerStableId;
    actor.player = patch.archetype == kReplicationArchetypePlayer;

    if (patch.hasPosition) {
        if (!inserted && actor.hasPosition) {
            actor.previousPosition = actor.position;
        } else {
            actor.previousPosition = patch.position;
        }
        actor.position = patch.position;
        actor.hasPosition = true;
    }
    if (patch.hasVelocity) {
        actor.velocity = patch.velocity;
        actor.hasVelocity = true;
    }
    if (patch.hasHealth) {
        actor.health = patch.health;
        actor.hasHealth = true;
    }
    if (patch.hasSprite) {
        actor.sprite = patch.sprite;
        actor.hasSprite = true;
    }
    if (patch.hasFacing) {
        actor.facing = patch.facing;
        actor.hasFacing = true;
    }
    if (patch.hasAnim) {
        actor.anim = patch.anim;
        actor.hasAnim = true;
    }
    if (patch.hasItem) {
        actor.item = patch.item;
        actor.hasItem = true;
    }
    if (patch.hasEquipment) {
        actor.equipment = patch.equipment;
        actor.hasEquipment = true;
    }
    if (patch.hasAction) {
        actor.action = patch.action;
        actor.hasAction = true;
    }
}

bool EntityReplicationClient::Apply(const net::ReplicationSnapshot& snapshot,
                                    std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (snapshot.serverTick == 0) {
        m_needsFullSnapshot = true;
        SetError(error, "复制快照 serverTick 为 0");
        return false;
    }
    if (snapshot.serverTick <= m_lastAppliedTick) {
        m_needsFullSnapshot = true;
        SetError(error, "复制快照 tick 未前进");
        return false;
    }
    if (snapshot.full && snapshot.baseTick != 0) {
        m_needsFullSnapshot = true;
        SetError(error, "全量复制快照 baseTick 非 0");
        return false;
    }
    if (snapshot.full && !snapshot.removals.empty()) {
        m_needsFullSnapshot = true;
        SetError(error, "全量复制快照携带移除列表");
        return false;
    }
    if (!snapshot.full && snapshot.baseTick != m_lastAppliedTick) {
        m_needsFullSnapshot = true;
        SetError(error, "复制增量基线不匹配，需要全量快照");
        return false;
    }

    std::vector<ActorPatch> patches;
    patches.reserve(snapshot.upserts.size());
    std::unordered_set<std::uint64_t> upsertIds;
    upsertIds.reserve(snapshot.upserts.size());
    for (const net::ReplicatedEntity& entity : snapshot.upserts) {
        if (!upsertIds.insert(entity.stableId).second) {
            m_needsFullSnapshot = true;
            SetError(error, "复制快照包含重复实体");
            return false;
        }
        ActorPatch patch;
        if (!DecodePatch(entity, &patch, error)) {
            m_needsFullSnapshot = true;
            return false;
        }
        if (!snapshot.full && !patch.hasPosition &&
            m_actors.find(entity.stableId) == m_actors.end()) {
            m_needsFullSnapshot = true;
            SetError(error, "增量复制新实体缺少 Position");
            return false;
        }
        if (snapshot.full && !patch.hasPosition) {
            m_needsFullSnapshot = true;
            SetError(error, "全量复制实体缺少 Position");
            return false;
        }
        patches.push_back(std::move(patch));
    }

    if (snapshot.full) {
        m_actors.clear();
        for (const ActorPatch& patch : patches) {
            ApplyPatch(patch);
            ++m_stats.entitiesSpawned;
        }
        ++m_stats.fullSnapshots;
    } else {
        for (const ActorPatch& patch : patches) {
            const bool existed = m_actors.find(patch.stableId) != m_actors.end();
            ApplyPatch(patch);
            if (!existed) {
                ++m_stats.entitiesSpawned;
            }
        }
        ++m_stats.deltaSnapshots;
    }

    for (const std::uint64_t stableId : snapshot.removals) {
        if (m_actors.erase(stableId) > 0) {
            ++m_stats.entitiesRemoved;
        }
        if (stableId == m_localPlayerId) {
            m_hasPredictedPosition = false;
        }
    }

    m_lastAppliedTick = snapshot.serverTick;
    ++m_stats.snapshotsApplied;
    m_needsFullSnapshot = false;

    if (m_localPlayerId != 0) {
        const auto it = m_actors.find(m_localPlayerId);
        if (it != m_actors.end() && it->second.hasPosition) {
            if (!m_hasPredictedPosition) {
                m_predictedPosition = it->second.position;
                m_hasPredictedPosition = true;
            }
            ReconcileLocalPlayerFromActor();
        } else {
            m_hasPredictedPosition = false;
        }
    }
    return true;
}

void EntityReplicationClient::Reset() {
    m_actors.clear();
    m_lastAppliedTick = 0;
    m_localPlayerId = 0;
    m_localMaxSpeed = 0.0f;
    m_predictedPosition = ecs::Position{};
    m_hasPredictedPosition = false;
    m_needsFullSnapshot = false;
    m_stats = Stats{};
}

bool EntityReplicationClient::SetLocalPlayer(std::uint64_t stableId,
                                             float maxSpeed) {
    if (stableId == 0 || !Finite(maxSpeed) || maxSpeed <= 0.0f) {
        return false;
    }
    m_localPlayerId = stableId;
    m_localMaxSpeed = maxSpeed;
    const auto it = m_actors.find(stableId);
    if (it != m_actors.end() && it->second.hasPosition) {
        m_predictedPosition = it->second.position;
        m_hasPredictedPosition = true;
    } else {
        m_hasPredictedPosition = false;
    }
    return true;
}

void EntityReplicationClient::ClearLocalPlayer() {
    m_localPlayerId = 0;
    m_localMaxSpeed = 0.0f;
    m_hasPredictedPosition = false;
}

bool EntityReplicationClient::AdvanceLocalPrediction(float dt, float dirX,
                                                     float dirY) {
    if (m_localPlayerId == 0 || !m_hasPredictedPosition) {
        return false;
    }
    if (!Finite(dt) || dt < 0.0f || !Finite(dirX) || !Finite(dirY)) {
        return false;
    }
    const float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length > 1.0f) {
        dirX /= length;
        dirY /= length;
    }
    const float step = std::min(dt, kMaxLocalPredictionStep);
    m_predictedPosition.x += dirX * m_localMaxSpeed * step;
    m_predictedPosition.y += dirY * m_localMaxSpeed * step;
    return true;
}

void EntityReplicationClient::ReconcileLocalPlayer() {
    ReconcileLocalPlayerFromActor();
}

void EntityReplicationClient::ReconcileLocalPlayerFromActor() {
    const auto it = m_actors.find(m_localPlayerId);
    if (it == m_actors.end() || !it->second.hasPosition) {
        return;
    }
    if (!m_hasPredictedPosition) {
        m_predictedPosition = it->second.position;
        m_hasPredictedPosition = true;
        return;
    }

    const ecs::Position& server = it->second.position;
    const float dx = m_predictedPosition.x - server.x;
    const float dy = m_predictedPosition.y - server.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    m_stats.lastCorrectionDistance = distance;
    if (distance <= 0.0001f) {
        m_predictedPosition = server;
        return;
    }
    ++m_stats.corrections;

    if (distance > m_options.localSnapDistance) {
        m_predictedPosition = server;
        m_predictedPosition.z = server.z;
        ++m_stats.snaps;
        return;
    }
    const float blend = m_options.localCorrectionBlend;
    m_predictedPosition.x += (server.x - m_predictedPosition.x) * blend;
    m_predictedPosition.y += (server.y - m_predictedPosition.y) * blend;
    m_predictedPosition.z = server.z;
}

bool EntityReplicationClient::SamplePosition(std::uint64_t stableId, float alpha,
                                             ecs::Position* position) const {
    if (position == nullptr) {
        return false;
    }
    if (stableId == m_localPlayerId && m_hasPredictedPosition) {
        *position = m_predictedPosition;
        return true;
    }
    const auto it = m_actors.find(stableId);
    if (it == m_actors.end() || !it->second.hasPosition) {
        return false;
    }
    const ReplicatedActor& actor = it->second;
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    position->x = actor.previousPosition.x +
                  (actor.position.x - actor.previousPosition.x) * t;
    position->y = actor.previousPosition.y +
                  (actor.position.y - actor.previousPosition.y) * t;
    position->z = actor.position.z;
    return true;
}

bool EntityReplicationClient::LocalPlayerPosition(ecs::Position* position) const {
    if (position == nullptr) {
        return false;
    }
    if (m_localPlayerId != 0 && m_hasPredictedPosition) {
        *position = m_predictedPosition;
        return true;
    }
    const auto it = m_actors.find(m_localPlayerId);
    if (it == m_actors.end() || !it->second.hasPosition) {
        return false;
    }
    *position = it->second.position;
    return true;
}

const ReplicatedActor* EntityReplicationClient::Find(
    std::uint64_t stableId) const {
    const auto it = m_actors.find(stableId);
    return it == m_actors.end() ? nullptr : &it->second;
}

}  // namespace zl::game
