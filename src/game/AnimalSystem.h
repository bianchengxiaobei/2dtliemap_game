#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/SoundEvent.h"
#include "ecs/EcsWorld.h"

namespace zl::characters {
class CombatSystem;
}

namespace zl::ecs {
struct AnimalWander;
struct Flee;
}

namespace zl::world {
class World;
}

namespace zl::game {

struct AnimalDef;
struct AnimalSpawnRule;
class AnimalRegistry;
struct PoiConfig;

class AnimalSystem {
public:
    struct Params {
        bool enabled = true;
        bool logAnimals = false;
        int maxLive = 36;
        float minPlayerDistance = 18.0f;
        float poiRadius = 16.0f;
        std::uint32_t seed = 20261030u;
        float wanderRadius = 6.0f;
        float soundWallAttenuation = 0.6f;
    };

    struct Stats {
        int live = 0;
        int seededChunks = 0;
        int pendingChunks = 0;
        int savedAnimals = 0;
        std::uint64_t spawned = 0;
        std::uint64_t restored = 0;
        std::uint64_t updated = 0;
        std::uint64_t skippedFar = 0;
        int fleeing = 0;
        int retaliating = 0;
        std::uint64_t heard = 0;
        std::uint64_t attacks = 0;
    };

    // M25-T4-V20：本 tick 成功命中的动物攻击，供车辆/联机侧读取；
    // AnimalSystem 直接走 ApplyDamage，不经过 CombatSystem 的玩家命中记录。
    struct AttackInfo {
        ecs::Entity attacker = ecs::EcsWorld::kNull;
        ecs::Entity target = ecs::EcsWorld::kNull;
        std::string speciesId;
        float damage = 0.0f;
    };

    static Params DefaultParams();
    AnimalSystem(const AnimalRegistry& animals, world::World& world,
                 ecs::EcsWorld& ecs, const PoiConfig* pois, Params params = DefaultParams());
    ~AnimalSystem();

    AnimalSystem(const AnimalSystem&) = delete;
    AnimalSystem& operator=(const AnimalSystem&) = delete;

    void SetFocus(float x, float y);
    void SetCombat(characters::CombatSystem* combat);
    void PushSound(const core::SoundEvent& sound);
    void Update(float dt);
    void OnChunkLoaded(int chunkX, int chunkY);
    void OnChunkUnloaded(int chunkX, int chunkY);
    void Reset();
    void StoreState(ecs::EcsWorld& ecs) const;
    void LoadState(ecs::EcsWorld& ecs);

    const Stats& GetStats() const { return m_stats; }
    const std::vector<AttackInfo>& GetAttacks() const { return m_attacks; }

private:
    struct SavedAnimal {
        std::string speciesId;
        float x = 0.0f;
        float y = 0.0f;
        float health = 1.0f;
        bool fleeing = false;
        float fleeRemaining = 0.0f;
        float fleeDirX = 0.0f;
        float fleeDirY = 0.0f;
        float threatX = 0.0f;
        float threatY = 0.0f;
        float alertness = 0.0f;
    };

    struct SpawnCell {
        int x = 0;
        int y = 0;
        bool forest = false;
    };

    struct PlayerTarget {
        ecs::Entity entity = ecs::EcsWorld::kNull;
        float x = 0.0f;
        float y = 0.0f;
        int z = 0;
    };

    std::uint64_t ChunkKey(int chunkX, int chunkY) const;
    bool IsStandable(float x, float y) const;
    bool HasTreeNear(int wx, int wy,
                     const std::unordered_set<std::uint64_t>& trees) const;
    bool NearPoi(const AnimalSpawnRule& rule, int wx, int wy) const;
    void SpawnChunk(int chunkX, int chunkY);
    ecs::Entity SpawnAnimal(const AnimalDef& def, float x, float y,
                            float health, const SavedAnimal* saved);
    void RestoreChunk(const std::vector<SavedAnimal>& saved);
    void RefreshCorpseVisuals();
    int CountLive() const;
    int CountAnimalsInChunk(int chunkX, int chunkY) const;
    bool AnimalNear(float x, float y, float radius) const;
    void RefreshLive();
    void ChooseGoal(ecs::Entity entity, ecs::AnimalWander& wander,
                    const AnimalDef& def);
    void UpdateWander(ecs::Entity entity, ecs::AnimalWander& wander,
                      const AnimalDef& def, float dt);
    void CollectPlayers();
    const PlayerTarget* NearestPlayer(float x, float y, float maxRadius) const;
    bool HasLineOfSight(float x0, float y0, float x1, float y1) const;
    bool HearsSound(const AnimalDef& def, const core::SoundEvent& sound,
                    float x, float y) const;
    void StartFlee(ecs::Entity entity, const AnimalDef& def, ecs::Flee& flee,
                   ecs::AnimalWander& wander, float threatX, float threatY);
    void StartRetaliate(ecs::Entity entity, const AnimalDef& def,
                        ecs::AnimalWander& wander, float threatX, float threatY);
    void UpdateFlee(ecs::Entity entity, ecs::Flee& flee, const AnimalDef& def,
                    float dt);
    void UpdateRetaliate(ecs::Entity entity, ecs::AnimalWander& wander,
                         const AnimalDef& def, float dt);
    bool StepToward(ecs::Entity entity, float targetX, float targetY, float speed,
                    float dt, float stopDistance);
    float NextFloat();
    std::uint64_t SpawnRoll(std::uint64_t key, const std::string& speciesId,
                            int category, int slot) const;

    const AnimalRegistry& m_animals;
    world::World& m_world;
    ecs::EcsWorld& m_ecs;
    const PoiConfig* m_pois = nullptr;
    characters::CombatSystem* m_combat = nullptr;
    Params m_params;
    std::mt19937 m_rng;
    std::unordered_set<std::uint64_t> m_seeded;
    std::unordered_map<std::uint64_t, std::vector<SavedAnimal>> m_saved;
    std::vector<core::SoundEvent> m_pendingSounds;
    std::vector<PlayerTarget> m_players;
    std::vector<AttackInfo> m_attacks;
    float m_focusX = 0.0f;
    float m_focusY = 0.0f;
    float m_logTimer = 0.0f;
    float m_nearestLogTimer = 0.0f;
    Stats m_stats;
};

inline AnimalSystem::Params AnimalSystem::DefaultParams() { return {}; }

}
