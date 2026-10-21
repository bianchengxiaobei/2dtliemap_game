#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "save/BinaryFormat.h"

namespace zl::save {

class BinaryReader;
class BinaryWriter;

inline constexpr std::uint16_t kSaveComponentVersion = 1;

inline constexpr std::uint32_t kSaveComponentPosition =
    MakeMagic('P', 'O', 'S', 'N');
inline constexpr std::uint32_t kSaveComponentVelocity =
    MakeMagic('V', 'E', 'L', 'O');
inline constexpr std::uint32_t kSaveComponentSprite =
    MakeMagic('S', 'P', 'R', 'T');
inline constexpr std::uint32_t kSaveComponentFacing =
    MakeMagic('F', 'A', 'C', 'E');
inline constexpr std::uint32_t kSaveComponentHealth =
    MakeMagic('H', 'L', 'T', 'H');
inline constexpr std::uint32_t kSaveComponentNeeds =
    MakeMagic('N', 'E', 'E', 'D');
inline constexpr std::uint32_t kSaveComponentPlayerTag =
    MakeMagic('P', 'L', 'A', 'Y');
inline constexpr std::uint32_t kSaveComponentItem =
    MakeMagic('I', 'T', 'E', 'M');
inline constexpr std::uint32_t kSaveComponentContainer =
    MakeMagic('C', 'T', 'N', 'R');
inline constexpr std::uint32_t kSaveComponentEquipment =
    MakeMagic('E', 'Q', 'P', 'M');
inline constexpr std::uint32_t kSaveComponentCorpse =
    MakeMagic('C', 'O', 'R', 'P');
inline constexpr std::uint32_t kSaveComponentKnownRecipes =
    MakeMagic('K', 'N', 'R', 'C');
inline constexpr std::uint32_t kSaveComponentCraftingJob =
    MakeMagic('C', 'R', 'F', 'T');

struct ComponentCodec {
    std::uint32_t type = 0;
    std::string name;
    std::uint16_t version = kSaveComponentVersion;
    std::function<bool(const entt::registry&, entt::entity)> has;
    std::function<bool(const entt::registry&, entt::entity, BinaryWriter&)> write;
    std::function<bool(BinaryReader&, entt::registry&, entt::entity)> read;
};

class ComponentRegistry {
public:
    bool Register(ComponentCodec codec, std::string* error = nullptr);

    template <typename T, typename WriteFn, typename ReadFn>
    bool Register(std::uint32_t type, std::string name, WriteFn writeFn, ReadFn readFn,
                  std::uint16_t version = kSaveComponentVersion,
                  std::string* error = nullptr) {
        ComponentCodec codec;
        codec.type = type;
        codec.name = std::move(name);
        codec.version = version;
        codec.has = [](const entt::registry& registry, entt::entity entity) {
            return registry.all_of<T>(entity);
        };
        codec.write = [writeFn = std::move(writeFn)](
                          const entt::registry& registry, entt::entity entity,
                          BinaryWriter& writer) {
            if (!registry.all_of<T>(entity)) {
                return false;
            }
            if constexpr (std::is_empty_v<T>) {
                T value{};
                return writeFn(value, writer);
            } else {
                const T* value = registry.try_get<T>(entity);
                return value != nullptr && writeFn(*value, writer);
            }
        };
        codec.read = [readFn = std::move(readFn)](
                         BinaryReader& reader, entt::registry& registry,
                         entt::entity entity) {
            T value{};
            if (!readFn(reader, &value)) {
                return false;
            }
            if constexpr (std::is_empty_v<T>) {
                registry.emplace_or_replace<T>(entity);
            } else {
                registry.emplace_or_replace<T>(entity, std::move(value));
            }
            return true;
        };
        return Register(std::move(codec), error);
    }

    const ComponentCodec* Find(std::uint32_t type) const;
    const std::vector<ComponentCodec>& All() const { return m_codecs; }
    std::size_t Size() const { return m_codecs.size(); }

private:
    std::vector<ComponentCodec> m_codecs;
    std::unordered_map<std::uint32_t, std::size_t> m_byType;
};

bool RegisterBuiltinComponents(ComponentRegistry& registry,
                               std::string* error = nullptr);

}  // namespace zl::save
