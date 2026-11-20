#include "game/EntityReplication.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "characters/systems/AnimationSystem.h"
#include "core/Constants.h"
#include "game/Companion.h"
#include "game/HomesteadComponents.h"
#include "game/SettlementNpc.h"
#include "game/StructureComponents.h"
#include "game/Traps.h"
#include "game/VehicleMods.h"

namespace zl::game {
namespace {

constexpr std::uint16_t kReplicationComponentVersion = 4;
constexpr std::size_t kMaxLootReplicationItems = 8;
constexpr float kMaxLocalPredictionStep = 0.25f;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool Finite(float value) {
    return std::isfinite(value);
}

bool ValidWoundSource(ecs::DamageType type) {
    return type == ecs::DamageType::Blunt || type == ecs::DamageType::Claw ||
           type == ecs::DamageType::Bite;
}

bool ValidWounds(const ecs::Wounds& wounds) {
    if (wounds.count > ecs::kMaxWounds) {
        return false;
    }
    for (std::uint8_t i = 0; i < wounds.count; ++i) {
        const ecs::WoundEntry& entry = wounds.entries[i];
        if (!ValidWoundSource(entry.source) || !Finite(entry.severity) ||
            !Finite(entry.bleedRate) || !Finite(entry.bleedRemaining) ||
            !Finite(entry.bleedDuration) || entry.severity < 0.0f ||
            entry.severity > 1.0f || entry.bleedRate < 0.0f ||
            entry.bleedRate > 50.0f || entry.bleedRemaining < 0.0f ||
            entry.bleedDuration < 0.0f || entry.bleedDuration > 3600.0f ||
            entry.bleedRemaining > entry.bleedDuration ||
            (entry.bandaged && (entry.bleedRate > 0.0f ||
                                entry.bleedRemaining > 0.0f))) {
            return false;
        }
    }
    return true;
}

bool ValidPain(const ecs::Pain& pain) {
    return Finite(pain.baseLevel) && pain.baseLevel >= 0.0f && pain.baseLevel <= 1.0f &&
           Finite(pain.level) && pain.level >= 0.0f && pain.level <= 1.0f &&
           Finite(pain.moveScale) && pain.moveScale >= 0.05f &&
           pain.moveScale <= 1.0f && Finite(pain.attackCooldownScale) &&
           pain.attackCooldownScale >= 1.0f && pain.attackCooldownScale <= 3.0f &&
           Finite(pain.reliefStrength) && pain.reliefStrength >= 0.0f &&
           pain.reliefStrength <= 1.0f && Finite(pain.reliefRemaining) &&
           pain.reliefRemaining >= 0.0f && Finite(pain.reliefDuration) &&
           pain.reliefDuration >= 0.0f && pain.reliefDuration <= 600.0f &&
           pain.reliefRemaining <= pain.reliefDuration + 0.001f;
}

bool ValidInfection(const ecs::Infection& infection) {
    if (static_cast<std::uint8_t>(infection.stage) >
            static_cast<std::uint8_t>(ecs::InfectionStage::Severe) ||
        !Finite(infection.progress) || infection.progress < 0.0f ||
        infection.progress > 1.0f || !Finite(infection.slowStrength) ||
        infection.slowStrength < 0.0f || infection.slowStrength > 1.0f ||
        !Finite(infection.slowRemaining) || infection.slowRemaining < 0.0f ||
        infection.slowRemaining > 3600.0f ||
        !Finite(infection.slowDuration) || infection.slowDuration < 0.0f ||
        infection.slowDuration > 3600.0f ||
        infection.slowRemaining > infection.slowDuration + 0.001f ||
        !Finite(infection.timeToNextStage) ||
        infection.timeToNextStage < 0.0f ||
        infection.timeToNextStage > 86400.0f) {
        return false;
    }
    if (infection.stage == ecs::InfectionStage::None) {
        return infection.progress <= 0.0001f &&
               infection.slowStrength <= 0.0001f &&
               infection.slowRemaining <= 0.0001f &&
               infection.timeToNextStage <= 0.001f;
    }
    return true;
}

bool ValidZombieSpecial(const ecs::ZombieSpecial& special) {
    return static_cast<std::uint8_t>(special.kind) <=
               static_cast<std::uint8_t>(ecs::ZombieSpecialKind::Breaker) &&
           Finite(special.cooldown) && special.cooldown >= 0.0f &&
           special.cooldown <= 3600.0f && Finite(special.windup) &&
           special.windup >= 0.0f && special.windup <= 60.0f &&
           Finite(special.windupDuration) && special.windupDuration >= 0.0f &&
           special.windupDuration <= 60.0f &&
           special.windup <= special.windupDuration + 0.001f;
}

bool ValidFireMaterialId(std::string_view id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}

bool ValidFireSource(const ecs::FireSource& source) {
    const auto phase = static_cast<std::uint8_t>(source.phase);
    return ValidFireMaterialId(source.materialId) && phase <= 3 &&
           Finite(source.ignitionProgress) &&
           Finite(source.ignitionDuration) &&
           Finite(source.burnRemaining) && Finite(source.burnDuration) &&
           Finite(source.smokePerSecond) && Finite(source.heatRadius) &&
           Finite(source.smolderElapsed) &&
           Finite(source.spreadAccumulator) &&
           source.ignitionProgress >= 0.0f &&
           source.ignitionDuration >= 0.0f &&
           source.ignitionDuration <= 3600.0f &&
           source.ignitionProgress <= source.ignitionDuration + 0.001f &&
           source.burnDuration >= 0.0f &&
           source.burnDuration <= 1.0e7f && source.burnRemaining >= 0.0f &&
           source.burnRemaining <= source.burnDuration + 0.001f &&
           source.smokePerSecond >= 0.0f &&
           source.smokePerSecond <= 1.0e5f && source.heatRadius >= 0.0f &&
           source.heatRadius <= 1.0e5f && source.smolderElapsed >= 0.0f &&
           source.smolderElapsed <= 1.0e6f &&
           source.spreadAccumulator >= 0.0f &&
           source.spreadAccumulator <= 1.0e6f;
}

bool ValidCombatStance(const ecs::CombatStance& stance) {
    if (!Finite(stance.blockElapsed) || stance.blockElapsed < 0.0f ||
        stance.blockElapsed > 3600.0f || !Finite(stance.guardBreakLeft) ||
        stance.guardBreakLeft < 0.0f || stance.guardBreakLeft > 60.0f ||
        !Finite(stance.dodgeLeft) || stance.dodgeLeft < 0.0f ||
        stance.dodgeLeft > 60.0f || !Finite(stance.dodgeInvulnLeft) ||
        stance.dodgeInvulnLeft < 0.0f ||
        stance.dodgeInvulnLeft > stance.dodgeLeft + 0.001f ||
        !Finite(stance.dodgeDirX) || !Finite(stance.dodgeDirY)) {
        return false;
    }
    const float directionLength =
        std::sqrt(stance.dodgeDirX * stance.dodgeDirX +
                  stance.dodgeDirY * stance.dodgeDirY);
    if (!Finite(directionLength) || directionLength > 1.5f) {
        return false;
    }
    if (stance.dodgeLeft > 0.0f && directionLength < 0.5f) {
        return false;
    }
    return true;
}

bool ValidBurningZombie(const ecs::BurningZombie& burning) {
    return Finite(burning.remaining) && burning.remaining >= 0.0f &&
           burning.remaining <= 120.0f &&
           Finite(burning.damageAccumulator) &&
           burning.damageAccumulator >= 0.0f &&
           burning.damageAccumulator <= 3600.0f &&
           Finite(burning.spreadAccumulator) &&
           burning.spreadAccumulator >= 0.0f &&
           burning.spreadAccumulator <= 3600.0f &&
           Finite(burning.panicRemaining) && burning.panicRemaining >= 0.0f &&
           burning.panicRemaining <= 120.0f &&
           Finite(burning.panicSpeedScale) &&
           burning.panicSpeedScale >= 0.5f &&
           burning.panicSpeedScale <= 3.0f &&
           Finite(burning.panicChaseRadius) &&
           burning.panicChaseRadius >= 0.0f &&
           burning.panicChaseRadius <= 64.0f;
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

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
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

    bool U64(std::uint64_t* value) {
        if (!Has(8)) {
            return false;
        }
        std::uint64_t decoded = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            decoded |= static_cast<std::uint64_t>(m_bytes[m_offset + i])
                       << (i * 8U);
        }
        m_offset += 8;
        *value = decoded;
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

    bool String(std::string* value, std::size_t maxBytes,
                bool allowEmpty = false) {
        std::uint16_t size = 0;
        if (!U16(&size) || size > maxBytes || (!allowEmpty && size == 0) ||
            !Has(size)) {
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
                       std::uint64_t viewerStableId,
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

    const ecs::Equipment* equipment =
        registry.try_get<ecs::Equipment>(entity);
    if (equipment == nullptr) {
        if (const auto* companionState =
                registry.try_get<CompanionState>(entity);
            companionState != nullptr) {
            equipment = &companionState->equipment;
        }
    }
    if (equipment != nullptr) {
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

    if (const auto* companion = registry.try_get<Companion>(entity);
        companion != nullptr) {
        const auto* state = registry.try_get<CompanionState>(entity);
        if (state == nullptr) {
            SetError(error, "复制同伴缺少状态");
            return false;
        }
        if (!IsValidCompanionToken(companion->defId) ||
            companion->defId.size() > kMaxCompanionIdBytes ||
            companion->ownerStableId == 0 ||
            static_cast<std::uint8_t>(state->command) >
                static_cast<std::uint8_t>(CompanionCommand::Retreat) ||
            static_cast<std::uint8_t>(state->personalStatus) >
                static_cast<std::uint8_t>(CompanionQuestStatus::Failed) ||
            state->loyalty < 0 || state->loyalty > 100 ||
            state->personalStage < 0 ||
            state->personalStage >
                static_cast<int>(kMaxCompanionQuestStages) ||
            state->personalProgress < 0 || state->personalProgress > 1000000 ||
            state->personalRewardMask >
                (1u << kMaxCompanionQuestStages) - 1u ||
            !Finite(state->downedRemaining) ||
            state->downedRemaining < 0.0f ||
            state->downedRemaining > 3600.0f) {
            SetError(error, "复制同伴状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(companion->defId.size()));
        bytes.insert(bytes.end(), companion->defId.begin(),
                     companion->defId.end());
        PutU8(bytes, static_cast<std::uint8_t>(state->command));
        std::uint8_t flags = 0;
        if (state->standby) {
            flags |= 1u << 0;
        }
        if (state->downed) {
            flags |= 1u << 1;
        }
        if (state->dead) {
            flags |= 1u << 2;
        }
        PutU8(bytes, flags);
        PutU8(bytes, static_cast<std::uint8_t>(state->loyalty));
        PutU8(bytes, static_cast<std::uint8_t>(state->personalStatus));
        PutU16(bytes, static_cast<std::uint16_t>(state->personalStage));
        PutU32(bytes, static_cast<std::uint32_t>(state->personalProgress));
        PutU32(bytes, state->personalRewardMask);
        PutF32(bytes, state->downedRemaining);
        components->push_back(
            MakeComponent(kReplicationCompanion, std::move(bytes)));
    }

    if (const auto* stance = registry.try_get<ecs::CombatStance>(entity);
        stance != nullptr && registry.all_of<ecs::PlayerTag>(entity)) {
        if (!ValidCombatStance(*stance)) {
            SetError(error, "复制 CombatStance 状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, stance->blocking ? 1u : 0u);
        PutF32(bytes, stance->blockElapsed);
        PutF32(bytes, stance->guardBreakLeft);
        PutF32(bytes, stance->dodgeLeft);
        PutF32(bytes, stance->dodgeInvulnLeft);
        PutF32(bytes, stance->dodgeDirX);
        PutF32(bytes, stance->dodgeDirY);
        components->push_back(
            MakeComponent(kReplicationCombatStance, std::move(bytes)));
    }

    if (const auto* stealth = registry.try_get<ecs::StealthState>(entity);
        stealth != nullptr && registry.all_of<ecs::PlayerTag>(entity)) {
        std::uint8_t flags = stealth->crouched ? 1u : 0u;
        if (const auto* velocity = registry.try_get<ecs::Velocity>(entity);
            velocity != nullptr &&
            std::sqrt(velocity->x * velocity->x + velocity->y * velocity->y) >
                0.05f) {
            flags |= 2u;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, flags);
        components->push_back(
            MakeComponent(kReplicationStealth, std::move(bytes)));
    }

    if (const auto* storage = registry.try_get<ecs::PlayerStorage>(entity);
        storage != nullptr) {
        const auto* storageOwner =
            registry.try_get<ecs::StructureOwner>(entity);
        const bool storageVisible =
            storageOwner == nullptr || storageOwner->ownerStableId == 0 ||
            viewerStableId == 0 ||
            storageOwner->ownerStableId == viewerStableId;
        if (storageVisible) {
            const auto* container = registry.try_get<ecs::Container>(entity);
            if (container == nullptr || storage->maxStacks < 1 ||
                storage->maxStacks > 6 ||
                !Finite(container->capacityKg) ||
                container->capacityKg <= 0.0f ||
                container->items.size() >
                    static_cast<std::size_t>(storage->maxStacks)) {
                SetError(error, "复制 PlayerStorage 负载非法");
                return false;
            }
            std::vector<std::uint8_t> bytes;
            PutU8(bytes, static_cast<std::uint8_t>(storage->maxStacks));
            PutF32(bytes, container->capacityKg);
            PutU16(bytes,
                   static_cast<std::uint16_t>(container->items.size()));
            for (const ecs::ItemInstance& item : container->items) {
                if (!item.Valid() || item.defId.size() > 128 ||
                    item.count > 9999 || !Finite(item.condition) ||
                    item.condition < 0.0f || item.condition > 1.0f) {
                    SetError(error, "复制 PlayerStorage 物品状态非法");
                    return false;
                }
                PutU16(bytes,
                       static_cast<std::uint16_t>(item.defId.size()));
                bytes.insert(bytes.end(), item.defId.begin(), item.defId.end());
                PutU16(bytes, static_cast<std::uint16_t>(item.count));
                PutF32(bytes, item.condition);
            }
            if (bytes.size() > net::kMaxReplicationComponentBytes) {
                SetError(error, "复制 PlayerStorage 负载超过上限");
                return false;
            }
            components->push_back(
                MakeComponent(kReplicationStorage, std::move(bytes)));
        }
    }

    if (const auto* vehicle = registry.try_get<ecs::Vehicle>(entity);
        vehicle != nullptr) {
        const std::uint8_t vehicleType =
            static_cast<std::uint8_t>(vehicle->type);
        if (!Finite(vehicle->fuel) || vehicle->fuel < 0.0f ||
            vehicle->fuel > 1.0f || !Finite(vehicle->condition) ||
            vehicle->condition < 0.0f || vehicle->condition > 1.0f ||
            vehicleType > 2 || !Finite(vehicle->heading)) {
            SetError(error, "澶嶅埗 Vehicle 璐熻浇闈炴硶");
            return false;
        }
        const auto* driver = registry.try_get<ecs::VehicleDriver>(entity);
        const auto* passenger = registry.try_get<ecs::VehiclePassenger>(entity);
        std::vector<std::uint8_t> bytes;
        PutF32(bytes, vehicle->fuel);
        PutF32(bytes, vehicle->condition);
        PutU64(bytes, driver != nullptr ? driver->stableId : 0);
        PutU64(bytes, passenger != nullptr ? passenger->stableId : 0);
        PutU64(bytes,
               passenger != nullptr ? passenger->secondStableId : 0);
        const std::uint8_t flags =
            static_cast<std::uint8_t>((vehicle->headlights ? 1u : 0u) |
                                      (vehicleType << 1));
        PutU8(bytes, flags);
        PutF32(bytes, std::remainder(vehicle->heading,
                                     6.283185307179586f));
        components->push_back(
            MakeComponent(kReplicationVehicle, std::move(bytes)));

        if (const auto* mods =
                registry.try_get<ecs::VehicleMods>(entity);
            mods != nullptr) {
            std::vector<std::uint8_t> modBytes;
            for (const std::string& defId : mods->defIds) {
                if (defId.empty()) {
                    PutU16(modBytes, 0);
                    continue;
                }
                if (defId.size() > kMaxVehicleModIdBytes ||
                    !IsValidVehicleModToken(defId)) {
                    SetError(error, "复制 VehicleMods 组件非法");
                    return false;
                }
                PutU16(modBytes,
                       static_cast<std::uint16_t>(defId.size()));
                modBytes.insert(modBytes.end(), defId.begin(), defId.end());
            }
            components->push_back(
                MakeComponent(kReplicationVehicleMods, std::move(modBytes)));
        }
    }

    if (const auto* plot = registry.try_get<ecs::FarmPlot>(entity);
        plot != nullptr) {
        if (plot->cropId.size() > 64 || !Finite(plot->growthMinutes) ||
            !Finite(plot->waterMinutes) || !Finite(plot->dryMinutes) ||
            !Finite(plot->growMinutes) || plot->growthMinutes < 0.0f ||
            plot->waterMinutes < 0.0f || plot->dryMinutes < 0.0f ||
            plot->growMinutes <= 0.0f ||
            static_cast<std::uint8_t>(plot->stage) >
                static_cast<std::uint8_t>(ecs::CropStage::Withered) ||
            plot->harvestYield < 0 || plot->harvestYield > 9999) {
            SetError(error, "复制 FarmPlot 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(plot->cropId.size()));
        bytes.insert(bytes.end(), plot->cropId.begin(), plot->cropId.end());
        PutF32(bytes, plot->growthMinutes);
        PutF32(bytes, plot->waterMinutes);
        PutF32(bytes, plot->dryMinutes);
        PutF32(bytes, plot->growMinutes);
        PutU8(bytes, static_cast<std::uint8_t>(plot->stage));
        PutI32(bytes, plot->harvestYield);
        std::uint8_t flags = 0;
        if (plot->diseased) {
            flags |= 1u << 0;
        }
        if (plot->fertilized) {
            flags |= 1u << 1;
        }
        PutU8(bytes, flags);
        components->push_back(
            MakeComponent(kReplicationFarmPlot, std::move(bytes)));
    }

    if (const auto* generator = registry.try_get<ecs::Generator>(entity);
        generator != nullptr) {
        if (!Finite(generator->fuelMinutes) ||
            !Finite(generator->radius) || !Finite(generator->load) ||
            generator->fuelMinutes < 0.0f || generator->radius <= 0.0f ||
            generator->fuelMinutes > 1.0e7f || generator->radius > 1.0e5f ||
            generator->load < 0.0f || generator->load > 1.0e6f) {
            SetError(error, "复制 Generator 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutF32(bytes, generator->fuelMinutes);
        PutF32(bytes, generator->radius);
        PutU8(bytes, generator->running ? 1u : 0u);
        PutF32(bytes, generator->load);
        components->push_back(
            MakeComponent(kReplicationGenerator, std::move(bytes)));
    }

    if (const auto* storage = registry.try_get<ecs::ColdStorage>(entity);
        storage != nullptr) {
        components->push_back(MakeComponent(
            kReplicationColdStorage,
            {static_cast<std::uint8_t>(storage->powered ? 1u : 0u)}));
    }

    if (const auto* floodlight = registry.try_get<ecs::Floodlight>(entity);
        floodlight != nullptr) {
        components->push_back(MakeComponent(
            kReplicationFloodlight,
            {static_cast<std::uint8_t>(floodlight->on ? 1u : 0u),
             static_cast<std::uint8_t>(floodlight->powered ? 1u : 0u)}));
    }

    if (const auto* trap = registry.try_get<ecs::SpikeTrap>(entity);
        trap != nullptr) {
        if (!Finite(trap->cooldown) || !Finite(trap->cooldownSeconds) ||
            !Finite(trap->damage) || !Finite(trap->radius) ||
            !Finite(trap->staggerSeconds) || trap->cooldown < 0.0f ||
            trap->cooldownSeconds <= 0.0f || trap->damage < 0.0f ||
            trap->radius <= 0.0f || trap->staggerSeconds < 0.0f ||
            trap->uses < 0 || trap->uses > 9999) {
            SetError(error, "复制 SpikeTrap 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, trap->armed ? 1u : 0u);
        PutF32(bytes, trap->cooldown);
        PutF32(bytes, trap->cooldownSeconds);
        PutF32(bytes, trap->damage);
        PutF32(bytes, trap->radius);
        PutF32(bytes, trap->staggerSeconds);
        PutI32(bytes, trap->uses);
        components->push_back(MakeComponent(kReplicationTrap, std::move(bytes)));
    }

    if (const auto* placed = registry.try_get<ecs::PlacedTrap>(entity);
        placed != nullptr) {
        if (!IsValidTrapToken(placed->defId) || !Finite(placed->cooldown) ||
            placed->cooldown < 0.0f || placed->uses < 0 ||
            placed->uses > 999) {
            SetError(error, "复制 PlacedTrap 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(placed->defId.size()));
        bytes.insert(bytes.end(), placed->defId.begin(), placed->defId.end());
        PutU8(bytes, placed->armed ? 1u : 0u);
        PutF32(bytes, placed->cooldown);
        PutI32(bytes, placed->uses);
        PutU64(bytes, placed->owner);
        components->push_back(
            MakeComponent(kReplicationPlacedTrap, std::move(bytes)));
    }

    if (const auto* merchant = registry.try_get<ecs::MerchantStock>(entity);
        merchant != nullptr) {
        if (merchant->restockDay < 0 || merchant->restockDay > 1000000) {
            SetError(error, "复制 MerchantStock 负载非法");
            return false;
        }
        for (const int count : merchant->stock) {
            if (count < 0 || count > 9999) {
                SetError(error, "复制 MerchantStock 负载非法");
                return false;
            }
        }
        std::vector<std::uint8_t> bytes;
        for (const int count : merchant->stock) {
            PutI32(bytes, count);
        }
        PutI32(bytes, merchant->restockDay);
        components->push_back(
            MakeComponent(kReplicationMerchant, std::move(bytes)));
    }

    if (const auto* npc = registry.try_get<SettlementNpc>(entity);
        npc != nullptr) {
        if (npc->settlementId.empty() ||
            npc->settlementId.size() > kMaxSettlementStateIdBytes ||
            static_cast<std::uint8_t>(npc->kind) >
                static_cast<std::uint8_t>(SettlementNpcKind::Requester) ||
            static_cast<std::uint8_t>(npc->activity) >
                static_cast<std::uint8_t>(NpcActivity::Defend) ||
            !Finite(npc->homeX) || !Finite(npc->homeY) ||
            !Finite(npc->wanderRadius) || npc->wanderRadius < 0.0f ||
            npc->wanderRadius > 16.0f) {
            SetError(error, "复制 SettlementNpc 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(npc->settlementId.size()));
        bytes.insert(bytes.end(), npc->settlementId.begin(),
                     npc->settlementId.end());
        PutU8(bytes, static_cast<std::uint8_t>(npc->kind));
        PutU8(bytes, static_cast<std::uint8_t>(npc->activity));
        PutF32(bytes, npc->homeX);
        PutF32(bytes, npc->homeY);
        PutF32(bytes, npc->wanderRadius);
        components->push_back(
            MakeComponent(kReplicationSettlementNpc, std::move(bytes)));
    }

    if (const auto* loot = registry.try_get<ecs::LootContainer>(entity);
        loot != nullptr) {
        const auto* container = registry.try_get<ecs::Container>(entity);
        if (container == nullptr || loot->tableId.empty() ||
            loot->tableId.size() > 128 || !Finite(container->capacityKg) ||
            container->capacityKg <= 0.0f ||
            container->items.size() > kMaxLootReplicationItems) {
            SetError(error, "复制 LootContainer 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(loot->tableId.size()));
        bytes.insert(bytes.end(), loot->tableId.begin(), loot->tableId.end());
        PutU8(bytes, loot->searched ? 1u : 0u);
        PutF32(bytes, container->capacityKg);
        PutU16(bytes, static_cast<std::uint16_t>(container->items.size()));
        for (const ecs::ItemInstance& item : container->items) {
            if (!item.Valid() || item.defId.size() > 128 || item.count > 9999 ||
                !Finite(item.condition) || item.condition < 0.0f ||
                item.condition > 1.0f) {
                SetError(error, "复制 LootContainer 物品状态非法");
                return false;
            }
            PutU16(bytes, static_cast<std::uint16_t>(item.defId.size()));
            bytes.insert(bytes.end(), item.defId.begin(), item.defId.end());
            PutU16(bytes, static_cast<std::uint16_t>(item.count));
            PutF32(bytes, item.condition);
        }
        if (bytes.size() > net::kMaxReplicationComponentBytes) {
            SetError(error, "复制 LootContainer 负载超过上限");
            return false;
        }
        components->push_back(MakeComponent(kReplicationLoot, std::move(bytes)));
    }

    if (const auto* animal = registry.try_get<ecs::Animal>(entity);
        animal != nullptr) {
        if (animal->speciesId.empty() ||
            animal->speciesId.size() > kMaxReplicationAnimalSpeciesBytes ||
            static_cast<std::uint8_t>(animal->kind) >
                static_cast<std::uint8_t>(ecs::AnimalKind::Dangerous)) {
            SetError(error, "复制 Animal 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(animal->speciesId.size()));
        bytes.insert(bytes.end(), animal->speciesId.begin(),
                     animal->speciesId.end());
        PutU8(bytes, static_cast<std::uint8_t>(animal->kind));
        components->push_back(MakeComponent(kReplicationAnimal, std::move(bytes)));
    }

    if (const auto* corpse = registry.try_get<ecs::AnimalCorpse>(entity);
        corpse != nullptr) {
        if (corpse->speciesId.empty() ||
            corpse->speciesId.size() > kMaxReplicationAnimalSpeciesBytes) {
            SetError(error, "复制 AnimalCorpse 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU16(bytes, static_cast<std::uint16_t>(corpse->speciesId.size()));
        bytes.insert(bytes.end(), corpse->speciesId.begin(),
                     corpse->speciesId.end());
        PutU8(bytes, corpse->harvested ? 1u : 0u);
        components->push_back(
            MakeComponent(kReplicationAnimalCorpse, std::move(bytes)));
    }

    if (const auto* source = registry.try_get<ecs::FireSource>(entity);
        source != nullptr) {
        const auto* anchor = registry.try_get<ecs::FireAnchor>(entity);
        if (anchor == nullptr || !ValidFireSource(*source)) {
            SetError(error, "复制 FireSource 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutI32(bytes, anchor->x);
        PutI32(bytes, anchor->y);
        PutI32(bytes, anchor->z);
        PutU16(bytes, static_cast<std::uint16_t>(source->materialId.size()));
        bytes.insert(bytes.end(), source->materialId.begin(),
                     source->materialId.end());
        PutU8(bytes, static_cast<std::uint8_t>(source->phase));
        PutF32(bytes, source->ignitionProgress);
        PutF32(bytes, source->ignitionDuration);
        PutF32(bytes, source->burnRemaining);
        PutF32(bytes, source->burnDuration);
        PutF32(bytes, source->smokePerSecond);
        PutF32(bytes, source->heatRadius);
        PutF32(bytes, source->smolderElapsed);
        PutF32(bytes, source->spreadAccumulator);
        components->push_back(MakeComponent(kReplicationFire, std::move(bytes)));
    }

    if (const auto* burning = registry.try_get<ecs::BurningZombie>(entity);
        burning != nullptr) {
        if (!ValidBurningZombie(*burning)) {
            SetError(error, "复制 BurningZombie 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutF32(bytes, burning->remaining);
        PutF32(bytes, burning->damageAccumulator);
        PutF32(bytes, burning->spreadAccumulator);
        PutF32(bytes, burning->panicRemaining);
        PutF32(bytes, burning->panicSpeedScale);
        PutF32(bytes, burning->panicChaseRadius);
        components->push_back(
            MakeComponent(kReplicationBurning, std::move(bytes)));
    }

    if (const auto* action = registry.try_get<ecs::ActionState>(entity);
        action != nullptr) {
        if (static_cast<std::uint8_t>(action->action) >
                static_cast<std::uint8_t>(ecs::ActorAction::Howling) ||
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

    if (const auto* variant = registry.try_get<ecs::ZombieVariant>(entity);
        variant != nullptr) {
        if (static_cast<std::uint8_t>(variant->kind) >
                static_cast<std::uint8_t>(ecs::ZombieVariantKind::Armored) ||
            !Finite(variant->speedScale) || variant->speedScale < 0.25f ||
            variant->speedScale > 4.0f || !Finite(variant->damageScale) ||
            variant->damageScale < 0.25f || variant->damageScale > 4.0f ||
            !Finite(variant->visualScale) || variant->visualScale < 0.25f ||
            variant->visualScale > 4.0f) {
            SetError(error, "澶嶅埗 ZombieVariant 璐熻浇闈炴硶");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, static_cast<std::uint8_t>(variant->kind));
        PutF32(bytes, variant->speedScale);
        PutF32(bytes, variant->damageScale);
        PutF32(bytes, variant->visualScale);
        components->push_back(MakeComponent(kReplicationVariant, std::move(bytes)));
    }

    if (const auto* special = registry.try_get<ecs::ZombieSpecial>(entity);
        special != nullptr) {
        if (!ValidZombieSpecial(*special)) {
            SetError(error, "复制 ZombieSpecial 负载非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, static_cast<std::uint8_t>(special->kind));
        PutF32(bytes, special->cooldown);
        PutF32(bytes, special->windup);
        PutF32(bytes, special->windupDuration);
        PutU32(bytes, special->serial);
        components->push_back(MakeComponent(kReplicationSpecial, std::move(bytes)));
    }

    if (const auto* wounds = registry.try_get<ecs::Wounds>(entity); wounds != nullptr) {
        if (!ValidWounds(*wounds)) {
            SetError(error, "复制实体伤口状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, wounds->count);
        for (std::uint8_t i = 0; i < wounds->count; ++i) {
            const ecs::WoundEntry& entry = wounds->entries[i];
            PutU8(bytes, static_cast<std::uint8_t>(entry.source));
            PutF32(bytes, entry.severity);
            PutF32(bytes, entry.bleedRate);
            PutF32(bytes, entry.bleedRemaining);
            PutF32(bytes, entry.bleedDuration);
            PutU8(bytes, entry.bandaged ? 1u : 0u);
        }
        components->push_back(MakeComponent(kReplicationWounds, std::move(bytes)));
    }

    if (const auto* pain = registry.try_get<ecs::Pain>(entity); pain != nullptr) {
        if (!ValidPain(*pain)) {
            SetError(error, "复制实体疼痛状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutF32(bytes, pain->baseLevel);
        PutF32(bytes, pain->level);
        PutF32(bytes, pain->moveScale);
        PutF32(bytes, pain->attackCooldownScale);
        PutF32(bytes, pain->reliefStrength);
        PutF32(bytes, pain->reliefRemaining);
        PutF32(bytes, pain->reliefDuration);
        components->push_back(MakeComponent(kReplicationPain, std::move(bytes)));
    }

    if (const auto* infection = registry.try_get<ecs::Infection>(entity);
        infection != nullptr) {
        if (!ValidInfection(*infection)) {
            SetError(error, "复制实体感染状态非法");
            return false;
        }
        std::vector<std::uint8_t> bytes;
        PutU8(bytes, static_cast<std::uint8_t>(infection->stage));
        PutF32(bytes, infection->progress);
        PutF32(bytes, infection->slowStrength);
        PutF32(bytes, infection->slowRemaining);
        PutF32(bytes, infection->slowDuration);
        PutF32(bytes, infection->timeToNextStage);
        components->push_back(
            MakeComponent(kReplicationInfection, std::move(bytes)));
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
    if (!registry.Register(kReplicationAction, "ActionState", 9,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationCombatStance, "CombatStance", 25,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationStealth, "Stealth", 1,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationVariant, "ZombieVariant", 13,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationSpecial, "ZombieSpecial", 17,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationWounds, "Wounds", 73,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationPain, "Pain", 28,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationInfection, "Infection", 21,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationStorage, "PlayerStorage", 900,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationVehicle, "Vehicle", 40,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationFarmPlot, "FarmPlot", 96,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationGenerator, "Generator", 20,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationColdStorage, "ColdStorage", 1,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationFloodlight, "Floodlight", 2,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationTrap, "SpikeTrap", 32,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationPlacedTrap, "PlacedTrap", 96,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationMerchant, "MerchantStock", 24,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationSettlementNpc, "SettlementNpc", 80,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationFire, "FireSource", 128,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationBurning, "BurningZombie", 24,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationAnimal, "Animal", 67,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationAnimalCorpse, "AnimalCorpse", 67,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationCompanion, "Companion", 96,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    if (!registry.Register(kReplicationVehicleMods, "VehicleMods", 384,
                           kReplicationComponentVersion, error)) {
        return false;
    }
    return registry.Register(kReplicationLoot, "LootContainer",
                             net::kMaxReplicationComponentBytes,
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

    registry.view<ecs::StableId, ecs::Position>().each(
        [&](const entt::entity entity, const ecs::StableId& stableId,
            const ecs::Position& position) {
            if (!failure.empty()) {
                return;
            }
            const bool player = registry.all_of<ecs::PlayerTag>(entity);
            const bool settlementNpc =
                registry.all_of<SettlementNpc>(entity);
            const bool item = registry.all_of<ecs::ItemInstance>(entity);
            const bool effect = registry.all_of<ecs::TransientEffect>(entity);
            const bool projectile = registry.all_of<ecs::Projectile>(entity);
            const bool fire = registry.all_of<ecs::FireSource>(entity);
            if (fire && !registry.all_of<ecs::FireAnchor>(entity)) {
                return;
            }
            const bool animal = registry.all_of<ecs::Animal>(entity);
            const bool animalCorpse =
                registry.all_of<ecs::AnimalCorpse>(entity);
            const bool vehicle = registry.all_of<ecs::Vehicle>(entity);
            const bool prop =
                registry.all_of<ecs::PlayerStorage>(entity) ||
                registry.all_of<ecs::FarmPlot>(entity) ||
                registry.all_of<ecs::Generator>(entity) ||
                registry.all_of<ecs::ColdStorage>(entity) ||
                registry.all_of<ecs::Floodlight>(entity) ||
                registry.all_of<ecs::SpikeTrap>(entity) ||
                registry.all_of<ecs::PlacedTrap>(entity) ||
                registry.all_of<ecs::MerchantStock>(entity) ||
                registry.all_of<ecs::LootContainer>(entity) ||
                vehicle || animalCorpse || fire;
            if (!player && !item && !effect && !projectile && !prop &&
                !animal && !settlementNpc &&
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
                : prop       ? kReplicationArchetypeProp
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
            if (const auto* owner = registry.try_get<ecs::StructureOwner>(entity);
                owner != nullptr) {
                replicated.ownerStableId = owner->ownerStableId;
            }
            if (const auto* placed = registry.try_get<ecs::PlacedTrap>(entity);
                placed != nullptr && placed->owner != 0) {
                replicated.ownerStableId = placed->owner;
            }
            if (const auto* companion = registry.try_get<Companion>(entity);
                companion != nullptr) {
                replicated.ownerStableId = companion->ownerStableId;
            }
            if (!CaptureComponents(registry, entity, m_options.viewerStableId,
                                   &replicated.components, &failure)) {
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
                action > static_cast<std::uint8_t>(ecs::ActorAction::Howling) ||
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
        } else if (component.type == kReplicationCombatStance) {
            ecs::CombatStance stance;
            std::uint8_t blocking = 0;
            if (!reader.U8(&blocking) || blocking > 1 ||
                !reader.F32(&stance.blockElapsed) ||
                !reader.F32(&stance.guardBreakLeft) ||
                !reader.F32(&stance.dodgeLeft) ||
                !reader.F32(&stance.dodgeInvulnLeft) ||
                !reader.F32(&stance.dodgeDirX) ||
                !reader.F32(&stance.dodgeDirY) || !reader.AtEnd() ||
                !ValidCombatStance(stance)) {
                SetError(error, "复制 CombatStance 负载非法");
                return false;
            }
            stance.blocking = blocking != 0;
            decoded.combatStance = stance;
            decoded.hasCombatStance = true;
        } else if (component.type == kReplicationStealth) {
            std::uint8_t flags = 0;
            if (!reader.U8(&flags) || (flags & ~0x03u) != 0 ||
                !reader.AtEnd()) {
                SetError(error, "复制 Stealth 负载非法");
                return false;
            }
            decoded.stealthCrouched = (flags & 0x01u) != 0;
            decoded.stealthMoving = (flags & 0x02u) != 0;
            decoded.hasStealth = true;
        } else if (component.type == kReplicationVariant) {
            std::uint8_t kind = 0;
            if (!reader.U8(&kind) || !reader.F32(&decoded.variant.speedScale) ||
                !reader.F32(&decoded.variant.damageScale) ||
                !reader.F32(&decoded.variant.visualScale) || !reader.AtEnd() ||
                kind > static_cast<std::uint8_t>(ecs::ZombieVariantKind::Armored) ||
                !Finite(decoded.variant.speedScale) ||
                decoded.variant.speedScale < 0.25f ||
                decoded.variant.speedScale > 4.0f ||
                !Finite(decoded.variant.damageScale) ||
                decoded.variant.damageScale < 0.25f ||
                decoded.variant.damageScale > 4.0f ||
                !Finite(decoded.variant.visualScale) ||
                decoded.variant.visualScale < 0.25f ||
                decoded.variant.visualScale > 4.0f) {
                SetError(error, "复制 ZombieVariant 负载非法");
                return false;
            }
            decoded.variant.kind = static_cast<ecs::ZombieVariantKind>(kind);
            decoded.hasVariant = true;
        } else if (component.type == kReplicationSpecial) {
            std::uint8_t kind = 0;
            ecs::ZombieSpecial loaded;
            if (!reader.U8(&kind) || !reader.F32(&loaded.cooldown) ||
                !reader.F32(&loaded.windup) ||
                !reader.F32(&loaded.windupDuration) ||
                !reader.U32(&loaded.serial) || !reader.AtEnd() ||
                kind > static_cast<std::uint8_t>(
                           ecs::ZombieSpecialKind::Breaker) ||
                !ValidZombieSpecial(loaded)) {
                SetError(error, "复制 ZombieSpecial 负载非法");
                return false;
            }
            loaded.kind = static_cast<ecs::ZombieSpecialKind>(kind);
            decoded.special = loaded;
            decoded.hasSpecial = true;
        } else if (component.type == kReplicationWounds) {
            std::uint8_t count = 0;
            if (!reader.U8(&count) || count > ecs::kMaxWounds) {
                SetError(error, "复制 Wounds 负载非法");
                return false;
            }
            ecs::Wounds loaded;
            loaded.count = count;
            for (std::uint8_t i = 0; i < count; ++i) {
                std::uint8_t source = 0;
                std::uint8_t bandaged = 0;
                ecs::WoundEntry& entry = loaded.entries[i];
                if (!reader.U8(&source) || !reader.F32(&entry.severity) ||
                    !reader.F32(&entry.bleedRate) ||
                    !reader.F32(&entry.bleedRemaining) ||
                    !reader.F32(&entry.bleedDuration) || !reader.U8(&bandaged) ||
                    bandaged > 1) {
                    SetError(error, "复制 Wounds 负载非法");
                    return false;
                }
                entry.source = static_cast<ecs::DamageType>(source);
                entry.bandaged = bandaged != 0;
            }
            if (!reader.AtEnd() || !ValidWounds(loaded)) {
                SetError(error, "复制 Wounds 负载非法");
                return false;
            }
            decoded.wounds = loaded;
            decoded.hasWounds = true;
        } else if (component.type == kReplicationPain) {
            ecs::Pain loaded;
            if (!reader.F32(&loaded.baseLevel) || !reader.F32(&loaded.level) ||
                !reader.F32(&loaded.moveScale) ||
                !reader.F32(&loaded.attackCooldownScale) ||
                !reader.F32(&loaded.reliefStrength) ||
                !reader.F32(&loaded.reliefRemaining) ||
                !reader.F32(&loaded.reliefDuration) || !reader.AtEnd() ||
                !ValidPain(loaded)) {
                SetError(error, "复制 Pain 负载非法");
                return false;
            }
            decoded.pain = loaded;
            decoded.hasPain = true;
        } else if (component.type == kReplicationInfection) {
            std::uint8_t stage = 0;
            ecs::Infection loaded;
            if (!reader.U8(&stage) || !reader.F32(&loaded.progress) ||
                !reader.F32(&loaded.slowStrength) ||
                !reader.F32(&loaded.slowRemaining) ||
                !reader.F32(&loaded.slowDuration) ||
                !reader.F32(&loaded.timeToNextStage) || !reader.AtEnd() ||
                stage > static_cast<std::uint8_t>(ecs::InfectionStage::Severe)) {
                SetError(error, "复制 Infection 负载非法");
                return false;
            }
            loaded.stage = static_cast<ecs::InfectionStage>(stage);
            if (!ValidInfection(loaded)) {
                SetError(error, "复制 Infection 负载非法");
                return false;
            }
            decoded.infection = loaded;
            decoded.hasInfection = true;
        } else if (component.type == kReplicationStorage) {
            std::uint8_t maxStacks = 0;
            std::uint16_t itemCount = 0;
            ecs::Container container;
            if (!reader.U8(&maxStacks) || maxStacks < 1 || maxStacks > 6 ||
                !reader.F32(&container.capacityKg) ||
                !Finite(container.capacityKg) ||
                container.capacityKg <= 0.0f ||
                !reader.U16(&itemCount) ||
                itemCount > static_cast<std::uint16_t>(maxStacks)) {
                SetError(error, "复制 PlayerStorage 负载非法");
                return false;
            }
            container.items.reserve(itemCount);
            for (std::uint16_t i = 0; i < itemCount; ++i) {
                ecs::ItemInstance item;
                std::uint16_t count = 0;
                if (!reader.String(&item.defId, 128) ||
                    !reader.U16(&count) || !reader.F32(&item.condition) ||
                    count == 0 || count > 9999 ||
                    !Finite(item.condition) || item.condition < 0.0f ||
                    item.condition > 1.0f) {
                    SetError(error, "复制 PlayerStorage 物品状态非法");
                    return false;
                }
                item.count = static_cast<int>(count);
                container.items.push_back(std::move(item));
            }
            if (!reader.AtEnd()) {
                SetError(error, "复制 PlayerStorage 负载非法");
                return false;
            }
            decoded.storage.maxStacks = static_cast<int>(maxStacks);
            decoded.storageContainer = std::move(container);
            decoded.hasStorage = true;
        } else if (component.type == kReplicationVehicle) {
            std::uint8_t flags = 0;
            if (!reader.F32(&decoded.vehicle.fuel) ||
                !reader.F32(&decoded.vehicle.condition) ||
                !reader.U64(&decoded.vehicleDriverStableId) ||
                !reader.U64(&decoded.vehiclePassengerStableId) ||
                !reader.U64(&decoded.vehicleSecondPassengerStableId) ||
                !reader.U8(&flags) ||
                !reader.F32(&decoded.vehicle.heading) || !reader.AtEnd() ||
                (flags & ~0x07u) != 0 ||
                ((flags >> 1) & 0x03u) > 2 ||
                !Finite(decoded.vehicle.fuel) ||
                decoded.vehicle.fuel < 0.0f || decoded.vehicle.fuel > 1.0f ||
                !Finite(decoded.vehicle.condition) ||
                decoded.vehicle.condition < 0.0f ||
                decoded.vehicle.condition > 1.0f ||
                !Finite(decoded.vehicle.heading) ||
                (decoded.vehicleDriverStableId != 0 &&
                 (decoded.vehicleDriverStableId ==
                      decoded.vehiclePassengerStableId ||
                  decoded.vehicleDriverStableId ==
                      decoded.vehicleSecondPassengerStableId)) ||
                (decoded.vehiclePassengerStableId != 0 &&
                 decoded.vehiclePassengerStableId ==
                     decoded.vehicleSecondPassengerStableId) ||
                (decoded.vehiclePassengerStableId == 0 &&
                 decoded.vehicleSecondPassengerStableId != 0)) {
                SetError(error, "复制 Vehicle 负载非法");
                return false;
            }
            decoded.vehicle.headlights = (flags & 0x01u) != 0;
            decoded.vehicle.type =
                static_cast<ecs::VehicleType>((flags >> 1) & 0x03u);
            decoded.vehicle.heading = std::remainder(
                decoded.vehicle.heading, 6.283185307179586f);
            decoded.hasVehicle = true;
        } else if (component.type == kReplicationVehicleMods) {
            VehicleModsReplicationState mods;
            for (std::size_t i = 0; i < mods.defIds.size(); ++i) {
                if (!reader.String(&mods.defIds[i], kMaxVehicleModIdBytes,
                                   true) ||
                    (!mods.defIds[i].empty() &&
                     !IsValidVehicleModToken(mods.defIds[i]))) {
                    SetError(error, "复制 VehicleMods 组件非法");
                    return false;
                }
            }
            if (!reader.AtEnd()) {
                SetError(error, "复制 VehicleMods 组件非法");
                return false;
            }
            decoded.vehicleMods = std::move(mods);
            decoded.hasVehicleMods = true;
        } else if (component.type == kReplicationFarmPlot) {
            ecs::FarmPlot loaded;
            std::uint8_t stage = 0;
            std::uint8_t flags = 0;
            if (!reader.String(&loaded.cropId, 64, true) ||
                !reader.F32(&loaded.growthMinutes) ||
                !reader.F32(&loaded.waterMinutes) ||
                !reader.F32(&loaded.dryMinutes) ||
                !reader.F32(&loaded.growMinutes) ||
                !reader.U8(&stage) || !reader.I32(&loaded.harvestYield) ||
                !reader.U8(&flags) || (flags & ~0x03u) != 0 ||
                !reader.AtEnd() || !Finite(loaded.growthMinutes) ||
                !Finite(loaded.waterMinutes) || !Finite(loaded.dryMinutes) ||
                !Finite(loaded.growMinutes) || loaded.growthMinutes < 0.0f ||
                loaded.waterMinutes < 0.0f || loaded.dryMinutes < 0.0f ||
                loaded.growMinutes <= 0.0f ||
                stage > static_cast<std::uint8_t>(ecs::CropStage::Withered) ||
                loaded.harvestYield < 0 || loaded.harvestYield > 9999) {
                SetError(error, "复制 FarmPlot 负载非法");
                return false;
            }
            loaded.stage = static_cast<ecs::CropStage>(stage);
            loaded.diseased = (flags & 0x01u) != 0;
            loaded.fertilized = (flags & 0x02u) != 0;
            decoded.farmPlot = std::move(loaded);
            decoded.hasFarmPlot = true;
        } else if (component.type == kReplicationGenerator) {
            std::uint8_t running = 0;
            if (!reader.F32(&decoded.generator.fuelMinutes) ||
                !reader.F32(&decoded.generator.radius) ||
                !reader.U8(&running) || running > 1 ||
                !reader.F32(&decoded.generator.load) || !reader.AtEnd() ||
                !Finite(decoded.generator.fuelMinutes) ||
                !Finite(decoded.generator.radius) ||
                !Finite(decoded.generator.load) ||
                decoded.generator.fuelMinutes < 0.0f ||
                decoded.generator.fuelMinutes > 1.0e7f ||
                decoded.generator.radius <= 0.0f ||
                decoded.generator.radius > 1.0e5f ||
                decoded.generator.load < 0.0f ||
                decoded.generator.load > 1.0e6f) {
                SetError(error, "复制 Generator 负载非法");
                return false;
            }
            decoded.generator.running = running != 0;
            decoded.hasGenerator = true;
        } else if (component.type == kReplicationColdStorage) {
            std::uint8_t powered = 0;
            if (!reader.U8(&powered) || powered > 1 || !reader.AtEnd()) {
                SetError(error, "复制 ColdStorage 负载非法");
                return false;
            }
            decoded.coldStorage.powered = powered != 0;
            decoded.hasColdStorage = true;
        } else if (component.type == kReplicationFloodlight) {
            std::uint8_t on = 0;
            std::uint8_t powered = 0;
            if (!reader.U8(&on) || on > 1 || !reader.U8(&powered) ||
                powered > 1 || !reader.AtEnd()) {
                SetError(error, "复制 Floodlight 负载非法");
                return false;
            }
            decoded.floodlight.on = on != 0;
            decoded.floodlight.powered = powered != 0;
            decoded.hasFloodlight = true;
        } else if (component.type == kReplicationTrap) {
            std::uint8_t armed = 0;
            if (!reader.U8(&armed) || armed > 1 ||
                !reader.F32(&decoded.trap.cooldown) ||
                !reader.F32(&decoded.trap.cooldownSeconds) ||
                !reader.F32(&decoded.trap.damage) ||
                !reader.F32(&decoded.trap.radius) ||
                !reader.F32(&decoded.trap.staggerSeconds) ||
                !reader.I32(&decoded.trap.uses) || !reader.AtEnd() ||
                !Finite(decoded.trap.cooldown) ||
                !Finite(decoded.trap.cooldownSeconds) ||
                !Finite(decoded.trap.damage) ||
                !Finite(decoded.trap.radius) ||
                !Finite(decoded.trap.staggerSeconds) ||
                decoded.trap.cooldown < 0.0f ||
                decoded.trap.cooldownSeconds <= 0.0f ||
                decoded.trap.damage < 0.0f || decoded.trap.radius <= 0.0f ||
                decoded.trap.staggerSeconds < 0.0f ||
                decoded.trap.uses < 0 || decoded.trap.uses > 9999) {
                SetError(error, "复制 SpikeTrap 负载非法");
                return false;
            }
            decoded.trap.armed = armed != 0;
            decoded.hasTrap = true;
        } else if (component.type == kReplicationPlacedTrap) {
            std::uint8_t armed = 0;
            if (!reader.String(&decoded.placedTrap.defId, 64, true) ||
                !IsValidTrapToken(decoded.placedTrap.defId) ||
                !reader.U8(&armed) || armed > 1 ||
                !reader.F32(&decoded.placedTrap.cooldown) ||
                !reader.I32(&decoded.placedTrap.uses) ||
                !reader.U64(&decoded.placedTrap.owner) || !reader.AtEnd() ||
                !Finite(decoded.placedTrap.cooldown) ||
                decoded.placedTrap.cooldown < 0.0f ||
                decoded.placedTrap.uses < 0 ||
                decoded.placedTrap.uses > 999) {
                SetError(error, "复制 PlacedTrap 负载非法");
                return false;
            }
            decoded.placedTrap.armed = armed != 0;
            decoded.hasPlacedTrap = true;
        } else if (component.type == kReplicationMerchant) {
            ecs::MerchantStock loaded;
            for (int& count : loaded.stock) {
                if (!reader.I32(&count) || count < 0 || count > 9999) {
                    SetError(error, "复制 MerchantStock 负载非法");
                    return false;
                }
            }
            if (!reader.I32(&loaded.restockDay) ||
                loaded.restockDay < 0 || loaded.restockDay > 1000000 ||
                !reader.AtEnd()) {
                SetError(error, "复制 MerchantStock 负载非法");
                return false;
            }
            decoded.merchantStock = loaded;
            decoded.hasMerchantStock = true;
        } else if (component.type == kReplicationSettlementNpc) {
            SettlementNpc npc;
            std::uint8_t kind = 0;
            std::uint8_t activity = 0;
            if (!reader.String(&npc.settlementId,
                               kMaxSettlementStateIdBytes, false) ||
                !reader.U8(&kind) || kind > 2 || !reader.U8(&activity) ||
                activity > static_cast<std::uint8_t>(NpcActivity::Defend) ||
                !reader.F32(&npc.homeX) || !reader.F32(&npc.homeY) ||
                !reader.F32(&npc.wanderRadius) ||
                !Finite(npc.homeX) || !Finite(npc.homeY) ||
                !Finite(npc.wanderRadius) || npc.wanderRadius < 0.0f ||
                npc.wanderRadius > 16.0f || !reader.AtEnd()) {
                SetError(error, "复制 SettlementNpc 负载非法");
                return false;
            }
            npc.kind = static_cast<SettlementNpcKind>(kind);
            npc.activity = static_cast<NpcActivity>(activity);
            decoded.settlementNpc = std::move(npc);
            decoded.hasSettlementNpc = true;
        } else if (component.type == kReplicationAnimal) {
            std::uint8_t kind = 0;
            if (!reader.String(&decoded.animal.speciesId,
                               kMaxReplicationAnimalSpeciesBytes, false) ||
                !reader.U8(&kind) || !reader.AtEnd() ||
                kind > static_cast<std::uint8_t>(ecs::AnimalKind::Dangerous)) {
                SetError(error, "复制 Animal 负载非法");
                return false;
            }
            decoded.animal.kind = static_cast<ecs::AnimalKind>(kind);
            decoded.hasAnimal = true;
        } else if (component.type == kReplicationAnimalCorpse) {
            std::uint8_t harvested = 0;
            if (!reader.String(&decoded.animalCorpse.speciesId,
                               kMaxReplicationAnimalSpeciesBytes, false) ||
                !reader.U8(&harvested) || harvested > 1 || !reader.AtEnd()) {
                SetError(error, "复制 AnimalCorpse 负载非法");
                return false;
            }
            decoded.animalCorpse.harvested = harvested != 0;
            decoded.hasAnimalCorpse = true;
        } else if (component.type == kReplicationCompanion) {
            CompanionReplicationState companion;
            std::uint8_t command = 0;
            std::uint8_t flags = 0;
            std::uint8_t loyalty = 0;
            std::uint8_t status = 0;
            std::uint16_t stage = 0;
            std::uint32_t progress = 0;
            if (!reader.String(&companion.defId, kMaxCompanionIdBytes,
                               false) ||
                !IsValidCompanionToken(companion.defId) ||
                !reader.U8(&command) ||
                command > static_cast<std::uint8_t>(
                              CompanionCommand::Retreat) ||
                !reader.U8(&flags) || (flags & ~0x07u) != 0 ||
                !reader.U8(&loyalty) || loyalty > 100 ||
                !reader.U8(&status) ||
                status > static_cast<std::uint8_t>(
                             CompanionQuestStatus::Failed) ||
                !reader.U16(&stage) ||
                stage > kMaxCompanionQuestStages ||
                !reader.U32(&progress) || progress > 1000000 ||
                !reader.U32(&companion.rewardMask) ||
                companion.rewardMask >
                    (1u << kMaxCompanionQuestStages) - 1u ||
                !reader.F32(&companion.downedRemaining) ||
                !Finite(companion.downedRemaining) ||
                companion.downedRemaining < 0.0f ||
                companion.downedRemaining > 3600.0f || !reader.AtEnd()) {
                SetError(error, "复制 Companion 负载非法");
                return false;
            }
            companion.command = static_cast<CompanionCommand>(command);
            companion.standby = (flags & 0x01u) != 0;
            companion.downed = (flags & 0x02u) != 0;
            companion.dead = (flags & 0x04u) != 0;
            companion.loyalty = static_cast<int>(loyalty);
            companion.questStatus =
                static_cast<CompanionQuestStatus>(status);
            companion.questStage = static_cast<int>(stage);
            companion.questProgress = static_cast<int>(progress);
            decoded.companion = std::move(companion);
            decoded.hasCompanion = true;
        } else if (component.type == kReplicationLoot) {
            ecs::LootContainer loot;
            ecs::Container container;
            std::uint8_t searched = 0;
            std::uint16_t itemCount = 0;
            if (!reader.String(&loot.tableId, 128, false) ||
                !reader.U8(&searched) || searched > 1 ||
                !reader.F32(&container.capacityKg) ||
                !Finite(container.capacityKg) ||
                container.capacityKg <= 0.0f ||
                !reader.U16(&itemCount) || itemCount > kMaxLootReplicationItems) {
                SetError(error, "复制 LootContainer 负载非法");
                return false;
            }
            container.items.reserve(itemCount);
            for (std::uint16_t i = 0; i < itemCount; ++i) {
                ecs::ItemInstance item;
                std::uint16_t count = 0;
                if (!reader.String(&item.defId, 128, false) ||
                    !reader.U16(&count) || count == 0 || count > 9999 ||
                    !reader.F32(&item.condition) ||
                    !Finite(item.condition) || item.condition < 0.0f ||
                    item.condition > 1.0f) {
                    SetError(error, "复制 LootContainer 物品状态非法");
                    return false;
                }
                item.count = static_cast<int>(count);
                container.items.push_back(std::move(item));
            }
            if (!reader.AtEnd()) {
                SetError(error, "复制 LootContainer 负载非法");
                return false;
            }
            loot.searched = searched != 0;
            decoded.lootContainer = std::move(loot);
            decoded.lootContainerItems = std::move(container);
            decoded.hasLoot = true;
        } else if (component.type == kReplicationFire) {
            ecs::FireSource source;
            std::uint8_t phase = 0;
            if (!reader.I32(&decoded.fireAnchor.x) ||
                !reader.I32(&decoded.fireAnchor.y) ||
                !reader.I32(&decoded.fireAnchor.z) ||
                !reader.String(&source.materialId, 64, false) ||
                !reader.U8(&phase) ||
                !reader.F32(&source.ignitionProgress) ||
                !reader.F32(&source.ignitionDuration) ||
                !reader.F32(&source.burnRemaining) ||
                !reader.F32(&source.burnDuration) ||
                !reader.F32(&source.smokePerSecond) ||
                !reader.F32(&source.heatRadius) ||
                !reader.F32(&source.smolderElapsed) ||
                !reader.F32(&source.spreadAccumulator) || phase > 3 ||
                decoded.fireAnchor.x < -1000000 ||
                decoded.fireAnchor.x > 1000000 ||
                decoded.fireAnchor.y < -1000000 ||
                decoded.fireAnchor.y > 1000000 ||
                decoded.fireAnchor.z < 0 || decoded.fireAnchor.z >= 16 ||
                !reader.AtEnd()) {
                SetError(error, "复制 FireSource 负载非法");
                return false;
            }
            source.phase = static_cast<ecs::FirePhase>(phase);
            if (!ValidFireSource(source)) {
                SetError(error, "复制 FireSource 负载非法");
                return false;
            }
            decoded.fireSource = std::move(source);
            decoded.hasFire = true;
        } else if (component.type == kReplicationBurning) {
            ecs::BurningZombie burning;
            if (!reader.F32(&burning.remaining) ||
                !reader.F32(&burning.damageAccumulator) ||
                !reader.F32(&burning.spreadAccumulator) ||
                !reader.F32(&burning.panicRemaining) ||
                !reader.F32(&burning.panicSpeedScale) ||
                !reader.F32(&burning.panicChaseRadius) ||
                !reader.AtEnd() || !ValidBurningZombie(burning)) {
                SetError(error, "复制 BurningZombie 负载非法");
                return false;
            }
            decoded.burning = burning;
            decoded.hasBurning = true;
        } else {
            SetError(error, "复制实体包含未知组件类型");
            return false;
        }
    }
    if (decoded.hasCompanion && decoded.ownerStableId == 0) {
        SetError(error, "复制同伴缺少 owner");
        return false;
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
    if (patch.hasCombatStance) {
        actor.combatStance = patch.combatStance;
        actor.hasCombatStance = true;
    }
    if (patch.hasStealth) {
        actor.stealthCrouched = patch.stealthCrouched;
        actor.stealthMoving = patch.stealthMoving;
        actor.hasStealth = true;
    }
    if (patch.hasVariant) {
        actor.variant = patch.variant;
        actor.hasVariant = true;
    }
    if (patch.hasSpecial) {
        actor.special = patch.special;
        actor.hasSpecial = true;
    }
    if (patch.hasWounds) {
        actor.wounds = patch.wounds;
        actor.hasWounds = true;
    }
    if (patch.hasPain) {
        actor.pain = patch.pain;
        actor.hasPain = true;
    }
    if (patch.hasInfection) {
        actor.infection = patch.infection;
        actor.hasInfection = true;
    }
    if (patch.hasStorage) {
        actor.storage = patch.storage;
        actor.storageContainer = patch.storageContainer;
        actor.hasStorage = true;
    }
    if (patch.hasVehicle) {
        actor.vehicle = patch.vehicle;
        actor.vehicleDriverStableId = patch.vehicleDriverStableId;
        actor.vehiclePassengerStableId = patch.vehiclePassengerStableId;
        actor.vehicleSecondPassengerStableId =
            patch.vehicleSecondPassengerStableId;
        actor.hasVehicle = true;
    }
    if (patch.hasVehicleMods) {
        actor.vehicleMods = patch.vehicleMods;
        actor.hasVehicleMods = true;
    }
    if (patch.hasFarmPlot) {
        actor.farmPlot = patch.farmPlot;
        actor.hasFarmPlot = true;
    }
    if (patch.hasGenerator) {
        actor.generator = patch.generator;
        actor.hasGenerator = true;
    }
    if (patch.hasColdStorage) {
        actor.coldStorage = patch.coldStorage;
        actor.hasColdStorage = true;
    }
    if (patch.hasFloodlight) {
        actor.floodlight = patch.floodlight;
        actor.hasFloodlight = true;
    }
    if (patch.hasTrap) {
        actor.trap = patch.trap;
        actor.hasTrap = true;
    }
    if (patch.hasPlacedTrap) {
        actor.placedTrap = patch.placedTrap;
        actor.hasPlacedTrap = true;
    }
    if (patch.hasMerchantStock) {
        actor.merchantStock = patch.merchantStock;
        actor.hasMerchantStock = true;
    }
    if (patch.hasSettlementNpc) {
        actor.settlementNpc = patch.settlementNpc;
        actor.hasSettlementNpc = true;
    }
    if (patch.hasLoot) {
        actor.lootContainer = patch.lootContainer;
        actor.lootContainerItems = patch.lootContainerItems;
        actor.hasLoot = true;
    }
    if (patch.hasFire) {
        actor.fireSource = patch.fireSource;
        actor.fireAnchor = patch.fireAnchor;
        actor.hasFire = true;
    }
    if (patch.hasBurning) {
        actor.burning = patch.burning;
        actor.hasBurning = true;
    }
    if (patch.hasAnimal) {
        actor.animal = patch.animal;
        actor.hasAnimal = true;
    }
    if (patch.hasAnimalCorpse) {
        actor.animalCorpse = patch.animalCorpse;
        actor.hasAnimalCorpse = true;
    }
    if (patch.hasCompanion) {
        actor.companion = patch.companion;
        actor.hasCompanion = true;
    }
    if (actor.hasAnim) {
        // M14-T3：clip 不进入复制协议；远端用已复制的速度/动作/生命重新推导。
        actor.anim.clip = characters::AnimationSystem::SelectClip(
            actor.hasVelocity ? &actor.velocity : nullptr,
            actor.hasAction ? &actor.action : nullptr,
            actor.hasHealth ? &actor.health : nullptr);
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
