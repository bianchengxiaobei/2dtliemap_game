#include "save/ComponentRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "characters/components/CharacterComponents.h"
#include "ecs/components/CommonComponents.h"
#include "items/ItemComponents.h"
#include "save/BinaryReader.h"
#include "save/BinaryWriter.h"

namespace zl::save {
namespace {

constexpr std::uint32_t kMaxItemDefIdBytes = 64;
constexpr int kMaxItemStack = 9999;
constexpr std::uint32_t kMinItemBytes = 12;

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

bool Finite(float value) {
    return std::isfinite(value);
}

bool ValidItem(const ecs::ItemInstance& item) {
    return !item.defId.empty() &&
           item.defId.size() <= kMaxItemDefIdBytes &&
           item.count >= 1 && item.count <= kMaxItemStack &&
           Finite(item.condition) && item.condition >= 0.0f &&
           item.condition <= 1.0f;
}

bool WriteItem(const ecs::ItemInstance& item, BinaryWriter& writer) {
    if (!ValidItem(item)) {
        return false;
    }
    return writer.WriteString(item.defId, kMaxItemDefIdBytes) &&
           writer.Write(static_cast<std::int32_t>(item.count)) &&
           writer.Write(item.condition);
}

bool ReadItem(BinaryReader& reader, ecs::ItemInstance* item) {
    std::string defId;
    std::int32_t count = 0;
    float condition = 0.0f;
    if (!reader.ReadString(defId, kMaxItemDefIdBytes) || !reader.Read(count) ||
        !reader.Read(condition)) {
        return false;
    }
    if (defId.empty() || count < 1 || count > kMaxItemStack ||
        !Finite(condition) || condition < 0.0f || condition > 1.0f) {
        return false;
    }
    item->defId = std::move(defId);
    item->count = static_cast<int>(count);
    item->condition = condition;
    return true;
}

bool WriteContainer(const ecs::Container& container, BinaryWriter& writer) {
    if (!Finite(container.capacityKg) || container.capacityKg < 0.0f ||
        container.items.size() > kMaxContainerElements) {
        return false;
    }
    if (!writer.Write(container.capacityKg) ||
        !writer.Write(static_cast<std::uint32_t>(container.items.size()))) {
        return false;
    }
    for (const ecs::ItemInstance& item : container.items) {
        if (!WriteItem(item, writer)) {
            return false;
        }
    }
    return true;
}

bool ReadContainer(BinaryReader& reader, ecs::Container* container) {
    float capacity = 0.0f;
    std::uint32_t count = 0;
    if (!reader.Read(capacity) || !reader.Read(count)) {
        return false;
    }
    if (!Finite(capacity) || capacity < 0.0f ||
        count > kMaxContainerElements ||
        count > reader.RemainingInSegment() / kMinItemBytes) {
        return false;
    }
    ecs::Container loaded;
    loaded.capacityKg = capacity;
    loaded.items.reserve(std::min<std::uint32_t>(count, 4096));
    for (std::uint32_t i = 0; i < count; ++i) {
        ecs::ItemInstance item;
        if (!ReadItem(reader, &item)) {
            return false;
        }
        loaded.items.push_back(std::move(item));
    }
    *container = std::move(loaded);
    return true;
}

bool WriteEquipment(const ecs::Equipment& equipment, BinaryWriter& writer) {
    for (const auto& slot : equipment.slots) {
        if (!writer.Write(slot.has_value())) {
            return false;
        }
        if (slot.has_value() && !WriteItem(*slot, writer)) {
            return false;
        }
    }
    return true;
}

bool ReadEquipment(BinaryReader& reader, ecs::Equipment* equipment) {
    ecs::Equipment loaded;
    for (auto& slot : loaded.slots) {
        bool present = false;
        if (!reader.Read(present)) {
            return false;
        }
        if (!present) {
            slot.reset();
            continue;
        }
        ecs::ItemInstance item;
        if (!ReadItem(reader, &item)) {
            return false;
        }
        slot = std::move(item);
    }
    *equipment = std::move(loaded);
    return true;
}

bool ValidHealth(const ecs::Health& health) {
    return Finite(health.current) && Finite(health.max) &&
           health.max > 0.0f && health.current >= 0.0f &&
           health.current <= health.max;
}

bool ValidNeeds(const ecs::Needs& needs) {
    return Finite(needs.hunger) && Finite(needs.thirst) &&
           Finite(needs.fatigue) && needs.hunger >= 0.0f &&
           needs.hunger <= 1.0f && needs.thirst >= 0.0f &&
           needs.thirst <= 1.0f && needs.fatigue >= 0.0f &&
           needs.fatigue <= 1.0f;
}

}  // namespace

bool ComponentRegistry::Register(ComponentCodec codec, std::string* error) {
    if (codec.type == 0) {
        return Fail(error, "组件类型 ID 不能为 0");
    }
    if (codec.name.empty()) {
        return Fail(error, "组件名称不能为空");
    }
    if (codec.version == 0) {
        return Fail(error, "组件版本不能为 0");
    }
    if (!codec.has || !codec.write || !codec.read) {
        return Fail(error, "组件注册缺少 has/write/read");
    }
    if (m_byType.contains(codec.type)) {
        return Fail(error, "组件类型重复注册: " + codec.name);
    }
    m_byType.emplace(codec.type, m_codecs.size());
    m_codecs.push_back(std::move(codec));
    return true;
}

const ComponentCodec* ComponentRegistry::Find(std::uint32_t type) const {
    const auto it = m_byType.find(type);
    return it == m_byType.end() ? nullptr : &m_codecs[it->second];
}

bool RegisterBuiltinComponents(ComponentRegistry& registry, std::string* error) {
    if (!registry.Register<ecs::Position>(
            kSaveComponentPosition, "Position",
            [](const ecs::Position& position, BinaryWriter& writer) {
                return Finite(position.x) && Finite(position.y) &&
                       writer.Write(position.x) && writer.Write(position.y) &&
                       writer.Write(position.z);
            },
            [](BinaryReader& reader, ecs::Position* position) {
                float x = 0.0f;
                float y = 0.0f;
                int z = 0;
                if (!reader.Read(x) || !reader.Read(y) || !reader.Read(z) ||
                    !Finite(x) || !Finite(y)) {
                    return false;
                }
                *position = ecs::Position{x, y, z};
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Velocity>(
            kSaveComponentVelocity, "Velocity",
            [](const ecs::Velocity& velocity, BinaryWriter& writer) {
                return Finite(velocity.x) && Finite(velocity.y) &&
                       writer.Write(velocity.x) && writer.Write(velocity.y);
            },
            [](BinaryReader& reader, ecs::Velocity* velocity) {
                float x = 0.0f;
                float y = 0.0f;
                if (!reader.Read(x) || !reader.Read(y) || !Finite(x) ||
                    !Finite(y)) {
                    return false;
                }
                *velocity = ecs::Velocity{x, y};
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::SpriteRef>(
            kSaveComponentSprite, "SpriteRef",
            [](const ecs::SpriteRef& sprite, BinaryWriter& writer) {
                return writer.Write(sprite.sprite) && writer.Write(sprite.layer) &&
                       writer.Write(sprite.tint);
            },
            [](BinaryReader& reader, ecs::SpriteRef* sprite) {
                std::uint32_t texture = 0;
                std::int32_t layer = 0;
                std::uint32_t tint = 0;
                if (!reader.Read(texture) || !reader.Read(layer) ||
                    !reader.Read(tint)) {
                    return false;
                }
                *sprite = ecs::SpriteRef{texture, layer, tint};
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Facing>(
            kSaveComponentFacing, "Facing",
            [](const ecs::Facing& facing, BinaryWriter& writer) {
                const auto value = static_cast<std::uint8_t>(facing.dir);
                return value <= static_cast<std::uint8_t>(ecs::Direction::Up) &&
                       writer.Write(value);
            },
            [](BinaryReader& reader, ecs::Facing* facing) {
                std::uint8_t value = 0;
                if (!reader.Read(value) ||
                    value > static_cast<std::uint8_t>(ecs::Direction::Up)) {
                    return false;
                }
                facing->dir = static_cast<ecs::Direction>(value);
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Health>(
            kSaveComponentHealth, "Health",
            [](const ecs::Health& health, BinaryWriter& writer) {
                return ValidHealth(health) && writer.Write(health.current) &&
                       writer.Write(health.max);
            },
            [](BinaryReader& reader, ecs::Health* health) {
                float current = 0.0f;
                float maximum = 0.0f;
                if (!reader.Read(current) || !reader.Read(maximum)) {
                    return false;
                }
                const ecs::Health value{current, maximum};
                if (!ValidHealth(value)) {
                    return false;
                }
                *health = value;
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Needs>(
            kSaveComponentNeeds, "Needs",
            [](const ecs::Needs& needs, BinaryWriter& writer) {
                return ValidNeeds(needs) && writer.Write(needs.hunger) &&
                       writer.Write(needs.thirst) && writer.Write(needs.fatigue);
            },
            [](BinaryReader& reader, ecs::Needs* needs) {
                ecs::Needs value;
                if (!reader.Read(value.hunger) || !reader.Read(value.thirst) ||
                    !reader.Read(value.fatigue) || !ValidNeeds(value)) {
                    return false;
                }
                *needs = value;
                return true;
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::PlayerTag>(
            kSaveComponentPlayerTag, "PlayerTag",
            [](const ecs::PlayerTag&, BinaryWriter&) { return true; },
            [](BinaryReader&, ecs::PlayerTag*) { return true; },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::ItemInstance>(
            kSaveComponentItem, "ItemInstance",
            [](const ecs::ItemInstance& item, BinaryWriter& writer) {
                return WriteItem(item, writer);
            },
            [](BinaryReader& reader, ecs::ItemInstance* item) {
                return ReadItem(reader, item);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Container>(
            kSaveComponentContainer, "Container",
            [](const ecs::Container& container, BinaryWriter& writer) {
                return WriteContainer(container, writer);
            },
            [](BinaryReader& reader, ecs::Container* container) {
                return ReadContainer(reader, container);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    if (!registry.Register<ecs::Equipment>(
            kSaveComponentEquipment, "Equipment",
            [](const ecs::Equipment& equipment, BinaryWriter& writer) {
                return WriteEquipment(equipment, writer);
            },
            [](BinaryReader& reader, ecs::Equipment* equipment) {
                return ReadEquipment(reader, equipment);
            },
            kSaveComponentVersion, error)) {
        return false;
    }

    return registry.Register<ecs::Corpse>(
        kSaveComponentCorpse, "Corpse",
        [](const ecs::Corpse& corpse, BinaryWriter& writer) {
            return writer.Write(corpse.sourceId);
        },
        [](BinaryReader& reader, ecs::Corpse* corpse) {
            return reader.Read(corpse->sourceId);
        },
        kSaveComponentVersion, error);
}

}  // namespace zl::save
