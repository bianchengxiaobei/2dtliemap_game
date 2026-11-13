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
// M23-F1-T2：Item/Container/Equipment 增加 loadedAmmo；旧 v1 仍可读，缺失时按 0 处理。
inline constexpr std::uint16_t kSaveComponentVersionLoadedAmmo = 2;
// M23-F5-T2：FarmPlot 增加病害/施肥标记；旧 v1 仍可读，缺失时按 false 处理。
inline constexpr std::uint16_t kSaveComponentVersionFarmPlot = 2;
// M25-T4-V2：Vehicle 增加车灯开关；旧 v1 仍可读，缺失时按 false 处理。
inline constexpr std::uint16_t kSaveComponentVersionVehicle = 3;
inline constexpr std::uint16_t kSaveComponentVersionMissionLog = 2;

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
inline constexpr std::uint32_t kSaveComponentFoodState =
    MakeMagic('F', 'O', 'O', 'D');
inline constexpr std::uint32_t kSaveComponentPlayerTag =
    MakeMagic('P', 'L', 'A', 'Y');
inline constexpr std::uint32_t kSaveComponentItem =
    MakeMagic('I', 'T', 'E', 'M');
inline constexpr std::uint32_t kSaveComponentContainer =
    MakeMagic('C', 'T', 'N', 'R');
inline constexpr std::uint32_t kSaveComponentLootContainer =
    MakeMagic('L', 'O', 'O', 'T');
inline constexpr std::uint32_t kSaveComponentPlayerStorage =
    MakeMagic('S', 'T', 'R', 'G');
inline constexpr std::uint32_t kSaveComponentStructureOwner =
    MakeMagic('S', 'T', 'R', 'O');
inline constexpr std::uint32_t kSaveComponentEquipment =
    MakeMagic('E', 'Q', 'P', 'M');
inline constexpr std::uint32_t kSaveComponentCorpse =
    MakeMagic('C', 'O', 'R', 'P');
inline constexpr std::uint32_t kSaveComponentKnownRecipes =
    MakeMagic('K', 'N', 'R', 'C');
inline constexpr std::uint32_t kSaveComponentCraftingJob =
    MakeMagic('C', 'R', 'F', 'T');
inline constexpr std::uint32_t kSaveComponentObjective =
    MakeMagic('O', 'B', 'J', 'V');
inline constexpr std::uint32_t kSaveComponentZombieVariant =
    MakeMagic('Z', 'V', 'A', 'R');
inline constexpr std::uint32_t kSaveComponentZombieSpecial =
    MakeMagic('Z', 'S', 'P', 'C');
inline constexpr std::uint32_t kSaveComponentWounds =
    MakeMagic('W', 'N', 'D', 'S');
inline constexpr std::uint32_t kSaveComponentPain =
    MakeMagic('P', 'A', 'I', 'N');
inline constexpr std::uint32_t kSaveComponentInfection =
    MakeMagic('I', 'N', 'F', 'C');
inline constexpr std::uint32_t kSaveComponentClimate =
    MakeMagic('C', 'L', 'M', 'T');
inline constexpr std::uint32_t kSaveComponentProgression =
    MakeMagic('P', 'R', 'G', 'R');
inline constexpr std::uint32_t kSaveComponentPoiGuard =
    MakeMagic('P', 'G', 'R', 'D');
inline constexpr std::uint32_t kSaveComponentPoiTasks =
    MakeMagic('P', 'O', 'T', 'S');
inline constexpr std::uint32_t kSaveComponentMissionLog =
    MakeMagic('M', 'S', 'L', 'G');
inline constexpr std::uint32_t kSaveComponentFarmPlot =
    MakeMagic('F', 'A', 'R', 'M');
inline constexpr std::uint32_t kSaveComponentGenerator =
    MakeMagic('G', 'E', 'N', 'R');
inline constexpr std::uint32_t kSaveComponentColdStorage =
    MakeMagic('C', 'O', 'L', 'D');
inline constexpr std::uint32_t kSaveComponentFloodlight =
    MakeMagic('F', 'L', 'D', 'L');
inline constexpr std::uint32_t kSaveComponentSpikeTrap =
    MakeMagic('T', 'R', 'A', 'P');
inline constexpr std::uint32_t kSaveComponentMerchantStock =
    MakeMagic('M', 'R', 'C', 'H');
inline constexpr std::uint32_t kSaveComponentSettlementState =
    MakeMagic('S', 'T', 'L', 'S');
inline constexpr std::uint32_t kSaveComponentForage =
    MakeMagic('F', 'R', 'G', 'S');
inline constexpr std::uint32_t kSaveComponentFireAnchor =
    MakeMagic('F', 'I', 'R', 'A');
inline constexpr std::uint32_t kSaveComponentFireSource =
    MakeMagic('F', 'I', 'R', 'E');
inline constexpr std::uint32_t kSaveComponentBurningZombie =
    MakeMagic('B', 'U', 'R', 'N');
inline constexpr std::uint32_t kSaveComponentAnimal =
    MakeMagic('A', 'N', 'M', 'L');
inline constexpr std::uint32_t kSaveComponentFlee =
    MakeMagic('F', 'L', 'E', 'E');
inline constexpr std::uint32_t kSaveComponentAnimalCorpse =
    MakeMagic('A', 'C', 'R', 'P');
inline constexpr std::uint32_t kSaveComponentAnimalWorldState =
    MakeMagic('A', 'W', 'S', 'T');
inline constexpr std::uint32_t kSaveComponentVehicle =
    MakeMagic('V', 'E', 'H', 'I');
inline constexpr std::uint32_t kSaveComponentRandomEventActor =
    MakeMagic('R', 'E', 'V', 'T');
inline constexpr std::uint32_t kSaveComponentSettlementReputation =
    MakeMagic('S', 'R', 'P', 'T');

struct ComponentCodec {
    std::uint32_t type = 0;
    std::string name;
    std::uint16_t version = kSaveComponentVersion;
    std::uint16_t minimumVersion = kSaveComponentVersion;
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
                  std::string* error = nullptr,
                  std::uint16_t minimumVersion = 0) {
        ComponentCodec codec;
        codec.type = type;
        codec.name = std::move(name);
        codec.version = version;
        codec.minimumVersion = minimumVersion == 0 ? version : minimumVersion;
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
