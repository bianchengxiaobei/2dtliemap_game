#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "game/Fire.h"
#include "game/FireComponents.h"

namespace zl::ecs {
class EcsWorld;
struct Container;
}

namespace zl::world {
class World;
}

namespace zl::items {
class ItemRegistry;
}

namespace zl::characters {
class CombatSystem;
}

namespace zl::game {

struct FireParams {
    float ignitionSeconds = 1.5f;
    float wetnessPenalty = 0.90f;
    float minIgnitionChance = 0.0f;
    float smolderSeconds = 20.0f;
    float spreadIntervalSeconds = 6.0f;
    float spreadChanceScale = 0.20f;
    float rainBurnDecayScale = 1.0f;
    int maxActiveSources = 128;
    int maxSpreadsPerTick = 2;
    float extinguishRange = 2.5f;
    float bareHandsChance = 0.15f;
    float bareHandsSelfDamage = 2.0f;
    float fireDamagePerSecond = 6.0f;
    float fireDamageRadius = 1.0f;
    float fireDamageIntervalSeconds = 0.5f;
    float zombieBurnSeconds = 8.0f;
    float zombieBurnDamagePerSecond = 3.0f;
    float zombieBurnDamageIntervalSeconds = 0.5f;
    float zombieSpreadIntervalSeconds = 1.5f;
    float zombieSpreadChance = 0.35f;
    float zombiePanicSeconds = 4.0f;
    float zombiePanicSpeedScale = 1.25f;
    float zombiePanicChaseRadius = 12.0f;
};

enum class FireIgnitionResult : std::uint8_t {
    Ignited = 0,
    TooWet = 1,
    AlreadyActive = 2,
    InvalidMaterial = 3,
    AtCapacity = 4,
};

FireParams SanitizeFireParams(FireParams params);
float FireIgnitionChance(const Flammability& flammability, float wetness,
                         const FireParams& params);
float FireIgnitionSeconds(const Flammability& flammability,
                          const FireParams& params);
float FireSpreadInterval(float burnSeconds, const FireParams& params);
float FireSpreadChance(const Flammability& flammability, float wetness,
                       const FireParams& params);
struct FireDamageCredit {
    std::uint64_t ownerStableId = 0;
    std::uint64_t targetStableId = 0;
    float amount = 0.0f;
};

std::size_t ApplyFireDamage(
    ecs::EcsWorld& ecs, characters::CombatSystem& combat,
    const FireParams& params, float elapsedSeconds,
    std::vector<FireDamageCredit>* credits = nullptr);
std::size_t ApplyBurningZombieDamage(
    ecs::EcsWorld& ecs, characters::CombatSystem& combat,
    const FireParams& params, float elapsedSeconds,
    std::vector<FireDamageCredit>* credits = nullptr);
FireIgnitionResult TryIgniteFire(ecs::FireSource& source,
                                 const FireMaterialDef& material, float wetness,
                                 float roll, const FireParams& params);
void AdvanceFire(ecs::FireSource& source, float dt, const FireParams& params,
                 float ambientWetness = 0.0f);
void ExtinguishFire(ecs::FireSource& source);
bool FireIsActive(const ecs::FireSource& source);
bool FireProvidesHeat(const ecs::FireSource& source);
float FireSmokePerSecond(const ecs::FireSource& source);
const char* FirePhaseName(ecs::FirePhase phase);

struct FireTarget {
    int x = 0;
    int y = 0;
    int z = 0;
    ecs::FireSource source;
    const FireMaterialDef* material = nullptr;
    float distanceSq = 0.0f;
};

struct FireBurnOut {
    int x = 0;
    int y = 0;
    int z = 0;
    std::string materialId;
};

struct FireBurnedDrop {
    std::string defId;
    int count = 1;
};

struct FireBurnResolution {
    bool structureRemoved = false;
    std::vector<FireBurnedDrop> drops;
};

struct FireExtinguishPlan {
    bool usesItem = false;
    bool bareHands = true;
    int index = -1;
    std::string defId;
    std::string name;
    std::string replacementDefId;
    float chance = 0.0f;
    float wear = 0.0f;
    float selfDamage = 0.0f;
};

FireExtinguishPlan PlanFireExtinguish(const ecs::Container& container,
                                      const items::ItemRegistry& registry,
                                      const FireParams& params);
bool ConsumeFireExtinguishItem(ecs::Container& container,
                               const items::ItemRegistry& registry,
                               const FireExtinguishPlan& plan);
bool FireExtinguishSucceeds(float chance, float roll);

class FireSystem {
public:
    explicit FireSystem(const FireRegistry& registry,
                        FireParams params = {},
                        std::uint32_t seed = 20261029u);

    void Update(ecs::EcsWorld& ecs, float dt,
                world::World* world = nullptr, float ambientWetness = 0.0f,
                std::vector<FireBurnOut>* burnOuts = nullptr);
    std::size_t SpreadFromBurningZombies(ecs::EcsWorld& ecs,
                                         world::World& world, float dt,
                                         float ambientWetness = 0.0f);

    FireIgnitionResult IgniteAt(ecs::EcsWorld& ecs, world::World& world, int wx,
                                int wy, int z, float wetness,
                                std::string* error = nullptr,
                                std::uint64_t ownerStableId = 0);
    bool IgniteForcedAt(ecs::EcsWorld& ecs, world::World& world, int wx, int wy,
                        int z, float burnSeconds, float heatRadius,
                        std::uint64_t ownerStableId = 0);
    bool ExtinguishAt(ecs::EcsWorld& ecs, int wx, int wy, int z);
    std::size_t UnloadChunk(ecs::EcsWorld& ecs, int chunkX, int chunkY);

    // 自然燃尽后删除对应格的可燃结构并返回掉落。主动灭火不会调用。
    FireBurnResolution ResolveBurnedCell(world::World& world,
                                         const FireBurnOut& burned) const;

    ecs::FireSource* FindAt(ecs::EcsWorld& ecs, int wx, int wy, int z);
    const ecs::FireSource* FindAt(const ecs::EcsWorld& ecs, int wx, int wy,
                                  int z) const;

    bool HasActiveFireNear(const ecs::EcsWorld& ecs, float x, float y,
                           int z) const;
    bool FindNearestActiveFire(const ecs::EcsWorld& ecs, float x, float y,
                               int z, float radius,
                               FireTarget* out = nullptr) const;
    std::size_t ActiveSourceCount(const ecs::EcsWorld& ecs) const;

    const FireParams& Params() const { return m_params; }
    std::vector<FireDamageCredit>* MutableDamageCredits() {
        return &m_damageCredits;
    }
    std::vector<FireDamageCredit> TakeDamageCredits() {
        return std::exchange(m_damageCredits, {});
    }

private:
    const FireRegistry* m_registry = nullptr;
    FireParams m_params;
    std::mt19937 m_rng;
    std::vector<FireDamageCredit> m_damageCredits;
};

}  // namespace zl::game
